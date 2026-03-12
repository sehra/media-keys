#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <array>
#include <utility>
#include "resource.h"
#include "UniqueHandle.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr auto APP_NAME = L"Media Keys";
constexpr auto CLASS_NAME = L"MediaKeysTrayClass";
constexpr auto REG_RUN_PATH = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

constexpr UINT WM_TRAYICON = WM_USER + 1;
constexpr UINT WM_SEND_MEDIA_KEY = WM_USER + 2;
constexpr UINT ID_TRAY_AUTOSTART = 1000;
constexpr UINT ID_TRAY_EXIT = 1001;

// Win + F1..F7 -> media / volume virtual-key codes
constexpr std::array<std::pair<DWORD, WORD>, 7> keyMappings = { {
	{ VK_F1, VK_MEDIA_PREV_TRACK },
	{ VK_F2, VK_MEDIA_PLAY_PAUSE },
	{ VK_F3, VK_MEDIA_STOP },
	{ VK_F4, VK_MEDIA_NEXT_TRACK },
	{ VK_F5, VK_VOLUME_UP },
	{ VK_F6, VK_VOLUME_DOWN },
	{ VK_F7, VK_VOLUME_MUTE },
} };

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------

struct AppState
{
	NOTIFYICONDATA nid{};
	HINSTANCE hInstance = nullptr;
	UINT wmTaskbarCreated = 0;
};

static HWND g_hwnd = nullptr;

// ---------------------------------------------------------------------------
// Registry helpers – autostart
// ---------------------------------------------------------------------------

[[nodiscard]] static bool IsAutostartEnabled() noexcept
{
	HKEY raw = nullptr;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_READ, &raw) != ERROR_SUCCESS)
		return false;
	RegKey key(raw);

	wchar_t szValue[MAX_PATH]{};
	DWORD dwSize = sizeof(szValue);
	if (RegQueryValueEx(key.get(), APP_NAME, nullptr, nullptr,
		reinterpret_cast<LPBYTE>(szValue), &dwSize) != ERROR_SUCCESS)
		return false;

	wchar_t szPath[MAX_PATH]{};
	if (GetModuleFileName(nullptr, szPath, MAX_PATH) == 0)
		return false;

	return _wcsicmp(szValue, szPath) == 0;
}

[[nodiscard]] static bool SetAutostart(bool enable) noexcept
{
	HKEY raw = nullptr;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_WRITE, &raw) != ERROR_SUCCESS)
		return false;
	RegKey key(raw);

	if (enable)
	{
		wchar_t szPath[MAX_PATH]{};
		const DWORD len = GetModuleFileName(nullptr, szPath, MAX_PATH);
		if (len == 0 || len >= MAX_PATH)
			return false;

		return RegSetValueEx(key.get(), APP_NAME, 0, REG_SZ,
			reinterpret_cast<LPBYTE>(szPath),
			static_cast<DWORD>((wcslen(szPath) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
	}

	return RegDeleteValue(key.get(), APP_NAME) == ERROR_SUCCESS;
}

// ---------------------------------------------------------------------------
// Media key simulation
// ---------------------------------------------------------------------------

static void SendMediaKey(WORD vk) noexcept
{
	std::array<INPUT, 2> inputs{};
	inputs[0].type = INPUT_KEYBOARD;
	inputs[0].ki.wVk = vk;
	inputs[1].type = INPUT_KEYBOARD;
	inputs[1].ki.wVk = vk;
	inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

// ---------------------------------------------------------------------------
// Low-level keyboard hook
// ---------------------------------------------------------------------------

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) noexcept
{
	if (nCode < 0)
		return CallNextHookEx(nullptr, nCode, wParam, lParam);

	const bool keyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
	const auto* pkb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

	if (pkb->flags & LLKHF_INJECTED)
		return CallNextHookEx(nullptr, nCode, wParam, lParam);

	const auto it = std::find_if(keyMappings.begin(), keyMappings.end(),
		[pkb](const auto& mapping) { return mapping.first == pkb->vkCode; });

	if (it != keyMappings.end())
	{
		const bool winHeld =
			(GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
			(GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

		if (keyDown && winHeld)
		{
			PostMessage(g_hwnd, WM_SEND_MEDIA_KEY, it->second, 0);
			return 1;
		}
	}

	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Window procedure & tray icon
// ---------------------------------------------------------------------------

[[nodiscard]] static AppState* GetAppState(HWND hwnd) noexcept
{
	return reinterpret_cast<AppState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_NCCREATE)
	{
		auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	auto* state = GetAppState(hwnd);
	if (!state)
		return DefWindowProc(hwnd, uMsg, wParam, lParam);

	switch (uMsg)
	{
	case WM_SEND_MEDIA_KEY:
		SendMediaKey(static_cast<WORD>(wParam));
		break;

	case WM_TRAYICON:
		if (LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == NIN_SELECT || LOWORD(lParam) == NIN_KEYSELECT)
		{
			POINT pt{ GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam) };

			Menu menu(CreatePopupMenu());

			const UINT autostartFlags = MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0u);
			AppendMenu(menu.get(), autostartFlags, ID_TRAY_AUTOSTART, L"Autostart");
			AppendMenu(menu.get(), MF_SEPARATOR, 0, nullptr);
			AppendMenu(menu.get(), MF_STRING, ID_TRAY_EXIT, L"Exit");

			SetForegroundWindow(hwnd);
			TrackPopupMenu(menu.get(), TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
		}
		break;

	case WM_COMMAND:
		if (LOWORD(wParam) == ID_TRAY_EXIT)
		{
			DestroyWindow(hwnd);
		}
		else if (LOWORD(wParam) == ID_TRAY_AUTOSTART)
		{
			const bool currentState = IsAutostartEnabled();
			(void)SetAutostart(!currentState);
		}
		break;

	case WM_DESTROY:
		Shell_NotifyIcon(NIM_DELETE, &state->nid);
		PostQuitMessage(0);
		break;

	default:
		if (uMsg == state->wmTaskbarCreated)
		{
			Shell_NotifyIcon(NIM_ADD, &state->nid);
			Shell_NotifyIcon(NIM_SETVERSION, &state->nid);
			return 0;
		}
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	return 0;
}

[[nodiscard]] static bool CreateTrayIcon(HWND hwnd, AppState& state) noexcept
{
	state.nid.cbSize = sizeof(NOTIFYICONDATA);
	state.nid.hWnd = hwnd;
	state.nid.uID = 1;
	state.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
	state.nid.uCallbackMessage = WM_TRAYICON;
	state.nid.hIcon = LoadIcon(state.hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wcsncpy_s(state.nid.szTip, _countof(state.nid.szTip), APP_NAME, _TRUNCATE);

	if (!Shell_NotifyIcon(NIM_ADD, &state.nid))
		return false;

	state.nid.uVersion = NOTIFYICON_VERSION_4;
	return Shell_NotifyIcon(NIM_SETVERSION, &state.nid);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	Handle instanceMutex(CreateMutex(nullptr, TRUE, L"MediaKeys_SingleInstance"));
	const DWORD mutexErr = GetLastError();
	if (!instanceMutex || mutexErr == ERROR_ALREADY_EXISTS)
		return EXIT_SUCCESS;

	AppState state;
	state.hInstance = hInstance;
	state.wmTaskbarCreated = RegisterWindowMessage(L"TaskbarCreated");

	WNDCLASS wc{};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;

	RegisterClass(&wc);

	g_hwnd = CreateWindowEx(0, CLASS_NAME, APP_NAME, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		nullptr, nullptr, hInstance, &state);

	if (g_hwnd == nullptr)
		return EXIT_FAILURE;

	if (!CreateTrayIcon(g_hwnd, state))
		return EXIT_FAILURE;

	Hook keyboardHook(SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0));

	if (!keyboardHook)
		return EXIT_FAILURE;

	MSG msg{};

	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	UnregisterClass(CLASS_NAME, hInstance);

	return EXIT_SUCCESS;
}
