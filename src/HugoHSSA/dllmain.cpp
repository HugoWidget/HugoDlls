/*
 * Copyright 2025-2026 howdy213, JYardX
 *
 * This file is part of HugoUtils.
 *
 * HugoUtils is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * HugoUtils is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with HugoUtils. If not, see <https://www.gnu.org/licenses/>.
 */
#include "pch.h"
#include <windows.h>
#include <string>
#include <memory>
#include "MinHook/MinHook.h"
#include "WinUtils/WinUtils.h"
#include "WinUtils/Logger.h"
#include "HugoUtils/HugoLock.h"

#pragma comment(lib, "libMinHook.x86.lib")
#pragma comment(lib, "libMinHook.x64.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

static HMODULE g_hThisDll = nullptr;
static std::unique_ptr<SharedFlag> g_sharedFlag = nullptr;

typedef BOOL(WINAPI* pSetWindowPos)(HWND, HWND, int, int, int, int, UINT);
typedef HHOOK(WINAPI* pSetWindowsHookExW)(int, HOOKPROC, HINSTANCE, DWORD);
typedef HHOOK(WINAPI* pSetWindowsHookExA)(int, HOOKPROC, HINSTANCE, DWORD);
typedef BOOL(WINAPI* pFreeLibrary)(HMODULE);
typedef BOOL(WINAPI* pShowWindow)(HWND, int);
typedef BOOL(WINAPI* pBringWindowToTop)(HWND);
typedef BOOL(WINAPI* pSetForegroundWindow)(HWND);
typedef HWND(WINAPI* pSetActiveWindow)(HWND);

static pSetWindowPos Original_SetWindowPos = nullptr;
static pSetWindowsHookExW Original_SetWindowsHookExW = nullptr;
static pSetWindowsHookExA Original_SetWindowsHookExA = nullptr;
static pFreeLibrary Original_FreeLibrary = nullptr;
static pShowWindow Original_ShowWindow = nullptr;
static pBringWindowToTop Original_BringWindowToTop = nullptr;
static pSetForegroundWindow Original_SetForegroundWindow = nullptr;
static pSetActiveWindow Original_SetActiveWindow = nullptr;

bool ShouldIntercept(HWND hWnd) {
	if (!g_sharedFlag || !g_sharedFlag->Valid()) return false;
	if (g_sharedFlag->Get() == FALSE) return false;
	return WinUtils::IsWindowFullScreen(hWnd, 100, true);
}

bool ShouldIntercept(int x, int y, int cx, int cy) {
	if (!g_sharedFlag || !g_sharedFlag->Valid()) return false;
	if (g_sharedFlag->Get() == FALSE) return false;
	return WinUtils::IsWindowFullScreen(x, y, cx, cy, 100, true);
}

void DoIntercept(HWND hWnd) {
	if (Original_SetWindowPos) {
		Original_SetWindowPos(hWnd, HWND_BOTTOM, 0, 0, 0, 0,
			SWP_HIDEWINDOW | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
	}
	LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
	SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
	SetLayeredWindowAttributes(hWnd, 0, 0, LWA_ALPHA);
}

BOOL WINAPI Hooked_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
	if (ShouldIntercept(hWnd) || ShouldIntercept(X, Y, cx, cy)) {
		DoIntercept(hWnd);
		return TRUE;
	}
	return Original_SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

BOOL WINAPI Hooked_ShowWindow(HWND hWnd, int nCmdShow) {
	if (ShouldIntercept(hWnd)) {
		DoIntercept(hWnd);
		return TRUE;
	}
	return Original_ShowWindow(hWnd, nCmdShow);
}

BOOL WINAPI Hooked_BringWindowToTop(HWND hWnd) {
	if (ShouldIntercept(hWnd)) {
		DoIntercept(hWnd);
		return TRUE;
	}
	return Original_BringWindowToTop(hWnd);
}

BOOL WINAPI Hooked_SetForegroundWindow(HWND hWnd) {
	if (ShouldIntercept(hWnd)) {
		DoIntercept(hWnd);
		return TRUE;
	}
	return Original_SetForegroundWindow(hWnd);
}

HWND WINAPI Hooked_SetActiveWindow(HWND hWnd) {
	if (ShouldIntercept(hWnd)) {
		DoIntercept(hWnd);
		return NULL;
	}
	return Original_SetActiveWindow(hWnd);
}

HHOOK WINAPI Hooked_SetWindowsHookExW(int idHook, HOOKPROC lpfn, HINSTANCE hMod, DWORD dwThreadId) {
	if (idHook == WH_KEYBOARD_LL) {
		return NULL;
	}
	return Original_SetWindowsHookExW(idHook, lpfn, hMod, dwThreadId);
}

HHOOK WINAPI Hooked_SetWindowsHookExA(int idHook, HOOKPROC lpfn, HINSTANCE hMod, DWORD dwThreadId) {
	if (idHook == WH_KEYBOARD_LL) {
		return NULL;
	}
	return Original_SetWindowsHookExA(idHook, lpfn, hMod, dwThreadId);
}

BOOL WINAPI Hooked_FreeLibrary(HMODULE hModule) {
	if (hModule == g_hThisDll) return TRUE;
	return Original_FreeLibrary(hModule);
}

BOOL InitializeHooks() {
	if (MH_Initialize() != MH_OK) return FALSE;

	MH_CreateHook(&FreeLibrary, &Hooked_FreeLibrary, reinterpret_cast<void**>(&Original_FreeLibrary));
	MH_CreateHookApi(L"user32.dll", "SetWindowsHookExW", Hooked_SetWindowsHookExW, (LPVOID*)&Original_SetWindowsHookExW);
	MH_CreateHookApi(L"user32.dll", "SetWindowsHookExA", Hooked_SetWindowsHookExA, (LPVOID*)&Original_SetWindowsHookExA);
	MH_CreateHookApi(L"user32.dll", "SetWindowPos", Hooked_SetWindowPos, (LPVOID*)&Original_SetWindowPos);
	MH_CreateHookApi(L"user32.dll", "ShowWindow", Hooked_ShowWindow, (LPVOID*)&Original_ShowWindow);
	MH_CreateHookApi(L"user32.dll", "BringWindowToTop", Hooked_BringWindowToTop, (LPVOID*)&Original_BringWindowToTop);
	MH_CreateHookApi(L"user32.dll", "SetForegroundWindow", Hooked_SetForegroundWindow, (LPVOID*)&Original_SetForegroundWindow);
	MH_CreateHookApi(L"user32.dll", "SetActiveWindow", Hooked_SetActiveWindow, (LPVOID*)&Original_SetActiveWindow);

	if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) return FALSE;
	return TRUE;
}

void CleanupHooks() {
	MH_DisableHook(MH_ALL_HOOKS);
	MH_RemoveHook(MH_ALL_HOOKS);
	MH_Uninitialize();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		g_hThisDll = hModule;
		g_sharedFlag = std::make_unique<SharedFlag>(L"HugoLockFlag");
		InitializeHooks();
		break;
	case DLL_PROCESS_DETACH:
		CleanupHooks();
		g_sharedFlag.reset();
		break;
	}
	return TRUE;
}