// =====================================================================
// ui —— Tauri 界面层
//
// Tauri 的界面是 WebView（HTML/CSS/JS），Rust 侧只保留与核心共享的
// 状态（AppShared）。UI 渲染全部在前端 frontend/ 目录完成，通过
// src/commands.rs 的 Tauri 命令与后端交互。
// =====================================================================

// 【Rust 语法】pub mod：声明公开子模块 shared，对应 src/ui/shared.rs
pub mod shared;
