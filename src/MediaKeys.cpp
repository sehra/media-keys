#include <windows.h>
#include <shellapi.h>
#include <array>
#include <utility>
#include "resource.h"

constexpr UINT WM_TRAYICON = WM_USER + 1;
constexpr UINT ID_TRAY_AUTOSTART = 1000;
constexpr UINT ID_TRAY_EXIT = 1001;
constexpr auto CLASS_NAME = L"MediaKeysTrayClass";
constexpr auto REG_RUN_PATH = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto APP_NAME = L"Media Keys";

struct RegKey
{
	HKEY hKey = nullptr;

	RegKey() = default;
	RegKey(const RegKey&) = delete;
	RegKey& operator=(const RegKey&) = delete;
	~RegKey() { if (hKey) RegCloseKey(hKey); }

	HKEY* put() noexcept { return &hKey; }
	explicit operator bool() const noexcept { return hKey != nullptr; }
};

constexpr std::array<std::pair<DWORD, WORD>, 7> g_keyMappings = {{
	{ VK_F1, VK_MEDIA_PREV_TRACK },
	{ VK_F2, VK_MEDIA_PLAY_PAUSE },
	{ VK_F3, VK_MEDIA_STOP },
	{ VK_F4, VK_MEDIA_NEXT_TRACK },
	{ VK_F5, VK_VOLUME_UP },
	{ VK_F6, VK_VOLUME_DOWN },
	{ VK_F7, VK_VOLUME_MUTE },
}};

NOTIFYICONDATA g_nid{};
HINSTANCE g_hInstance = nullptr;
HHOOK g_hKeyboardHook = nullptr;

static bool IsAutostartEnabled()
{
	RegKey key;

	if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_READ, key.put()) != ERROR_SUCCESS)
		return false;

	wchar_t szValue[MAX_PATH]{};
	DWORD dwSize = sizeof(szValue);
	return RegQueryValueEx(key.hKey, APP_NAME, nullptr, nullptr,
		reinterpret_cast<LPBYTE>(szValue), &dwSize) == ERROR_SUCCESS;
}

static bool SetAutostart(bool enable)
{
	RegKey key;

	if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_WRITE, key.put()) != ERROR_SUCCESS)
		return false;

	if (enable)
	{
		wchar_t szPath[MAX_PATH]{};
		GetModuleFileName(nullptr, szPath, MAX_PATH);

		return RegSetValueEx(key.hKey, APP_NAME, 0, REG_SZ,
			reinterpret_cast<LPBYTE>(szPath),
			static_cast<DWORD>((wcslen(szPath) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
	}

	return RegDeleteValue(key.hKey, APP_NAME) == ERROR_SUCCESS;
}

static void SendMediaKey(WORD vk)
{
	INPUT input{};
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = vk;
	SendInput(1, &input, sizeof(INPUT));

	input.ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(1, &input, sizeof(INPUT));
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION)
	{
		auto* pkb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
		bool winDown = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);

		if (winDown)
		{
			for (const auto& [trigger, media] : g_keyMappings)
			{
				if (pkb->vkCode == trigger)
				{
					if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
						SendMediaKey(media);
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
			POINT pt{};
			GetCursorPos(&pt);

			HMENU hMenu = CreatePopupMenu();

			UINT autostartFlags = MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0u);
			AppendMenu(hMenu, autostartFlags, ID_TRAY_AUTOSTART, L"Autostart");
			AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
			AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

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
		else if (LOWORD(wParam) == ID_TRAY_AUTOSTART)
		{
			bool currentState = IsAutostartEnabled();
			SetAutostart(!currentState);
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
	wcscpy_s(g_nid.szTip, APP_NAME);

	return Shell_NotifyIcon(NIM_ADD, &g_nid);
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	g_hInstance = hInstance;

	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;

	RegisterClass(&wc);

	HWND hwnd = CreateWindowEx(0, CLASS_NAME, APP_NAME, WS_OVERLAPPEDWINDOW,
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

	MSG msg{};

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
