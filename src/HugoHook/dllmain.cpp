#include "pch.h"

#include <Windows.h>
#include <format>
#include <string>
#include <cstdio>

#include "MinHook.h"
#include "WinUtils/WinUtils.h"

#pragma comment(lib, "libMinHook.x86.lib")
#pragma comment(lib, "libMinHook.x64.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

static HMODULE g_hThisDll = nullptr;

typedef BOOL(WINAPI* pSetWindowPos)(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
typedef HHOOK(WINAPI* pSetWindowsHookExW)(int idHook, HOOKPROC lpfn, HINSTANCE hMod, DWORD dwThreadId);
typedef HHOOK(WINAPI* pSetWindowsHookExA)(int idHook, HOOKPROC lpfn, HINSTANCE hMod, DWORD dwThreadId);
typedef LRESULT(WINAPI* pCallNextHookEx)(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam);
typedef BOOL(WINAPI* pFreeLibrary)(HMODULE hModule);

typedef BOOL(WINAPI* pShowWindow)(HWND hWnd, int nCmdShow);
typedef BOOL(WINAPI* pBringWindowToTop)(HWND hWnd);
typedef BOOL(WINAPI* pSetForegroundWindow)(HWND hWnd);
typedef HWND(WINAPI* pSetActiveWindow)(HWND hWnd);

static pSetWindowPos Original_SetWindowPos = nullptr;
static pSetWindowsHookExW Original_SetWindowsHookExW = nullptr;
static pSetWindowsHookExA Original_SetWindowsHookExA = nullptr;
static pCallNextHookEx Original_CallNextHookEx = nullptr;
static pFreeLibrary Original_FreeLibrary = nullptr;

static pShowWindow Original_ShowWindow = nullptr;
static pBringWindowToTop Original_BringWindowToTop = nullptr;
static pSetForegroundWindow Original_SetForegroundWindow = nullptr;
static pSetActiveWindow Original_SetActiveWindow = nullptr;

BOOL HandleFullScreenWindow(HWND hWnd)
{
    if (IsWindow(hWnd) && WinUtils::IsWindowFullScreen(hWnd, 100, true))
    {
        if (Original_SetWindowPos != nullptr)
        {
            Original_SetWindowPos(hWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
        }
        LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
        SetWindowLongPtrW(hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hWnd, 0, 0, LWA_ALPHA);
        return TRUE;
    }
    return FALSE;
}

/**
 * Hook SetWindowsHookExW：拦截WH_KEYBOARD_LL类型的全局键盘钩子创建
 */
HHOOK WINAPI My_SetWindowsHookExW(int idHook, HOOKPROC lpfn, HINSTANCE hMod, DWORD dwThreadId)
{
    // 拦截WH_KEYBOARD_LL钩子（阻止拦截Alt+F4等键盘事件）
    if (idHook == WH_KEYBOARD_LL)
    {
        //OutputDebugLog(L"【拦截】SetWindowsHookExW: 尝试创建WH_KEYBOARD_LL钩子，已阻止！");
        return NULL;
    }

    // 其他类型钩子，调用原始API放行
    return Original_SetWindowsHookExW(idHook, lpfn, hMod, dwThreadId);
}

/**
 * Hook SetWindowsHookExA：逻辑与W版本一致，覆盖ANSI版本API
 */
HHOOK WINAPI My_SetWindowsHookExA(int idHook, HOOKPROC lpfn, HINSTANCE hMod, DWORD dwThreadId)
{
    if (idHook == WH_KEYBOARD_LL)
    {
        //OutputDebugLog(L"【拦截】SetWindowsHookExA: 尝试创建WH_KEYBOARD_LL钩子，已阻止！");
        return NULL;
    }

    return Original_SetWindowsHookExA(idHook, lpfn, hMod, dwThreadId);
}

/**
 * Hook CallNextHookEx：确保按键消息正常传递
 */
LRESULT WINAPI My_CallNextHookEx(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam)
{
    return Original_CallNextHookEx(hhk, nCode, wParam, lParam);
}

/**
 * Hook SetWindowPos：处理全屏窗口隐藏/透明
 */
BOOL WINAPI Hooked_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
    if (HandleFullScreenWindow(hWnd))
        return TRUE;
    return Original_SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

/**
 * Hook ShowWindow：拦截全屏窗口的显示/置顶操作
 */
BOOL WINAPI Hooked_ShowWindow(HWND hWnd, int nCmdShow)
{
    if (HandleFullScreenWindow(hWnd))
        return TRUE;
    return Original_ShowWindow(hWnd, nCmdShow);
}

/**
 * Hook BringWindowToTop：拦截窗口置顶操作
 */
BOOL WINAPI Hooked_BringWindowToTop(HWND hWnd)
{
    if (HandleFullScreenWindow(hWnd))
    {
        return TRUE;
    }
    return Original_BringWindowToTop(hWnd);
}

/**
 * 【新增】Hook SetForegroundWindow：拦截窗口置前操作
 */
BOOL WINAPI Hooked_SetForegroundWindow(HWND hWnd)
{
    if (HandleFullScreenWindow(hWnd))
        return TRUE;
    return Original_SetForegroundWindow(hWnd);
}

/**
 * 【新增】Hook SetActiveWindow：拦截窗口激活操作
 */
HWND WINAPI Hooked_SetActiveWindow(HWND hWnd)
{
    if (HandleFullScreenWindow(hWnd))
        return NULL;
    return Original_SetActiveWindow(hWnd);
}

/**
 * Hook FreeLibrary：监控模块卸载，阻止自身DLL被卸载
 */
BOOL WINAPI Hooked_FreeLibrary(HMODULE hModule)
{
    if (hModule == g_hThisDll) return TRUE;
    return Original_FreeLibrary(hModule);
}

/**
 * 初始化所有钩子
 */
BOOL InitializeHooks(HMODULE hModule)
{
    MH_STATUS mhStatus = MH_Initialize();
    if (mhStatus != MH_OK) return FALSE;

    mhStatus = MH_CreateHook(&FreeLibrary, &Hooked_FreeLibrary, reinterpret_cast<void**>(&Original_FreeLibrary));
    mhStatus = MH_EnableHook(&FreeLibrary);

    mhStatus = MH_CreateHookApi(L"user32.dll", "SetWindowsHookExW", My_SetWindowsHookExW, (LPVOID*)&Original_SetWindowsHookExW);
    mhStatus = MH_CreateHookApi(L"user32.dll", "SetWindowsHookExA", My_SetWindowsHookExA, (LPVOID*)&Original_SetWindowsHookExA);
    mhStatus = MH_CreateHookApi(L"user32.dll", "CallNextHookEx", My_CallNextHookEx, (LPVOID*)&Original_CallNextHookEx);

    mhStatus = MH_CreateHookApi(L"user32.dll", "SetWindowPos", Hooked_SetWindowPos, (LPVOID*)&Original_SetWindowPos);
    mhStatus = MH_CreateHookApi(L"user32.dll", "ShowWindow", Hooked_ShowWindow, (LPVOID*)&Original_ShowWindow);
    mhStatus = MH_CreateHookApi(L"user32.dll", "BringWindowToTop", Hooked_BringWindowToTop, (LPVOID*)&Original_BringWindowToTop);
    mhStatus = MH_CreateHookApi(L"user32.dll", "SetForegroundWindow", Hooked_SetForegroundWindow, (LPVOID*)&Original_SetForegroundWindow);
    mhStatus = MH_CreateHookApi(L"user32.dll", "SetActiveWindow", Hooked_SetActiveWindow, (LPVOID*)&Original_SetActiveWindow);

    mhStatus = MH_EnableHook(MH_ALL_HOOKS);
    return (mhStatus == MH_OK);
}

/**
 * 清理所有钩子
 */
void CleanupHooks()
{
    if (Original_FreeLibrary != nullptr)
    {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_RemoveHook(MH_ALL_HOOKS);
        MH_Uninitialize();

        Original_FreeLibrary = nullptr;
        Original_SetWindowsHookExW = nullptr;
        Original_SetWindowsHookExA = nullptr;
        Original_CallNextHookEx = nullptr;
        Original_SetWindowPos = nullptr;
        Original_ShowWindow = nullptr;
        Original_BringWindowToTop = nullptr;
        Original_SetForegroundWindow = nullptr;
        Original_SetActiveWindow = nullptr;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    g_hThisDll = hModule;

    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        InitializeHooks(hModule);
        break;

    case DLL_PROCESS_DETACH:
        CleanupHooks();
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }

    return TRUE;
}
