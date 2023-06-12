#include "stdafx.h"
#include "stdio.h"
#define WIN32_LEAN_AND_MEAN
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#include <windows.h>
#include "..\includes\injector\injector.hpp"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <d3d9.h>

#define GAME_HWND_ADDR 0x00982BF4
#define GAME_D3DDEVICE_ADDR 0x00982BDC
#define WNDPROC_POINTER_ADDR 0x6E6AF1

#define CFENG_INSTANCE_ADDR 0x0091CADC

#define POPUP_MODAL_HEADER "Manual IP Connection"

static LPDIRECT3DDEVICE9        g_pd3dDevice = NULL;
bool bInitedImgui = false;
bool bBlockedGameInput = false;
// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

uintptr_t GameInitFuncAddr;
uintptr_t GameLoopFuncAddr;
uintptr_t GameLoopPostRenderFuncAddr;
uintptr_t RenderLoopFuncAddr;
uintptr_t RenderResetFuncAddr;

bool bShowIPInputWindow = false;

// needed because MW reports a tiny viewport for some reason
void(__stdcall* GetCurrentRes)(unsigned int* OutWidth, unsigned int* OutHeight) = (void(__stdcall*)(unsigned int*, unsigned int*))0x6C27D0;
#pragma runtime_checks("", off)

void InitiateConnection()
{
	uintptr_t FEngInstance = *(uintptr_t*)0x0091CADC;
	uintptr_t FEDatabase = *(uintptr_t*)0x0091CF90;

	if (!FEngInstance) return;
	if (!FEDatabase) return;

	*(uint32_t*)(FEDatabase + 0x12C) |= 1 << 6; // to indicate it's in LAN mode
	*(uint8_t*)(FEDatabase + 0x172) = 0; // to indicate we're not the server

	return reinterpret_cast<void(__thiscall*)(uintptr_t, const char*, int32_t, uint32_t, bool)>(0x005257F0)(FEngInstance, "UI_PC_LAN.fng", 0, 0, false); // QueuePackagePush
}

void DrawIPInputWindow()
{
	if (ImGui::BeginPopupModal(POPUP_MODAL_HEADER, &bShowIPInputWindow, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::InputText("IP Address", (char*)0x008F42EC, 32);
		ImGui::InputInt("Port", (int*)0x008F430C);
		*(int*)0x008F430C &= 0xFFFF;

		if (ImGui::Button("Connect"))
		{
			bShowIPInputWindow = false;
			InitiateConnection();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			bShowIPInputWindow = false;
		}

		ImGui::EndPopup();
	}
}

void ShowWindows() 
{
	if (bShowIPInputWindow)
	{
		if (!ImGui::IsPopupOpen(POPUP_MODAL_HEADER))
			ImGui::OpenPopup(POPUP_MODAL_HEADER);
		DrawIPInputWindow();
	}
}

uintptr_t PCLAN_NotificationMessage = 0x560640;
void __stdcall PCLAN_NotificationMessageHook(uint32_t msg, void* FEObject, uint32_t unk1, uint32_t unk2)
{
	uintptr_t that;
	_asm mov that, ecx

	if (msg == 0xC98356BA)
	{
		reinterpret_cast<void(__thiscall*)(uintptr_t, uint32_t, void*, uint32_t, uint32_t)>(PCLAN_NotificationMessage)(that, msg, FEObject, unk1, unk2);
	
		if (GetAsyncKeyState(0x32) >> 15) // press 2 on keyboard to invoke the menu while in the server browser
		{
			if (!bShowIPInputWindow)
				bShowIPInputWindow = true;
		}
		return;
	}
	return reinterpret_cast<void(__thiscall*)(uintptr_t, uint32_t, void*, uint32_t, uint32_t)>(PCLAN_NotificationMessage)(that, msg, FEObject, unk1, unk2);
}

unsigned int GameWndProcAddr = 0;
LRESULT(WINAPI* GameWndProc)(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI ImguiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		// ignore the following messages because it conflicts with the game & XtendedInput
	case WM_SETCURSOR:
	case WM_MOUSEWHEEL:
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
	case WM_MBUTTONUP:
	case WM_XBUTTONUP:
	case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
	case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
	case WM_MOUSEMOVE:
		return GameWndProc(hWnd, msg, wParam, lParam);;
	}

	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return TRUE;

	return GameWndProc(hWnd, msg, wParam, lParam);
}

void ImguiUpdate()
{
	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void ImguiReset()
{
	if (bInitedImgui)
		ImGui_ImplDX9_InvalidateDeviceObjects();

	if (bInitedImgui)
		ImGui_ImplDX9_CreateDeviceObjects();
}

void ImguiRenderFrame()
{
	if (bInitedImgui)
	{
		ImGui::EndFrame();
		ImGui::Render();
		ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
	}
}

void InitImgui()
{
	// Setup Dear ImGui context
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	io.IniFilename = NULL;
	io.LogFilename = NULL;
	ImFontConfig config;
	config.PixelSnapH = true;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer backends
	g_pd3dDevice = (IDirect3DDevice9*)(*(int*)GAME_D3DDEVICE_ADDR);
	ImGui_ImplWin32_Init((HWND)(*(int*)GAME_HWND_ADDR));
	ImGui_ImplDX9_Init(g_pd3dDevice);

	bInitedImgui = true;
}

namespace InputBlocker
{
	HMODULE mhXtendedInput;
	bool(__cdecl* XtendedInputSetPollingState)(bool state);
	bool bLookedForXInput = false;
	bool bFoundXInput = false;
	
	void LookForXtendedInput()
	{
		if (!mhXtendedInput)
		{
			mhXtendedInput = GetModuleHandleA("NFS_XtendedInput.asi");
			if (mhXtendedInput)
			{
				XtendedInputSetPollingState = reinterpret_cast<bool(__cdecl*)(bool)>(GetProcAddress(mhXtendedInput, "SetPollingState"));
				bFoundXInput = XtendedInputSetPollingState != nullptr;
			}
		}
	}

	uintptr_t ptrGameDevicePoll = 0x6349B0;
	void __stdcall GameDevice_PollDevice_Hook()
	{
		uintptr_t that;
		_asm mov that, ecx

		if (bBlockedGameInput)
			return;

		return reinterpret_cast<void(__thiscall*)(uintptr_t)>(ptrGameDevicePoll)(that);
	}
}


void GameInit()
{
	InitImgui();

	return reinterpret_cast<void(*)()>(GameInitFuncAddr)();
}

void GameLoop()
{
	return reinterpret_cast<void(*)()>(GameLoopFuncAddr)();
}

void GameLoopPostRender()
{
	ImGuiIO& io = ImGui::GetIO();

	HWND focused_window = ::GetForegroundWindow();
	const bool is_app_focused = (focused_window == *(HWND*)GAME_HWND_ADDR);
	if (is_app_focused)
	{
		MSG inMsg;
		PeekMessageA(&inMsg, *(HWND*)GAME_HWND_ADDR, WM_MOUSEFIRST, WM_MOUSELAST, PM_NOREMOVE);
		switch (inMsg.message)
		{
			// re-read the messages later because imgui needs info
		case WM_MOUSEWHEEL:
		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		case WM_MBUTTONUP:
		case WM_XBUTTONUP:
		case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
		case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
		case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
			ImGui_ImplWin32_WndProcHandler(*(HWND*)GAME_HWND_ADDR, inMsg.message, inMsg.wParam, inMsg.lParam);
		}

		if (!InputBlocker::bLookedForXInput)
		{
			InputBlocker::LookForXtendedInput();
			InputBlocker::bLookedForXInput = true;
		}

		if (InputBlocker::bFoundXInput)
		{
			if (io.WantCaptureKeyboard || io.WantCaptureMouse)
				InputBlocker::XtendedInputSetPollingState(false);
			else
				InputBlocker::XtendedInputSetPollingState(true);
		}
		else
		{
			if (io.WantCaptureKeyboard || io.WantCaptureMouse)
				bBlockedGameInput = true;
			else
				bBlockedGameInput = false;
		}
	}
	else
	{
		if (InputBlocker::bFoundXInput)
		{
			InputBlocker::XtendedInputSetPollingState(false);
		}
		else
		{
			bBlockedGameInput = true;
		}
	}

	return reinterpret_cast<void(*)()>(GameLoopPostRenderFuncAddr)();
}

void ImguiSetCursorPos(int X, int Y)
{
	ImGuiIO& io = ImGui::GetIO();
	io.WantSetMousePos = true;
	if (!InputBlocker::bFoundXInput)
	{
		uint32_t resX, resY;
		GetCurrentRes(&resX, &resY);

		float ratio = resY / 480.0f;
		io.AddMousePosEvent(static_cast<float>(X) * ratio, static_cast<float>(Y) * ratio);
	}
	else
	{
		POINT pos;
		if (::GetCursorPos(&pos) && ::ScreenToClient(*(HWND*)GAME_HWND_ADDR, &pos))
			io.AddMousePosEvent((float)pos.x, (float)pos.y);
	}
}

void __cdecl RenderLoop(int X, int Y)
{
	if (bInitedImgui)
	{
		ImguiSetCursorPos(X, Y);
		ImguiUpdate();
		ShowWindows();
		ImguiRenderFrame();
	}
	return reinterpret_cast<void(__cdecl*)(int, int)>(RenderLoopFuncAddr)(X, Y);
}


void RenderReset()
{
	ImguiReset();

	return reinterpret_cast<void(*)()>(RenderResetFuncAddr)();
}

char* ServerIP = (char*)0x8F42EC;
char* ReturnServerIP()
{
	return ServerIP;
}

#pragma runtime_checks("", restore)
int Init()
{
	GameInitFuncAddr = (uintptr_t)injector::MakeCALL(0x00666146, GameInit).get_raw<void>();
	//GameLoopFuncAddr = (uintptr_t)injector::MakeCALL(0x00663EAE, GameLoop).get_raw<void>();
	GameLoopPostRenderFuncAddr = (uintptr_t)injector::MakeJMP(0x006DF8E5, GameLoopPostRender).get_raw<void>();
	RenderLoopFuncAddr = (uintptr_t)injector::MakeCALL(0x00516F93, RenderLoop).get_raw<void>();
	RenderResetFuncAddr = (uintptr_t)injector::MakeCALL(0x006DB265, RenderReset).get_raw<void>();
	InputBlocker::ptrGameDevicePoll = *(uintptr_t*)0x008A7BAC;
	injector::WriteMemory<uintptr_t>(0x008A7BAC, (uintptr_t)InputBlocker::GameDevice_PollDevice_Hook, true);

	PCLAN_NotificationMessage = *(uintptr_t*)0x0089E39C;
	injector::WriteMemory<uintptr_t>(0x0089E39C, (uintptr_t)PCLAN_NotificationMessageHook, true);

	GameWndProcAddr = *(unsigned int*)WNDPROC_POINTER_ADDR;
	GameWndProc = (LRESULT(WINAPI*)(HWND, UINT, WPARAM, LPARAM))GameWndProcAddr;
	injector::WriteMemory<unsigned int>(WNDPROC_POINTER_ADDR, (unsigned int)&ImguiWndProc, true);

	// hook to ignore the redirected slave server IP - needed to connect via internet
	injector::MakeCALL(0x00841665, ReturnServerIP);
	injector::MakeCALL(0x00841DD5, ReturnServerIP);

	return 0;
}


BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD reason, LPVOID /*lpReserved*/)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		Init();
	}
	return TRUE;
}

