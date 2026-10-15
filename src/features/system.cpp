#include "std_include.hpp"

#include <avrt.h>

#include "component.hpp"
#include "d3d.hpp"
#include "log.hpp"
#include "paths.hpp"

// Process-level settings against stalls; all revert when the process exits.
// The game already sets a 1 ms timer at load and runs its audio thread under
// MMCSS "Pro Audio" (critical), which stays above the render thread.
namespace tb::system
{
	namespace
	{
		int read_int(const char* key, int fallback)
		{
			return static_cast<int>(GetPrivateProfileIntA("system", key, fallback, paths::file("iidx_toolbox.ini").c_str()));
		}

		std::string read_str(const char* key, const char* fallback)
		{
			char buffer[64] = {};
			GetPrivateProfileStringA("system", key, fallback, buffer, sizeof(buffer), paths::file("iidx_toolbox.ini").c_str());
			return buffer;
		}

		// MMCSS sets the thread priority within its own range (16-22 for "Games") and
		// drops it to 1-7 once its share is used up; with a high or realtime priority
		// class (spice2x -processpriority) that lowers the render thread instead.
		void render_thread_mmcss(IDirect3DSwapChain9*)
		{
			static bool done = false;
			if (done)
				return;
			done = true;
			const DWORD cls = GetPriorityClass(GetCurrentProcess());
			if (cls == HIGH_PRIORITY_CLASS || cls == REALTIME_PRIORITY_CLASS)
			{
				tb::log("system", "MMCSS skipped: process priority is already %s", cls == HIGH_PRIORITY_CLASS ? "high" : "realtime");
				return;
			}
			DWORD task = 0;
			if (AvSetMmThreadCharacteristicsW(L"Games", &task))
				tb::log("system", "render thread %lu registered with MMCSS \"Games\"", GetCurrentThreadId());
			else
				tb::log("system", "MMCSS registration failed (%lu)", GetLastError());
		}
	}

	class component final : public component_interface
	{
	public:
		void post_load() override
		{
			if (read_int("NoPowerThrottling", 1))
			{
				PROCESS_POWER_THROTTLING_STATE state{};
				state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
				state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
				state.StateMask = 0;
				BOOL ok = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
				if (!ok)
				{
					// the timer flag needs Windows 11
					state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
					ok = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
				}
				tb::log("system", "power throttling off: %s", ok ? "ok" : "not supported");
			}

			const auto priority = read_str("ProcessPriority", "normal");
			DWORD cls = 0;
			if (_stricmp(priority.c_str(), "above") == 0)
				cls = ABOVE_NORMAL_PRIORITY_CLASS;
			else if (_stricmp(priority.c_str(), "high") == 0)
				cls = HIGH_PRIORITY_CLASS;
			if (cls)
				tb::log("system", "process priority %s: %s", priority.c_str(), SetPriorityClass(GetCurrentProcess(), cls) ? "ok" : "failed");

			if (read_int("Mmcss", 1))
				d3d::on_before_present(&render_thread_mmcss);
		}
	};
}

TB_REGISTER_COMPONENT(tb::system::component, 5)