// Simulates the play loop with the game's frame limiter, a D3D render queue
// (Present blocks while 'latency' frames wait to be shown) and vsync, then
// compares the game's own stepping with the millisecond clock: chart steps that
// do not match what the display did (uneven), those on a regular display
// (artificial judder), and chart-vs-audio deviation of what is shown.

#include <cstdio>
#include <deque>
#include <random>
#include <vector>

#include "timing_logic.hpp"

namespace
{
	struct scenario_t
	{
		const char* name;
		double tick_hz;
		double display_hz;
		int latency; // D3D maximum frame latency
		double spike_prob;
		double spike_min_ms;
		double spike_max_ms;
		double stall_at_s;
		double stall_ms;
		double seconds;
	};

	struct result_t
	{
		int uneven = 0;
		int artificial = 0;
		int repeats = 0; // vsyncs that showed an old frame again
		double sd_ms = 0.0;
		double spread_ms = 0.0;
	};

	result_t run(const scenario_t& sc, bool ours, unsigned seed)
	{
		std::mt19937 rng(seed);
		std::uniform_real_distribution<double> u(0.0, 1.0);
		const double t_ms = 1000.0 / sc.tick_hz;
		const double p_ms = 1000.0 / sc.display_hz;
		const double audio0 = 23.0;
		const int32_t audio_tick0 = static_cast<int32_t>(audio0 / t_ms);

		tb::timing_logic::controller ctl;
		int32_t tick = 0;
		bool odd = false;
		bool stalled = false;
		double start = 0.0;
		double last_return = 0.0;
		std::deque<double> shown; // display times of recent frames
		double prev_display = -1.0;
		int32_t prev_tick = 0;
		std::vector<double> dev;
		result_t r;
		int64_t frame = 0;

		while (start < sc.seconds * 1000.0)
		{
			++frame;
			double before = 0.3 + 0.3 * u(rng);
			double after = 1.0 + 0.5 * u(rng);
			if (u(rng) < sc.spike_prob)
			{
				const double spike = sc.spike_min_ms + (sc.spike_max_ms - sc.spike_min_ms) * u(rng);
				(u(rng) < 0.5 ? before : after) += spike;
			}
			if (!stalled && start >= sc.stall_at_s * 1000.0)
			{
				before += sc.stall_ms;
				stalled = true;
			}

			const double step = start + before;
			const double audio_true = step - audio0;
			const double audio_ms = std::floor(audio_true / 10.0) * 10.0;
			int32_t want = tick + 1;
			if (ours)
			{
				const double clock_pos = step / t_ms;
				if (audio_ms > 0.0 && (!ctl.anchored || (frame & 3) == 0))
					ctl.observe_audio(clock_pos, audio_tick0 + audio_ms / t_ms);
				const int32_t steered = ctl.target(tick, 1 << 30, clock_pos);
				if (steered >= 0)
					want = steered;
			}
			else
			{
				odd = !odd;
				if (odd && audio_ms > 0.0 && (want - audio_tick0) * t_ms + 17.0 < audio_ms)
					++want;
			}
			tick = want;

			// limiter: sleep to 1 ms before the frame budget (1 ms timer), then Present
			const double work_end = step + after;
			const double v7 = t_ms - (work_end - last_return) - 1.0;
			double present = work_end;
			if (v7 > 0.0)
				present += std::floor(v7) + 0.1 + 0.9 * u(rng);
			// Present blocks while 'latency' frames are still waiting for their vsync
			double ret = present;
			if (static_cast<int>(shown.size()) >= sc.latency)
			{
				const double oldest = shown[shown.size() - static_cast<size_t>(sc.latency)];
				if (oldest > present)
					ret = oldest;
			}
			ret += 0.05;
			const double ready = ret + 0.5;
			double display = std::ceil(ready / p_ms) * p_ms;
			if (!shown.empty())
				display = std::max(display, shown.back() + p_ms);
			shown.push_back(display);
			if (shown.size() > 8)
				shown.pop_front();

			if (prev_display > 0.0 && start > 2000.0)
			{
				const double vsyncs = (display - prev_display) / p_ms;
				const bool regular = std::fabs(vsyncs - 1.0) < 0.01;
				if (!regular)
					r.repeats += static_cast<int>(std::llround(vsyncs)) - 1;
				const auto expected = static_cast<int32_t>(std::llround((display - prev_display) / t_ms));
				if (tick - prev_tick != expected)
				{
					++r.uneven;
					if (regular)
						++r.artificial;
				}
				dev.push_back(tick * t_ms - display);
			}
			prev_display = display;
			prev_tick = tick;
			last_return = ret;
			start = ret;
		}

		double mean = 0.0, lo = 1e18, hi = -1e18;
		for (const double x : dev)
		{
			mean += x;
			lo = std::min(lo, x);
			hi = std::max(hi, x);
		}
		mean /= static_cast<double>(std::max<size_t>(1, dev.size()));
		double var = 0.0;
		for (const double x : dev)
			var += (x - mean) * (x - mean);
		r.sd_ms = std::sqrt(var / static_cast<double>(std::max<size_t>(1, dev.size())));
		r.spread_ms = dev.empty() ? 0.0 : hi - lo;
		return r;
	}

	int failures = 0;
}

int main()
{
	const scenario_t scenarios[] = {
		{ "TDJ 120 Hz, queue 3, CPU spikes 3-6 ms (2%)", 120, 120, 3, 0.02, 3, 6, 1e9, 0, 120 },
		{ "TDJ 120 Hz, queue 3, stalls 9-15 ms (1%)", 120, 120, 3, 0.01, 9, 15, 1e9, 0, 120 },
		{ "TDJ 120 Hz, queue 1, stalls 9-15 ms (1%)", 120, 120, 1, 0.01, 9, 15, 1e9, 0, 120 },
		{ "TDJ 120 Hz, queue 3, stalls 20-40 ms (0.3%)", 120, 120, 3, 0.003, 20, 40, 1e9, 0, 120 },
		{ "TDJ 120 Hz, queue 3, one 80 ms stall", 120, 120, 3, 0.0, 0, 0, 30, 80, 60 },
		{ "TDJ 120 ticks on a 119.88 Hz display", 120, 119.88, 3, 0.02, 3, 6, 1e9, 0, 120 },
		{ "TDJ 120 ticks on a 120.12 Hz display", 120, 120.12, 3, 0.02, 3, 6, 1e9, 0, 120 },
		{ "LDJ 60 Hz, queue 3, CPU spikes 5-14 ms (2%)", 60, 60, 3, 0.02, 5, 14, 1e9, 0, 120 },
		{ "LDJ 60 Hz, queue 1, one 80 ms stall", 60, 60, 1, 0.0, 0, 0, 30, 80, 60 },
		{ "LDJ 60 Hz, queue 3, stalls 20-40 ms (0.3%)", 60, 60, 3, 0.003, 20, 40, 1e9, 0, 120 },
		{ "TDJ 120 Hz, queue 2, stalls 9-30 ms (0.5%)", 120, 120, 2, 0.005, 9, 30, 1e9, 0, 120 },
	};
	for (const auto& sc : scenarios)
	{
		const auto game = run(sc, false, 7);
		const auto ours = run(sc, true, 7);
		std::printf("%s\n", sc.name);
		std::printf("  game  uneven %4d (artificial %4d), %4d repeated vsyncs | sync sd %5.2f ms, spread %6.2f ms\n",
			game.uneven, game.artificial, game.repeats, game.sd_ms, game.spread_ms);
		std::printf("  ours  uneven %4d (artificial %4d), %4d repeated vsyncs | sync sd %5.2f ms, spread %6.2f ms\n",
			ours.uneven, ours.artificial, ours.repeats, ours.sd_ms, ours.spread_ms);
		// never more judder on a regular display than the game (beyond the drift a
		// rate mismatch forces), never worse sync
		const double drift_ticks = std::fabs(sc.display_hz - sc.tick_hz) * sc.seconds;
		bool ok = ours.artificial <= game.artificial + 1 + static_cast<int>(drift_ticks * 1.2);
		ok = ok && ours.sd_ms <= game.sd_ms + 0.25;
		std::printf("  -> %s\n\n", ok ? "ok" : "FAIL");
		if (!ok)
			++failures;
	}
	std::printf(failures ? "timing_logic_test: %d FAILED\n" : "timing_logic_test: all checks passed\n", failures);
	return failures ? 1 : 0;
}