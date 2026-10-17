#include "std_include.hpp"

#include <imgui.h>

#include "config.hpp"
#include "menu_ui.hpp"

namespace tb::menu_ui
{
	namespace
	{
		using namespace random_logic;

		float g_scale = 1.f;

		const ImVec4 k_bg{ 0.035f, 0.035f, 0.035f, 0.96f };
		const ImVec4 k_panel{ 0.085f, 0.085f, 0.085f, 1.f };
		const ImVec4 k_frame{ 0.15f, 0.15f, 0.15f, 1.f };
		const ImVec4 k_frame_hover{ 0.23f, 0.23f, 0.23f, 1.f };
		const ImVec4 k_line{ 1.f, 1.f, 1.f, 0.14f };
		const ImVec4 k_text{ 0.95f, 0.95f, 0.95f, 1.f };
		const ImVec4 k_text_dim{ 0.55f, 0.55f, 0.55f, 1.f };
		const ImVec4 k_on{ 0.96f, 0.96f, 0.96f, 1.f };
		const ImVec4 k_on_text{ 0.04f, 0.04f, 0.04f, 1.f };
		const ImVec4 k_error{ 0.45f, 0.12f, 0.12f, 1.f };
		const ImVec4 k_white_key{ 0.93f, 0.93f, 0.93f, 1.f };
		const ImVec4 k_black_key{ 0.13f, 0.13f, 0.13f, 1.f };

		constexpr const char* k_label_normal = "\xE6\xAD\xA3";
		constexpr const char* k_label_mirror = "\xE9\x8F\xA1";
		constexpr const char* k_label_ran = "\xE4\xB9\xB1";
		constexpr const char* k_label_group = "\xE7\xB5\x84";

		ImVec4 with_alpha(ImVec4 c, float a)
		{
			c.w *= a;
			return c;
		}

		ImVec4 mix(const ImVec4& a, const ImVec4& b, float t)
		{
			return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
		}

		ImU32 col(const ImVec4& c)
		{
			return ImGui::GetColorU32(c);
		}

		float px(float v)
		{
			return std::round(v * g_scale);
		}

		float animate(ImGuiID id, float target, float speed = 16.f)
		{
			float* value = ImGui::GetStateStorage()->GetFloatRef(id, target);
			*value += (target - *value) * (1.f - std::exp(-ImGui::GetIO().DeltaTime * speed));
			if (std::fabs(target - *value) < 0.001f)
				*value = target;
			return *value;
		}

		bool toggle(const char* label, bool* value)
		{
			ImGui::PushID(label);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const float row_h = ImGui::GetFrameHeight();
			const float h = std::round(row_h * 0.72f);
			const float w = std::round(h * 1.8f);
			const float gap = px(12.f);
			const ImVec2 ts = ImGui::CalcTextSize(label);
			const bool pressed = ImGui::InvisibleButton("##t", ImVec2(w + gap + ts.x, row_h));
			if (pressed)
				*value = !*value;
			const bool hovered = ImGui::IsItemHovered();
			const float t = animate(ImGui::GetItemID(), *value ? 1.f : 0.f);

			auto* dl = ImGui::GetWindowDrawList();
			const float y = std::round(pos.y + (row_h - h) * 0.5f);
			dl->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + w, y + h), col(mix(hovered ? k_frame_hover : k_frame, k_on, t)), h * 0.5f);
			const float r = h * 0.5f - px(3.f);
			const float x = pos.x + h * 0.5f + (w - h) * t;
			dl->AddCircleFilled(ImVec2(x, y + h * 0.5f), r, col(mix(k_text, k_on_text, t)));
			dl->AddText(ImVec2(pos.x + w + gap, std::round(pos.y + (row_h - ts.y) * 0.5f)), col(mix(k_text_dim, k_text, t)), label);
			ImGui::PopID();
			return pressed;
		}

		// square-ish button; 'active' fills it white with black text
		bool pill(const char* label, bool active, float width = 0.f)
		{
			const float h = ImGui::GetFrameHeight();
			const ImVec2 ts = ImGui::CalcTextSize(label);
			const ImVec2 size(width > 0.f ? width : std::max(std::round(h * 1.1f), ts.x + px(16.f)), h);
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const bool pressed = ImGui::InvisibleButton(label, size);
			const bool hovered = ImGui::IsItemHovered();
			const float t = animate(ImGui::GetItemID(), active ? 1.f : 0.f);
			auto* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), col(mix(hovered ? k_frame_hover : k_frame, k_on, t)),
				ImGui::GetStyle().FrameRounding);
			dl->AddText(ImVec2(std::round(pos.x + (size.x - ts.x) * 0.5f), std::round(pos.y + (size.y - ts.y) * 0.5f)),
				col(mix(k_text, k_on_text, t)), label);
			return pressed;
		}

		void divider()
		{
			ImGui::Dummy(ImVec2(0.f, px(2.f)));
			const ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, std::round(p.y)),
				ImVec2(p.x + ImGui::GetContentRegionAvail().x, std::round(p.y) + px(1.f)), col(k_line));
			ImGui::Dummy(ImVec2(0.f, px(6.f)));
		}

		void section(const char* text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, k_text_dim);
			ImGui::TextUnformatted(text);
			ImGui::PopStyleColor();
		}

		// ---- Random Fix --------------------------------------------------------

		void draw_key_block(ImDrawList* dl, ImVec2 a, ImVec2 b, int key, bool hovered, float alpha, bool show_digit)
		{
			const bool white = (key % 2) == 0;
			const float r = px(7.f);
			dl->AddRectFilled(a, b, col(with_alpha(white ? k_white_key : k_black_key, alpha)), r);
			if (!white)
				dl->AddRect(a, b, col(ImVec4(1.f, 1.f, 1.f, 0.18f * alpha)), r, 0, px(1.f));
			if (hovered)
				dl->AddRect(a, b, col(with_alpha(white ? k_on_text : k_text, alpha)), r, 0, px(2.f));
			if (!show_digit)
				return;
			const char digit[2] = { static_cast<char>('1' + key), 0 };
			const float size = std::round(ImGui::GetFontSize() * 1.4f);
			const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.f, digit);
			dl->AddText(ImGui::GetFont(), size, ImVec2(std::round(a.x + (b.x - a.x - ts.x) * 0.5f), std::round(a.y + (b.y - a.y - ts.y) * 0.5f)),
				col(with_alpha(white ? k_on_text : k_text, alpha)), digit);
		}

		struct key_row_t
		{
			float x[7] = {};
			bool placed = false;
			int drag_key = -1;
			float grab_dx = 0.f;
			int last_frame = -2;
			perm_t last_screen{};
		};
		key_row_t g_rows[2];

		constexpr float k_block_w = 40.f;
		constexpr float k_block_h = 58.f;
		constexpr float k_block_gap = 6.f;
		constexpr float k_flip_w = 30.f;

		float key_row_width()
		{
			return px(k_block_w) * 7.f + px(k_block_gap) * 6.f;
		}

		// key blocks plus the flip button
		float side_row_width()
		{
			return key_row_width() + px(k_block_gap) + px(k_flip_w);
		}

		// two arrows, right over left
		void draw_flip_icon(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color)
		{
			const float cx = std::round((a.x + b.x) * 0.5f);
			const float cy = std::round((a.y + b.y) * 0.5f);
			const float half = px(8.f);
			const float head = px(4.f);
			const float dy = px(6.f);
			const float thick = std::max(1.f, px(1.6f));
			const float y1 = cy - dy;
			const float y2 = cy + dy;
			dl->AddLine(ImVec2(cx - half, y1), ImVec2(cx + half - head, y1), color, thick);
			dl->AddTriangleFilled(ImVec2(cx + half, y1), ImVec2(cx + half - head, y1 - head), ImVec2(cx + half - head, y1 + head), color);
			dl->AddLine(ImVec2(cx + half, y2), ImVec2(cx - half + head, y2), color, thick);
			dl->AddTriangleFilled(ImVec2(cx - half, y2), ImVec2(cx - half + head, y2 - head), ImVec2(cx - half + head, y2 + head), color);
		}

		// dragging lifts a block; the others slide aside; releasing inserts it there
		void draw_keys(int side, side_config_t& sc, bool& changed)
		{
			const bool show_digits = !sc.group;
			const float block_w = px(k_block_w);
			const float block_h = px(k_block_h);
			const float pitch = block_w + px(k_block_gap);
			const float row_w = key_row_width();
			const auto& io = ImGui::GetIO();
			auto& row = g_rows[side];

			ImGui::Dummy(ImVec2(0.f, px(2.f)));
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const perm_t screen = inverse(sc.perm);

			int pressed_slot = -1;
			int hovered_slot = -1;
			ImGui::BeginDisabled(sc.ran);
			for (int slot = 0; slot < 7; ++slot)
			{
				ImGui::SetCursorScreenPos(ImVec2(origin.x + slot * pitch, origin.y));
				ImGui::PushID(slot);
				ImGui::InvisibleButton("key", ImVec2(block_w, block_h));
				if (!sc.ran && ImGui::IsItemActive())
					pressed_slot = slot;
				if (!sc.ran && ImGui::IsItemHovered())
					hovered_slot = slot;
				ImGui::PopID();
			}
			ImGui::EndDisabled();

			// flips the order left to right at once; a new order is drawn every play under ran
			const ImVec2 flip_a(origin.x + row_w + px(k_block_gap), origin.y);
			const ImVec2 flip_b(flip_a.x + px(k_flip_w), origin.y + block_h);
			ImGui::SetCursorScreenPos(flip_a);
			ImGui::BeginDisabled(sc.ran);
			const bool flip = ImGui::InvisibleButton("flip", ImVec2(px(k_flip_w), block_h)) && !sc.ran;
			const bool flip_hover = !sc.ran && ImGui::IsItemHovered();
			ImGui::EndDisabled();

			ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + block_h));
			ImGui::Dummy(ImVec2(side_row_width(), 0.f));

			const int frame_no = ImGui::GetFrameCount();
			if (frame_no != row.last_frame + 1)
			{
				row.drag_key = -1;
				row.placed = false;
			}
			row.last_frame = frame_no;
			if (sc.ran)
				row.drag_key = -1;
			// a group redraw keeps the colours: snap instead of sliding identical blocks
			if (row.placed && sc.group && row.drag_key < 0 && screen != row.last_screen &&
				color_pattern(sc.perm) == color_pattern(inverse(row.last_screen)))
				row.placed = false;
			if (!row.placed)
			{
				for (int slot = 0; slot < 7; ++slot)
					row.x[screen[static_cast<size_t>(slot)]] = slot * pitch;
				row.placed = true;
			}

			if (flip)
			{
				sc.perm = random_logic::flip(sc.perm);
				changed = true;
				row.drag_key = -1;
			}

			if (row.drag_key < 0 && pressed_slot >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.f * g_scale))
			{
				row.drag_key = screen[static_cast<size_t>(pressed_slot)];
				row.grab_dx = io.MouseClickedPos[ImGuiMouseButton_Left].x - (origin.x + row.x[row.drag_key]);
			}

			std::array<int32_t, 7> order = screen;
			float drag_x = 0.f;
			int drop_slot = -1;
			if (row.drag_key >= 0)
			{
				drag_x = std::clamp(io.MousePos.x - origin.x - row.grab_dx, 0.f, row_w - block_w);
				const int target = std::clamp(static_cast<int>(std::floor(drag_x / pitch + 0.5f)), 0, 6);
				std::vector<int32_t> v(screen.begin(), screen.end());
				v.erase(std::find(v.begin(), v.end(), row.drag_key));
				v.insert(v.begin() + target, row.drag_key);
				std::copy(v.begin(), v.end(), order.begin());
				drop_slot = target;
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					if (order != screen)
					{
						sc.perm = inverse(order);
						changed = true;
					}
					row.drag_key = -1;
					drop_slot = -1;
				}
			}

			const float follow = 1.f - std::exp(-io.DeltaTime * 18.f);
			for (int slot = 0; slot < 7; ++slot)
			{
				const int key = order[static_cast<size_t>(slot)];
				const float tx = key == row.drag_key ? drag_x : slot * pitch;
				row.x[key] += (tx - row.x[key]) * (key == row.drag_key ? 1.f : follow);
			}

			auto* dl = ImGui::GetWindowDrawList();
			const float alpha = sc.ran ? 0.5f : 1.f;
			dl->AddRectFilled(flip_a, flip_b, col(with_alpha(flip_hover ? k_frame_hover : k_frame, sc.ran ? 0.4f : 1.f)), px(7.f));
			draw_flip_icon(dl, flip_a, flip_b, col(with_alpha(k_text, sc.ran ? 0.35f : 1.f)));
			if (drop_slot >= 0)
			{
				const ImVec2 a(origin.x + drop_slot * pitch, origin.y);
				dl->AddRect(a, ImVec2(a.x + block_w, a.y + block_h), col(with_alpha(k_text, 0.5f)), px(7.f), 0, px(1.5f));
			}
			for (int slot = 0; slot < 7; ++slot)
			{
				const int key = order[static_cast<size_t>(slot)];
				if (key == row.drag_key)
					continue;
				const bool hover = row.drag_key < 0 && hovered_slot >= 0 && screen[static_cast<size_t>(hovered_slot)] == key;
				const ImVec2 a(std::round(origin.x + row.x[key]), origin.y - (hover ? px(2.f) : 0.f));
				draw_key_block(dl, a, ImVec2(a.x + block_w, a.y + block_h), key, hover, alpha, show_digits);
			}
			if (row.drag_key >= 0)
			{
				const float grow = px(3.f);
				const ImVec2 a(std::round(origin.x + row.x[row.drag_key]) - grow, origin.y - px(6.f) - grow);
				const ImVec2 b(a.x + block_w + grow * 2.f, a.y + block_h + grow * 2.f);
				dl->AddRectFilled(ImVec2(a.x + px(2.f), a.y + px(6.f)), ImVec2(b.x + px(2.f), b.y + px(6.f)), col(ImVec4(0.f, 0.f, 0.f, 0.35f)), px(9.f));
				draw_key_block(dl, a, b, row.drag_key, false, 1.f, show_digits);
				dl->AddRect(a, b, col(k_text), px(7.f), 0, px(2.f));
			}
			row.last_screen = inverse(sc.perm);
		}

		char g_order_buf[2][8] = {};
		bool g_order_invalid[2] = {};
		bool g_order_editing[2] = {};

		int digit_filter(ImGuiInputTextCallbackData* data)
		{
			return (data->EventChar >= '1' && data->EventChar <= '7') ? 0 : 1;
		}

		void draw_side(int side, side_config_t& sc, bool& changed)
		{
			ImGui::PushID(side);
			const float pad = px(12.f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
			ImGui::BeginChild("side", ImVec2(side_row_width() + pad * 2.f, 0.f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
			ImGui::PopStyleVar();

			const bool drawn = sc.ran || sc.group;
			if (drawn || (!g_order_editing[side] && !g_order_invalid[side]))
			{
				const auto text = screen_order(sc.perm);
				std::memcpy(g_order_buf[side], text.data(), 7);
				g_order_buf[side][7] = '\0';
				if (drawn)
					g_order_invalid[side] = false;
			}

			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(side == 0 ? "P1" : "P2");
			ImGui::SameLine();
			const float button_w = std::round(ImGui::GetFrameHeight() * 1.1f);
			const float spacing = ImGui::GetStyle().ItemSpacing.x;

			ImGui::BeginDisabled(drawn);
			// InputText can change g_order_invalid; pop exactly what was pushed
			const bool marked = g_order_invalid[side];
			if (marked)
				ImGui::PushStyleColor(ImGuiCol_FrameBg, k_error);
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 4.f * (button_w + spacing));
			if (ImGui::InputText("##order", g_order_buf[side], sizeof(g_order_buf[side]), ImGuiInputTextFlags_CallbackCharFilter, digit_filter))
			{
				perm_t perm{};
				g_order_invalid[side] = !parse_screen_order(g_order_buf[side], perm);
				if (!g_order_invalid[side])
				{
					sc.perm = perm;
					changed = true;
				}
			}
			g_order_editing[side] = ImGui::IsItemActive();
			if (marked)
				ImGui::PopStyleColor();
			ImGui::SameLine();
			if (pill(k_label_normal, false, button_w))
			{
				sc.perm = { 0, 1, 2, 3, 4, 5, 6 };
				g_order_invalid[side] = false;
				changed = true;
			}
			ImGui::SameLine();
			if (pill(k_label_mirror, false, button_w))
			{
				sc.perm = { 6, 5, 4, 3, 2, 1, 0 };
				g_order_invalid[side] = false;
				changed = true;
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			ImGui::BeginDisabled(sc.group);
			if (pill(k_label_ran, sc.ran, button_w))
			{
				sc.ran = !sc.ran;
				changed = true;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (pill(k_label_group, sc.group, button_w))
			{
				sc.group = !sc.group;
				if (sc.group)
					sc.ran = false;
				changed = true;
			}

			draw_keys(side, sc, changed);
			ImGui::EndChild();
			ImGui::PopID();
		}

		// ---- effects -------------------------------------------------------------

		bool opacity_row(const char* label, std::atomic<int>& value)
		{
			const int start = value.load();
			ImGui::PushID(label);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::SameLine(px(120.f));
			// nothing selected = 100 % (off); clicking the selected preset turns it off
			int v = value.load();
			const int presets[] = { 75, 50, 25, 0 };
			const float w = std::round(ImGui::GetFrameHeight() * 1.6f);
			for (const int p : presets)
			{
				char text[8];
				sprintf_s(text, "%d", p);
				if (pill(text, v == p, w))
					value = v = (v == p ? 100 : p);
				ImGui::SameLine();
			}
			ImGui::SetNextItemWidth(std::round(ImGui::GetFrameHeight() * 2.4f));
			if (ImGui::InputInt("##manual", &v, 0, 0))
				value = std::clamp(v, 0, 100);
			ImGui::SameLine(0.f, px(4.f));
			ImGui::TextUnformatted("%");
			ImGui::PopID();
			return value.load() != start;
		}
	}

	void apply_style(float scale)
	{
		g_scale = scale;
		ImGui::StyleColorsDark();
		auto& s = ImGui::GetStyle();
		s.WindowRounding = 10.f;
		s.ChildRounding = 8.f;
		s.FrameRounding = 6.f;
		s.WindowPadding = ImVec2(18.f, 16.f);
		s.FramePadding = ImVec2(9.f, 6.f);
		s.ItemSpacing = ImVec2(8.f, 9.f);
		s.WindowBorderSize = 1.f;
		s.ChildBorderSize = 0.f;
		s.FrameBorderSize = 0.f;
		s.DisabledAlpha = 0.4f;

		auto* c = s.Colors;
		c[ImGuiCol_WindowBg] = k_bg;
		c[ImGuiCol_ChildBg] = k_panel;
		c[ImGuiCol_Border] = k_line;
		c[ImGuiCol_FrameBg] = k_frame;
		c[ImGuiCol_FrameBgHovered] = k_frame_hover;
		c[ImGuiCol_FrameBgActive] = k_frame_hover;
		c[ImGuiCol_Button] = k_frame;
		c[ImGuiCol_ButtonHovered] = k_frame_hover;
		c[ImGuiCol_ButtonActive] = k_frame_hover;
		c[ImGuiCol_TitleBg] = k_panel;
		c[ImGuiCol_TitleBgActive] = k_panel;
		c[ImGuiCol_TitleBgCollapsed] = k_panel;
		c[ImGuiCol_CheckMark] = k_text;
		c[ImGuiCol_Text] = k_text;
		c[ImGuiCol_TextDisabled] = k_text_dim;
		c[ImGuiCol_TextSelectedBg] = ImVec4(1.f, 1.f, 1.f, 0.25f);
		c[ImGuiCol_InputTextCursor] = k_text;
		c[ImGuiCol_NavCursor] = ImVec4(1.f, 1.f, 1.f, 0.5f);
		s.ScaleAllSizes(scale);
	}

	bool draw(float scale)
	{
		g_scale = scale;
		bool open = true;
		const auto& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
		ImGui::Begin("IIDX Toolbox", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

		const auto before = config::get_random();
		auto after = before;
		bool changed = toggle("RANDOM FIX", &after.enabled);
		if (after.enabled)
		{
			for (int side = 0; side < 2; ++side)
				draw_side(side, after.side[side], changed);
		}
		if (changed)
			config::set_random(before, after);

		// every change is saved at once (background, coalesced): a game closed with the
		// menu open or killed keeps the settings
		bool dirty = false;
		divider();
		bool scratch = config::all_scratch;
		bool charge = config::all_charge;
		if (toggle("ALL SCRATCH", &scratch))
		{
			config::all_scratch = scratch;
			if (scratch)
				config::all_charge = false;
			dirty = true;
		}
		if (toggle("ALL CHARGE", &charge))
		{
			config::all_charge = charge;
			if (charge)
				config::all_scratch = false;
			dirty = true;
		}

		divider();
		section("OPACITY");
		dirty |= opacity_row("BOMB", config::bomb_opacity);
		dirty |= opacity_row("KEY BEAM", config::beam_opacity);
		dirty |= opacity_row("JUDGE", config::judge_opacity);
		bool pgreat = config::pgreat_blue;
		if (toggle("PGREAT BLUE", &pgreat))
		{
			config::pgreat_blue = pgreat;
			dirty = true;
		}
		bool no_blink = config::no_blink;
		if (toggle("NO BLINK", &no_blink))
		{
			config::no_blink = no_blink;
			dirty = true;
		}

		divider();
		bool timing = config::timing_enabled;
		if (toggle("MS CLOCK", &timing))
		{
			config::timing_enabled = timing;
			dirty = true;
		}

		if (dirty)
			config::save();
		ImGui::End();
		return open;
	}
}