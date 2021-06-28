#include "renderer.hpp"
#include <mutex>

std::once_flag is_init;

bool render_interface = false;
bool window_selected = true;
bool main_window_enabled = true;

HWND window;
HWND global_raw_hwnd = nullptr; // roblox hwnd
HWND global_hwnd = nullptr; // global `protected` hwnd

int s_w, s_h;

RECT rc;
RECT client_rect;

WNDPROC original_wnd_proc;

present_t d3d11_present = nullptr;
resize_buffers_t d3d11_resize_buffers = nullptr;

DXGI_SWAP_CHAIN_DESC sd;

ID3D11Device* global_device = nullptr;
ID3D11DeviceContext* global_context = nullptr;
ID3D11RenderTargetView* main_render_target_view;
ID3D11Texture2D* back_buffer = nullptr;
IDXGISwapChain* global_swapchain = nullptr;

LRESULT __stdcall wnd_proc(const HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
{
	ImGuiIO& io = ImGui::GetIO();

	ImGui_ImplWin32_WndProcHandler(hwnd, message, w_param, l_param);

	if (render_interface) ImGui::GetIO().MouseDrawCursor = true;
	else ImGui::GetIO().MouseDrawCursor = false;

	switch (message)
	{
	case WM_KILLFOCUS:
		window_selected = false;
		break;
	case WM_SETFOCUS:
		window_selected = true;
		break;
	case WH_CBT:
		window = sd.OutputWindow;
		global_hwnd = hwnd;
		break;
	case WM_KEYDOWN:
		if (w_param == VK_INSERT) render_interface = !render_interface;
		break;
	case WM_MOUSEMOVE:
		if (render_interface && window_selected)
			return TRUE;
		break;

	case 522:
	case 513:
	case 533:
	case 514:
	case 134:
	case 516:
	case 517:
	case 258:
	case 257:
	case 132:
	case 127:
	case 255:
	case 523:
	case 524:
	case 793:
		if (render_interface) return TRUE; //block basically all messages we don't want roblox to receive
		break;
	}

	return CallWindowProc(original_wnd_proc, hwnd, message, w_param, l_param);
}

HRESULT __stdcall present_hook(IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags)
{
	std::call_once(is_init, [&]()
	{
		swap_chain->GetDesc(&sd);
		swap_chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&global_device));
		swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back_buffer));

		window = sd.OutputWindow;
		original_wnd_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wnd_proc)));

		global_device->GetImmediateContext(&global_context);
		global_device->CreateRenderTargetView(back_buffer, nullptr, &main_render_target_view); back_buffer->Release();

		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();

		io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
		io.IniFilename = NULL;

		ImGui::StyleColorsDark();

		ImGui_ImplWin32_Init(window);
		ImGui_ImplDX11_Init(global_device, global_context);
	});

	if (main_render_target_view == nullptr)
	{
		swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back_buffer));
		global_device->CreateRenderTargetView(back_buffer, nullptr, &main_render_target_view);

		back_buffer->Release();
	}

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();

	if (render_interface)
	{
		ImDrawList* bgDrawList = ImGui::GetBackgroundDrawList();
		bgDrawList->AddRectFilled(ImVec2(0, 0), ImVec2(s_w, s_h), ImColor(0.0f, 0.0f, 0.0f, 0.5f));

		ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_Once);
		ImGui::Begin("Fully Detected Chair", &main_window_enabled, ImGuiWindowFlags_NoResize);
		{

		}

		ImGui::End();
	}

	ImGui::Render();

	global_context->OMSetRenderTargets(1, &main_render_target_view, NULL);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	return d3d11_present(swap_chain, sync_interval, flags);
}

HRESULT __stdcall resize_buffers_hook(IDXGISwapChain* this_ptr, UINT buffer_count, UINT width, UINT height, DXGI_FORMAT new_format, UINT swap_chain_flags)
{
	if (main_render_target_view)
	{
		main_render_target_view->Release();
		main_render_target_view = nullptr;
	}

	window = sd.OutputWindow;

	GetWindowRect(global_hwnd, &rc);
	GetClientRect(global_hwnd, &client_rect);

	s_w = rc.right - rc.left;
	s_h = rc.bottom - rc.top;

	return d3d11_resize_buffers(this_ptr, buffer_count, width, height, new_format, swap_chain_flags);
}

void renderer::init()
{
	const std::uintptr_t base_address = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("DiscordHook.dll"));

	global_raw_hwnd = FindWindowW(0, L"Roblox");
	global_hwnd = reinterpret_cast<HWND>(CreateMenu());

	SetForegroundWindow(global_raw_hwnd);
	GetWindowRect(global_raw_hwnd, &rc);
	GetClientRect(global_raw_hwnd, &client_rect);

	s_w = rc.right - rc.left;
	s_h = rc.bottom - rc.top;

	if (base_address)
	{
		const std::uintptr_t present_address = base_address + discord_overlay::addresses::present;
		const std::uintptr_t resize_buffers = base_address + discord_overlay::addresses::resize_buffers;

		std::printf
		(
			"[+] Discord Module => 0x%X\n"
			"[+] Discord Overlay Present => 0x%X\n"
			"[+] Discord Overlay Resize Buffers => 0x%X\n\n",

			base_address,
			present_address,
			resize_buffers
		);

		d3d11_present = *reinterpret_cast<decltype(d3d11_present)*>(present_address);
		d3d11_resize_buffers = *reinterpret_cast<decltype(d3d11_resize_buffers)*>(resize_buffers);

		*reinterpret_cast<void**>(present_address) = reinterpret_cast<void*>(&present_hook);
		*reinterpret_cast<void**>(resize_buffers) = reinterpret_cast<void*>(&resize_buffers_hook);

		std::printf("[+] Discord Overlay Hijacked!\n\n");
	}
	
	else
	{
		std::printf("[+] Discord Module Missing!\n\n");

		D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
		D3D_FEATURE_LEVEL obtained_level;

		DXGI_SWAP_CHAIN_DESC sd;
		{
			ZeroMemory(&sd, sizeof(sd));
			sd.BufferCount = 1;
			sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
			sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
			sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
			sd.OutputWindow = global_hwnd;
			sd.SampleDesc.Count = 1;
			sd.Windowed = ((GetWindowLongPtrA(global_hwnd, GWL_STYLE) & WS_POPUP) != 0) ? false : true;
			sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
			sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
			sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
			sd.BufferDesc.Width = 1;
			sd.BufferDesc.Height = 1;
			sd.BufferDesc.RefreshRate.Numerator = 0;
			sd.BufferDesc.RefreshRate.Denominator = 1;
		}

		HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, sizeof(levels) / sizeof(D3D_FEATURE_LEVEL), D3D11_SDK_VERSION, &sd, &global_swapchain, &global_device, &obtained_level, &global_context);

		std::uintptr_t* vt_swapchain;
		memcpy(&vt_swapchain, reinterpret_cast<LPVOID>(global_swapchain), sizeof(std::uintptr_t));

		DWORD old_protection;
		VirtualProtect(vt_swapchain, sizeof(std::uintptr_t), PAGE_EXECUTE_READWRITE, &old_protection);

		d3d11_present = reinterpret_cast<decltype(d3d11_present)>(vt_swapchain[8]);
		d3d11_resize_buffers = reinterpret_cast<decltype(d3d11_resize_buffers)>(vt_swapchain[13]);

		vt_swapchain[8] = reinterpret_cast<std::uintptr_t>(&present_hook);
		vt_swapchain[13] = reinterpret_cast<std::uintptr_t>(&resize_buffers_hook);

		VirtualProtect(vt_swapchain, sizeof(std::uintptr_t), old_protection, &old_protection);

		std::printf("[+] D3D11 Hooked!\n\n");
	}
}

int renderer::get_w()
{
	return client_rect.right - client_rect.left;
}

int renderer::get_h()
{
	return client_rect.bottom - client_rect.top;
}

RECT renderer::get_screen_rect()
{
	return client_rect;
}