#pragma once

#include <windows.h>
#include <memory>

struct RegKeyDeleter {
	using pointer = HKEY;
	void operator()(HKEY h) const noexcept { RegCloseKey(h); }
};
using RegKey = std::unique_ptr<HKEY, RegKeyDeleter>;

struct HookDeleter {
	using pointer = HHOOK;
	void operator()(HHOOK h) const noexcept { UnhookWindowsHookEx(h); }
};
using Hook = std::unique_ptr<HHOOK, HookDeleter>;

struct MenuDeleter {
	using pointer = HMENU;
	void operator()(HMENU h) const noexcept { DestroyMenu(h); }
};
using Menu = std::unique_ptr<HMENU, MenuDeleter>;

struct HandleDeleter {
	using pointer = HANDLE;
	void operator()(HANDLE h) const noexcept {
		if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
	}
};
using Handle = std::unique_ptr<HANDLE, HandleDeleter>;
