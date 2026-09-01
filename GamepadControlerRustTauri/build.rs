// 【Rust 语法】fn main()：程序入口函数；build.rs 是 Cargo 的构建脚本，编译本 crate 之前会先编译并执行它
fn main() {
    // 【Rust 语法】调用 tauri_build::build() 生成 Tauri 构建所需的代码；因是函数体最后一个表达式，可省略末尾分号
    // tauri_build::build()

    #[cfg(target_os = "windows")]
    {
        use tauri_build::WindowsAttributes;
        
        let mut windows = WindowsAttributes::new();
        // include_str! 在编译期把独立文件内容嵌入为字符串，
        // 路径相对于 build.rs 所在目录（即项目根）
        windows = windows.app_manifest(include_str!("app.manifest"));
        tauri_build::try_build(tauri_build::Attributes::new().windows_attributes(windows))
            .expect("failed to run build script");
    }

    #[cfg(not(target_os = "windows"))]
    {
        tauri_build::build()
    }
}
