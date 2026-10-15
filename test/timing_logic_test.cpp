// Frame-level simulation of the millisecond clock: a vsync grid, the game's frame
// limiter, CPU spikes that do or do not miss a vsync, a long stall, a display
// slightly slower than the chart, and audio in 10 ms steps. Compares the game's
// own stepping, a time-based controller and the vsync-grid controller.

#include <cstdio>
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
		double spike_prob;
		double spike_min_ms;
		double spike_max_ms;
		double stall_at_s;
		double stall_ms;
		double seconds;
		double gpu_hiccup_prob = 0.0; // the GPU delays a frame by a vsync with the step on time
	};

	struct result_t
	{
		int uneven = 0;     // frames whose chart step differs from what the display did
		int artificial = 0; // of those, frames where the display itself was regular
		int drops = 0;      // displayed frames that skipped vsyncs
		double spread_ms = 0.0;
		double sd_ms = 0.0;
	};

	// first version: clock from the step time, no confirmation
	struct time_controller
	{
		double offset = 0.0;
		bool anchored = false;
		void observe(double clock_pos, double audio_pos)
		{
			const double err = audio_pos - clock_pos;
			if (!anchored || std::fabs(err - offset) > 3.0)
			{
				offset = err;
				anchored = true;
			}
			else
				offset += (err - offset) * 0.02;
		}
		int32_t target(int32_t tick, double clock_pos) const
		{
			const double ideal = clock_pos + offset;
			const int32_t nominal = tick + 1;
			int32_t want = nominal;
			if (std::fabs(ideal - nominal) > 0.75)
				want = static_cast<int32_t>(std::floor(ideal + 0.5));
			return std::clamp(want, tick, tick + 8);
		}
	};

	result_t run(const scenario_t& sc, int method, unsigned seed)
	{
		std::mt19937 rng(seed);
		std::uniform_real_distribution<double> u(0.0, 1.0);
		const double t_ms = 1000.0 / sc.tick_hz;
		const double p_ms = 1000.0 / sc.display_hz;
		const double audio0 = 23.0;
		const int32_t audio_tick0 = static_cast<int32_t>(audio0 / t_ms);

		tb::timing_logic::controller ctl;
		tb::timing_logic::miss_predictor predictor;
		int64_t prev_start = -1;
		double prev_phase = 0.0;
		time_controller old;
		int32_t tick = 0;
		int64_t base_index = 0;
		bool based = false;
		bool odd = false;
		double v = p_ms * 4; // vsync the frame starts on
		bool stalled = false;
		std::vector<double> dev;
		result_t r;
		int64_t frame = 0;
		double prev_display = -1.0;
		int32_t prev_tick = 0;

		while (v < sc.seconds * 1000.0)
		{
			++frame;
			double d = 0.4 + 0.3 * u(rng);
			if (u(rng) < sc.spike_prob)
				d += sc.spike_min_ms + (sc.spike_max_ms - sc.spike_min_ms) * u(rng);
			if (!stalled && v >= sc.stall_at_s * 1000.0)
			{
				d += sc.stall_ms;
				stalled = true;
			}
			const double s = v + d; // play step time
			const double audio_true = s - audio0;
			const double audio_ms = std::floor(audio_true / 10.0) * 10.0;

			int32_t want = tick + 1;
			if (method == 0)
			{
				odd = !odd;
				const double chart_ms = (want - audio_tick0) * t_ms;
				if (odd && audio_ms > 0.0 && chart_ms + 17.0 < audio_ms)
					++want;
			}
			else if (method == 1)
			{
				const double clock_pos = s / t_ms;
				if (audio_ms > 0.0)
					old.observe(clock_pos, audio_tick0 + audio_ms / t_ms);
				if (old.anchored)
					want = old.target(tick, clock_pos);
			}
			else
			{
				const int64_t start = static_cast<int64_t>(std::llround(v / p_ms));
				const double phase = (s - v) / p_ms;
				if (prev_start >= 0)
					predictor.learn(prev_phase, start - prev_start - 1);
				prev_start = start;
				prev_phase = phase;
				const int64_t shown = start + 1 + predictor.vsyncs_late(phase);
				if (!based)
				{
					base_index = shown;
					based = true;
				}
				const double clock_pos = static_cast<double>(shown - base_index) * p_ms / t_ms;
				if (audio_ms > 0.0 && (!ctl.anchored || (frame & 3) == 0))
					ctl.observe_audio(clock_pos, audio_tick0 + audio_ms / t_ms);
				const int32_t steered = ctl.target(tick, 1 << 30, shown, p_ms / t_ms, clock_pos);
				if (steered >= 0)
					want = steered;
			}
			tick = want;

			// limiter sleeps to 1 ms before the frame budget, then Present waits for vsync
			const double present = std::max(s + 1.5, v + t_ms - 1.0 + 0.6 * u(rng));
			const double gpu = u(rng) < sc.gpu_hiccup_prob ? p_ms : 0.0;
			const double display = std::ceil((present + 0.3 + gpu) / p_ms) * p_ms;

			if (prev_display > 0.0 && v > 2000.0)
			{
				const double refreshes = (display - prev_display) / p_ms;
				const bool regular = std::fabs(refreshes - 1.0) < 0.01;
				if (!regular)
					++r.drops;
				const double expected = (display - prev_display) / t_ms;
				if ((tick - prev_tick) != static_cast<int32_t>(std::llround(expected)))
				{
					++r.uneven;
					if (regular)
						++r.artificial;
				}
				dev.push_back(tick * t_ms - (display - audio0));
			}
			prev_display = display;
			prev_tick = tick;
			v = display;
		}

		double lo = 1e9, hi = -1e9, sum = 0.0;
		for (const double x : dev)
		{
			lo = std::min(lo, x);
			hi = std::max(hi, x);
			sum += x;
		}
		const double mean = dev.empty() ? 0.0 : sum / static_cast<double>(dev.size());
		double var = 0.0;
		for (const double x : dev)
			var += (x - mean) * (x - mean);
		r.spread_ms = dev.empty() ? 0.0 : hi - lo;
		r.sd_ms = dev.empty() ? 0.0 : std::sqrt(var / static_cast<double>(dev.size()));
		return r;
	}

	int failures = 0;
}

int main()
{
	const scenario_t scenarios[] = {
		{ "TDJ 120/120, CPU spikes 3-6 ms (2%)", 120, 120, 0.02, 3, 6, 1e9, 0, 120 },
		{ "TDJ 120/120, spikes that miss a vsync (1%)", 120, 120, 0.01, 9, 15, 1e9, 0, 120 },
		{ "TDJ 120/120, one 80 ms stall", 120, 120, 0.0, 0, 0, 30, 80, 60 },
		{ "TDJ 120 ticks on 119.88 Hz", 120, 119.88, 0.02, 3, 6, 1e9, 0, 120 },
		{ "LDJ 60/60, spikes 5-14 ms (2%)", 60, 60, 0.02, 5, 14, 1e9, 0, 120 },
		{ "LDJ 60/60, one 80 ms stall", 60, 60, 0.0, 0, 0, 30, 80, 60 },
		{ "TDJ 120/120, GPU hiccups (0.5%) + CPU spikes", 120, 120, 0.02, 3, 12, 1e9, 0, 120, 0.005 },
	};
	const char* names[] = { "game", "time-based", "grid+predict" };
	for (const auto& sc : scenarios)
	{
		std::printf("%s\n", sc.name);
		result_t res[3];
		for (int m = 0; m < 3; ++m)
		{
			res[m] = run(sc, m, 7);
			std::printf("  %-11s uneven %4d (artificial %4d) of %4d drops | sync spread %6.2f ms  sd %5.2f ms\n",
				names[m], res[m].uneven, res[m].artificial, res[m].drops, res[m].spread_ms, res[m].sd_ms);
		}
		// the new controller must be at least as even and as well synced as both others
		const auto& g = res[2];
		const double tick_ms = 1000.0 / sc.tick_hz;
		bool ok = g.uneven <= std::min(res[0].uneven, res[1].uneven) + 2;
		// one learning transient may show in the worst case
		ok = ok && g.spread_ms <= std::min(res[0].spread_ms, res[1].spread_ms) + tick_ms + 0.05;
		// integer ticks: the chart may settle on either side of the audio within half a tick
		ok = ok && g.sd_ms <= std::min(res[0].sd_ms, res[1].sd_ms) + tick_ms * 0.5 + 0.5;
		std::printf("  -> %s\n\n", ok ? "ok" : "FAIL");
		if (!ok)
			++failures;
	}
	std::printf(failures ? "timing_logic_test: %d FAILED\n" : "timing_logic_test: all checks passed\n", failures);
	return failures ? 1 : 0;
}