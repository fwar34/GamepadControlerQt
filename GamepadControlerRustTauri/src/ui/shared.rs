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

// 【Rust 语法】use 语句：导入本 crate 内 core::app 模块的 AppCore 类型
use crate::core::app::AppCore;
// 【Rust 语法】use 语句：导入 core::mapper 模块的 LookRunner 类型（视角运行器）
use crate::core::mapper::LookRunner;
// 【Rust 语法】use 语句：从 core::xinput_source 一次性导入多个项 SourceCallback/SourceEvent/XInputGamepadSource
use crate::core::xinput_source::{SourceCallback, SourceEvent, XInputGamepadSource};
// 【Rust 语法】use 语句：导入标准库原子类型 AtomicBool（原子布尔）与 Ordering（内存序枚举）
use std::sync::atomic::{AtomicBool, Ordering};
// 【Rust 语法】use 语句：导入标准库 Arc（原子引用计数智能指针，多线程共享所有权）与 Mutex（互斥锁）
use std::sync::{Arc, Mutex};

// 【Rust 语法】pub struct：定义公开结构体 AppShared，集中管理跨线程共享的全部状态
pub struct AppShared {
    // 【Rust 语法】字段类型 Arc<Mutex<AppCore>>：Arc 使多个线程共享所有权，Mutex 保证同一时刻只有一线程能访问
    pub core: Arc<Mutex<AppCore>>,
    // 视角运行器的共享指针（同样加锁保护）
    pub look: Arc<Mutex<LookRunner>>,
    // 手柄读取源的共享指针（同样加锁保护）
    pub source: Arc<Mutex<XInputGamepadSource>>,
    // 【Rust 语法】AtomicBool 原子布尔：无需加锁即可被多线程安全读写，表示映射是否在运行
    pub running: Arc<AtomicBool>,
}

// 【Rust 语法】impl 块：为 AppShared 类型实现方法（构造函数与实例方法都定义在这里）
impl AppShared {
    /// 创建共享状态：AppCore + LookRunner + XInputGamepadSource（回调接回 core）
    pub fn new() -> Self { // 【Rust 语法】关联函数 new：返回 Self（即 AppShared）；惯例上用 new 作为构造函数名
        // 【Rust 语法】std::sync::mpsc::channel() 创建多生产者单消费者（MPSC）消息通道，返回 (Sender, Receiver) 元组
        let (tx, _rx) = std::sync::mpsc::channel(); // 元组解构：tx 是发送端，_rx 接收端用下划线开头命名表示暂不使用
        let mut app_core = AppCore::new(tx); // 【Rust 语法】let mut 声明可变绑定；AppCore::new 传入发送端构造核心对象
        // LookRunner 与 AppCore 共享同一个 LookState / 注入器
        let look_state = Arc::clone(&app_core.look); // 【Rust 语法】Arc::clone 增加引用计数，多个 Arc 指向同一底层数据（非深拷贝）
        let injector = Arc::clone(&app_core.injector); // 同样克隆注入器 Arc 引用
        let core = Arc::new(Mutex::new(app_core)); // 【Rust 语法】Arc::new(Mutex::new(...))：先加互斥锁再放进引用计数指针，成为可跨线程共享对象

        let look = Arc::new(Mutex::new(LookRunner::new(injector, look_state))); // 创建视角运行器并共享，注入器与视角状态传给其构造

        // 手柄源回调：锁定 core 后分发事件
        let core_cb = Arc::clone(&core); // 克隆一份 core 的 Arc 引用，供闭包捕获使用（不移动原变量所有权）
        // 【Rust 语法】闭包：move |e: SourceEvent| {...} 用 move 关键字把捕获变量移入闭包；Arc::new 包成可共享的回调对象
        let callback: SourceCallback = Arc::new(move |e: SourceEvent| {
            if let Ok(mut c) = core_cb.lock() { // 【Rust 语法】Mutex::lock() 返回 Result；if let Ok 取出可变守卫 MutexGuard（mut c）
                c.handle_source_event(e); // 通过守卫访问 AppCore，把手柄事件分发给映射+注入逻辑
            }
        });
        let source = Arc::new(Mutex::new(XInputGamepadSource::new(callback))); // 创建手柄读取源并加锁共享，回调接回 core

        // 【Rust 语法】Self { ... } 结构体字面量构造；字段名与局部变量同名时可缩写（core 即 core: core）
        Self {
            core, // 共享的核心对象（AppCore）
            look, // 共享的视角运行器
            source, // 共享的手柄读取源
            running: Arc::new(AtomicBool::new(false)), // 运行标志初始为 false（尚未开始映射）
        }
    }

    /// 开始映射：更新视角参数、启动视角线程与手柄轮询
    pub fn start_mapping(&self) { // 【Rust 语法】实例方法用 &self 接收对自身的不可变借用；通过实例调用
        if self.running.load(Ordering::SeqCst) { // 【Rust 语法】AtomicBool::load 读取当前值；Ordering::SeqCst 表示顺序一致内存序
            return; // 已在运行则直接返回，防止重复启动
        }
        if let Ok(mut core) = self.core.lock() { // 尝试锁定 core；成功则取得可变守卫，失败（锁中毒）则跳过
            core.start_mapping(); // 启动 AppCore 的映射逻辑
        }
        if let Ok(mut look) = self.look.lock() { // 锁定视角运行器
            look.start(); // 启动视角线程
        }
        if let Ok(mut src) = self.source.lock() { // 锁定手柄读取源
            src.start(); // 启动手柄轮询线程
        }
        self.running.store(true, Ordering::SeqCst); // 【Rust 语法】AtomicBool::store 以顺序一致内存序写入 true，标记运行中
    }

    /// 停止映射：释放全部注入、停视角线程与手柄轮询
    pub fn stop_mapping(&self) { // 【Rust 语法】实例方法，&self 不可变借用；停止顺序与启动相反（先停线程再停核心）
        if !self.running.load(Ordering::SeqCst) { // 读取运行标志并取反：未运行则直接返回
            return; // 避免重复停止
        }
        if let Ok(mut look) = self.look.lock() { // 锁定视角运行器
            look.stop(); // 停止视角线程
        }
        if let Ok(mut src) = self.source.lock() { // 锁定手柄读取源
            src.stop(); // 停止手柄轮询
        }
        if let Ok(mut core) = self.core.lock() { // 锁定 core
            core.stop_mapping(); // 停止映射并释放全部已注入的按键/鼠标状态
        }
        self.running.store(false, Ordering::SeqCst); // 写入 false，标记已停止
    }
}
