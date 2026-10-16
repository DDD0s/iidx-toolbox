#pragma once

#include <cstdint>

// MS CLOCK keeps the game's own timing. The game advances one tick per step and,
// at every second step, adds one tick when the audio position leads the chart by
// more than 17 ms (it never pulls back), so after a stall it catches up one tick
// per two frames. MS CLOCK adds at once all but the last of the ticks the game
// would add over its next checks and leaves the last one to the game's own check:
// the chart ends where the game would, sooner. It never removes a tick and does
// nothing while one game check is enough, so in normal play nothing changes.
// test/timing_logic_test.cpp
namespace tb::timing_logic
{
	constexpr int k_game_threshold_ms = 17;
	constexpr int k_max_extra = 8; // per step

	// the game's chart time at a tick, as in its own check (sub_180743DE0)
	inline int chart_ms(int32_t tick, int32_t audio_tick0, float fps)
	{
		return static_cast<int>(static_cast<float>(static_cast<int>(1000 * (tick - audio_tick0))) / fps);
	}

	// ticks to add before a step that will process up to 'target'
	inline int extra_ticks(int audio_ms, int32_t target, int32_t audio_tick0, float fps)
	{
		if (audio_ms <= 0 || chart_ms(target, audio_tick0, fps) <= 0)
			return 0;
		int extra = 0;
		// one more tick only while the game would still add another after it
		while (extra < k_max_extra && chart_ms(target + extra + 1, audio_tick0, fps) + k_game_threshold_ms < audio_ms)
			++extra;
		return extra;
	}
}
