#include "std_include.hpp"

#include <cctype>
#include <thread>

#include "config.hpp"
#include "paths.hpp"

namespace tb::config
{
	std::atomic<bool> timing_enabled{ true };
	std::atomic<bool> all_scratch{ false };
	std::atomic<bool> all_charge{ false };
	std::atomic<int> bomb_opacity{ 100 };
	std::atomic<int> beam_opacity{ 100 };
	std::atomic<int> judge_opacity{ 100 };
	std::atomic<bool> pgreat_blue{ false };
	std::atomic<bool> no_blink{ false };

	namespace
	{
		std::mutex g_random_mutex;
		random_t g_random{};
		std::mutex g_file_mutex; // WritePrivateProfileString rewrites the whole file
		std::atomic<bool> g_save_queued{ false };
		bool g_log = true;
		int g_menu_key = VK_INSERT;

		std::string ini()
		{
			return paths::file("iidx_toolbox.ini");
		}

		std::string read(const char* section, const char* key, const char* fallback)
		{
			char buffer[256] = {};
			GetPrivateProfileStringA(section, key, fallback, buffer, sizeof(buffer), ini().c_str());
			return buffer;
		}

		int read_int(const char* section, const char* key, int fallback)
		{
			return static_cast<int>(GetPrivateProfileIntA(section, key, fallback, ini().c_str()));
		}

		void write(const char* section, const char* key, const std::string& value)
		{
			WritePrivateProfileStringA(section, key, value.c_str(), ini().c_str());
		}

		int clamp_percent(int v)
		{
			return std::clamp(v, 0, 100);
		}

		int parse_key(std::string name)
		{
			for (auto& c : name)
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
			if (name.empty())
				return VK_INSERT;
			if (name.size() > 2 && name[0] == '0' && name[1] == 'X')
				return static_cast<int>(std::strtol(name.c_str() + 2, nullptr, 16));
			if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0])))
				return name[0];
			if (name[0] == 'F' && name.size() <= 3 && std::isdigit(static_cast<unsigned char>(name[1])))
			{
				const int n = std::atoi(name.c_str() + 1);
				if (n >= 1 && n <= 24)
					return VK_F1 + n - 1;
			}
			if (name.rfind("NUMPAD", 0) == 0 && name.size() == 7 && std::isdigit(static_cast<unsigned char>(name[6])))
				return VK_NUMPAD0 + (name[6] - '0');
			static const std::pair<const char*, int> named[] = {
				{ "INSERT", VK_INSERT }, { "DELETE", VK_DELETE }, { "HOME", VK_HOME }, { "END", VK_END },
				{ "PAGEUP", VK_PRIOR }, { "PAGEDOWN", VK_NEXT }, { "PAUSE", VK_PAUSE }, { "SCROLLLOCK", VK_SCROLL },
				{ "NUMLOCK", VK_NUMLOCK }, { "PRINTSCREEN", VK_SNAPSHOT }, { "TAB", VK_TAB }, { "BACKSPACE", VK_BACK },
			};
			for (const auto& [n, vk] : named)
				if (name == n)
					return vk;
			if (std::isdigit(static_cast<unsigned char>(name[0])))
				return std::atoi(name.c_str());
			return VK_INSERT;
		}

		void read_side(const char* key, random_logic::side_config_t& side)
		{
			random_logic::perm_t perm{};
			if (random_logic::parse_screen_order(read("random", key, "1234567"), perm))
				side.perm = perm;
			const std::string k(key);
			side.ran = read_int("random", (k + "Ran").c_str(), 0) != 0;
			side.group = read_int("random", (k + "Group").c_str(), 0) != 0;
			if (side.group)
				side.ran = false;
		}

		void write_side(const char* key, const random_logic::side_config_t& side)
		{
			const std::string k(key);
			write("random", key, random_logic::screen_order(side.perm));
			write("random", (k + "Ran").c_str(), side.ran ? "1" : "0");
			write("random", (k + "Group").c_str(), side.group ? "1" : "0");
		}

		void write_all()
		{
			const random_t random = [] {
				std::lock_guard lock(g_random_mutex);
				return g_random;
			}();
			write("random", "Enabled", random.enabled ? "1" : "0");
			write_side("P1", random.side[0]);
			write_side("P2", random.side[1]);
			write("timing", "MillisecondClock", timing_enabled ? "1" : "0");
			write("chart", "AllScratch", all_scratch ? "1" : "0");
			write("chart", "AllCharge", all_charge ? "1" : "0");
			write("effects", "BombOpacity", std::to_string(bomb_opacity.load()));
			write("effects", "BeamOpacity", std::to_string(beam_opacity.load()));
			write("effects", "JudgeOpacity", std::to_string(judge_opacity.load()));
			write("effects", "PGreatBlue", pgreat_blue ? "1" : "0");
			write("effects", "NoBlink", no_blink ? "1" : "0");
		}
	}

	void load()
	{
		g_log = read_int("general", "Log", 1) != 0;
		g_menu_key = parse_key(read("general", "MenuKey", "Insert"));
		timing_enabled = read_int("timing", "MillisecondClock", 1) != 0;

		{
			std::lock_guard lock(g_random_mutex);
			g_random.enabled = read_int("random", "Enabled", 0) != 0;
			read_side("P1", g_random.side[0]);
			read_side("P2", g_random.side[1]);
		}

		all_scratch = read_int("chart", "AllScratch", 0) != 0;
		all_charge = !all_scratch && read_int("chart", "AllCharge", 0) != 0;

		bomb_opacity = clamp_percent(read_int("effects", "BombOpacity", 100));
		beam_opacity = clamp_percent(read_int("effects", "BeamOpacity", 100));
		judge_opacity = clamp_percent(read_int("effects", "JudgeOpacity", 100));
		pgreat_blue = read_int("effects", "PGreatBlue", 0) != 0;
		no_blink = read_int("effects", "NoBlink", 0) != 0;
	}

	// file writes take milliseconds; never on the game or render thread
	void save()
	{
		if (g_save_queued.exchange(true))
			return;
		std::thread([] {
			Sleep(100);
			g_save_queued = false;
			std::lock_guard lock(g_file_mutex);
			write_all();
		}).detach();
	}

	void save_final()
	{
		std::unique_lock lock(g_file_mutex, std::try_to_lock);
		if (lock.owns_lock())
			write_all();
	}

	bool log_enabled()
	{
		return g_log;
	}

	int menu_key()
	{
		return g_menu_key;
	}

	random_t get_random()
	{
		std::lock_guard lock(g_random_mutex);
		return g_random;
	}

	void set_random(const random_t& before, const random_t& after)
	{
		{
			std::lock_guard lock(g_random_mutex);
			if (before.enabled != after.enabled)
				g_random.enabled = after.enabled;
			for (int s = 0; s < 2; ++s)
			{
				const auto& b = before.side[s];
				const auto& a = after.side[s];
				auto& cur = g_random.side[s];
				if (b.perm != a.perm)
					cur.perm = a.perm;
				if (b.ran != a.ran)
					cur.ran = a.ran;
				if (b.group != a.group)
					cur.group = a.group;
				if (cur.group)
					cur.ran = false;
			}
		}
		save();
	}

	void store_drawn(int side, const random_logic::perm_t& perm)
	{
		if (side < 0 || side > 1)
			return;
		{
			std::lock_guard lock(g_random_mutex);
			g_random.side[side].perm = perm;
		}
		save();
	}
}