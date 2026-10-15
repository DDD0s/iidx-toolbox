#include "std_include.hpp"

#include "component.hpp"
#include "d3d.hpp"
#include "hook.hpp"
#include "log.hpp"

namespace tb::d3d
{
	namespace
	{
		using create_device_ex_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD,
			D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);
		using create_device_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
			D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);

		hook::detour g_create_device;
		hook::detour g_create_device_ex;
		hook::detour g_present;
		hook::detour g_reset;
		hook::detour g_reset_ex;
		std::once_flag g_device_once;

		std::atomic<bool> g_ready{ false };
		std::atomic<ULONGLONG> g_last_main_present{ 0 };
		std::atomic<HWND> g_main_window{ nullptr };

		HWND chain_window(IDirect3DSwapChain9* chain, IDirect3DDevice9* device)
		{
			D3DPRESENT_PARAMETERS pp{};
			if (SUCCEEDED(chain->GetPresentParameters(&pp)) && pp.hDeviceWindow)
				return pp.hDeviceWindow;
			D3DDEVICE_CREATION_PARAMETERS cp{};
			if (SUCCEEDED(device->GetCreationParameters(&cp)))
				return cp.hFocusWindow;
			return nullptr;
		}
		std::atomic<IDirect3DDevice9*> g_device{ nullptr };
		std::atomic<IDirect3DSwapChain9*> g_main_chain{ nullptr };
		thread_local bool g_inside = false;

		std::vector<chain_callback> g_before_present;
		std::vector<device_callback> g_before_reset;
		std::vector<device_callback> g_after_reset;

		void refresh_main_chain()
		{
			IDirect3DSwapChain9* chain = nullptr;
			if (const auto device = g_device.load(); device && SUCCEEDED(device->GetSwapChain(0, &chain)) && chain)
			{
				g_main_chain = chain;
				chain->Release(); // the device keeps it alive
			}
		}

		// a device created first and released later (or replaced) must not keep the
		// callbacks: when the main chain has been silent for a second, the implicit
		// chain of another HAL device on the same window takes over (never a second
		// screen's device)
		void maybe_readopt(IDirect3DSwapChain9* chain)
		{
			const ULONGLONG now = GetTickCount64();
			if (now - g_last_main_present.load() < 1000)
				return;
			IDirect3DDevice9* device = nullptr;
			if (FAILED(chain->GetDevice(&device)) || !device)
				return;
			D3DDEVICE_CREATION_PARAMETERS cp{};
			IDirect3DSwapChain9* first = nullptr;
			if (SUCCEEDED(device->GetCreationParameters(&cp)) && cp.DeviceType == D3DDEVTYPE_HAL &&
				SUCCEEDED(device->GetSwapChain(0, &first)) && first)
			{
				if (first == chain && chain_window(chain, device) == g_main_window.load())
				{
					g_device = device;
					g_main_chain = chain;
					g_last_main_present = now;
					tb::log("d3d", "main swap chain is now %p (device %p)", static_cast<void*>(chain), static_cast<void*>(device));
				}
				first->Release();
			}
			device->Release();
		}

		HRESULT STDMETHODCALLTYPE present_stub(IDirect3DSwapChain9* chain, const RECT* src, const RECT* dst,
			HWND override_window, const RGNDATA* dirty, DWORD flags)
		{
			if (!g_inside && chain != g_main_chain.load() && g_ready.load(std::memory_order_acquire))
				maybe_readopt(chain);
			const bool main = g_ready.load(std::memory_order_acquire) && !g_inside && chain == g_main_chain.load();
			if (main)
				g_last_main_present = GetTickCount64();
			if (main)
			{
				g_inside = true;
				for (auto& callback : g_before_present)
					callback(chain);
			}
			const auto hr = g_present.invoke<HRESULT>(chain, src, dst, override_window, dirty, flags);
			if (main)
				g_inside = false;
			return hr;
		}

		HRESULT STDMETHODCALLTYPE reset_stub(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pp)
		{
			const bool ours = g_ready.load(std::memory_order_acquire) && device == g_device.load();
			if (ours)
				for (auto& callback : g_before_reset)
					callback(device);
			const auto hr = g_reset.invoke<HRESULT>(device, pp);
			if (ours)
			{
				refresh_main_chain();
				if (SUCCEEDED(hr))
					for (auto& callback : g_after_reset)
						callback(device);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE reset_ex_stub(IDirect3DDevice9Ex* device, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* mode)
		{
			const bool ours = g_ready.load(std::memory_order_acquire) && device == g_device.load();
			if (ours)
				for (auto& callback : g_before_reset)
					callback(device);
			const auto hr = g_reset_ex.invoke<HRESULT>(device, pp, mode);
			if (ours)
			{
				refresh_main_chain();
				if (SUCCEEDED(hr))
					for (auto& callback : g_after_reset)
						callback(device);
			}
			return hr;
		}

		void try_hook(hook::detour& detour, void* place, void* target, const char* name)
		{
			try
			{
				detour.create(place, target);
			}
			catch (const std::exception& e)
			{
				tb::log("d3d", "%s not hooked (%s)", name, e.what());
			}
		}

		// runs inside the game's CreateDevice: nothing may throw out of here
		void adopt_device(IDirect3DDevice9* device)
		{
			std::call_once(g_device_once, [device] {
				IDirect3DSwapChain9* chain = nullptr;
				if (FAILED(device->GetSwapChain(0, &chain)) || !chain)
				{
					tb::log("d3d", "device has no swap chain; frame hooks unavailable");
					return;
				}
				void** device_vt = *reinterpret_cast<void***>(device);
				void** chain_vt = *reinterpret_cast<void***>(chain);
				chain->Release();
				g_device = device;
				refresh_main_chain();
				g_main_window = chain_window(g_main_chain.load(), device);
				g_last_main_present = GetTickCount64();
				try_hook(g_present, chain_vt[3], reinterpret_cast<void*>(&present_stub), "Present");
				try_hook(g_reset, device_vt[16], reinterpret_cast<void*>(&reset_stub), "Reset");
				IDirect3DDevice9Ex* is_ex = nullptr;
				if (SUCCEEDED(device->QueryInterface(__uuidof(IDirect3DDevice9Ex), reinterpret_cast<void**>(&is_ex))) && is_ex)
				{
					is_ex->Release();
					try_hook(g_reset_ex, device_vt[132], reinterpret_cast<void*>(&reset_ex_stub), "ResetEx");
				}
				UINT latency = 0;
				IDirect3DDevice9Ex* ex = nullptr;
				if (SUCCEEDED(device->QueryInterface(__uuidof(IDirect3DDevice9Ex), reinterpret_cast<void**>(&ex))) && ex)
				{
					ex->GetMaximumFrameLatency(&latency);
					ex->Release();
				}
				tb::log("d3d", "game device %p (max frame latency %u), swap chain Present hooked", static_cast<void*>(device), latency);
			});
		}

		HRESULT STDMETHODCALLTYPE create_device_ex_stub(IDirect3D9Ex* d3d, UINT adapter, D3DDEVTYPE type, HWND window,
			DWORD flags, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* mode, IDirect3DDevice9Ex** out)
		{
			const auto hr = g_create_device_ex.invoke<HRESULT>(d3d, adapter, type, window, flags, pp, mode, out);
			if (SUCCEEDED(hr) && out && *out && type == D3DDEVTYPE_HAL)
				adopt_device(*out);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE create_device_stub(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND window,
			DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out)
		{
			const auto hr = g_create_device.invoke<HRESULT>(d3d, adapter, type, window, flags, pp, out);
			if (SUCCEEDED(hr) && out && *out && type == D3DDEVTYPE_HAL)
				adopt_device(*out);
			return hr;
		}
	}

	void on_before_present(chain_callback callback) { g_before_present.push_back(std::move(callback)); }
	void on_before_reset(device_callback callback) { g_before_reset.push_back(std::move(callback)); }
	void on_after_reset(device_callback callback) { g_after_reset.push_back(std::move(callback)); }

	IDirect3DDevice9* device()
	{
		return g_device.load();
	}

	void set_ready()
	{
		g_ready.store(true, std::memory_order_release);
	}

	// spice2x loads the module before the game creates its device. The callbacks
	// are registered by components with a higher priority number, before any frame.
	class component final : public component_interface
	{
	public:
		void post_load() override
		{
			const HMODULE d3d9 = LoadLibraryA("d3d9.dll");
			using create_t = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
			const auto create = d3d9 ? reinterpret_cast<create_t>(GetProcAddress(d3d9, "Direct3DCreate9Ex")) : nullptr;
			IDirect3D9Ex* d3d = nullptr;
			const HRESULT created = create ? create(D3D_SDK_VERSION, &d3d) : E_NOINTERFACE;
			if (FAILED(created) || !d3d)
				throw std::runtime_error("Direct3DCreate9Ex failed");
			void** vt = *reinterpret_cast<void***>(d3d);
			g_create_device.create(vt[16], reinterpret_cast<void*>(&create_device_stub));
			g_create_device_ex.create(vt[20], reinterpret_cast<void*>(&create_device_ex_stub));
			d3d->Release();
			tb::log("d3d", "waiting for the game's device");
		}

	};
}

TB_REGISTER_COMPONENT(tb::d3d::component, 0)