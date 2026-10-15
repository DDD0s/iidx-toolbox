// Installs IIDX Toolbox into an inert-mapped bm2dx.dll: every post_load runs for real
// (signature scans, patches, detours) but no game code executes. Prints the log.
// Run: smoke.exe <bm2dx.dll> <iidx_toolbox.dll>

#include <Windows.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		std::printf("usage: smoke <bm2dx.dll> <iidx_toolbox.dll>\n");
		return 2;
	}
	if (!LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES))
	{
		std::printf("cannot map %s\n", argv[1]);
		return 1;
	}
	const HMODULE plugin = LoadLibraryA(argv[2]);
	if (!plugin)
	{
		std::printf("cannot load %s (%lu)\n", argv[2], GetLastError());
		return 1;
	}
	using fn_t = void(__cdecl*)();
	const auto init = reinterpret_cast<fn_t>(GetProcAddress(plugin, "iidx_toolbox_init"));
	const auto shutdown = reinterpret_cast<fn_t>(GetProcAddress(plugin, "iidx_toolbox_shutdown"));
	if (!init || !shutdown || !GetProcAddress(plugin, "spice_sdk_entry_point"))
	{
		std::printf("missing exports\n");
		return 1;
	}
	init();
	Sleep(200);
	shutdown();

	char path[MAX_PATH] = {};
	GetModuleFileNameA(plugin, path, sizeof(path));
	std::string log(path);
	log = log.substr(0, log.find_last_of('\\')) + "\\iidx_toolbox.log";
	std::ifstream in(log);
	std::cout << in.rdbuf();
	return 0;
}
