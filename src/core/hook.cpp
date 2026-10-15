#include "hook.hpp"

#include <MinHook.h>

#include <cstring>
#include <mutex>

namespace tb::hook
{
	namespace
	{
		void minhook_init()
		{
			static std::once_flag once;
			std::call_once(once, [] {
				const auto status = MH_Initialize();
				if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
					throw std::runtime_error("MinHook init failed");
			});
		}
	}

	void detour::create(void* place, void* target)
	{
		minhook_init();
		this->clear();
		if (MH_CreateHook(place, target, &this->original_) != MH_OK)
			throw std::runtime_error("MH_CreateHook failed");
		if (MH_EnableHook(place) != MH_OK)
		{
			MH_RemoveHook(place);
			throw std::runtime_error("MH_EnableHook failed");
		}
		this->place_ = place;
	}

	void detour::clear()
	{
		if (this->place_)
			MH_RemoveHook(this->place_);
		this->place_ = nullptr;
		this->original_ = nullptr;
	}

	void copy(void* place, const void* data, size_t length)
	{
		DWORD old_protect{};
		VirtualProtect(place, length, PAGE_EXECUTE_READWRITE, &old_protect);
		std::memmove(place, data, length);
		VirtualProtect(place, length, old_protect, &old_protect);
		FlushInstructionCache(GetCurrentProcess(), place, length);
	}

	void nop(void* place, size_t length)
	{
		DWORD old_protect{};
		VirtualProtect(place, length, PAGE_EXECUTE_READWRITE, &old_protect);
		std::memset(place, 0x90, length);
		VirtualProtect(place, length, old_protect, &old_protect);
		FlushInstructionCache(GetCurrentProcess(), place, length);
	}
}
