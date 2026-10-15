#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

// Target-tick decision for the millisecond clock (test/timing_logic_test.cpp).
// Positions are in ticks: clock_pos from the time of the play step, audio_pos from
// the game's audio position. The chart advances one tick per frame. A CPU stall
// that the render queue absorbs leaves no visible drop: the frames after it come
// a little faster (the game's limiter) until the queue is full again, so the
// deviation shrinks back. A real dropped frame leaves a deviation that stays.
// Only a deviation outside the band that has stopped shrinking is corrected.
namespace tb::timing_logic
{
	constexpr double k_hysteresis = 1.0;
	constexpr int k_confirm = 8;        // frames, also the window for the shrink test
	constexpr double k_shrinking = 0.25; // ticks over k_confirm frames
	constexpr double k_anchor_rate = 0.08; // per audio sample
	constexpr double k_resnap = 3.0;
	constexpr int k_max_catch_up = 8;

	struct controller
	{
		double offset = 0.0; // audio_pos - clock_pos, low-passed
		bool anchored = false;
		int run = 0; // signed count of consecutive frames outside the band
		double history[k_confirm] = {};
		int history_pos = 0;
		uint64_t resnaps = 0;
		uint64_t absorbed = 0; // deviations that faded before confirmation
		double max_dev = 0.0;

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

		// -1 = leave the game's own stepping alone
		int32_t target(int32_t tick, int32_t end, double clock_pos)
		{
			if (!anchored)
				return -1;
			const int32_t nominal = std::min(tick + 1, end);
			const double ideal = clock_pos + offset;
			const double dev = ideal - nominal;
			max_dev = std::max(max_dev, std::fabs(dev));
			const double earlier = history[history_pos];
			history[history_pos] = dev;
			history_pos = (history_pos + 1) % k_confirm;

			const int dir = dev > k_hysteresis ? 1 : dev < -k_hysteresis ? -1 : 0;
			if (dir == 0)
			{
				if (run != 0)
					++absorbed;
				run = 0;
				return nominal;
			}
			run = (run == 0 || (run > 0) != (dir > 0)) ? dir : run + dir;
			// 'earlier' must lie inside this run, so the run has to be longer than the window
			if (std::abs(run) <= k_confirm || std::fabs(dev) < std::fabs(earlier) - k_shrinking)
				return nominal;

			const auto want = static_cast<int32_t>(std::floor(ideal + 0.5));
			const int32_t clamped = std::clamp(want, tick, std::min(tick + k_max_catch_up, end));
			if (clamped == want)
				run = 0; // a clamped catch-up continues on the next frame
			return clamped;
		}
	};
}