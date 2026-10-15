#include "std_include.hpp"

#include <condition_variable>
#include <thread>

#include "config.hpp"
#include "log.hpp"
#include "paths.hpp"

namespace tb
{
	namespace
	{
		std::mutex g_mutex;
		std::condition_variable g_cv;
		std::string g_pending;
		std::thread g_worker;
		bool g_started = false;
		bool g_stopped = false;

		const std::string& log_path()
		{
			static const std::string path = [] {
				const auto file = paths::file("iidx_toolbox.log");
				const auto old = file + ".1";
				DeleteFileA(old.c_str());
				MoveFileA(file.c_str(), old.c_str());
				return file;
			}();
			return path;
		}

		HANDLE open_log()
		{
			return CreateFileA(log_path().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		}

		void write_direct(const std::string& text)
		{
			const HANDLE h = open_log();
			if (h == INVALID_HANDLE_VALUE)
				return;
			DWORD written = 0;
			WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
			CloseHandle(h);
		}

		void worker(HANDLE file)
		{
			std::string batch;
			for (;;)
			{
				{
					std::unique_lock lock(g_mutex);
					g_cv.wait(lock, [] { return g_stopped || !g_pending.empty(); });
					if (g_pending.empty() && g_stopped)
						break;
					batch.swap(g_pending);
				}
				DWORD written = 0;
				WriteFile(file, batch.data(), static_cast<DWORD>(batch.size()), &written, nullptr);
				batch.clear();
			}
			CloseHandle(file);
		}
	}

	void log(const char* tag, const char* fmt, ...)
	{
		if (!config::log_enabled())
			return;

		char msg[768];
		va_list ap;
		va_start(ap, fmt);
		vsprintf_s(msg, sizeof(msg), fmt, ap);
		va_end(ap);

		SYSTEMTIME st;
		GetLocalTime(&st);
		char line[900];
		const int n = sprintf_s(line, sizeof(line), "[%02d:%02d:%02d.%03d][%s] %s\r\n",
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, tag, msg);
		if (n <= 0)
			return;

		std::unique_lock lock(g_mutex);
		if (g_stopped)
		{
			lock.unlock();
			write_direct(std::string(line, static_cast<size_t>(n)));
			return;
		}
		if (!g_started)
		{
			g_started = true;
			const HANDLE file = open_log();
			if (file == INVALID_HANDLE_VALUE)
			{
				g_stopped = true;
				return;
			}
			g_worker = std::thread(worker, file);
		}
		g_pending.append(line, static_cast<size_t>(n));
		lock.unlock();
		g_cv.notify_one();
	}

	void flush_log_at_exit()
	{
		// the writer may have died holding the mutex; nothing else runs now
		std::unique_lock lock(g_mutex, std::try_to_lock);
		g_stopped = true;
		if (!g_pending.empty())
		{
			std::string rest;
			rest.swap(g_pending);
			write_direct(rest);
		}
		if (g_worker.joinable())
			g_worker.detach();
	}

	void flush_log()
	{
		{
			std::lock_guard lock(g_mutex);
			if (g_stopped)
				return;
			g_stopped = true;
		}
		g_cv.notify_one();
		if (g_worker.joinable())
			g_worker.join();
	}
}