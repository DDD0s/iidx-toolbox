#pragma once

#include <atomic>

#include "random_logic.hpp"

// All settings, stored in iidx_toolbox.ini next to the DLL. Scalars are atomics
// (read on the game thread, written by the menu); Random Fix sides are copied
// under a lock.
namespace tb::config
{
	void load();
	void save();

	bool log_enabled();
	int menu_key(); // virtual-key code, default VK_INSERT

	extern std::atomic<bool> timing_enabled;

	struct random_t
	{
		bool enabled = false;
		random_logic::side_config_t side[2]; // 0 = P1 / DP left, 1 = P2 / DP right
	};
	random_t get_random();
	// menu: applies only the fields that differ between before and after, so a
	// draw stored at play start in between is not overwritten; saves
	void set_random(const random_t& before, const random_t& after);
	void store_drawn(int side, const random_logic::perm_t& perm); // play start draw; saves

	extern std::atomic<bool> all_scratch; // mutually exclusive with all_charge
	extern std::atomic<bool> all_charge;

	// percent 0..100, 100 = unchanged
	extern std::atomic<int> bomb_opacity;
	extern std::atomic<int> beam_opacity;
	extern std::atomic<int> judge_opacity;
	extern std::atomic<bool> pgreat_blue;
	extern std::atomic<bool> no_blink;
}
