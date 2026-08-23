#pragma once

#include <QDialog>

// =====================================================================
// HelpDialog —— 使用说明对话框
// 深色主题，与主窗口风格一致；内容为程序使用方式与注意点。
// =====================================================================
class HelpDialog : public QDialog {
    Q_OBJECT
public:
    explicit HelpDialog(QWidget* parent = nullptr);
};
