#pragma once   // 【C++ 语法】预处理指令：保证本头文件只被编译一次，避免重复包含导致重复定义

#include <QDialog>   // 【Qt】包含 QDialog 基类的声明头文件，本类继承自它

// =====================================================================
// HelpDialog —— 使用说明对话框
// 深色主题，与主窗口风格一致；内容为程序使用方式与注意点。
// =====================================================================
class HelpDialog : public QDialog {   // 【C++ 语法】类定义：HelpDialog 公有继承 QDialog（is-a 关系，可作为对话框使用）
    Q_OBJECT   // 【Qt】宏：启用 Qt 元对象系统（信号/槽、tr() 翻译、qobject_cast 等特性）
public:   // 【C++ 语法】访问控制关键字：以下成员为公有，外部代码可访问
    explicit HelpDialog(QWidget* parent = nullptr);   // 【C++ 语法】explicit 禁止隐式类型转换；【Qt】构造函数声明，parent 为父窗口指针，默认 nullptr 表示无父窗口（顶层窗口）
};
