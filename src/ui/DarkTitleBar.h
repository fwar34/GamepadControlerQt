#pragma once   // 【C++ 语法】预处理指令：保证本头文件只被编译一次，避免重复包含导致重复定义

// ============================================================
// DarkTitleBar.h
// 通过 DWM 为 Windows 窗口开启沉浸式深色标题栏，
// 与深色主题界面统一。动态加载 dwmapi，MinGW 无需改链接选项。
// ============================================================

#include <QWidget>   // 【Qt】包含 QWidget 类声明头文件，函数形参 QWidget* 需要该类型定义

#ifdef Q_OS_WIN   // 【C++ 语法】条件编译：仅当宏 Q_OS_WIN 被定义（Qt 在 Windows 平台会自动定义）时编译以下代码块
#include <windows.h>   // 【Windows API】Windows 核心 API 头文件：HWND、LoadLibraryW、GetProcAddress、HRESULT 等类型与函数均来自这里
#endif   // 【C++ 语法】结束 #ifdef 条件编译块

inline void enableDarkTitleBar(QWidget* widget) {   // 【C++ 语法】inline 内联函数：定义在头文件中也不会产生多重定义链接错误；【Qt】形参 widget 为要设置深色标题栏的窗口指针
#ifdef Q_OS_WIN   // 【C++ 语法】条件编译：仅 Windows 平台下执行深色标题栏逻辑
    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());   // 【Windows API】HWND 为窗口句柄类型；【C++ 语法】reinterpret_cast 做底层强制类型转换；【Qt】winId() 返回窗口的原生系统句柄
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);   // 【C++ 语法】using 定义函数指针类型别名：指向“返回 HRESULT、参数为 (HWND, DWORD, LPCVOID, DWORD)、调用约定 WINAPI”的函数；对应 DWM 接口 DwmSetWindowAttribute 的签名
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");   // 【Windows API】动态加载 dwmapi.dll 动态链接库，返回模块句柄 HMODULE；L"..." 为宽字符字符串；失败时返回 NULL
    if (!dwm) return;   // 【C++ 语法】if 单语句：加载失败（句柄为空）则直接返回，不执行后续
    auto fn = reinterpret_cast<DwmSetWindowAttributeFn>(   // 【C++ 语法】auto 自动推导类型；reinterpret_cast 把 GetProcAddress 返回的 FARPROC 强转为上面定义的函数指针类型
        GetProcAddress(dwm, "DwmSetWindowAttribute"));   // 【Windows API】按名称从已加载的 dll 中获取函数地址（此处为设置窗口属性的 DWM API）
    if (fn) {   // 【C++ 语法】if 单语句块：函数地址非空说明系统提供该函数（老版本系统可能没有）
        const BOOL dark = TRUE;   // 【Windows API】BOOL 为 Windows 布尔类型；TRUE 表示开启深色模式；const 表示值不可修改
        // DWMWA_USE_IMMERSIVE_DARK_MODE：Win10 2004+ 属性号 20，1809-1903 为 19
        fn(hwnd, 20, &dark, sizeof(dark));   // 【Windows API】对新系统调用属性号 20（沉浸式深色模式）；参数依次为：窗口句柄、属性号、属性值指针、属性值字节数
        fn(hwnd, 19, &dark, sizeof(dark));   // 【Windows API】对旧系统（Win10 1809-1903）调用属性号 19；同时调用两个属性号以兼容不同版本的 Windows 10
    }
    FreeLibrary(dwm);   // 【Windows API】卸载已加载的 dwmapi.dll 动态链接库，释放系统资源
#else   // 【C++ 语法】#ifdef 的否则分支：非 Windows 平台编译以下代码
    Q_UNUSED(widget);   // 【Qt】宏：标记形参未被使用，消除编译器的“未使用参数”警告
#endif   // 【C++ 语法】结束条件编译块
}
