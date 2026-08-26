// =====================================================================
// shared.rs —— UI 与核心的共享状态
//
// 把 AppCore（含注入器/视角状态）与手柄读取源、视角线程包装成 Arc 共享，
// 供各窗口 View 与手柄轮询线程共同使用。
//
// 线程模型：
//   - 手柄轮询线程：经 core 锁调用 handle_source_event（映射+注入）
//   - 视角线程：独立读取 core.look 原子量 + 注入器
//   - UI 线程：周期性 lock core 读取快照刷新界面，并管理启停
// =====================================================================

use crate::core::app::AppCore;
use crate::core::mapper::LookRunner;
use crate::core::xinput_source::{SourceCallback, SourceEvent, XInputGamepadSource};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

pub struct AppShared {
    pub core: Arc<Mutex<AppCore>>,
    pub look: Arc<Mutex<LookRunner>>,
    pub source: Arc<Mutex<XInputGamepadSource>>,
    pub running: Arc<AtomicBool>,
}

impl AppShared {
    /// 创建共享状态：AppCore + LookRunner + XInputGamepadSource（回调接回 core）
    pub fn new() -> Self {
        let (tx, _rx) = std::sync::mpsc::channel();
        let mut app_core = AppCore::new(tx);
        // LookRunner 与 AppCore 共享同一个 LookState / 注入器
        let look_state = Arc::clone(&app_core.look);
        let injector = Arc::clone(&app_core.injector);
        let core = Arc::new(Mutex::new(app_core));

        let look = Arc::new(Mutex::new(LookRunner::new(injector, look_state)));

        // 手柄源回调：锁定 core 后分发事件
        let core_cb = Arc::clone(&core);
        let callback: SourceCallback = Arc::new(move |e: SourceEvent| {
            if let Ok(mut c) = core_cb.lock() {
                c.handle_source_event(e);
            }
        });
        let source = Arc::new(Mutex::new(XInputGamepadSource::new(callback)));

        Self {
            core,
            look,
            source,
            running: Arc::new(AtomicBool::new(false)),
        }
    }

    /// 开始映射：更新视角参数、启动视角线程与手柄轮询
    pub fn start_mapping(&self) {
        if self.running.load(Ordering::SeqCst) {
            return;
        }
        if let Ok(mut core) = self.core.lock() {
            core.start_mapping();
        }
        if let Ok(mut look) = self.look.lock() {
            look.start();
        }
        if let Ok(mut src) = self.source.lock() {
            src.start();
        }
        self.running.store(true, Ordering::SeqCst);
    }

    /// 停止映射：释放全部注入、停视角线程与手柄轮询
    pub fn stop_mapping(&self) {
        if !self.running.load(Ordering::SeqCst) {
            return;
        }
        if let Ok(mut look) = self.look.lock() {
            look.stop();
        }
        if let Ok(mut src) = self.source.lock() {
            src.stop();
        }
        if let Ok(mut core) = self.core.lock() {
            core.stop_mapping();
        }
        self.running.store(false, Ordering::SeqCst);
    }
}
