#include <windows.h>
#include <shellapi.h>
#include <tchar.h>
#include "resource.h"

#define WM_TRAYICON (WM_USER + 1)
constexpr auto ID_TRAY_EXIT = 1001;
constexpr auto CLASS_NAME = _T("MediaKeysTrayClass");

NOTIFYICONDATA g_nid = {};
HINSTANCE g_hInstance = nullptr;
HHOOK g_hKeyboardHook = nullptr;

static void SendMediaKey(WORD key) {
	INPUT input = { 0 };
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = key;
	SendInput(1, &input, sizeof(INPUT));

	input.ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(1, &input, sizeof(INPUT));
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION)
	{
		KBDLLHOOKSTRUCT* pkb = (KBDLLHOOKSTRUCT*)lParam;
		bool winDown = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);

		if (winDown)
		{
			if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
			{
				switch (pkb->vkCode)
				{
				case VK_F1:
					SendMediaKey(VK_MEDIA_PREV_TRACK);
					return 1;
				case VK_F2:
					SendMediaKey(VK_MEDIA_PLAY_PAUSE);
					return 1;
				case VK_F3:
					SendMediaKey(VK_MEDIA_STOP);
					return 1;
				case VK_F4:
					SendMediaKey(VK_MEDIA_NEXT_TRACK);
					return 1;
				case VK_F5:
					SendMediaKey(VK_VOLUME_UP);
					return 1;
				case VK_F6:
					SendMediaKey(VK_VOLUME_DOWN);
					return 1;
				case VK_F7:
					SendMediaKey(VK_VOLUME_MUTE);
					return 1;
				}
			}
			else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
			{
				switch (pkb->vkCode)
				{
				case VK_F1:
				case VK_F2:
				case VK_F3:
				case VK_F4:
				case VK_F5:
				case VK_F6:
				case VK_F7:
					return 1;
				}
			}
		}
	}

	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_TRAYICON:
		if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP)
		{
			POINT pt;
			GetCursorPos(&pt);

			HMENU hMenu = CreatePopupMenu();
			AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, _T("Exit"));

			SetForegroundWindow(hwnd);
			TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
			DestroyMenu(hMenu);
		}
		break;

	case WM_COMMAND:
		if (LOWORD(wParam) == ID_TRAY_EXIT)
		{
			PostQuitMessage(0);
		}
		break;

	case WM_DESTROY:
		Shell_NotifyIcon(NIM_DELETE, &g_nid);
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	return 0;
}

static bool CreateTrayIcon(HWND hwnd)
{
	g_nid.cbSize = sizeof(NOTIFYICONDATA);
	g_nid.hWnd = hwnd;
	g_nid.uID = 1;
	g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	g_nid.uCallbackMessage = WM_TRAYICON;
	g_nid.hIcon = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_ICON1));
	lstrcpy(g_nid.szTip, _T("Media Keys"));

	return Shell_NotifyIcon(NIM_ADD, &g_nid);
}

static int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	g_hInstance = hInstance;

	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;

	RegisterClass(&wc);

	HWND hwnd = CreateWindowEx(0, CLASS_NAME, _T("Media Keys"), WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		nullptr, nullptr, hInstance, nullptr);

	if (hwnd == nullptr)
	{
		return 1;
	}

	if (!CreateTrayIcon(hwnd))
	{
		return 1;
	}

	g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);

	if (!g_hKeyboardHook)
	{
		return 1;
	}

	MSG msg = {};

	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	if (g_hKeyboardHook)
	{
		UnhookWindowsHookEx(g_hKeyboardHook);
		g_hKeyboardHook = nullptr;
	}

	return 0;
}
