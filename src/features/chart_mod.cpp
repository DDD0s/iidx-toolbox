#include "std_include.hpp"

#include "component.hpp"
#include "config.hpp"
#include "hook.hpp"
#include "log.hpp"
#include "module.hpp"

namespace tb::chart_mod
{
	namespace
	{
		enum event_type_t : int8_t
		{
			note_p1 = 0x0,
			note_p2 = 0x1,
			sample_p1 = 0x2,
			sample_p2 = 0x3,
			end_of_song = 0x6,
			bgm = 0x7,
		};

		struct event_t
		{
			int32_t tick;
			int8_t type;
			int8_t param;
			int16_t value;
		};
		static_assert(sizeof(event_t) == 8);

		// the chart object holds 0x18000 bytes of events, then per-side data
		constexpr size_t k_max_events = 0x18000 / sizeof(event_t);
		constexpr uint64_t k_autoscratch_bit = 0x80000000000ull;

		using chart_t = std::vector<event_t>;

		hook::detour g_post_chart;
		hook::detour g_assist;
		hook::detour g_option_bits;
		std::atomic<bool> g_modified[2] = {};
		ULONGLONG g_last_load = 0;

		chart_t::iterator find_forward(chart_t& chart, int8_t type, int8_t param, chart_t::iterator from)
		{
			for (auto j = from + 1; j < chart.end(); ++j)
				if (j->type == type && j->param == param)
					return j;
			return chart.end();
		}

		chart_t::iterator find_backward(chart_t& chart, int8_t type, int8_t param, chart_t::iterator from)
		{
			for (auto j = from; j != chart.begin();)
			{
				--j;
				if (j->type == type && j->param == param)
					return j;
			}
			return chart.end();
		}

		// 2dxplus all-scratch: a key note at least 50 ticks clear of other scratch
		// notes moves to the scratch lane, with its keysound re-assigned there
		chart_t all_scratch(chart_t chart)
		{
			chart_t out;
			out.reserve(chart.size() + chart.size() / 4);
			std::array<event_t, 8> samples_p1{};
			std::array<event_t, 8> samples_p2{};
			int16_t scratch_sample = 0;
			int32_t scratch_tick = 0;

			const auto move_sample = [&](const event_t& ev, int16_t value) {
				if (scratch_sample != value)
					out.push_back({ scratch_tick + (ev.tick - scratch_tick) / 2, static_cast<int8_t>(ev.type + 2), 7, value });
				scratch_sample = value;
				scratch_tick = ev.tick + ev.value;
			};

			for (auto it = chart.begin(); it < chart.end(); ++it)
			{
				auto& ev = *it;
				if (ev.type == sample_p1 && ev.param >= 0 && ev.param < 8)
					samples_p1[static_cast<size_t>(ev.param)] = ev;
				if (ev.type == sample_p2 && ev.param >= 0 && ev.param < 8)
					samples_p2[static_cast<size_t>(ev.param)] = ev;
				else if (ev.type < 2 && ev.param == 7)
				{
					move_sample(ev, (ev.type == note_p1 ? samples_p1 : samples_p2)[7].value);
				}
				else if (ev.type < 2 && ev.param >= 0 && ev.param < 7 && !ev.value && ev.tick - scratch_tick >= 50)
				{
					const auto next = find_forward(chart, ev.type, 7, it);
					if (next == chart.end() || next->tick - ev.tick >= 50)
					{
						const auto value = (ev.type == note_p1 ? samples_p1 : samples_p2)[static_cast<size_t>(ev.param)].value;
						ev.param = 7;
						move_sample(ev, value);
					}
				}
				out.push_back(ev);
			}
			return out;
		}

		// pairs of plain notes on one lane become a charge note from the first to the second
		chart_t all_charge(chart_t chart)
		{
			for (auto i = chart.begin(); i < chart.end(); ++i)
			{
				if (i->type >= 2 || i->param == 7 || i->value)
					continue;
				const auto next = std::find_if(i + 1, chart.end(), [&](const event_t& e) {
					return e.type == i->type && e.param == i->param && e.tick > i->tick;
				});
				if (next == chart.end() || next->value)
					continue;
				const auto sample = find_backward(chart, static_cast<int8_t>(next->type + 2), next->param, next);
				if (sample == chart.end())
					continue;
				i->value = static_cast<int16_t>(std::min(next->tick - i->tick, 0x7FFF));
				next->type = bgm;
				next->param = 0;
				next->value = sample->value;
			}
			return chart;
		}

		void post_chart_stub(event_t* raw, int side)
		{
			const bool scratch = config::all_scratch.load();
			const bool charge = !scratch && config::all_charge.load();
			bool modified = false;

			if (raw && (scratch || charge))
			{
				event_t* end = std::find_if(raw, raw + k_max_events, [](const event_t& e) { return e.type == end_of_song; });
				if (end != raw + k_max_events)
				{
					chart_t chart(raw, end + 1);
					chart = scratch ? all_scratch(std::move(chart)) : all_charge(std::move(chart));
					if (chart.size() <= k_max_events)
					{
						std::memcpy(raw, chart.data(), chart.size() * sizeof(event_t));
						modified = true;
					}
					else
					{
						tb::log("chart", "side %d: modified chart has %zu events (limit %zu), left unchanged", side, chart.size(), k_max_events);
					}
				}
			}

			// DP loads both sides back to back; a gap means a new play, so the
			// other side's flag from an earlier play is cleared
			const ULONGLONG now = GetTickCount64();
			if (now - g_last_load > 500)
				g_modified[0] = g_modified[1] = false;
			g_last_load = now;
			if (side >= 0 && side < 2)
				g_modified[side] = modified;
			if (modified)
				tb::log("chart", "side %d: %s, reported as an AUTO SCRATCH assist play", side, scratch ? "ALL SCRATCH" : "ALL CHARGE");

			g_post_chart.invoke<void>(raw, side);
		}

		bool assist_stub(int side)
		{
			if (g_assist.invoke<bool>(side))
				return true;
			return (side == 0 || side == 1) && g_modified[side];
		}

		uint64_t option_bits_stub(int side, int dp)
		{
			auto bits = g_option_bits.invoke<uint64_t>(side, dp);
			if ((side == 0 || side == 1) && g_modified[side])
				bits |= k_autoscratch_bit;
			return bits;
		}
	}

	class component final : public component_interface
	{
	public:
		void post_load() override
		{
			auto* io_site = module::find_unique("E8 ? ? ? ? 8B D7 48 8B C8 E8 ? ? ? ? 84 C0 74 08 48 8B CB E8 ? ? ? ? 8B D7 48 8B CB E8");
			auto* assist = module::find_unique("40 55 41 56 48 83 EC 28 8B E9 83 F9 01 0F 87 ? ? ? ? E8 ? ? ? ? 84 C0 0F 84");
			auto* bits = module::find_unique("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 8B FA 8B F1 33 DB E8");
			if (!io_site || !assist || !bits)
				throw std::runtime_error("chart load, assist check or option bits not found");

			auto* post_chart = module::rel32(io_site + 33);
			static const uint8_t head[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18 };
			if (std::memcmp(post_chart, head, sizeof(head)) != 0)
				throw std::runtime_error("post chart prologue differs");

			g_post_chart.create(post_chart, reinterpret_cast<void*>(&post_chart_stub));
			g_assist.create(assist, reinterpret_cast<void*>(&assist_stub));
			g_option_bits.create(bits, reinterpret_cast<void*>(&option_bits_stub));
			tb::log("chart", "installed (post chart %p, assist %p, option bits %p)",
				static_cast<void*>(post_chart), static_cast<void*>(assist), static_cast<void*>(bits));
		}
	};
}

TB_REGISTER_COMPONENT(tb::chart_mod::component, 20)
