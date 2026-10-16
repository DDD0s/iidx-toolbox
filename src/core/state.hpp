#pragma once

#include <atomic>

// Runtime state shared between components.
namespace tb::state
{
	inline std::atomic<bool> menu_open{ false };
}
