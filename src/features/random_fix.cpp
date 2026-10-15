#include "std_include.hpp"

#include <random>

#include "component.hpp"
#include "config.hpp"
#include "hook.hpp"
#include "log.hpp"
#include "module.hpp"

namespace tb::random_fix
{
	namespace
	{
		using namespace random_logic;

		using getter_t = void* (*)();
		using is_dp_t = int (*)();
		using option_pred_t = bool (*)(void* options, int side, int dp);
		using ticket_pred_t = bool (*)(void* io, int side);

		constexpr size_t k_map_offset = 0x10;   // int32 map[2][8]
		constexpr size_t k_flags_offset = 0x50; // uint8 flags[2][8]

		hook::detour g_apply;
		getter_t g_options = nullptr;
		is_dp_t g_is_dp = nullptr;
		option_pred_t g_is_random = nullptr;
		option_pred_t g_is_mirror = nullptr;
		option_pred_t g_is_autoscratch = nullptr;
		getter_t g_io = nullptr;
		ticket_pred_t g_ticket_armed = nullptr;
		// seeded on first use (play start), not in DllMain
		std::mt19937& rng()
		{
			static std::mt19937 engine{ std::random_device{}() };
			return engine;
		}

		void apply_fixed(uint8_t* obj, const config::random_t& cfg)
		{
			void* options = g_options();
			const int dp = g_is_dp() != 0 ? 1 : 0;
			for (int side = 0; side < 2; ++side)
			{
				if (!g_is_random(options, side, dp))
					continue;
				if (!dp && g_ticket_armed(g_io(), side))
				{
					tb::log("random", "side %d: lane ticket armed, left to the game", side);
					continue;
				}

				const uint8_t* flags = obj + k_flags_offset + static_cast<size_t>(side) * 8;
				auto* map_ptr = reinterpret_cast<int32_t*>(obj + k_map_offset + static_cast<size_t>(side) * 0x20);
				const bool mirror = g_is_mirror(options, side, dp);
				const bool autoscratch = g_is_autoscratch(options, side, dp);

				auto sc = cfg.side[side];
				if (check_applicable(flags, arrange_t::random, autoscratch, true) != apply_result::applied_random)
				{
					tb::log("random", "side %d left native (%s)", side, autoscratch ? "auto scratch" : "non-standard pool");
					continue;
				}

				const char* kind = "fixed";
				if (sc.group || sc.ran)
				{
					sc.perm = sc.group ? group_random_perm(sc.perm, rng()) : random_perm(rng());
					kind = sc.group ? "group" : "ran";
					config::store_drawn(side, sc.perm);
				}

				std::array<int32_t, 8> map{};
				std::copy_n(map_ptr, 8, map.begin());
				if (apply_to_map(map, flags, arrange_t::random, mirror, autoscratch, sc, true) != apply_result::applied_random)
					continue;
				std::copy_n(map.begin(), 8, map_ptr);
				tb::log("random", "side %d %s %s (%s)%s", side, kind, screen_order(sc.perm).c_str(),
					color_pattern(sc.perm).c_str(), mirror ? " +mirror" : "");
			}
		}

		uint64_t apply_stub(uint8_t* obj)
		{
			const auto result = g_apply.invoke<uint64_t>(obj);
			const auto cfg = config::get_random();
			if (obj && cfg.enabled)
				apply_fixed(obj, cfg);
			return result;
		}

		// one-call predicate: sub rsp,28h ; call getter ; cmp eax,1 ; setz al ; add rsp,28h ; ret
		bool is_option_pred(const uint8_t* p)
		{
			static const uint8_t head[] = { 0x48, 0x83, 0xEC, 0x28, 0xE8 };
			static const uint8_t tail[] = { 0x83, 0xF8, 0x01, 0x0F, 0x94, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3 };
			return std::memcmp(p, head, sizeof(head)) == 0 && std::memcmp(p + 9, tail, sizeof(tail)) == 0;
		}

		uint8_t* call_target(uint8_t* at)
		{
			if (*at != 0xE8)
				throw std::runtime_error("expected a call");
			return module::rel32(at + 1);
		}
	}

	class component final : public component_interface
	{
	public:
		void post_load() override
		{
			auto* site = module::find_unique("E8 ? ? ? ? 48 8B C8 E8 ? ? ? ? 4C 8D 86 ? ? ? ? 45 8B CC");
			auto* io_site = module::find_unique("E8 ? ? ? ? 8B D7 48 8B C8 E8 ? ? ? ? 84 C0 74 08 48 8B CB E8 ? ? ? ? 8B D7 48 8B CB E8");
			if (!site || !io_site)
				throw std::runtime_error("arrange_apply or lane ticket check not found");

			auto* apply = call_target(site + 8);
			static const uint8_t head[] = { 0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x78 };
			if (std::memcmp(apply, head, sizeof(head)) != 0)
				throw std::runtime_error("arrange_apply prologue differs");

			auto* options = call_target(apply + 0x40);
			if (options[0] != 0x48 || options[1] != 0x8B || options[2] != 0x05 || options[7] != 0xC3)
				throw std::runtime_error("options getter differs");
			auto* autoscratch = call_target(apply + 0x55);
			auto* random = call_target(apply + 0x99);
			auto* mirror = call_target(apply + 0x205);
			if (!is_option_pred(autoscratch) || !is_option_pred(random) || !is_option_pred(mirror))
				throw std::runtime_error("option predicates differ");

			g_options = reinterpret_cast<getter_t>(options);
			g_is_dp = reinterpret_cast<is_dp_t>(call_target(apply + 0x48));
			g_is_autoscratch = reinterpret_cast<option_pred_t>(autoscratch);
			g_is_random = reinterpret_cast<option_pred_t>(random);
			g_is_mirror = reinterpret_cast<option_pred_t>(mirror);
			g_io = reinterpret_cast<getter_t>(call_target(io_site));
			g_ticket_armed = reinterpret_cast<ticket_pred_t>(call_target(io_site + 10));

			g_apply.create(apply, reinterpret_cast<void*>(&apply_stub));
			tb::log("random", "installed (arrange_apply %p)", static_cast<void*>(apply));
		}
	};
}

TB_REGISTER_COMPONENT(tb::random_fix::component, 10)