#include "std_include.hpp"

#include "component.hpp"
#include "config.hpp"
#include "d3d.hpp"
#include "hook.hpp"
#include "log.hpp"
#include "module.hpp"
#include "state.hpp"
#include "timing_logic.hpp"

// MS CLOCK (timing_logic.hpp): speeds up the game's own audio catch-up after a
// stall, never changes a step the game's single check can handle. Active only
// while frames come at the tick rate (60 LDJ, 120 TDJ).
// Every song gets one log line when its chart ends (or at the next chart start if
// it was left), MS CLOCK on or off: the game's catch-up ticks, the ticks MS CLOCK
// added, the audio lead as the game's check computes it. Stalls and catch-ups get
// a line each, with the song time and whether the menu was open.
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

		double g_frame_ms = 0.0; // smoothed step interval, kept across songs
		int g_long_run = 0;      // consecutive steps longer than 1.5 ticks
		bool g_active = false;
		int64_t g_last_step = 0;

		constexpr int k_event_lines = 30; // per song and kind

		struct song_t
		{
			uint64_t frames = 0, long_frames = 0;
			uint64_t game_extra = 0, removed = 0; // removed must stay 0
			uint64_t added = 0, catch_ups = 0;
			int largest = 0;
			uint64_t lead_n = 0, over17 = 0;
			double lead_sum = 0.0, lead_min = 1e9, lead_max = -1e9;
			double longest_ms = 0.0;
			int stall_lines = 0, catch_up_lines = 0;
			bool reported = false;
		} g_song;

		int64_t qpc()
		{
			LARGE_INTEGER v;
			QueryPerformanceCounter(&v);
			return v.QuadPart;
		}

		void report()
		{
			auto& s = g_song;
			if (!s.frames || s.reported)
				return;
			s.reported = true;
			char lead[160];
			if (s.lead_n)
				sprintf_s(lead, "mean %.1f ms, min %.0f, max %.0f, over 17 ms %llu of %llu",
					s.lead_sum / static_cast<double>(s.lead_n), s.lead_min, s.lead_max,
					static_cast<unsigned long long>(s.over17), static_cast<unsigned long long>(s.lead_n));
			else
				sprintf_s(lead, "no samples");
			tb::log("timing", "song: %llu frames (%llu longer than 1.5 ticks, longest %.1f ms), game catch-up ticks %llu, MS CLOCK added %llu in %llu catch-ups (largest %d), audio lead at the game's check: %s%s",
				static_cast<unsigned long long>(s.frames), static_cast<unsigned long long>(s.long_frames), s.longest_ms,
				static_cast<unsigned long long>(s.game_extra), static_cast<unsigned long long>(s.added),
				static_cast<unsigned long long>(s.catch_ups), s.largest, lead,
				s.removed ? ", WARNING: game ticks removed" : "");
		}

		double song_seconds(int32_t tick, float ms_per_tick)
		{
			return static_cast<double>(tick) * ms_per_tick / 1000.0;
		}

		// frames at the tick rate (within 5 %); a lasting other rate turns MS CLOCK off
		void update_rate(float ms_per_tick, int32_t tick)
		{
			const int64_t now = qpc();
			if (g_last_step)
			{
				const double dt = static_cast<double>(now - g_last_step) / g_qpc_per_ms;
				auto& s = g_song;
				s.longest_ms = std::max(s.longest_ms, dt);
				if (dt > ms_per_tick * 1.5)
				{
					++s.long_frames;
					if (s.stall_lines++ < k_event_lines)
						tb::log("timing", "stall %.1f ms at %.2f s%s", dt, song_seconds(tick, ms_per_tick),
							state::menu_open ? ", menu open" : "");
					if (++g_long_run >= 30)
						g_frame_ms = dt;
				}
				else if (dt > 0.0)
				{
					g_long_run = 0;
					g_frame_ms = g_frame_ms > 0.0 ? g_frame_ms + (dt - g_frame_ms) * 0.05 : dt;
				}
			}
			g_last_step = now;

			const bool matched = g_frame_ms > 0.0 && std::fabs(g_frame_ms - ms_per_tick) < ms_per_tick * 0.05;
			if (matched != g_active)
			{
				g_active = matched;
				tb::log("timing", matched ? "frames %.3f ms match the tick (%.3f ms): MS CLOCK on"
					: "frames %.3f ms do not match the tick (%.3f ms): MS CLOCK idle", g_frame_ms, ms_per_tick);
			}
		}

		bool step_stub(uint8_t* chart)
		{
			static bool checked = false;
			if (!checked)
			{
				checked = true;
				if (!d3d::device())
					tb::log("timing", "playing without a hooked D3D device: menu, PGREAT BLUE and NO BLINK are unavailable");
			}
			if (!chart)
				return g_step.invoke<bool>(chart);

			auto& target = *reinterpret_cast<int32_t*>(g_clock_obj + 0x5C);
			const auto& tick = *reinterpret_cast<int32_t*>(chart + g_tick_off);
			const int32_t end = *reinterpret_cast<int32_t*>(g_clock_obj + 0x60);
			const int32_t target_in = target;
			const float ms_per_tick = g_ms_per_tick();
			const bool running = tick > 0 && chart[g_tick_off + 5] != 0 &&
				*reinterpret_cast<uint64_t*>(chart + g_start_off) != 0 && target_in < end &&
				ms_per_tick > 3.f && ms_per_tick < 34.f;
			auto& s = g_song;
			if (tick <= 0)
			{
				report();
				s = {};
				g_last_step = 0;
			}
			else if (target_in >= end)
				report();

			if (running)
			{
				++s.frames;
				update_rate(ms_per_tick, tick);
				if (config::timing_enabled && g_active && (s.frames & 1) == 0)
				{
					const int audio_ms = g_audio_ms(1);
					const int audio_tick0 = g_audio_start(chart + g_audio_obj_off);
					int extra = timing_logic::extra_ticks(audio_ms, target_in, audio_tick0, 1000.f / ms_per_tick);
					extra = std::min(extra, end - 1 - target_in);
					if (extra > 0)
					{
						target = target_in + extra;
						s.added += static_cast<uint64_t>(extra);
						++s.catch_ups;
						s.largest = std::max(s.largest, extra);
						if (s.catch_up_lines++ < k_event_lines)
							tb::log("timing", "MS CLOCK +%d ticks at %.2f s (audio lead %d ms)", extra,
								song_seconds(tick, ms_per_tick),
								audio_ms - timing_logic::chart_ms(target_in, audio_tick0, 1000.f / ms_per_tick));
					}
				}
				if (target < target_in)
					s.removed += static_cast<uint64_t>(target_in - target);
			}

			const bool result = g_step.invoke<bool>(chart);

			if (running)
			{
				if (target - tick >= 2)
					s.game_extra += static_cast<uint64_t>(target - tick - 1);
				if ((s.frames & 3) == 0)
				{
					const int audio_ms = g_audio_ms(1);
					const int chart_ms = timing_logic::chart_ms(tick, g_audio_start(chart + g_audio_obj_off), 1000.f / ms_per_tick);
					if (audio_ms > 0 && chart_ms > 0)
					{
						const double lead = static_cast<double>(audio_ms - chart_ms);
						++s.lead_n;
						s.lead_sum += lead;
						s.lead_min = std::min(s.lead_min, lead);
						s.lead_max = std::max(s.lead_max, lead);
						if (lead > 17.0)
							++s.over17;
					}
				}
			}
			return result;
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
				g_audio_obj_off, config::timing_enabled ? "" : ", MS CLOCK disabled in iidx_toolbox.ini");
		}
	};
}

TB_REGISTER_COMPONENT(tb::timing::component, 40)