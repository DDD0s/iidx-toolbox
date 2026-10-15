// Loads IIDX Toolbox, creates a D3D9Ex HAL device the way the game does (so the
// device-creation hook fires), forces the menu open and saves the frame as BMP.
// Run: render.exe <bm2dx.dll> <iidx_toolbox.dll> <out.bmp>

#include <Windows.h>
#include <d3d9.h>

#include <cstdio>
#include <vector>

namespace
{
	bool save_bmp(IDirect3DDevice9Ex* device, IDirect3DSurface9* back, const char* path)
	{
		D3DSURFACE_DESC desc{};
		back->GetDesc(&desc);
		IDirect3DSurface9* sys = nullptr;
		if (FAILED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr)))
			return false;
		if (FAILED(device->GetRenderTargetData(back, sys)))
			return false;
		D3DLOCKED_RECT lr{};
		sys->LockRect(&lr, nullptr, D3DLOCK_READONLY);
		BITMAPFILEHEADER fh{};
		BITMAPINFOHEADER ih{};
		ih.biSize = sizeof(ih);
		ih.biWidth = static_cast<LONG>(desc.Width);
		ih.biHeight = -static_cast<LONG>(desc.Height);
		ih.biPlanes = 1;
		ih.biBitCount = 32;
		const DWORD bytes = desc.Width * desc.Height * 4;
		fh.bfType = 0x4D42;
		fh.bfOffBits = sizeof(fh) + sizeof(ih);
		fh.bfSize = fh.bfOffBits + bytes;
		FILE* f = nullptr;
		fopen_s(&f, path, "wb");
		if (!f)
			return false;
		fwrite(&fh, sizeof(fh), 1, f);
		fwrite(&ih, sizeof(ih), 1, f);
		for (UINT y = 0; y < desc.Height; ++y)
			fwrite(static_cast<const char*>(lr.pBits) + y * lr.Pitch, desc.Width * 4, 1, f);
		fclose(f);
		sys->UnlockRect();
		sys->Release();
		return true;
	}
}

int main(int argc, char** argv)
{
	if (argc < 4)
		return 2;
	if (!LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES))
		return 1;
	const HMODULE plugin = LoadLibraryA(argv[2]);
	if (!plugin)
		return 1;
	using fn_t = void(__cdecl*)();
	reinterpret_cast<fn_t>(GetProcAddress(plugin, "iidx_toolbox_init"))();
	reinterpret_cast<fn_t>(GetProcAddress(plugin, "iidx_toolbox_test_open_menu"))();

	WNDCLASSEXA wc{ sizeof(wc) };
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandleA(nullptr);
	wc.lpszClassName = "render_test";
	RegisterClassExA(&wc);
	RECT rc{ 0, 0, 1280, 720 };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	const HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "render", WS_OVERLAPPEDWINDOW, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
		nullptr, nullptr, wc.hInstance, nullptr);
	ShowWindow(hwnd, SW_SHOWNOACTIVATE);

	IDirect3D9Ex* d3d = nullptr;
	Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d);
	D3DPRESENT_PARAMETERS pp{};
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.hDeviceWindow = hwnd;
	pp.BackBufferWidth = 1280;
	pp.BackBufferHeight = 720;
	pp.BackBufferFormat = D3DFMT_X8R8G8B8;
	pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	IDirect3DDevice9Ex* device = nullptr;
	const HRESULT hr = d3d->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, nullptr, &device);
	if (FAILED(hr))
	{
		std::printf("CreateDeviceEx %08lx\n", hr);
		return 1;
	}
	IDirect3DSwapChain9* chain = nullptr;
	device->GetSwapChain(0, &chain);
	for (int frame = 0; frame < 30; ++frame)
	{
		MSG msg;
		while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
			DispatchMessageA(&msg);
		device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(30, 34, 52), 1.f, 0);
		if (frame == 29)
		{
			// the menu draws inside Present, before the swap: capture via a Present with no swap is not
			// possible, so draw once more and read the back buffer after the hook ran on the last frame
		}
		chain->Present(nullptr, nullptr, nullptr, nullptr, 0);
		Sleep(16);
	}
	// last Present drew the menu into the back buffer before swapping; with DISCARD the
	// content is undefined afterwards, so render one more frame without swapping:
	device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(30, 34, 52), 1.f, 0);
	using draw_t = void(__cdecl*)(IDirect3DSwapChain9*);
	reinterpret_cast<draw_t>(GetProcAddress(plugin, "iidx_toolbox_test_draw"))(chain);
	IDirect3DSurface9* back = nullptr;
	chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &back);
	const bool ok = save_bmp(device, back, argv[3]);
	std::printf("saved %s: %d\n", argv[3], ok ? 1 : 0);
	back->Release();
	chain->Release();
	reinterpret_cast<fn_t>(GetProcAddress(plugin, "iidx_toolbox_shutdown"))();
	return ok ? 0 : 1;
}
