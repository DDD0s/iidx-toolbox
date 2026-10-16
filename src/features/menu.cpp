#include "std_include.hpp"

#include <imgui.h>
#include <imgui_impl_dx9.h>

#include "component.hpp"
#include "config.hpp"
#include "d3d.hpp"
#include "log.hpp"
#include "menu_ui.hpp"
#include "state.hpp"

namespace tb::menu
{
	bool g_open = false;

	namespace
	{
		HWND g_hwnd = nullptr;
		WNDPROC g_old_wndproc = nullptr;
		bool g_unicode = false;
		bool g_imgui_ready = false;
		bool g_imgui_failed = false;
		IDirect3DDevice9* g_imgui_device = nullptr;
		bool g_key_was_down = false;
		LARGE_INTEGER g_last_time{};
		float g_scale = 1.f;

		std::mutex g_input_mutex;
		std::vector<std::pair<int, bool>> g_key_events;
		std::vector<unsigned int> g_chars;

		ImGuiKey vk_to_key(WPARAM vk)
		{
			switch (vk)
			{
			case VK_BACK: return ImGuiKey_Backspace;
			case VK_DELETE: return ImGuiKey_Delete;
			case VK_RETURN: return ImGuiKey_Enter;
			case VK_ESCAPE: return ImGuiKey_Escape;
			case VK_LEFT: return ImGuiKey_LeftArrow;
			case VK_RIGHT: return ImGuiKey_RightArrow;
			case VK_HOME: return ImGuiKey_Home;
			case VK_END: return ImGuiKey_End;
			case VK_TAB: return ImGuiKey_Tab;
			default: return ImGuiKey_None;
			}
		}

		// digits from WM_KEYDOWN: the game's message loop may not call TranslateMessage
		// (no WM_CHAR); the menu's fields only take digits
		LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
		{
			if (g_open && (msg == WM_KEYDOWN || msg == WM_KEYUP))
			{
				std::lock_guard lock(g_input_mutex);
				if (vk_to_key(wparam) != ImGuiKey_None)
					g_key_events.emplace_back(static_cast<int>(vk_to_key(wparam)), msg == WM_KEYDOWN);
				else if (msg == WM_KEYDOWN && wparam >= '0' && wparam <= '9')
					g_chars.push_back(static_cast<unsigned int>(wparam));
				else if (msg == WM_KEYDOWN && wparam >= VK_NUMPAD0 && wparam <= VK_NUMPAD9)
					g_chars.push_back(static_cast<unsigned int>('0' + (wparam - VK_NUMPAD0)));
			}
			return g_unicode ? CallWindowProcW(g_old_wndproc, hwnd, msg, wparam, lparam)
				: CallWindowProcA(g_old_wndproc, hwnd, msg, wparam, lparam);
		}

		void load_fonts(float scale)
		{
			auto& io = ImGui::GetIO();
			io.Fonts->Clear();
			ImFontGlyphRangesBuilder builder;
			builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
			builder.AddText("\xE6\xAD\xA3\xE9\x8F\xA1\xE4\xB9\xB1\xE7\xB5\x84");
			static ImVector<ImWchar> ranges;
			ranges.clear();
			builder.BuildRanges(&ranges);

			const float size = std::round(17.f * scale);
			static const char* candidates[] = { "YuGothM.ttc", "meiryo.ttc", "msgothic.ttc" };
			char windir[MAX_PATH] = {};
			GetWindowsDirectoryA(windir, MAX_PATH);
			for (const char* name : candidates)
			{
				const std::string path = std::string(windir) + "\\Fonts\\" + name;
				if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
					continue;
				ImFontConfig cfg;
				cfg.OversampleH = 2;
				if (io.Fonts->AddFontFromFileTTF(path.c_str(), size, &cfg, ranges.Data))
					return;
			}
			ImFontConfig cfg;
			cfg.SizePixels = size;
			io.Fonts->AddFontDefault(&cfg);
		}

		bool init_imgui(IDirect3DSwapChain9* chain)
		{
			D3DPRESENT_PARAMETERS pp{};
			if (FAILED(chain->GetPresentParameters(&pp)))
				return false;
			HWND hwnd = pp.hDeviceWindow;
			if (!hwnd)
			{
				D3DDEVICE_CREATION_PARAMETERS cp{};
				if (SUCCEEDED(d3d::device()->GetCreationParameters(&cp)))
					hwnd = cp.hFocusWindow;
			}
			if (!hwnd)
				return false;
			g_hwnd = hwnd;

			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
			io.ConfigErrorRecoveryEnableAssert = false; // a UI mistake must never abort the game

			g_scale = std::clamp(static_cast<float>(pp.BackBufferHeight) / 1080.f, 0.75f, 3.f);
			load_fonts(g_scale);
			menu_ui::apply_style(g_scale);
			if (!ImGui_ImplDX9_Init(d3d::device()))
				return false;
			g_imgui_device = d3d::device();

			// the game window is ANSI; subclassing it with a W procedure would make it Unicode
			g_unicode = IsWindowUnicode(hwnd) != FALSE;
			g_old_wndproc = reinterpret_cast<WNDPROC>(g_unicode
				? SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndproc))
				: SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndproc)));
			QueryPerformanceCounter(&g_last_time);
			tb::log("menu", "ready: %ux%u, key 0x%02X", pp.BackBufferWidth, pp.BackBufferHeight, config::menu_key());
			return true;
		}

		bool window_in_front()
		{
			const HWND fg = GetForegroundWindow();
			return fg && (fg == g_hwnd || GetAncestor(fg, GA_ROOT) == GetAncestor(g_hwnd, GA_ROOT));
		}

		void feed_input(IDirect3DSwapChain9* chain)
		{
			auto& io = ImGui::GetIO();
			D3DPRESENT_PARAMETERS pp{};
			chain->GetPresentParameters(&pp);
			io.DisplaySize = ImVec2(static_cast<float>(pp.BackBufferWidth), static_cast<float>(pp.BackBufferHeight));

			LARGE_INTEGER now, freq;
			QueryPerformanceCounter(&now);
			QueryPerformanceFrequency(&freq);
			io.DeltaTime = std::clamp(static_cast<float>(now.QuadPart - g_last_time.QuadPart) / static_cast<float>(freq.QuadPart), 1e-4f, 0.25f);
			g_last_time = now;

			const bool front = window_in_front();
			POINT cursor{};
			RECT client{};
			if (front && GetCursorPos(&cursor) && ScreenToClient(g_hwnd, &cursor) && GetClientRect(g_hwnd, &client) &&
				client.right > 0 && client.bottom > 0)
			{
				io.AddMousePosEvent(cursor.x * io.DisplaySize.x / static_cast<float>(client.right),
					cursor.y * io.DisplaySize.y / static_cast<float>(client.bottom));
			}
			io.AddMouseButtonEvent(0, front && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);

			std::lock_guard lock(g_input_mutex);
			for (const auto& [key, down] : g_key_events)
				io.AddKeyEvent(static_cast<ImGuiKey>(key), down);
			for (const auto c : g_chars)
				io.AddInputCharacter(c);
			g_key_events.clear();
			g_chars.clear();
		}

		void set_open(bool open)
		{
			g_open = open;
			state::menu_open = open;
			tb::log("menu", open ? "opened" : "closed");
			auto& io = ImGui::GetIO();
			io.MouseDrawCursor = open;
			io.ClearInputKeys();
			io.AddMouseButtonEvent(0, false);
			if (!open)
				config::save();
		}

		void draw(IDirect3DSwapChain9* chain)
		{
			const bool down = window_in_front() && (GetAsyncKeyState(config::menu_key()) & 0x8000) != 0;
			if (down && !g_key_was_down)
				set_open(!g_open);
			g_key_was_down = down;
			if (!g_open)
				return;

			feed_input(chain);
			ImGui_ImplDX9_NewFrame();
			ImGui::NewFrame();
			if (!menu_ui::draw(g_scale))
				set_open(false);
			ImGui::EndFrame();
			ImGui::Render();

			auto* device = d3d::device();
			IDirect3DSurface9* back = nullptr;
			IDirect3DSurface9* old_rt = nullptr;
			IDirect3DSurface9* old_ds = nullptr;
			if (FAILED(chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &back)))
				return;
			D3DVIEWPORT9 old_viewport{};
			device->GetViewport(&old_viewport);
			device->GetRenderTarget(0, &old_rt);
			device->GetDepthStencilSurface(&old_ds);
			device->SetRenderTarget(0, back);
			device->SetDepthStencilSurface(nullptr);
			if (SUCCEEDED(device->BeginScene()))
			{
				ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
				device->EndScene();
			}
			device->SetRenderTarget(0, old_rt);
			device->SetDepthStencilSurface(old_ds);
			device->SetViewport(&old_viewport);
			if (old_ds)
				old_ds->Release();
			if (old_rt)
				old_rt->Release();
			back->Release();
		}

		void before_present(IDirect3DSwapChain9* chain)
		{
			if (!g_imgui_ready && !g_imgui_failed)
			{
				g_imgui_ready = init_imgui(chain);
				g_imgui_failed = !g_imgui_ready;
				if (g_imgui_failed)
					tb::log("menu", "ImGui init failed; menu unavailable");
			}
			if (g_imgui_ready && d3d::device() != g_imgui_device)
			{
				ImGui_ImplDX9_Shutdown();
				g_imgui_device = d3d::device();
				ImGui_ImplDX9_Init(g_imgui_device);
				tb::log("menu", "moved to the new device");
			}
			if (g_imgui_ready)
				draw(chain);
		}
	}

	void test_draw(IDirect3DSwapChain9* chain)
	{
		if (g_imgui_ready)
			draw(chain);
	}

	class component final : public component_interface
	{
	public:
		void post_load() override
		{
			d3d::on_before_present(&before_present);
			d3d::on_before_reset([](IDirect3DDevice9*) {
				if (g_imgui_ready)
					ImGui_ImplDX9_InvalidateDeviceObjects();
			});
			d3d::on_after_reset([](IDirect3DDevice9*) {
				if (g_imgui_ready)
					ImGui_ImplDX9_CreateDeviceObjects();
			});
		}

		// the window keeps our procedure: the module is pinned and never unloaded
		void pre_destroy() override
		{
			config::save_final();
		}
	};
}

TB_REGISTER_COMPONENT(tb::menu::component, 90)

extern "C" __declspec(dllexport) void __cdecl iidx_toolbox_test_open_menu()
{
	tb::menu::g_open = true;
}

extern "C" __declspec(dllexport) void __cdecl iidx_toolbox_test_draw(IDirect3DSwapChain9* chain)
{
	tb::menu::test_draw(chain);
}