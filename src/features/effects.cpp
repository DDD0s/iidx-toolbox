#include "std_include.hpp"

#include "component.hpp"
#include "config.hpp"
#include "d3d.hpp"
#include "hook.hpp"
#include "log.hpp"
#include "module.hpp"

// Opacity: afp-core #49 sets a layer's colour multiplier (layer, r, g, b, a). While
// the bomb, key beam and judgement loaders run (once per stage) and while the combo
// digits are drawn (every frame), its alpha is multiplied by the setting. The
// game's own effect brightness option only feeds r, g, b and is left as it is.
namespace tb::effects
{
	namespace
	{
		using afp_color_t = uint64_t (*)(uint32_t layer, float r, float g, float b, float a);

		void** g_color_slot = nullptr;
		afp_color_t g_color = nullptr;
		thread_local float g_alpha = -1.f; // < 0: outside the wrapped functions

		hook::detour g_bomb;
		hook::detour g_beam;
		hook::detour g_judge;
		hook::detour g_combo;

		uint64_t color_stub(uint32_t layer, float r, float g, float b, float a)
		{
			if (g_alpha >= 0.f)
				a *= g_alpha;
			return g_color(layer, r, g, b, a);
		}

		// the four wrapped functions take at most four integer or pointer arguments
		template <hook::detour& Detour, std::atomic<int>& Percent>
		uint64_t with_alpha(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d)
		{
			const float previous = g_alpha;
			const int percent = Percent.load();
			g_alpha = percent >= 100 ? -1.f : static_cast<float>(std::max(percent, 0)) / 100.f;
			const auto result = Detour.template invoke<uint64_t>(a, b, c, d);
			g_alpha = previous;
			return result;
		}

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

		bool init_opacity()
		{
			g_color_slot = module::import_slot("afp-core.dll", 49);
			auto* bomb = module::find_unique("48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 60 01 00 00 48 8B 05 ? ? ? ? 48 33");
			auto* beam = module::find_unique("40 53 57 41 57 48 81 EC 80 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 50 02 00 00 4C 8B");
			auto* judge = module::find_unique("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 50 0F 29 74 24 40 48 8B F9 89 51 08 44 89 41 0C E8");
			auto* combo = module::find_unique("48 89 5C 24 10 48 89 6C 24 18 57 41 56 41 57 48 83 EC 50 48 8B E9 45 8B F1 8B 49 20 45 8B F8 8B FA E8");
			// the sprite colour method (vtable +0xA8) calls the same import slot
			auto* sprite_color = module::find_unique("48 83 EC 38 48 8B 01 F3 0F 10 42 08 F3 0F 10 52 04 F3 0F 10 0A F3 0F 10 62 0C 48 8B 90 A0 00 00 00");
			if (!g_color_slot || !bomb || !beam || !judge || !combo || !sprite_color || sprite_color[0x39] != 0xFF ||
				sprite_color[0x3A] != 0x15 || reinterpret_cast<void**>(module::rel32(sprite_color + 0x3B)) != g_color_slot)
			{
				tb::log("effects", "opacity: sites not found, not installed");
				return false;
			}

			g_bomb.create(bomb, reinterpret_cast<void*>(&with_alpha<g_bomb, config::bomb_opacity>));
			g_beam.create(beam, reinterpret_cast<void*>(&with_alpha<g_beam, config::beam_opacity>));
			g_judge.create(judge, reinterpret_cast<void*>(&with_alpha<g_judge, config::judge_opacity>));
			g_combo.create(combo, reinterpret_cast<void*>(&with_alpha<g_combo, config::judge_opacity>));
			g_color = reinterpret_cast<afp_color_t>(*g_color_slot);
			hook::set<void*>(g_color_slot, reinterpret_cast<void*>(&color_stub));
			return true;
		}
	}

	class component final : public component_interface
	{
	public:
		void post_load() override
		{
			const bool opacity = init_opacity();

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
			tb::log("effects", "installed (opacity %s, PGREAT %s, blink %s)", opacity ? "ok" : "missing",
				g_pgreat_blue.at ? "ok" : "missing", g_no_blink.at ? "ok" : "missing");
		}

		void pre_destroy() override
		{
			g_pgreat_blue.set(false);
			g_no_blink.set(false);
			if (g_color_slot && g_color)
				hook::set<void*>(g_color_slot, reinterpret_cast<void*>(g_color));
		}
	};
}

TB_REGISTER_COMPONENT(tb::effects::component, 30)
