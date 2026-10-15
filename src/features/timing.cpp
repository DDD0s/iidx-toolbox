#include "std_include.hpp"

#include "component.hpp"
#include "config.hpp"
#include "d3d.hpp"
#include "hook.hpp"
#include "log.hpp"
#include "module.hpp"
#include "timing_logic.hpp"

// The play step processes chart ticks up to a target that the game advances one
// tick per frame, plus one when its audio check (every other frame) finds the
// audio more than 17 ms ahead. Here the target is set before each step from the
// vsync on which the frame will be shown, offset-anchored to the audio position:
// CPU jitter inside a frame changes nothing, a frame that really missed vsyncs is
// caught up at once, and the chart stays on the audio.
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
		double g_qpc_per_us = 0.0;

		// vsync grid, written after each main Present, read by the play step
		struct grid_t
		{
			int64_t index = 0;   // vsync count at 'time'
			int64_t time = 0;    // QPC of that vsync (or of the Present return)
			double period = 0.0; // QPC ticks per vsync
			double frame = 0.0;  // QPC ticks per presented frame, smoothed
			bool exact = false;  // from present statistics
		};
		std::atomic<uint32_t> g_grid_seq{ 0 };
		grid_t g_grid_shared;

		void publish(const grid_t& g)
		{
			g_grid_seq.fetch_add(1, std::memory_order_acq_rel);
			g_grid_shared = g;
			g_grid_seq.fetch_add(1, std::memory_order_release);
		}

		bool read_grid(grid_t& out)
		{
			for (int i = 0; i < 4; ++i)
			{
				const auto a = g_grid_seq.load(std::memory_order_acquire);
				if (a & 1)
					continue;
				out = g_grid_shared;
				std::atomic_thread_fence(std::memory_order_acquire);
				if (g_grid_seq.load(std::memory_order_relaxed) == a)
					return true;
			}
			return false;
		}

		// render thread only
		struct present_state_t
		{
			IDirect3DSwapChain9Ex* ex = nullptr;
			bool ex_tried = false;
			bool stats_logged = false;
			uint32_t last_sync32 = 0;
			int64_t sync_count = 0;
			int64_t base_count = -1;
			int64_t base_time = 0;
			int64_t last_return = 0;
			int64_t fallback_index = 0;
			grid_t grid;
		} g_present;

		int64_t qpc()
		{
			LARGE_INTEGER v;
			QueryPerformanceCounter(&v);
			return v.QuadPart;
		}

		void after_present(IDirect3DSwapChain9* chain)
		{
			auto& p = g_present;
			auto& g = p.grid;
			const int64_t now = qpc();

			if (p.last_return)
			{
				const double dt = static_cast<double>(now - p.last_return);
				if (g.frame <= 0.0)
					g.frame = dt;
				else if (dt > g.frame * 0.5 && dt < g.frame * 1.5)
					g.frame += (dt - g.frame) * 0.05;
			}

			if (!p.ex_tried)
			{
				p.ex_tried = true;
				chain->QueryInterface(__uuidof(IDirect3DSwapChain9Ex), reinterpret_cast<void**>(&p.ex));
			}

			D3DPRESENTSTATS st{};
			const bool stats = p.ex && SUCCEEDED(p.ex->GetPresentStats(&st)) && st.SyncQPCTime.QuadPart > 0 && st.SyncRefreshCount > 0;
			if (stats)
			{
				p.sync_count += static_cast<uint32_t>(st.SyncRefreshCount - p.last_sync32);
				if (p.base_count < 0)
					p.sync_count = st.SyncRefreshCount;
				p.last_sync32 = st.SyncRefreshCount;
				const int64_t t = st.SyncQPCTime.QuadPart;
				if (p.base_count < 0 || p.sync_count <= p.base_count || t <= p.base_time)
				{
					p.base_count = p.sync_count;
					p.base_time = t;
				}
				else if (p.sync_count - p.base_count >= 30)
				{
					const double period = static_cast<double>(t - p.base_time) / static_cast<double>(p.sync_count - p.base_count);
					// a different refresh rate restarts the average
					if (g.period > 0.0 && std::fabs(period - g.period) > g.period * 0.02 && p.sync_count - p.base_count > 240)
					{
						p.base_count = p.sync_count;
						p.base_time = t;
					}
					else
						g.period = period;
				}
				if (g.period <= 0.0)
					g.period = g.frame;
				// the vsync that showed the last Present; equal to the latest one when Present blocks
				const auto behind = static_cast<int32_t>(st.SyncRefreshCount - st.PresentRefreshCount);
				const int64_t shown_behind = st.PresentRefreshCount && behind >= 0 && behind <= 8 ? behind : 0;
				g.index = p.sync_count - shown_behind;
				g.time = t - static_cast<int64_t>(static_cast<double>(shown_behind) * g.period);
				g.exact = g.period > 0.0 && p.sync_count - p.base_count >= 30;
				if (!p.stats_logged && g.exact)
				{
					p.stats_logged = true;
					tb::log("timing", "vsync grid from present statistics: %.3f ms", g.period / g_qpc_per_us / 1000.0);
				}
			}
			else
			{
				if (p.last_return && g.frame > 0.0)
					p.fallback_index += std::max<int64_t>(1, std::llround(static_cast<double>(now - p.last_return) / g.frame));
				g.index = p.fallback_index;
				g.time = now;
				g.period = g.frame;
				g.exact = false;
			}
			p.last_return = now;
			publish(g);
		}

		struct song_t
		{
			timing_logic::controller ctl;
			bool active = false;
			int64_t base_index = 0;
			int64_t prev_start = -1;
			double prev_phase = 0.0;
			uint64_t predicted_late = 0;
			uint64_t frames = 0, steered = 0, holds = 0, skips = 0;
			int max_jump = 0;
			double max_dev = 0.0;
			bool exact = false;
		} g_song;
		timing_logic::miss_predictor g_predictor; // kept across songs

		void report(float ms_per_tick)
		{
			auto& s = g_song;
			if (s.steered)
				tb::log("timing", "song: %llu frames steered (%s), %llu held, %llu caught up (max %d ticks), %llu predicted late (threshold %.2f), %llu audio corrections, audio offset %.2f ms, max deviation %.2f ms, %llu re-anchors",
					static_cast<unsigned long long>(s.steered), s.exact ? "present statistics" : "present returns",
					static_cast<unsigned long long>(s.holds), static_cast<unsigned long long>(s.skips), s.max_jump,
					static_cast<unsigned long long>(s.predicted_late), g_predictor.threshold,
					static_cast<unsigned long long>(s.ctl.audio_corrections),
					s.ctl.offset * ms_per_tick, s.max_dev * ms_per_tick, static_cast<unsigned long long>(s.ctl.resnaps));
			else if (s.frames && s.active)
				tb::log("timing", "song: audio position never available, game stepping");
			const bool active = s.active;
			s = {};
			s.active = active;
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

			if (tick <= 0 || !loaded || !started || target >= end)
			{
				if (tick <= 0 && s.frames)
					report(ms_per_tick);
				return;
			}
			if (!(ms_per_tick > 3.f && ms_per_tick < 34.f))
				return;

			grid_t grid;
			if (!read_grid(grid) || grid.frame <= 0.0 || grid.period <= 0.0)
				return;
			++s.frames;

			const double tick_qpc = static_cast<double>(ms_per_tick) * 1000.0 * g_qpc_per_us;
			const bool matched = std::fabs(grid.frame - tick_qpc) < tick_qpc * 0.03;
			if (matched != s.active)
			{
				s.active = matched;
				tb::log("timing", matched ? "frames %.3f ms match the tick (%.3f ms): millisecond clock on"
					: "frames %.3f ms do not match the tick (%.3f ms): game stepping", grid.frame / g_qpc_per_us / 1000.0, ms_per_tick);
			}
			if (!s.active)
				return;

			// the frame started on the vsync that showed the previous one and will be
			// shown on the next, unless this step runs late enough to miss it
			const double phase = static_cast<double>(qpc() - grid.time) / grid.period;
			if (s.prev_start >= 0)
				g_predictor.learn(s.prev_phase, grid.index - s.prev_start - 1);
			s.prev_start = grid.index;
			s.prev_phase = phase;
			const int late = g_predictor.vsyncs_late(phase);
			if (late)
				++s.predicted_late;
			const int64_t shown = grid.index + 1 + late;
			if (!s.ctl.anchored)
				s.base_index = shown;
			const double ticks_per_vsync = grid.period / tick_qpc;
			const double clock_pos = static_cast<double>(shown - s.base_index) * ticks_per_vsync;
			s.exact = grid.exact;

			// the game reads the audio position every other frame; every 4th is enough here
			if (!s.ctl.anchored || (s.frames & 3) == 0)
			{
				const int audio_ms = g_audio_ms(1);
				const int audio_tick0 = g_audio_start(chart + g_audio_obj_off);
				if (audio_ms > 0 && tick > audio_tick0)
				{
					const double audio_pos = audio_tick0 + audio_ms / static_cast<double>(ms_per_tick);
					if (s.ctl.anchored)
						s.max_dev = std::max(s.max_dev, std::fabs(audio_pos - (clock_pos + s.ctl.offset)));
					s.ctl.observe_audio(clock_pos, audio_pos);
				}
			}

			const int32_t want = s.ctl.target(tick, end, shown, ticks_per_vsync, clock_pos);
			if (want < 0)
				return;
			const int32_t nominal = tick + 1;
			if (want < nominal)
				++s.holds;
			else if (want > nominal)
				++s.skips;
			s.max_jump = std::max(s.max_jump, want - tick);
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
			g_qpc_per_us = static_cast<double>(freq.QuadPart) / 1e6;

			d3d::on_after_present(&after_present);
			g_step.create(step, reinterpret_cast<void*>(&step_stub));
			tb::log("timing", "installed (step %p, tick +0x%X, audio object +0x%X)%s", static_cast<void*>(step), g_tick_off,
				g_audio_obj_off, config::timing_enabled ? "" : ", disabled in iidx_toolbox.ini");
		}
	};
}

TB_REGISTER_COMPONENT(tb::timing::component, 40)