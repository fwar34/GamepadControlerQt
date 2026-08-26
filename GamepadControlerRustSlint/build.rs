// =====================================================================
// build.rs —— 编译 Slint UI 定义文件
// =====================================================================

fn main() {
    slint_build::compile("ui/main.slint").expect("Slint UI (main.slint) compilation failed");
    slint_build::compile("ui/overlay.slint").expect("Slint UI (overlay.slint) compilation failed");
}
