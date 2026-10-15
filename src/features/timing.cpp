#include "std_include.hpp"

#include "component.hpp"
#include "config.hpp"
#include "hook.hpp"
#include "log.hpp"
#include "module.hpp"
#include "timing_logic.hpp"

// The play step processes chart ticks up to a target that the game advances one
// tick per frame, plus one when its audio check (every other frame) finds the
// audio more than 17 ms ahead; it never holds. Here the target is set before each
// step: one tick per frame, corrected towards the audio-anchored clock only when
// a deviation of more than a tick has stopped shrinking (timing_logic.hpp).
// Only while frames come at the timing rate (60 LDJ, 120 TDJ).
namespace tb::timing
{
	namespace
	{
		using ms_per_tick_t = float (*)();
		using audio_ms_t = int (*)(int);
		using audio_start_t = int (*)(void*);

		hook::detour g_step;
		uint8_t* g_clock_obj = nullptr; // +0x5C target tick, +0x60 end tick
		uint32_t g_tick_off = 0;        // chart + off: current tick
		uint32_t g_start_off = 0;       // chart + off: chart start (us), set at tick 0
		uint32_t g_audio_obj_off = 0;   // chart + off: object holding the audio start tick
		ms_per_tick_t g_ms_per_tick = nullptr;
		audio_ms_t g_audio_ms = nullptr;
		audio_start_t g_audio_start = nullptr;
		double g_qpc_per_ms = 0.0;

		struct song_t
		{
			timing_logic::controller ctl;
			int64_t base = 0;
			int64_t last = 0;
			uint64_t frames = 0, steered = 0, holds = 0, skips = 0, long_frames = 0;
			int max_step = 0;
			bool reported = false;
		} g_song;
		double g_frame_ms = 0.0; // smoothed step interval, kept across songs
		bool g_active = false;

		int64_t qpc()
		{
			LARGE_INTEGER v;
			QueryPerformanceCounter(&v);
			return v.QuadPart;
		}

		void report(float ms_per_tick)
		{
			auto& s = g_song;
			if (s.reported || !s.frames)
				return;
			s.reported = true;
			if (s.steered)
				tb::log("timing", "song: %llu frames (%llu longer than 1.5 ticks), %llu skips + %llu holds (largest step %d ticks), %llu deviations left to the render queue, max deviation %.1f ms, audio offset %.2f ms, %llu re-anchors",
					static_cast<unsigned long long>(s.frames), static_cast<unsigned long long>(s.long_frames),
					static_cast<unsigned long long>(s.skips), static_cast<unsigned long long>(s.holds), s.max_step,
					static_cast<unsigned long long>(s.ctl.absorbed), s.ctl.max_dev * ms_per_tick, s.ctl.offset * ms_per_tick,
					static_cast<unsigned long long>(s.ctl.resnaps));
			else if (g_active)
				tb::log("timing", "song: audio position never available, game stepping");
		}

		void steer(uint8_t* chart)
		{
			auto& s = g_song;
			const int32_t tick = *reinterpret_cast<int32_t*>(chart + g_tick_off);
			auto& target = *reinterpret_cast<int32_t*>(g_clock_obj + 0x5C);
			const int32_t end = *reinterpret_cast<int32_t*>(g_clock_obj + 0x60);
			const bool loaded = chart[g_tick_off + 5] != 0;
			const bool started = *reinterpret_cast<uint64_t*>(chart + g_start_off) != 0;
			const float ms_per_tick = g_ms_per_tick();

			if (tick <= 0)
			{
				report(ms_per_tick);
				s = {};
				return;
			}
			if (!loaded || !started || !(ms_per_tick > 3.f && ms_per_tick < 34.f))
				return;
			if (target >= end)
			{
				report(ms_per_tick);
				return;
			}

			const int64_t now = qpc();
			++s.frames;
			if (s.last)
			{
				const double dt = static_cast<double>(now - s.last) / g_qpc_per_ms;
				if (dt > ms_per_tick * 1.5)
					++s.long_frames;
				else if (dt > 0.0)
					g_frame_ms = g_frame_ms > 0.0 ? g_frame_ms + (dt - g_frame_ms) * 0.05 : dt;
			}
			s.last = now;

			const bool matched = g_frame_ms > 0.0 && std::fabs(g_frame_ms - ms_per_tick) < ms_per_tick * 0.03;
			if (matched != g_active)
			{
				g_active = matched;
				tb::log("timing", matched ? "frames %.3f ms match the tick (%.3f ms): millisecond clock on"
					: "frames %.3f ms do not match the tick (%.3f ms): game stepping", g_frame_ms, ms_per_tick);
			}
			if (!g_active)
				return;

			if (!s.base)
				s.base = now;
			const double clock_pos = static_cast<double>(now - s.base) / g_qpc_per_ms / ms_per_tick;
			if (!s.ctl.anchored || (s.frames & 3) == 0)
			{
				const int audio_ms = g_audio_ms(1);
				const int audio_tick0 = g_audio_start(chart + g_audio_obj_off);
				if (audio_ms > 0 && tick > audio_tick0)
					s.ctl.observe_audio(clock_pos, audio_tick0 + audio_ms / static_cast<double>(ms_per_tick));
			}

			const int32_t want = s.ctl.target(tick, end, clock_pos);
			if (want < 0)
				return;
			const int32_t nominal = tick + 1;
			if (want < nominal)
				++s.holds;
			else if (want > nominal)
				++s.skips;
			s.max_step = std::max(s.max_step, want - tick);
			++s.steered;
			target = want;
		}

		bool step_stub(uint8_t* chart)
		{
			if (config::timing_enabled && chart)
				steer(chart);
			return g_step.invoke<bool>(chart);
		}
	}

	class component final : public component_interface
	{
	public:
		void post_load() override
		{
			auto* step = module::find_unique("48 89 5C 24 20 57 48 83 EC 50 48 8B F9 E8 ? ? ? ? 8B 40 5C 39 87 ? ? ? ? 0F 8D");
			auto* capture = module::find_unique("83 BF ? ? ? ? 00 75 0D FF 15 ? ? ? ? 48 89 87 ? ? ? ? E8 ? ? ? ? 66 0F 6E 8F");
			auto* audio = module::find_unique("B9 01 00 00 00 4C 89 74 24 70 E8 ? ? ? ? 8B E8 E8 ? ? ? ? 48 8B C8 E8 ? ? ? ? 0F 28 F0 E8 ? ? ? ? 48 8D 8F ? ? ? ? 8B 58 5C E8");
			if (!step || !capture || !audio)
				throw std::runtime_error("play step not found");

			auto* clock_getter = module::rel32(step + 14);
			if (clock_getter[0] != 0x48 || clock_getter[1] != 0x8D || clock_getter[2] != 0x05 || clock_getter[7] != 0xC3)
				throw std::runtime_error("chart clock getter differs");
			g_clock_obj = module::rel32(clock_getter + 3);

			std::memcpy(&g_tick_off, step + 23, 4);
			uint32_t capture_tick_off = 0;
			std::memcpy(&capture_tick_off, capture + 2, 4);
			std::memcpy(&g_start_off, capture + 18, 4);
			if (capture_tick_off != g_tick_off || g_start_off != g_tick_off + 8)
				throw std::runtime_error("chart layout differs");
			if (module::rel32(audio + 34) != clock_getter)
				throw std::runtime_error("audio check differs");

			g_ms_per_tick = reinterpret_cast<ms_per_tick_t>(module::rel32(capture + 23));
			g_audio_ms = reinterpret_cast<audio_ms_t>(module::rel32(audio + 11));
			std::memcpy(&g_audio_obj_off, audio + 41, 4);
			g_audio_start = reinterpret_cast<audio_start_t>(module::rel32(audio + 49));

			LARGE_INTEGER freq;
			QueryPerformanceFrequency(&freq);
			g_qpc_per_ms = static_cast<double>(freq.QuadPart) / 1000.0;

			g_step.create(step, reinterpret_cast<void*>(&step_stub));
			tb::log("timing", "installed (step %p, tick +0x%X, audio object +0x%X)%s", static_cast<void*>(step), g_tick_off,
				g_audio_obj_off, config::timing_enabled ? "" : ", disabled in iidx_toolbox.ini");
		}
	};
}

TB_REGISTER_COMPONENT(tb::timing::component, 40)
