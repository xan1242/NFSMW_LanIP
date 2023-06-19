#include "stdafx.h"
#include "stdio.h"
#define WIN32_LEAN_AND_MEAN
#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#include <windows.h>
#include "injector\injector.hpp"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "WndProcWalker.hpp"
#include <d3d9.h>
#include <WinSock2.h>
#include <wininet.h>
#include "upnpnat/upnpnat.h"
#include "Network.h"
#include <thread>

#pragma comment(lib, "wininet.lib")

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
bool bIPWindowOnceFlag = false;

uint32_t LastPortMapAddr = 0;
bool bUseUPnP = false;
bool bOnlineGame = false;

uint32_t external_addr = 0;
char external_addr_str[128];
constexpr const char* ipcheckdomain = "http://myexternalip.com/raw";
bool bCurrentlyCatchingIP = false;
bool bLocalChallengeActive = false;

// needed because MW reports a tiny viewport for some reason
void(__stdcall* GetCurrentRes)(unsigned int* OutWidth, unsigned int* OutHeight) = (void(__stdcall*)(unsigned int*, unsigned int*))0x6C27D0;
#pragma runtime_checks("", off)
// a local challenge client-side server through which the lobby server will determine if the user is locally available through port 9901
// this should never respond with "YESIMLOCAL" via an online conn! only via VPN and LAN it should give such a response!
void LocalChallengeServer()
{
	bLocalChallengeActive = true;
	//printf("Starting challenge!\n");

	double TIME_Frequency = 0.0;
	double startTime = 0.0;
	constexpr DWORD ChallengeTimeOut = 10000; // 10sec timeout
	try
	{
		UDPSocket Socket;
		uint32_t query = 0;
		uint32_t response = 0;

		Socket.Bind(9901);
		Socket.SetTimeout(ChallengeTimeOut);

		while (bLocalChallengeActive)
		{
			sockaddr_in add = Socket.RecvFrom((char*)&query, sizeof(uint32_t));
			add.sin_family = AF_INET; // force it due to Win11 forcing IPv6...

			if (query == 0x6A093EC9) // strhash of "LOCAL?" 6A093EC9
			{
				response = 0x8DB682D1; // strhash of "YESIMLOCAL" 8DB682D1
				Socket.SendTo(add, (char*)&response, sizeof(uint32_t));
				bLocalChallengeActive = false;
			}
			else
				bLocalChallengeActive = false;
		}
	}
	catch (std::system_error& e)
	{
		bLocalChallengeActive = false;
		//std::cout << e.what() << '\n';
	}
}

bool QueryExternalIP() 
{
	bCurrentlyCatchingIP = true;
	HINTERNET net = InternetOpenA("IP retriever",
		INTERNET_OPEN_TYPE_PRECONFIG,
		NULL,
		NULL,
		0);

	if (!net)
	{
		strcpy(external_addr_str, "Failed to open an internet connection.");
		bCurrentlyCatchingIP = false;
		return false;
	}

	HINTERNET conn = InternetOpenUrlA(net,
		ipcheckdomain,
		NULL,
		0,
		INTERNET_FLAG_RELOAD,
		0);

	if (!conn)
	{
		sprintf(external_addr_str, "Failed to open: %s\n", ipcheckdomain);
		bCurrentlyCatchingIP = false;
		return false;
	}

	char buffer[4096];
	DWORD read;
	BOOL status;

	status = InternetReadFile(conn, buffer, sizeof(buffer) / sizeof(buffer[0]), &read);
	if (!status)
	{
		sprintf(external_addr_str, "Failed to read: %s\n", ipcheckdomain);
		bCurrentlyCatchingIP = false;
		return false;
	}
	InternetCloseHandle(net);

	uint8_t p1;
	uint8_t p2;
	uint8_t p3;
	uint8_t p4;

	sscanf(buffer, "%hhu.%hhu.%hhu.%hhu", &p1, &p2, &p3, &p4);
	sprintf(external_addr_str, "%hhu.%hhu.%hhu.%hhu", p1, p2, p3, p4);

	external_addr = p1 << 24 | p2 << 16 | p3 << 8 | p4;


	bCurrentlyCatchingIP = false;
	return true;
}

bool __stdcall InitConnHook(char* host, uint16_t port, uint32_t timeout)
{
	uintptr_t that;
	_asm mov that, ecx

	if (!bLocalChallengeActive && bOnlineGame)
		std::thread(LocalChallengeServer).detach();

	return reinterpret_cast<bool(__thiscall*)(uintptr_t, char*, uint16_t, uint32_t)>(0x00792BF0)(that, host, port, timeout);
}

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

static void HelpMarker(const char* desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && ImGui::BeginTooltip())
	{
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

void DrawIPInputWindow()
{
	if (ImGui::BeginPopupModal(POPUP_MODAL_HEADER, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
	{
		if (!bIPWindowOnceFlag)
		{
			if (!external_addr && !bCurrentlyCatchingIP)
				std::thread(QueryExternalIP).detach();
			bUseUPnP = true;
			bOnlineGame = true;
			ImGui::SetKeyboardFocusHere();
			bIPWindowOnceFlag = true;
		}

		ImGui::Text("WAN IP: %s", external_addr_str);
		ImGui::SameLine(); HelpMarker("Your external IP address.\nNOTE: If this fails, do not panic. This is only critical for users local to the lobby server who want to play with online players!");
		ImGui::InputText("IP Address", (char*)0x008F42EC, 32);
		ImGui::InputInt("Port", (int*)0x008F430C);
		*(int*)0x008F430C &= 0xFFFF;

		ImGui::Checkbox("Use UPnP for host port", &bUseUPnP);
		ImGui::SameLine(); HelpMarker("Opens a port mapping via UPnP on UDP port 3658.");
		ImGui::Checkbox("Online", &bOnlineGame);
		ImGui::SameLine(); HelpMarker("Sends the server your external IP address.\nEnable this if you want to play online (even if the lobby server is in your LAN).");
		//ImGui::Checkbox("Local lobby", &bLocalConn);
		//ImGui::SameLine(); HelpMarker("Enable this if the lobby server is on the same network as the game and you want to play online.\nThis will dispatch the server to report its external IP to online players in the lobby instead of your local one.");

		if (ImGui::Button("Connect (Return)", ImVec2(ImGui::GetContentRegionAvail().x / 2.0f, 0)) || (GetAsyncKeyState(VK_RETURN) >> 15) && WndProcWalker::IsWindowActive())
		{
			ImGuiIO& io = ImGui::GetIO(); (void)io;
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

			bShowIPInputWindow = false;
			bIPWindowOnceFlag = false;
			InitiateConnection();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel (ESC)", ImVec2(ImGui::GetContentRegionAvail().x, 0)) || (GetAsyncKeyState(VK_ESCAPE) >> 15) && WndProcWalker::IsWindowActive()) // slightly buggy, can cause the menu to show in main menu if you're fast enough
		{
			ImGuiIO& io = ImGui::GetIO(); (void)io;
			io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
			bShowIPInputWindow = false;
			bIPWindowOnceFlag = false;
			bUseUPnP = false;
			bOnlineGame = false;
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
			{
				ImGuiIO& io = ImGui::GetIO(); (void)io;
				io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
				bShowIPInputWindow = true;
			}
		}
		return;
	}
	return reinterpret_cast<void(__thiscall*)(uintptr_t, uint32_t, void*, uint32_t, uint32_t)>(PCLAN_NotificationMessage)(that, msg, FEObject, unk1, unk2);
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

LRESULT WINAPI ImguiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
	return FALSE;
}

void ImguiUpdate()
{
	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void ImguiLost()
{
	if (bInitedImgui)
		ImGui_ImplDX9_InvalidateDeviceObjects();
}

void ImguiReset()
{
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

void GameInit()
{
	WndProcWalker::Init();
	WndProcWalker::AddWndProc(ImguiWndProc);

	InitImgui();

	if (!InputBlocker::bLookedForXInput)
	{
		InputBlocker::LookForXtendedInput();
		InputBlocker::bLookedForXInput = true;
	}

	return reinterpret_cast<void(*)()>(GameInitFuncAddr)();
}

void GameLoop()
{
	return reinterpret_cast<void(*)()>(GameLoopFuncAddr)();
}

void GameLoopPostRender()
{
	ImGuiIO& io = ImGui::GetIO();

	if (InputBlocker::bFoundXInput)
	{
		if (io.WantCaptureKeyboard || io.WantCaptureMouse || !WndProcWalker::IsWindowActive())
			InputBlocker::XtendedInputSetPollingState(false);
		else
			InputBlocker::XtendedInputSetPollingState(true);
	}
	else
	{
		if (io.WantCaptureKeyboard || io.WantCaptureMouse || !WndProcWalker::IsWindowActive())
			bBlockedGameInput = true;
		else
			bBlockedGameInput = false;
	}

	return reinterpret_cast<void(*)()>(GameLoopPostRenderFuncAddr)();
}

void __cdecl RenderLoop(int X, int Y)
{
	reinterpret_cast<void(__cdecl*)(int, int)>(RenderLoopFuncAddr)(X, Y);

	if (bInitedImgui)
	{
		ImguiUpdate();
		ShowWindows();
		ImguiRenderFrame();
	}
	return;
}


void RenderReset()
{
	ImguiLost();

	reinterpret_cast<void(*)()>(RenderResetFuncAddr)();

	ImguiReset();
}

char* ServerIP = (char*)0x8F42EC;
char* ReturnServerIP()
{
	return ServerIP;
}

// Network patches

uintptr_t GetNetworkLongAddr = 0x8519E0;
uint32_t CatchInterfaceAddr(uint32_t a0, uint32_t a1)
{
	uint32_t addr = reinterpret_cast<uint32_t(*)(uint32_t, uint32_t)>(GetNetworkLongAddr)(a0, a1);

	if (addr && (addr != 0x7F000001) && bUseUPnP)
	{
		if (LastPortMapAddr != addr)
		{
			UPNPNAT nat;

			nat.init(5, 10);

			if (!nat.discovery())
				return addr;

			char addr_str[32];
			sprintf(addr_str, "%u.%u.%u.%u", addr >> 24 & 0xFF, addr >> 16 & 0xFF, addr >> 8 & 0xFF, addr & 0xFF);

			if (nat.add_port_mapping("NFSMW", addr_str, 3658, 3658, "UDP", 86400))
				LastPortMapAddr = addr;
		}
	}

	return addr;
}

uintptr_t SocketFunc1 = 0x84AE70;
uintptr_t SocketPatch1(uint32_t af, uint32_t type, uint32_t protocol)
{
	return reinterpret_cast<uintptr_t(*)(uint32_t, uint32_t, uint32_t)>(SocketFunc1)(af, type, 17);
}

const char* ReturnSKUTag()
{
	if (external_addr)
		return external_addr_str;

	return "SKU_LANIP";
}

#pragma runtime_checks("", restore)
int Init()
{
	GameInitFuncAddr = (uintptr_t)injector::MakeCALL(0x00666146, GameInit).get_raw<void>();
	//GameLoopFuncAddr = (uintptr_t)injector::MakeCALL(0x00663EAE, GameLoop).get_raw<void>();
	GameLoopPostRenderFuncAddr = (uintptr_t)injector::MakeJMP(0x006DF8E5, GameLoopPostRender).get_raw<void>();
	RenderLoopFuncAddr = (uintptr_t)injector::MakeCALL(0x00516F93, RenderLoop).get_raw<void>();
	RenderResetFuncAddr = (uintptr_t)injector::MakeCALL(0x6DB302, RenderReset).get_raw<void>();
	injector::MakeCALL(0x6DE185, RenderReset);
	injector::MakeCALL(0x6E72C1, RenderReset);
	injector::MakeCALL(0x6E753E, RenderReset);
	injector::MakeCALL(0x6E7606, RenderReset);

	InputBlocker::ptrGameDevicePoll = *(uintptr_t*)0x008A7BAC;
	injector::WriteMemory<uintptr_t>(0x008A7BAC, (uintptr_t)InputBlocker::GameDevice_PollDevice_Hook, true);

	PCLAN_NotificationMessage = *(uintptr_t*)0x0089E39C;
	injector::WriteMemory<uintptr_t>(0x0089E39C, (uintptr_t)PCLAN_NotificationMessageHook, true);

	// hook to ignore the redirected slave server IP - needed to connect via internet
	injector::MakeCALL(0x00841665, ReturnServerIP);
	injector::MakeCALL(0x00841DD5, ReturnServerIP);

	GetNetworkLongAddr = (uintptr_t)injector::MakeCALL(0x841641, CatchInterfaceAddr).get_raw<void>();
	SocketFunc1 = (uintptr_t)injector::MakeCALL(0x00851482, SocketPatch1).get_raw<void>();
	injector::MakeCALL(0x0086408A, SocketPatch1);

	// nuke the demangler because it's a security risk (because the domain is still working for some ungodly reason) and because online doesn't work
	// TODO - remove / workaround this if the online mode ever gets revived
	injector::WriteMemory<uint32_t>(0x008C7ADC, 0, true);

	// skip UDP bind of client socket
	injector::MakeJMP(0x00851661, 0x8516CA);

	injector::MakeCALL(0x00794A52, ReturnSKUTag);

	injector::MakeCALL(0x00553AB5, InitConnHook);
	injector::MakeCALL(0x00553AFA, InitConnHook);

	strcpy(external_addr_str, "Querying IP...");

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

