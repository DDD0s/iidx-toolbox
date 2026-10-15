#pragma once

// iidx_toolbox.log next to the DLL (previous run kept as .log.1); written by a
// background thread so the game thread never waits on disk.
namespace tb
{
	void log(const char* tag, const char* fmt, ...);
	void flush_log();
}
