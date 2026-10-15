#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Target-tick decision for the millisecond clock (test/timing_logic_test.cpp).
// 'shown' is the vsync on which the frame will be shown (whole vsyncs, so jitter
// inside a frame does not move it). The chart advances by exactly the vsyncs
// between shown frames (cadence); the audio position only corrects slow drift.
// The target is the tick count the play step should have processed after this frame.
namespace tb::timing_logic
{
	constexpr double k_hysteresis = 0.75;
	constexpr double k_anchor_rate = 0.08; // per audio sample (every 4th frame)
	constexpr double k_resnap = 3.0;
	constexpr int k_max_catch_up = 8;

	struct controller
	{
		double offset = 0.0; // audio_pos - clock_pos, low-passed
		bool anchored = false;
		uint64_t resnaps = 0;
		uint64_t audio_corrections = 0;
		double pos = 0.0; // ticks processed after this frame, fractional
		int64_t last_shown = 0;
		bool started = false;

		void observe_audio(double clock_pos, double audio_pos)
		{
			const double err = audio_pos - clock_pos;
			if (!anchored || std::fabs(err - offset) > k_resnap)
			{
				if (anchored)
					++resnaps;
				offset = err;
				anchored = true;
				return;
			}
			offset += (err - offset) * k_anchor_rate;
		}

		// clock_pos = (shown - base) * ticks_per_vsync; -1 = leave the game's own stepping alone
		int32_t target(int32_t tick, int32_t end, int64_t shown, double ticks_per_vsync, double clock_pos)
		{
			if (!anchored)
				return -1;
			if (!started)
			{
				pos = tick + 1.0;
				started = true;
			}
			else
				pos += static_cast<double>(shown - last_shown) * ticks_per_vsync;
			last_shown = shown;

			const double ideal = clock_pos + offset;
			if (std::fabs(ideal - pos) > k_hysteresis)
			{
				pos += std::floor(ideal - pos + 0.5);
				++audio_corrections;
			}
			pos = std::max(pos, static_cast<double>(tick));
			const auto want = static_cast<int32_t>(std::floor(pos + 0.5));
			return std::clamp(want, tick, std::min(tick + k_max_catch_up, end));
		}
	};

	// A frame starts on the vsync that showed the previous one. If its play step
	// runs late in that period, the frame misses the next vsync. The phase from
	// which that happens is learned from what the following frames show.
	struct miss_predictor
	{
		double threshold = 1.0; // step phase, in vsync periods after the start vsync
		double typical = 0.0;   // usual phase of frames that were on time
		uint64_t learned = 0;

		[[nodiscard]] int vsyncs_late(double phase) const
		{
			if (phase < threshold)
				return 0;
			return 1 + static_cast<int>(std::floor(phase - threshold));
		}

		// late = vsyncs the frame really came late (shown at start + 1 + late).
		// The boundary repeats every period: a step at threshold + n misses n + 1 vsyncs.
		void learn(double phase, int64_t late)
		{
			if (late < 0 || late > 6 || phase < 0.0 || phase >= 6.0)
				return;
			const int predicted = vsyncs_late(phase);
			if (late == 0 && phase < 1.0)
				typical = typical > 0.0 ? typical + (phase - typical) * 0.02 : phase;
			const double boundary_lo = typical + 0.15;
			if (predicted < late)
			{
				// a miss at the usual phase was not caused by a late step (GPU, driver)
				const double candidate = phase - static_cast<double>(late - 1) - 0.02;
				if (candidate > boundary_lo)
				{
					threshold = std::min(1.0, candidate);
					++learned;
				}
			}
			else if (predicted > late)
			{
				threshold = std::clamp(phase - static_cast<double>(late) + 0.02, boundary_lo, 1.0);
				++learned;
			}
		}
	};
}