// 【Rust 语法】fn main()：程序入口函数；build.rs 是 Cargo 的构建脚本，编译本 crate 之前会先编译并执行它
fn main() {
    // 【Rust 语法】调用 tauri_build::build() 生成 Tauri 构建所需的代码；因是函数体最后一个表达式，可省略末尾分号
    tauri_build::build()
}
