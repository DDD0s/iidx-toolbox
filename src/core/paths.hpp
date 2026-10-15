#pragma once

#include <string>

namespace tb::paths
{
	void set_directory(const std::string& directory);
	const std::string& directory();
	std::string file(const char* name);
}
