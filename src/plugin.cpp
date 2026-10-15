#include "core/std_include.hpp"

#include <thread>

#include "core/component.hpp"
#include "core/config.hpp"
#include "core/log.hpp"
#include "core/module.hpp"
#include "core/paths.hpp"

namespace tb::components
{
	namespace
	{
		struct entry_t
		{
			std::string name;
			int priority;
			factory_t factory;
		};

		std::vector<entry_t>& factories()
		{
			static std::vector<entry_t> list;
			return list;
		}
	}

	void register_factory(const char* name, int priority, factory_t factory)
	{
		factories().push_back({ name, priority, std::move(factory) });
	}
}

namespace
{
	HMODULE g_self = nullptr;
	std::once_flag g_init_once;
	std::atomic<bool> g_shut_down{ false };
	std::vector<std::unique_ptr<component_interface>> g_components;

	std::string self_directory()
	{
		char path[MAX_PATH] = {};
		GetModuleFileNameA(g_self, path, sizeof(path));
		std::string dir(path);
		const auto slash = dir.find_last_of("\\/");
		return slash == std::string::npos ? std::string() : dir.substr(0, slash);
	}

	HMODULE find_game_module()
	{
		char name[MAX_PATH] = {};
		GetPrivateProfileStringA("general", "Module", "", name, sizeof(name), tb::paths::file("iidx_toolbox.ini").c_str());
		if (name[0])
			return GetModuleHandleA(name);
		if (const HMODULE h = GetModuleHandleA("bm2dx.dll"))
			return h;
		return GetModuleHandleA("bm2dx_omni.dll");
	}

	void start_components(HMODULE game_module)
	{
		tb::module::set(game_module);
		char path[MAX_PATH] = {};
		GetModuleFileNameA(game_module, path, sizeof(path));
		tb::log("toolbox", "IIDX Toolbox attached to %s (base %p)", path, static_cast<void*>(game_module));

		auto list = tb::components::factories();
		std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.priority < b.priority; });
		for (auto& entry : list)
		{
			try
			{
				auto component = entry.factory();
				component->post_load();
				g_components.push_back(std::move(component));
			}
			catch (const std::exception& e)
			{
				tb::log("toolbox", "%s: not installed (%s)", entry.name.c_str(), e.what());
			}
		}
		tb::log("toolbox", "%zu components loaded", g_components.size());
	}

	void init()
	{
		tb::paths::set_directory(self_directory());
		tb::config::load();
		tb::log("toolbox", "IIDX Toolbox (built " __DATE__ " " __TIME__ ") loading");

		if (const HMODULE h = find_game_module())
		{
			start_components(h);
			return;
		}
		// spice may call the entry point before the game DLL is mapped
		std::thread([] {
			for (int i = 0; i < 3000; ++i)
			{
				if (const HMODULE h = find_game_module())
				{
					start_components(h);
					return;
				}
				Sleep(10);
			}
			tb::log("toolbox", "bm2dx.dll not found after 30 s; nothing installed");
		}).detach();
	}

	void shutdown()
	{
		if (g_shut_down.exchange(true))
			return;
		for (auto& component : g_components)
			component->pre_destroy();
		tb::flush_log();
	}

	struct spice_sdk_v0
	{
		uint32_t size;
		void* functions[19];
	};
	using spice_destroy_cb = void(__cdecl*)();
	using spice_init_func = int(__cdecl*)(uint32_t version, spice_destroy_cb destroy, void* sdk);

	void __cdecl spice_destroy()
	{
		shutdown();
	}
}

extern "C" __declspec(dllexport) int __cdecl spice_sdk_entry_point(spice_init_func init_func)
{
	static spice_sdk_v0 sdk{};
	sdk.size = sizeof(sdk);
	if (init_func)
		init_func(0, spice_destroy, &sdk);
	std::call_once(g_init_once, init);
	return 1;
}

extern "C" __declspec(dllexport) void __cdecl iidx_toolbox_init()
{
	std::call_once(g_init_once, init);
}

extern "C" __declspec(dllexport) void __cdecl iidx_toolbox_shutdown()
{
	shutdown();
}

// spice2x -k only calls LoadLibrary. Init runs on a thread, which starts once
// the loader lock is released, well before the game creates its D3D device.
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_self = instance;
		DisableThreadLibraryCalls(instance);
		const HANDLE thread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
			std::call_once(g_init_once, init);
			return 0;
		}, nullptr, 0, nullptr);
		if (thread)
			CloseHandle(thread);
	}
	else if (reason == DLL_PROCESS_DETACH && reserved)
	{
		shutdown();
	}
	return TRUE;
}