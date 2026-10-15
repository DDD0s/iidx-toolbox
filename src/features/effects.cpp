#include "std_include.hpp"

#include <intrin.h>

#include "component.hpp"
#include "config.hpp"
#include "d3d.hpp"
#include "hook.hpp"
#include "log.hpp"
#include "module.hpp"

namespace tb::effects
{
	namespace
	{
		// The game's effect alpha (from a player option) is read when bombs, key
		// beams and the judgement text are loaded at stage start, and every frame
		// for the combo digits. Callers are told apart by return address.
		hook::detour g_alpha;
		const uint8_t* g_ret_bomb = nullptr;
		const uint8_t* g_ret_beam = nullptr;
		const uint8_t* g_ret_judge = nullptr;
		const uint8_t* g_ret_combo = nullptr;

		struct patch_t
		{
			uint8_t* at = nullptr;
			uint8_t original[3]{};
			uint8_t patched[3]{};
			size_t size = 0;
			bool applied = false;

			void set(bool on)
			{
				if (!at || on == applied)
					return;
				hook::copy(at, on ? patched : original, size);
				applied = on;
			}
		};
		patch_t g_pgreat_blue; // PGREAT colour index fixed to 2 (judge_p_great_blue)
		patch_t g_no_blink;    // GREAT..POOR drawn every frame

		float alpha_stub(void* options, int side, int dp)
		{
			const float alpha = g_alpha.invoke<float>(options, side, dp);
			const auto* ret = static_cast<const uint8_t*>(_ReturnAddress());
			int percent = 100;
			if (ret == g_ret_bomb)
				percent = config::bomb_opacity;
			else if (ret == g_ret_beam)
				percent = config::beam_opacity;
			else if (ret == g_ret_judge || ret == g_ret_combo)
				percent = config::judge_opacity;
			return percent >= 100 ? alpha : alpha * static_cast<float>(std::clamp(percent, 0, 100)) / 100.f;
		}

		const uint8_t* site_return(const char* name, const char* pattern, size_t offset, const uint8_t* getter)
		{
			auto* head = module::find_unique(pattern);
			if (!head || head[offset] != 0xE8 || (getter && module::rel32(head + offset + 1) != getter))
				throw std::runtime_error(std::string(name) + " alpha call not found");
			return head + offset + 5;
		}

		void init_patch(patch_t& patch, const char* name, const char* pattern, size_t offset,
			std::initializer_list<uint8_t> expect, std::initializer_list<uint8_t> replace)
		{
			auto* at = module::find_unique(pattern);
			if (!at)
			{
				tb::log("effects", "%s: site not found", name);
				return;
			}
			at += offset;
			if (!std::equal(expect.begin(), expect.end(), at))
			{
				tb::log("effects", "%s: unexpected bytes", name);
				return;
			}
			patch.at = at;
			patch.size = expect.size();
			std::copy(expect.begin(), expect.end(), patch.original);
			std::copy(replace.begin(), replace.end(), patch.patched);
		}
	}

	class component final : public component_interface
	{
	public:
		void post_load() override
		{
			auto* bomb = module::find_unique("48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 60 01 00 00 48 8B 05 ? ? ? ? 48 33");
			if (!bomb || bomb[0xE4] != 0xE8)
				throw std::runtime_error("bomb loader not found");
			auto* getter = module::rel32(bomb + 0xE5);
			g_ret_bomb = bomb + 0xE9;
			g_ret_beam = site_return("key beam", "40 53 57 41 57 48 81 EC 80 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 50 02 00 00 4C 8B", 0x276, getter);
			g_ret_judge = site_return("judgement", "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 50 0F 29 74 24 40 48 8B F9 89 51 08 44 89 41 0C E8", 0xAC, getter);
			g_ret_combo = site_return("combo", "48 89 5C 24 10 48 89 6C 24 18 57 41 56 41 57 48 83 EC 50 48 8B E9 45 8B F1 8B 49 20 45 8B F8 8B FA E8", 0x73, getter);
			g_alpha.create(const_cast<uint8_t*>(getter), reinterpret_cast<void*>(&alpha_stub));

			// cdq ; idiv ecx -> push 2 ; pop rax
			init_patch(g_pgreat_blue, "PGREAT colour", "41 8B C0 8B CA C1 E9 1F 03 CA 99 F7 F9 49 8B 14 24 49 8B 4C 24 10 48 63 D8",
				10, { 0x99, 0xF7, 0xF9 }, { 0x6A, 0x02, 0x58 });
			// jl skip_draw -> nop nop
			if (auto* blink = module::find_unique("8B CA 99 2B C2 D1 F8 3B C8 7C ? 49 8B 4C 24 10 B2 01"))
			{
				const uint8_t jl = blink[9];
				const uint8_t rel = blink[10];
				init_patch(g_no_blink, "judgement blink", "8B CA 99 2B C2 D1 F8 3B C8 7C ? 49 8B 4C 24 10 B2 01", 9, { jl, rel }, { 0x90, 0x90 });
			}

			// patched from the render thread, where the judgement draw cannot be running
			d3d::on_before_present([](IDirect3DSwapChain9*) {
				g_pgreat_blue.set(config::pgreat_blue);
				g_no_blink.set(config::no_blink);
			});
			tb::log("effects", "installed (alpha getter %p, PGREAT %s, blink %s)", static_cast<const void*>(getter),
				g_pgreat_blue.at ? "ok" : "missing", g_no_blink.at ? "ok" : "missing");
		}

		void pre_destroy() override
		{
			g_pgreat_blue.set(false);
			g_no_blink.set(false);
		}
	};
}

TB_REGISTER_COMPONENT(tb::effects::component, 30)