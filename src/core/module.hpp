#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

// The game module (bm2dx.dll) and signature scanning: hex bytes, '?' = one
// wildcard byte, spaces ignored.
namespace tb::module
{
	void set(HMODULE handle);
	HMODULE handle();

	std::vector<uint8_t*> find_all(const std::string& pattern);

	// nullptr unless the pattern matches exactly once
	uint8_t* find_unique(const std::string& pattern);

	// target of a rel32 operand at 'at' (next instruction = at + 4 + extra)
	uint8_t* rel32(uint8_t* at, int extra = 0);
}
