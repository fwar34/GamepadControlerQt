#pragma once

// ============================================================
// DarkTitleBar.h
// 通过 DWM 为 Windows 窗口开启沉浸式深色标题栏，
// 与深色主题界面统一。动态加载 dwmapi，MinGW 无需改链接选项。
// ============================================================

#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

inline void enableDarkTitleBar(QWidget* widget) {
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    auto fn = reinterpret_cast<DwmSetWindowAttributeFn>(
        GetProcAddress(dwm, "DwmSetWindowAttribute"));
    if (fn) {
        const BOOL dark = TRUE;
        // DWMWA_USE_IMMERSIVE_DARK_MODE：Win10 2004+ 属性号 20，1809-1903 为 19
        fn(hwnd, 20, &dark, sizeof(dark));
        fn(hwnd, 19, &dark, sizeof(dark));
    }
    FreeLibrary(dwm);
#else
    Q_UNUSED(widget);
#endif
}
