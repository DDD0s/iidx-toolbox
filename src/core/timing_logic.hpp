#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

// Target-tick decision for the millisecond clock (test/timing_logic_test.cpp).
// Positions are in ticks: clock_pos from the time of the play step, audio_pos from
// the game's audio position. The chart advances one tick per frame.
// - The deviation is measured from the song's own phase (median of the first
//   frames), so the game's start phase is kept and the band is symmetric.
// - A CPU stall that the render queue absorbs leaves no visible drop: the frames
//   after it come a little short (the game's limiter) until the queue is full
//   again, so the deviation keeps shrinking. A real dropped frame or a display
//   off the tick rate leaves a deviation that stays or grows.
// - Only a deviation outside the band that has not been shrinking over the last
//   k_confirm regular frames (frames without a jump) is corrected.
namespace tb::timing_logic
{
	// band: at least one tick and at least 12.5 ms (the game itself waits for 17 ms)
	constexpr double k_band_ms = 12.5;
	// after an absorbed stall the limiter runs frames 0.3-1 ms short: 0.02-0.06 tick per frame at 60 Hz
	constexpr int k_confirm = 12;        // regular frames
	constexpr double k_shrinking = 0.15; // ticks over k_confirm regular frames
	constexpr double k_jump = 0.3;       // per-frame change that marks a stall frame
	constexpr int k_centre_frames = 31;
	constexpr double k_anchor_rate = 0.08; // per audio sample
	constexpr double k_resnap = 3.0;
	constexpr int k_max_catch_up = 8;

	inline double band_for(double ms_per_tick)
	{
		return std::max(1.0, k_band_ms / ms_per_tick);
	}

	struct controller
	{
		double band = 1.0;   // ticks, from band_for()
		double offset = 0.0; // audio_pos - clock_pos, low-passed
		bool anchored = false;
		uint64_t resnaps = 0;
		uint64_t absorbed = 0; // deviations that faded before confirmation
		double max_dev = 0.0;

		std::array<double, k_centre_frames> first{};
		int first_count = 0;
		double centre = 0.0;

		int run = 0;             // signed count of consecutive frames outside the band
		int regular = 0;         // regular frames in this run
		double trend[k_confirm] = {}; // per-frame changes of regular frames in this run
		double last_dev = 0.0;
		bool has_last = false;
		bool catching_up = false; // a confirmed correction larger than k_max_catch_up

		void observe_audio(double clock_pos, double audio_pos)
		{
			const double err = audio_pos - clock_pos;
			if (!anchored || std::fabs(err - offset) > k_resnap)
			{
				if (anchored)
					++resnaps;
				offset = err;
				anchored = true;
				has_last = false;
				return;
			}
			offset += (err - offset) * k_anchor_rate;
		}

		// after a pause in steering (frame rate mismatch), older observations do not count
		void reset_confirmation()
		{
			run = 0;
			regular = 0;
			catching_up = false;
			has_last = false;
		}

		[[nodiscard]] bool centred() const
		{
			return first_count >= k_centre_frames;
		}

		// -1 = leave the game's own stepping alone
		int32_t target(int32_t tick, int32_t end, double clock_pos)
		{
			if (!anchored)
				return -1;
			const int32_t nominal = std::min(tick + 1, end);
			const double ideal = clock_pos + offset;
			const double raw = ideal - nominal;

			if (!centred())
			{
				first[static_cast<size_t>(first_count++)] = raw;
				if (centred())
				{
					auto sorted = first;
					std::nth_element(sorted.begin(), sorted.begin() + k_centre_frames / 2, sorted.end());
					// the game's start phase, but never further from the audio than one band
					centre = std::clamp(sorted[k_centre_frames / 2], -band, band);
				}
				return nominal;
			}

			const double dev = raw - centre;
			max_dev = std::max(max_dev, std::fabs(dev));
			const double change = has_last ? dev - last_dev : 0.0;
			const bool is_regular = has_last && std::fabs(change) < k_jump;
			last_dev = dev;
			has_last = true;

			const int dir = dev > band ? 1 : dev < -band ? -1 : 0;
			if (dir == 0)
			{
				if (run != 0 && !catching_up)
					++absorbed;
				run = 0;
				regular = 0;
				catching_up = false;
				return nominal;
			}
			if (run == 0 || (run > 0) != (dir > 0))
			{
				run = dir;
				regular = 0;
				catching_up = false;
			}
			else
				run += dir;
			if (is_regular)
				trend[regular++ % k_confirm] = change;

			if (!catching_up)
			{
				if (regular < k_confirm)
					return nominal;
				double sum = 0.0;
				for (const double c : trend)
					sum += c;
				// shrinking towards the centre: the queue is still refilling
				if (sum * dir < -k_shrinking)
					return nominal;
			}

			const auto want = static_cast<int32_t>(std::floor(ideal - centre + 0.5));
			const int32_t clamped = std::clamp(want, tick, std::min(tick + k_max_catch_up, end));
			catching_up = clamped != want; // continue on the next frames without waiting again
			if (!catching_up)
			{
				run = 0;
				regular = 0;
			}
			has_last = false; // the correction itself is not a trend
			return clamped;
		}
	};
}