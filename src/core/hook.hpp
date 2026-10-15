#pragma once

#include <Windows.h>
#include <cstdint>
#include <stdexcept>

namespace tb::hook
{
	class detour
	{
	public:
		detour() = default;
		// never removed on destruction: at exit another thread may still be inside a stub
		~detour() = default;
		detour(const detour&) = delete;
		detour& operator=(const detour&) = delete;

		void create(void* place, void* target);
		void clear();

		template <typename T = void, typename... Args>
		T invoke(Args... args)
		{
			return static_cast<T (*)(Args...)>(this->original_)(args...);
		}

		[[nodiscard]] bool installed() const { return this->place_ != nullptr; }

	private:
		void* place_{};
		void* original_{};
	};

	void copy(void* place, const void* data, size_t length);
	void nop(void* place, size_t length);

	template <typename T>
	void set(void* place, T value)
	{
		copy(place, &value, sizeof(T));
	}
}