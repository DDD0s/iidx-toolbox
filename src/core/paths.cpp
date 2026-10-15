#include "std_include.hpp"

#include "paths.hpp"

namespace tb::paths
{
	namespace
	{
		std::string g_directory;
	}

	void set_directory(const std::string& directory)
	{
		g_directory = directory;
		while (!g_directory.empty() && (g_directory.back() == '\\' || g_directory.back() == '/'))
			g_directory.pop_back();
	}

	const std::string& directory()
	{
		return g_directory;
	}

	std::string file(const char* name)
	{
		return g_directory.empty() ? std::string(name) : g_directory + "\\" + name;
	}
}
