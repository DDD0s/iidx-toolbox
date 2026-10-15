#pragma once

#include <d3d9.h>

#include <functional>

// The game's first HAL device and its implicit swap chain (TDJ adds a second
// swap chain for the sub screen; callbacks only see the main one). Present is
// hooked when the game creates the device.
namespace tb::d3d
{
	using chain_callback = std::function<void(IDirect3DSwapChain9*)>;
	using device_callback = std::function<void(IDirect3DDevice9*)>;

	void on_before_present(chain_callback callback); // render thread, before the original Present
	void on_before_reset(device_callback callback);
	void on_after_reset(device_callback callback);

	IDirect3DDevice9* device();
}