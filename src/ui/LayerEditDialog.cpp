// ============================================================
// LayerEditDialog.cpp
// 层编辑对话框：配置某个手柄按键的映射
// ------------------------------------------------------------
// 左侧为手柄按键列表，右侧为对应按键的映射编辑表单：
//   动作类型（无/键盘/鼠标点击/鼠标长按/切换层/鼠标移动/视角控制/
//             切换映射/切换屏幕键盘/切换悬浮窗）
//   + 参数（键、鼠标键、目标层等）+ 子命令（组合键，最多 3 个）
//
// 关键设计：
//   - "副本"模式：进入对话框时把目标层复制到 copy_，编辑期间
//     所有改动只写副本，只有点"确定"（accept）才回写真实层；
//     点"取消"则全部丢弃。因此编辑过程中不会污染运行中的映射。
//   - 切换左侧按钮列表项时，会先把当前按钮的表单保存进副本，
//     再加载新按钮的配置（saveFormFor / loadForm 配对）。
//   - 仅"键盘按键"动作允许配置子命令（组合键，实现 Alt+3 等）。
// ============================================================

// 【C++ 语法】#include 预处理指令：编译前把指定头文件内容原样插入本行，供编译使用
#include "LayerEditDialog.h"   // 【C++ 语法】双引号表示先在当前源码目录查找；包含本类声明，实现与声明必须对应
#include "DarkTitleBar.h"      // 包含深色标题栏辅助函数 enableDarkTitleBar 的声明

#include "../gamepad/XInputGamepadSource.h"   // 相对路径（上上级目录 gamepad）包含手柄源头文件，用于连接其按钮信号

// 【C++ 语法】以下 <...> 尖括号表示到编译器头文件搜索路径中查找的 Qt 库头文件
#include <QComboBox>           // Qt 下拉框控件头文件
#include <QDialogButtonBox>    // Qt 确定/取消按钮组控件头文件
#include <QHBoxLayout>         // Qt 水平布局类头文件
#include <QLabel>              // Qt 文本标签控件头文件
#include <QListWidget>         // Qt 列表控件头文件（左侧手柄按键列表）
#include <QStackedWidget>      // Qt 堆叠页面控件头文件（按动作类型切换参数面板）
#include <QStringList>         // Qt 字符串列表容器头文件
#include <QVBoxLayout>         // Qt 垂直布局类头文件

// 【C++ 语法】匿名命名空间 namespace { }：其中定义的名字只在本 .cpp 文件（翻译单元）内可见，
// 不会被导出到其它文件，避免与全局同名符号冲突；本文件的辅助函数与结构体都放在这里。
namespace {

// ------------------------------------------------------------
// 常用键条目：显示名 + Android keyCode
// ------------------------------------------------------------
// 【C++ 语法】struct 定义结构体：把多个相关数据成员打包成一个复合类型（成员默认公有）
struct KeyEntry {
    QString name;   // 【C++ 语法】成员变量：QString 类型的显示名（如 "Space"）
    int code;       // 【C++ 语法】成员变量：int 类型的 Android keyCode 键码（如 SPACE）
};   // 结构体定义结束

// ------------------------------------------------------------
// buildKeyList：构建全部可选按键列表（显示名 -> Android keyCode）
// ------------------------------------------------------------
// 与 InputTypes.cpp 的 keyCodeToName 保持一致的反向映射，
// 供键盘按键下拉框 / 子命令下拉框使用。
// 【C++ 语法】函数定义：返回类型为 QVector<KeyEntry>（Qt 动态数组容器，元素为 KeyEntry 结构体）
QVector<KeyEntry> buildKeyList() {
    QVector<KeyEntry> keys;   // 【Qt】QVector：Qt 动态数组容器；声明一个存放 KeyEntry 的空列表
    // 字母 A-Z（AndroidKey::A 起连续）
    // 【C++ 语法】for 循环：i 从 0 递增到 25（i < 26 为条件），循环体只执行下一条语句
    for (int i = 0; i < 26; ++i)
        keys.append(KeyEntry{QString(QChar('A' + i)), AndroidKey::A + i});   // 【C++ 语法】花括号初始化构造 KeyEntry；QChar('A'+i) 由字符'A'偏移生成字母；append 追加到列表
    // 数字 0-9（AndroidKey::N0 起连续）
    // 【C++ 语法】for 循环：i 从 0 到 9
    for (int i = 0; i < 10; ++i)
        keys.append(KeyEntry{QString(QChar('0' + i)), AndroidKey::N0 + i});   // 数字键 0-9，键码从 AndroidKey::N0 起连续递增
    // 功能键 F1-F12
    // 【C++ 语法】for 循环：i 从 0 到 11
    for (int i = 0; i < 12; ++i)
        keys.append(KeyEntry{QStringLiteral("F%1").arg(i + 1), AndroidKey::F1 + i});   // 【Qt】QStringLiteral 构造字符串字面量；arg(i+1) 把 %1 占位符替换成数字，生成 "F1"~"F12"
    // 小键盘 0-9
    // 【C++ 语法】for 循环：i 从 0 到 9
    for (int i = 0; i < 10; ++i)
        keys.append(KeyEntry{QStringLiteral("Num%1").arg(i), AndroidKey::NUMPAD_0 + i});   // 生成 "Num0"~"Num9"，键码从 NUMPAD_0 起递增

    // 散键（空格/回车/方向/符号/锁键等）
    // 【C++ 语法】const 修饰局部变量：初始化后不可再修改；花括号 { ... } 为初始化列表，直接给 QVector 赋值
    const QVector<KeyEntry> extra = {
        {QStringLiteral("Space"), AndroidKey::SPACE},   // 空格键条目：显示名 + 键码
        {QStringLiteral("Enter"), AndroidKey::ENTER},   // 回车键
        {QStringLiteral("Tab"), AndroidKey::TAB},       // Tab 制表键
        {QStringLiteral("Esc"), AndroidKey::ESCAPE},    // Esc 键
        {QStringLiteral("Backspace"), AndroidKey::DEL}, // 退格键（对应 Android 常量 DEL）
        {QStringLiteral("Insert"), AndroidKey::INSERT}, // Insert 插入键
        {QStringLiteral("Home"), AndroidKey::HOME},     // Home 键
        {QStringLiteral("End"), AndroidKey::MOVE_END},  // End 键（Android 常量名为 MOVE_END）
        {QStringLiteral("PageUp"), AndroidKey::PAGE_UP},      // PageUp 上翻页键
        {QStringLiteral("PageDown"), AndroidKey::PAGE_DOWN},  // PageDown 下翻页键
        {QStringLiteral("Shift"), AndroidKey::SHIFT_LEFT},    // 左 Shift 修饰键
        {QStringLiteral("Ctrl"), AndroidKey::CTRL_LEFT},      // 左 Ctrl 修饰键
        {QStringLiteral("Alt"), AndroidKey::ALT_LEFT},        // 左 Alt 修饰键
        {QStringLiteral("↑"), AndroidKey::DPAD_UP},           // 方向上（DPAD 十字键上）
        {QStringLiteral("↓"), AndroidKey::DPAD_DOWN},         // 方向下
        {QStringLiteral("←"), AndroidKey::DPAD_LEFT},         // 方向左
        {QStringLiteral("→"), AndroidKey::DPAD_RIGHT},        // 方向右
        {QStringLiteral("-"), AndroidKey::MINUS},             // 减号键
        {QStringLiteral("="), AndroidKey::EQUALS},            // 等号键
        {QStringLiteral("["), AndroidKey::LEFT_BRACKET},      // 左方括号键
        {QStringLiteral("]"), AndroidKey::RIGHT_BRACKET},     // 右方括号键
        {QStringLiteral("\\"), AndroidKey::BACKSLASH},        // 反斜杠键（源码里 "\\" 表示一个反斜杠字符）
        {QStringLiteral(";"), AndroidKey::SEMICOLON},         // 分号键
        {QStringLiteral("'"), AndroidKey::APOSTROPHE},        // 单引号键
        {QStringLiteral(","), AndroidKey::COMMA},             // 逗号键
        {QStringLiteral("."), AndroidKey::PERIOD},            // 句点键
        {QStringLiteral("/"), AndroidKey::SLASH},             // 斜杠键
        {QStringLiteral("`"), AndroidKey::GRAVE},             // 反引号键
        {QStringLiteral("CapsLock"), AndroidKey::CAPS_LOCK},   // 大小写锁定键
        {QStringLiteral("NumLock"), AndroidKey::NUM_LOCK},     // 数字小键盘锁定键
        {QStringLiteral("ScrollLock"), AndroidKey::SCROLL_LOCK}, // 滚动锁定键
    };   // 初始化列表结束（分号结束 extra 变量定义）
    keys += extra;   // 【C++ 语法】operator+= 运算符重载：把 extra 中所有元素追加到 keys 末尾
    return keys;     // 【C++ 语法】return 语句：返回构建好的键列表（按值返回，发生拷贝）
}

// ------------------------------------------------------------
// makeKeyCombo：创建键盘键下拉框
// ------------------------------------------------------------
// withNone=true 时首项为"无"（data=-1），供子命令使用。
// 【C++ 语法】函数定义：返回 QComboBox*（指向 QComboBox 对象的指针）；* 表示指针类型，函数可返回堆上对象地址
QComboBox* makeKeyCombo(bool withNone) {
    QComboBox* combo = new QComboBox;   // 【C++ 语法】new 在堆上动态创建 QComboBox 对象并返回其指针；-> 为指针访问成员运算符
    if (withNone)
        combo->addItem(QObject::tr("无"), -1);   // 【Qt】addItem(文本, data)：追加一个选项，data=-1 表示"无"；tr() 提供可翻译文本
    // 【C++ 语法】范围 for 循环：遍历 buildKeyList() 返回的容器，k 为 KeyEntry 的常量引用（& 避免整份拷贝）
    for (const KeyEntry& k : buildKeyList())
        combo->addItem(k.name, k.code);   // 追加选项：显示名 k.name，data 存键码 k.code
    return combo;   // 返回下拉框指针（所有权交给调用方管理）
}

// ------------------------------------------------------------
// makeMouseCombo：创建鼠标键下拉框（点击/长按共用）
// ------------------------------------------------------------
QComboBox* makeMouseCombo() {   // 【C++ 语法】函数定义：返回 QComboBox* 指针，无参数
    QComboBox* combo = new QComboBox;   // 堆上创建鼠标键下拉框
    const MouseButton buttons[] = {   // 【C++ 语法】C 风格数组：存放 MouseButton 枚举值；const 表示元素不可修改
        MouseButton::LEFT, MouseButton::RIGHT, MouseButton::MIDDLE,   // 鼠标左键、右键、中键
        MouseButton::FORWARD, MouseButton::BACK,   // 鼠标前进键、后退键（侧键）
    };   // 数组初始化结束
    // 【C++ 语法】范围 for：b 为 MouseButton 枚举元素（按值拷贝，枚举体积很小）
    for (const MouseButton b : buttons)
        combo->addItem(mouseButtonDisplayName(b), static_cast<int>(b));   // 【C++ 语法】static_cast<int>(b)：显式把枚举强转为 int 作为 data；显示名由工具函数生成
    return combo;   // 返回鼠标键下拉框指针
}

// ------------------------------------------------------------
// makeLayerCombo：创建目标层下拉框（切换层动作）
// ------------------------------------------------------------
// 目标层只列出「当前激活操作集」内的操作层（切换层不出本操作集范围）。
// 【C++ 语法】函数定义：参数为 ControllerProfile*（配置文件指针，用于读取层列表）
QComboBox* makeLayerCombo(ControllerProfile* profile) {
    QComboBox* combo = new QComboBox;   // 堆上创建目标层下拉框
    combo->addItem(QObject::tr("无"), QString());   // 首项"无"，data 为空字符串 QString() 表示未选层
    // 【C++ 语法】范围 for：遍历 profile->layers() 返回的操作层列表，l 为 OperationLayer 的常量引用
    for (const OperationLayer& l : profile->layers())
        combo->addItem(l.name, l.id);   // 追加选项：显示名 l.name，data 存层 id（QString）
    return combo;   // 返回下拉框指针
}

// ------------------------------------------------------------
// setComboIndex：按 data 值选中下拉项
// ------------------------------------------------------------
// 兼容 int（键码/鼠标键）与 QString（层名）两种 data 类型。
// 【C++ 语法】函数定义：参数 const QVariant& 为"任意类型值"的常量引用（& 引用、const 只读，避免拷贝）；void 无返回值
void setComboIndex(QComboBox* combo, const QVariant& data) {
    const int idx = combo->findData(data);   // 【Qt】findData：在全部选项中查找 data 等于 data 的项，返回其索引；找不到返回 -1
    if (idx >= 0)
        combo->setCurrentIndex(idx);   // 【Qt】setCurrentIndex：把当前选中项设为索引 idx 对应的选项
}

}  // namespace   // 匿名命名空间结束

// ============================================================
// 构造：初始化副本并搭建界面
// ============================================================
// copy_ = *layer 建立编辑副本；初始选中第一个手柄按钮并加载其配置。
// 【C++ 语法】构造函数定义：与类同名、无返回类型；参数为三个指针 + 父控件指针
// 【C++ 语法】初始化列表：冒号后 : QDialog(parent), profile_(profile), layer_(layer), copy_(*layer) 依次初始化基类与成员；
//   copy_(*layer) 用 *layer 解引用后拷贝构造编辑副本；初始化顺序按成员声明顺序执行
LayerEditDialog::LayerEditDialog(ControllerProfile* profile, OperationLayer* layer,   // 参数：配置、要编辑的层、手柄源
                                 XInputGamepadSource* gamepad, QWidget* parent)   // 参数续行：手柄源与父控件指针
    : QDialog(parent), profile_(profile), layer_(layer), copy_(*layer) {   // 初始化列表：基类 QDialog 与三个成员初始化
    setWindowTitle(tr("编辑层 - %1").arg(layer->name));   // 【Qt】setWindowTitle 设置窗口标题；tr() 支持翻译；%1 占位符被 layer->name 替换
    buildUi();   // 调用本类构建界面函数（定义见下方）

    // 手柄按键实时高亮：按下 -> 左侧列表对应项变色，松开恢复。
    // 显式 QueuedConnection：buttonChanged 从轮询线程发出，保证在 GUI 线程更新控件。
    // 【Qt】connect 信号槽连接：参数依次为 发送对象、信号（&类::信号）、接收对象、接收函数、连接类型；
    //   Qt::QueuedConnection 表示跨线程时把调用排队到接收者所在线程的事件循环（此处为 GUI 线程）
    connect(gamepad, &XInputGamepadSource::buttonChanged,
            this, &LayerEditDialog::onGamepadButton, Qt::QueuedConnection);   // 连接手柄按钮变化信号到本类槽，队列方式跨线程投递

    // 默认选中列表第一项（A 键）并加载表单
    buttonList_->setCurrentRow(0);   // 【Qt】setCurrentRow：把列表当前选中行设为第 0 行（即第一个按钮）
    if (buttonList_->currentItem())   // 【Qt】currentItem：返回当前选中列表项指针；无选中返回 nullptr，此处用作非空判断
        loadForm();   // 有选中项则加载该按钮的映射配置到表单控件
}

// ============================================================
// onGamepadButton：手柄按键按下/松开 -> 左侧按钮列表对应项高亮
// ============================================================
// 与主界面操作层激活按钮同样用青绿色（#22958c）提示，
// 方便在编辑映射时按手柄确认「哪个键对应哪个列表项」。
// 只维护 pressedButtons_ 集合，实际样式统一交给 refreshItemStyles()
// 重算（QListWidget::item 的 QSS 会覆盖 setBackground，故列表项文本
// 由 item widget（QLabel）承载，样式直接设置在标签上，可正常高亮背景）。
// 注：本对话框为模态，手柄线程的信号经队列投递到 UI 线程处理。
// 【C++ 语法】成员函数定义（槽函数）：void 无返回值；参数为手柄按键枚举与 bool 是否按下
void LayerEditDialog::onGamepadButton(ControllerButton button, bool isPressed) {
    if (!buttonList_)   // 【C++ 语法】! 逻辑取反：指针为 nullptr（空指针）时条件为真，防御性判断
        return;   // 界面尚未初始化则提前返回
    if (isPressed)
        pressedButtons_.insert(button);   // 【Qt】QSet::insert：把按下的按键加入"当前按下集合"
    else
        pressedButtons_.remove(button);   // 【Qt】QSet::remove：松开时从集合移除该按键
    refreshItemStyles();   // 统一重算并刷新全部列表项的高亮样式
}

// ============================================================
// buildUi：构建整个对话框界面
// ============================================================
// 布局：层名编辑行 -> （可选）触发按键提示 -> 左右分栏
//   （左：按键列表 | 右：动作类型 + 参数 + 子命令）-> 确定/取消。
// 【C++ 语法】成员函数定义：void 无返回值；auto* 由编译器自动推导变量类型（这里是 QVBoxLayout*）
void LayerEditDialog::buildUi() {
    auto* root = new QVBoxLayout(this);   // 【Qt】QVBoxLayout：垂直布局，子控件从上到下排列；new + (this) 表示布局挂到本对话框

    // ---- 层名称编辑 ----
    auto* nameLayout = new QHBoxLayout;   // 【Qt】QHBoxLayout：水平布局，一行内从左到右排布
    nameLayout->addWidget(new QLabel(tr("层名称："), this));   // 【Qt】addWidget：把控件加入布局；QLabel 为文本标签，tr() 文本可翻译
    layerNameEdit_ = new QLineEdit(copy_.name, this);   // 【Qt】QLineEdit：单行文本输入框，初值取副本的层名 copy_.name
    nameLayout->addWidget(layerNameEdit_);   // 把层名输入框加入水平布局
    root->addLayout(nameLayout);   // 【Qt】addLayout：把子布局作为一项加入垂直布局

    // 触发按键提示（仅操作层有）：扫描公共层中实际配置为
    // 「切换层 -> 本层」的按键，展示真正驱动进入本层的映射；
    // triggerButton 字段仅作无映射时的补充提示。
    const bool isCommon = (layer_ == profile_->commonLayer());   // 【C++ 语法】bool 布尔类型；== 指针比较：当前编辑层是否为公共层
    if (!isCommon) {   // 仅非公共层（操作层）显示触发按键提示
        QStringList triggers;   // 【Qt】QStringList：QString 的列表容器，存放触发按键的显示名
        const OperationLayer* common = profile_->commonLayer();   // 取公共层指针；const 表示不可通过该指针修改对象
        // 【C++ 语法】范围 for：遍历 allControllerButtons() 返回的全部手柄按键集合
        for (const ControllerButton b : allControllerButtons()) {
            if (!common) break;   // 公共层为空则提前跳出循环（防御性）
            const KeyMapping* m = common->getMapping(b);   // 查公共层中该按键的映射；无映射时返回 nullptr
            // 【C++ 语法】&& 逻辑与（三个条件同时满足）：有映射；动作类型是"切换层"；目标层名等于当前副本层 id
            if (m && m->action.type == MappedAction::Type::SwitchLayer
                && m->action.layerName == copy_.id)   // 续行条件：目标层名与当前编辑层 id 相同
                triggers.append(controllerButtonDisplayName(b));   // 命中则把该按键的显示名加入列表
        }
        QString triggerText;   // 【C++ 语法】声明字符串变量，下面按三种情况分别赋值
        if (!triggers.isEmpty()) {   // 【Qt】QStringList::isEmpty：列表为空返回 true；此处非空即实际配置了切换映射
            triggerText = tr("触发按键（切换到本层）：%1").arg(triggers.join(", "));   // 【Qt】join(", ")：把多个名称用逗号拼接成字符串并填入 %1
        } else if (copy_.hasTriggerButton) {   // 【C++ 语法】else if 分支：无实际切换映射但配置了 triggerButton 字段
            triggerText = tr("触发按键（建议，尚未配置切换映射）：%1")
                              .arg(controllerButtonDisplayName(copy_.triggerButton));   // 续行：显示字段中建议的触发按键名
        } else {   // 既无切换映射也无 triggerButton 字段
            triggerText = tr("触发按键：未配置（请在公共层或其他层为某按键设置“切换层”动作）");
        }
        auto* triggerLabel = new QLabel(triggerText, this);   // 创建提示标签，内容为拼好的 triggerText
        triggerLabel->setStyleSheet("color: #d0a060;");   // 【Qt】setStyleSheet：设置 Qt 样式表（QSS），这里把文字颜色设为橙色
        root->addWidget(triggerLabel);   // 把提示标签加入垂直布局
    }

    auto* hbox = new QHBoxLayout;   // 【Qt】QHBoxLayout：外层水平布局，承载"左列表 | 右表单"左右分栏

    // ---- 左侧：手柄按键列表 ----
    // 每个列表项用一个 QLabel 作为 item widget 承载文本与样式：
    // 直接 setBackground 会被下方 QListWidget::item 的 QSS 覆盖，
    // 改用 item widget 后样式设置在标签上，手柄按下时可正常高亮背景。
    buttonList_ = new QListWidget(this);   // 【Qt】QListWidget：列表控件，创建左侧手柄按键列表
    // 【C++ 语法】范围 for：遍历所有手柄按键，为每个按键创建一个列表项
    for (const ControllerButton b : allControllerButtons()) {
        QListWidgetItem* item = new QListWidgetItem(buttonList_);   // 【Qt】QListWidgetItem：列表项；创建时以列表为父对象，自动加入列表
        item->setData(Qt::UserRole, static_cast<int>(b));   // 【Qt】setData(Qt::UserRole, 值)：给列表项挂载自定义数据，这里存按键枚举的 int 值
        auto* label = new QLabel(   // 创建标签作为该列表项的显示控件
            QStringLiteral("%1   %2").arg(controllerButtonDisplayName(b),
                                          mappingDesc(copy_.getMapping(b))),   // 续行：文本为"按键显示名 + 映射描述"；父控件为列表
            buttonList_);   // QLabel 的父对象参数：列表控件
        label->setTextFormat(Qt::PlainText);   // 【Qt】setTextFormat(Qt::PlainText)：按纯文本渲染，避免描述中的特殊字符被当富文本
        label->setStyleSheet(itemStyle(false, false));   // 初始状态：未选中、未按下
        item->setSizeHint(label->sizeHint());   // 【Qt】setSizeHint/sizeHint：把列表项尺寸设为标签建议尺寸，保证高度匹配内容
        buttonList_->setItemWidget(item, label);   // 【Qt】setItemWidget：把标签设为列表项的 item widget，便于手柄按下时高亮背景
        buttonLabels_.insert(b, label);   // 【Qt】QHash::insert：记录"按键 -> 标签"映射，供刷新样式使用
    }
    // 切换选中项：先把上一个按钮的表单存进副本，再加载新按钮，
    // 最后刷新全部列表项样式（选中高亮跟随切换）
    // 【Qt】connect + lambda：把 currentItemChanged 信号（参数：新选中项、旧选中项）连接到 lambda 匿名函数
    // 【C++ 语法】lambda 捕获 [this]：lambda 内可直接访问 this 所指对象的成员；参数列表为 (current, previous)
    connect(buttonList_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem* previous) {   // lambda 定义开始：捕获 this，两个列表项指针参数
                if (previous)   // 存在上一个选中项
                    saveFormFor(static_cast<ControllerButton>(previous->data(Qt::UserRole).toInt()));   // 【C++ 语法】data().toInt() 取存的 int 再 static_cast 强转回枚举，保存其表单
                if (current)   // 存在新选中项
                    loadForm();   // 加载新按键的配置
                refreshItemStyles();   // 刷新全部列表项样式（选中高亮跟随切换）
            });   // lambda 结束；connect 调用结束
    hbox->addWidget(buttonList_, 1);   // 【Qt】addWidget(控件, 伸缩因子 1)：加入水平布局，伸缩因子越大占越多宽度

    // ---- 右侧：映射编辑表单 ----
    auto* form = new QVBoxLayout;   // 右侧表单的垂直布局

    // 动作类型下拉框：索引与 paramStack_ 页一一对应
    actionTypeCombo_ = new QComboBox(this);   // 【Qt】QComboBox：下拉框控件，创建动作类型下拉框
    actionTypeCombo_->addItems({   // 【Qt】addItems：一次性批量添加多个文本项（花括号初始化列表传入）
        tr("无（不映射）"), tr("键盘按键"), tr("鼠标点击"), tr("鼠标长按"),
        tr("鼠标滚轮上滚"), tr("鼠标滚轮下滚"),
        tr("切换层"), tr("鼠标移动"), tr("视角控制"),
        tr("切换映射"), tr("切换屏幕键盘"), tr("切换悬浮窗"),
    });   // 文本列表结束，共 12 个动作类型
    // 切换动作类型时刷新参数页
    // 【C++ 语法】QOverload<int>::of(&QComboBox::currentIndexChanged)：显式指定信号的重载版本（带 int 参数那个），
    // 因为 currentIndexChanged 同时有 (int) 与 (const QString&) 两个重载，需要写明取哪个
    connect(actionTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LayerEditDialog::updateParamPage);   // 动作类型变化 -> 刷新参数页
    form->addWidget(new QLabel(tr("动作类型"), this));   // 添加"动作类型"文字标签
    form->addWidget(actionTypeCombo_);   // 把动作类型下拉框加入右侧表单

    // 参数区（QStackedWidget 按动作类型切换页面）：
    //   0 无 | 1 键盘 | 2 鼠标点击 | 3 鼠标长按 | 4 滚轮上 | 5 滚轮下
    //   | 6 切换层 | 7 鼠标移动 | 8 视角控制 | 9 切换映射 | 10 屏幕键盘 | 11 悬浮窗
    paramStack_ = new QStackedWidget(this);   // 【Qt】QStackedWidget：堆叠控件，同一位置只显示当前页；页索引与动作类型一一对应
    paramStack_->addWidget(new QWidget(this));                     // 0 无：空页面占位
    keyCombo_ = makeKeyCombo(false);   // 创建键盘键下拉框（不带"无"项），存入成员指针
    paramStack_->addWidget(keyCombo_);                             // 1 键盘按键：显示键盘键下拉框
    mouseCombo_ = makeMouseCombo();   // 创建鼠标键下拉框（鼠标点击动作用）
    paramStack_->addWidget(mouseCombo_);                           // 2 鼠标点击：显示鼠标键下拉框
    mouseToggleCombo_ = makeMouseCombo();   // 创建另一个鼠标键下拉框（鼠标长按动作用，与点击分开存储）
    paramStack_->addWidget(mouseToggleCombo_);                     // 3 鼠标长按：显示鼠标键下拉框
    paramStack_->addWidget(new QLabel(tr("按下时向上滚动一格"), this)); // 4 滚轮上：纯提示标签
    paramStack_->addWidget(new QLabel(tr("按下时向下滚动一格"), this)); // 5 滚轮下：纯提示标签
    layerCombo_ = makeLayerCombo(profile_);   // 创建目标层下拉框（选项来自当前操作集的层列表）
    paramStack_->addWidget(layerCombo_);                           // 6 切换层：显示目标层下拉框
    paramStack_->addWidget(new QLabel(tr("由左摇杆输入驱动"), this)); // 7 鼠标移动：纯提示标签
    paramStack_->addWidget(new QLabel(tr("由右摇杆输入驱动"), this)); // 8 视角控制：纯提示标签
    paramStack_->addWidget(new QLabel(tr("按下时切换映射启停"), this)); // 9 切换映射：纯提示标签
    paramStack_->addWidget(new QLabel(tr("按下时切换屏幕键盘"), this)); // 10 切换屏幕键盘：纯提示标签
    paramStack_->addWidget(new QLabel(tr("按下时切换悬浮窗"), this)); // 11 切换悬浮窗：纯提示标签
    form->addWidget(paramStack_);   // 把参数堆叠面板加入右侧表单

    // 子命令（组合键）：仅键盘按键动作生效，最多 3 个
    form->addWidget(new QLabel(tr("子命令（组合键，最多 3 个；仅在键盘按键时生效）"), this));   // 子命令说明标签
    auto* subRow = new QHBoxLayout;   // 子命令所在的一行水平布局
    // 【C++ 语法】for 循环：创建 3 个子命令下拉框，下标 i 为 0~2
    for (int i = 0; i < 3; ++i) {
        subCombos_[i] = makeKeyCombo(true);   // 【C++ 语法】数组下标访问成员数组 subCombos_[i]；带"无"项（供子命令默认值）
        subRow->addWidget(subCombos_[i], 1);   // 加入水平布局，每个各占 1 份伸缩
    }
    form->addLayout(subRow);   // 子命令行加入表单
    form->addStretch(1);   // 【Qt】addStretch：加入弹簧占位，把上方控件顶到顶部，多余空间留在下方

    // 参数下拉框变更即保存并刷新左侧列表（键盘键/鼠标键/目标层/子命令），
    // 保证右侧任何改动都即时同步到左侧按钮描述，无需等重新打开对话框。
    // loading_ 置位时（loadForm 填充控件）跳过，避免初始化期递归保存。
    // 【C++ 语法】lambda 表达式：auto 推导类型；[this] 捕获 this 指针，函数体内可直接调用成员函数
    auto saveCurrent = [this]() {   // 定义 lambda 并保存到变量 saveCurrent
        if (!loading_)   // 非加载中才执行保存
            saveFormFor(currentButton());   // 把当前按钮的表单写入副本
    };   // lambda 定义结束
    // 【Qt】connect 信号到 lambda：下拉框当前索引变化（int 重载）时调用 saveCurrent
    connect(keyCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, saveCurrent);
    connect(mouseCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, saveCurrent);
    connect(mouseToggleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, saveCurrent);
    connect(layerCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, saveCurrent);
    // 【C++ 语法】for 循环：为 3 个子命令下拉框都连接 saveCurrent
    for (int i = 0; i < 3; ++i)
        connect(subCombos_[i], QOverload<int>::of(&QComboBox::currentIndexChanged), this, saveCurrent);   // 子命令下拉框变化也即时保存

    hbox->addLayout(form, 1);   // 右侧表单加入左右分栏布局，占 1 份宽度
    root->addLayout(hbox, 1);   // 左右分栏加入根垂直布局，占 1 份高度

    // 确定 / 取消
    // 【Qt】QDialogButtonBox：内置"确定/取消"按钮组；| 为按位或组合两个枚举标志
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);   // 创建按钮组（OK|Cancel），父对象为本对话框
    // 【Qt】accepted/rejected 信号：点击确定/取消时发出，分别连接到 accept()/reject() 槽
    connect(buttons, &QDialogButtonBox::accepted, this, &LayerEditDialog::accept);   // 确定 -> accept()
    connect(buttons, &QDialogButtonBox::rejected, this, &LayerEditDialog::reject);   // 取消 -> reject()
    root->addWidget(buttons);   // 按钮组加入根布局底部

    // ---- 深色主题（与主窗口一致的灰底 + 青绿强调） ----
    // 【Qt】setStyleSheet：设置整个对话框的 Qt 样式表（QSS）；R"( ... )" 是 C++11 原始字符串字面量，
    //   括号内可原样书写多行字符串，无需转义引号/反斜杠；下方各行的字符串内容不能改动
    setStyleSheet(R"(
        QDialog {
            background-color: #2b2d31;
        }
        QLabel {
            color: #d5d9df;
            background-color: transparent;
        }
        QLineEdit {
            background-color: #33363b;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            border-radius: 5px;
            padding: 4px 8px;
            selection-background-color: #7fc9c4;
            selection-color: #1c1e22;
        }
        QLineEdit:focus {
            border-color: #7fc9c4;
        }
        QListWidget {
            background-color: #2b2d31;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            border-radius: 6px;
            outline: none;
        }
        /* 列表项样式由 item widget（QLabel）承载，不再在此设置 ::item 规则，
           否则会覆盖标签背景导致手柄按下高亮失效 */
        QComboBox {
            background-color: #33363b;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            border-radius: 5px;
            padding: 3px 8px;
        }
        QComboBox:hover {
            border-color: #7fc9c4;
        }
        QComboBox::drop-down {
            border: none;
            background: transparent;
            width: 22px;
        }
        QComboBox::down-arrow {
            image: url(:/icons/down-arrow.png);
            width: 10px;
            height: 6px;
        }
        QComboBox QAbstractItemView {
            background-color: #33363b;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            selection-background-color: #22958c;
            selection-color: #ffffff;
        }
        QPushButton {
            background-color: #3d4147;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            border-radius: 6px;
            padding: 5px 14px;
        }
        QPushButton:hover {
            background-color: #474b52;
            border-color: #7fc9c4;
        }
        QPushButton:pressed {
            background-color: #2f3237;
        }
        QDialogButtonBox {
            background-color: transparent;
        }
    )");   // 原始字符串结束（)" 为定界符），; 结束 setStyleSheet 调用

    // ---- 标题栏深色化，与主窗口一致 ----
    enableDarkTitleBar(this);   // 调用深色标题栏工具，传入本对话框使其标题栏变暗

    resize(620, 480);   // 【Qt】resize：设置对话框初始大小（宽 620，高 480）
}

// ============================================================
// currentButton：返回左侧当前选中的手柄按钮
// ============================================================
// 【C++ 语法】const 成员函数：函数末尾 const 表示该函数不修改对象状态；返回 ControllerButton 枚举
ControllerButton LayerEditDialog::currentButton() const {
    // 【C++ 语法】if 的初始化语句（C++17）：在 if 条件中声明指针 item 并赋值，作用域限于 if 及其分支
    if (QListWidgetItem* item = buttonList_->currentItem())
        return static_cast<ControllerButton>(item->data(Qt::UserRole).toInt());   // 读取列表项自定义数据并强转为枚举返回
    return ControllerButton::A;   // 无选中项时返回默认值 A 键
}

// ============================================================
// loadForm：把副本中"当前按钮"的映射加载进表单控件
// ============================================================
// loading_ 置位可抑制 updateParamPage 的连带保存动作，避免循环。
// 【C++ 语法】成员函数定义（槽函数）：void 无返回值
void LayerEditDialog::loadForm() {
    loading_ = true;   // 【C++ 语法】置位成员布尔标志：防止填充控件时触发信号导致递归保存
    const ControllerButton b = currentButton();   // 获取当前选中的按键
    const KeyMapping* m = copy_.getMapping(b);   // 从副本查该按键的映射；无映射时返回 nullptr

    // 无映射 -> 动作类型选"无"；有映射 -> 按类型填充控件
    if (!m) {   // 该按键没有映射
        actionTypeCombo_->setCurrentIndex(0);   // 动作类型下拉框选第 0 项"无（不映射）"
    } else {   // 有映射，按动作类型分支填充
        // 【C++ 语法】switch-case 多分支选择：按 m->action.type 枚举值跳转到对应分支
        switch (m->action.type) {
            case MappedAction::Type::KeyboardKey:   // 键盘按键动作
                actionTypeCombo_->setCurrentIndex(1);   // 动作类型选"键盘按键"
                setComboIndex(keyCombo_, m->action.keyCode);   // 键盘键下拉框按键码选中对应项
                break;   // 【C++ 语法】break：结束当前分支，跳出 switch
            case MappedAction::Type::MouseClick:   // 鼠标点击动作
                actionTypeCombo_->setCurrentIndex(2);
                setComboIndex(mouseCombo_, static_cast<int>(m->action.mouseButton));   // 鼠标键枚举强转 int 后按 data 选中
                break;
            case MappedAction::Type::MouseToggle:   // 鼠标长按动作
                actionTypeCombo_->setCurrentIndex(3);
                setComboIndex(mouseToggleCombo_, static_cast<int>(m->action.mouseButton));   // 在"长按"下拉框中选中对应鼠标键
                break;
            case MappedAction::Type::WheelUp:   // 鼠标滚轮上滚
                actionTypeCombo_->setCurrentIndex(4);
                break;
            case MappedAction::Type::WheelDown:   // 鼠标滚轮下滚
                actionTypeCombo_->setCurrentIndex(5);
                break;
            case MappedAction::Type::SwitchLayer:   // 切换层动作
                actionTypeCombo_->setCurrentIndex(6);
                // 先按 id 匹配；匹配不到则按 name 匹配（兼容旧配置）
                {   // 【C++ 语法】块作用域：用花括号包住局部变量，避免与外层变量冲突并限定其生命周期
                    const int idx = layerCombo_->findData(m->action.layerName);   // 按层 id（data）查找下拉项索引
                    if (idx >= 0) {   // 按 id 找到了
                        layerCombo_->setCurrentIndex(idx);   // 直接选中该项
                    } else {   // 按 id 找不到，退而按层名 name 匹配（兼容旧配置）
                        // 【C++ 语法】for 循环：遍历下拉框每一项
                        for (int i = 0; i < layerCombo_->count(); ++i) {
                            const QString layerId = layerCombo_->itemData(i).toString();   // 【Qt】itemData(i)：取第 i 项的自定义 data（层 id）
                            const OperationLayer* l = profile_->findLayer(layerId);   // 按层 id 查找对应的操作层
                            if (l && l->name == m->action.layerName) {   // 层存在且名称与映射目标一致
                                layerCombo_->setCurrentIndex(i);   // 选中该项
                                break;   // 提前结束循环
                            }
                        }
                    }
                }
                break;
            case MappedAction::Type::MouseMove:   // 鼠标移动动作
                actionTypeCombo_->setCurrentIndex(7);
                break;
            case MappedAction::Type::LookAround:   // 视角控制动作
                actionTypeCombo_->setCurrentIndex(8);
                break;
            case MappedAction::Type::ToggleMapping:   // 切换映射动作
                actionTypeCombo_->setCurrentIndex(9);
                break;
            case MappedAction::Type::ToggleOnScreenKeyboard:   // 切换屏幕键盘动作
                actionTypeCombo_->setCurrentIndex(10);
                break;
            case MappedAction::Type::ToggleOverlay:   // 切换悬浮窗动作
                actionTypeCombo_->setCurrentIndex(11);
                break;
        }   // switch 结束
    }   // else 结束

    // 子命令：逐个填充（无子命令时保持"无"）
    // 【C++ 语法】for 循环：填充 3 个子命令下拉框
    for (int i = 0; i < 3; ++i) {
        subCombos_[i]->setCurrentIndex(0);   // 先默认选"无"
        if (m && i < m->subCommands.size())   // 有映射且第 i 个子命令存在
            setComboIndex(subCombos_[i], m->subCommands[i]);   // 按子命令键码选中对应下拉项
    }

    loading_ = false;   // 加载完成，清除加载标志
    updateParamPage(actionTypeCombo_->currentIndex());   // 按当前动作类型刷新参数页
}

// ============================================================
// saveFormFor：把表单当前内容保存进副本的指定按钮映射
// ============================================================
// 动作类型为"无"时移除该按钮映射（等价于清除配置）。
// 仅键盘按键动作收集子命令，且过滤掉与主动作相同或重复的键。
// 【C++ 语法】成员函数定义（槽函数）：参数为手柄按键枚举；void 无返回值
void LayerEditDialog::saveFormFor(ControllerButton button) {
    if (buttonList_ == nullptr)   // 列表尚未创建（防御性判断）
        return;   // 直接返回

    const int typeIdx = actionTypeCombo_->currentIndex();   // 读取动作类型下拉框当前索引
    if (typeIdx <= 0) {  // 无（不映射）：移除现有映射
        copy_.buttonMappings.remove(button);   // 【C++ 语法】QHash::remove：从副本的按键映射表中移除该按键
        updateButtonListItem(button);   // 刷新左侧该列表项文本
        return;   // 结束函数
    }

    KeyMapping m;   // 【C++ 语法】声明局部结构体变量 m 用于组装新映射（默认构造）
    // 【C++ 语法】switch-case：按动作类型下拉框索引分支组装映射
    switch (typeIdx) {
        case 1:  // 键盘按键
            m.action = MappedAction::keyboardKey(keyCombo_->currentData().toInt());   // 工厂函数构造键盘动作；currentData() 取当前项 data（键码）
            break;
        case 2:  // 鼠标点击
            m.action = MappedAction::mouseClick(
                static_cast<MouseButton>(mouseCombo_->currentData().toInt()));   // data 的 int 强转回 MouseButton 枚举后构造动作
            break;
        case 3:  // 鼠标长按
            m.action = MappedAction::mouseToggle(
                static_cast<MouseButton>(mouseToggleCombo_->currentData().toInt()));   // 从长按下拉框取鼠标键构造动作
            break;
        case 4:  // 鼠标滚轮上滚
            m.action = MappedAction::wheelUp();   // 构造滚轮上滚动作
            break;
        case 5:  // 鼠标滚轮下滚
            m.action = MappedAction::wheelDown();   // 构造滚轮下滚动作
            break;
        case 6: {  // 切换层：目标层为空则视为清除
            const QString layerName = layerCombo_->currentData().toString();   // 取目标层 id（QString）
            if (layerName.isEmpty()) {   // 目标层为空
                copy_.buttonMappings.remove(button);   // 移除映射（视为清除）
                updateButtonListItem(button);   // 刷新左侧文本
                return;   // 结束函数
            }
            m.action = MappedAction::switchLayer(layerName);   // 构造"切换层"动作
            break;
        }
        case 7:  // 鼠标移动
            m.action = MappedAction::mouseMove();   // 构造鼠标移动动作
            break;
        case 8:  // 视角控制
            m.action = MappedAction::lookAround();   // 构造视角控制动作
            break;
        case 9:  // 切换映射
            m.action = MappedAction::toggleMapping();   // 构造切换映射动作
            break;
        case 10:  // 切换屏幕键盘
            m.action = MappedAction::toggleOnScreenKeyboard();   // 构造切换屏幕键盘动作
            break;
        case 11:  // 切换悬浮窗
            m.action = MappedAction::toggleOverlay();   // 构造切换悬浮窗动作
            break;
        default:   // 未知索引
            return;   // 直接返回
    }   // switch 结束

    // 收集子命令：跳过"无"、与主动作相同的键、已存在的重复键
    // 【C++ 语法】for 循环：遍历 3 个子命令下拉框
    for (int i = 0; i < 3; ++i) {
        const int sub = subCombos_[i]->currentData().toInt();   // 取第 i 个子命令键码
        // 【C++ 语法】&& 逻辑与（三个条件同时满足）：子命令非"无"(>=0)；不等于主动作键码；未包含重复
        if (sub >= 0 && sub != m.action.keyCode && !m.subCommands.contains(sub))
            m.subCommands.append(sub);   // 【C++ 语法】QVector::append：把键码追加到子命令列表
    }
    copy_.buttonMappings.insert(button, m);   // 把组装好的映射写入副本（覆盖该按键的旧映射）
    updateButtonListItem(button);   // 刷新左侧列表项描述
}

// ============================================================
// mappingDesc：生成一条映射的描述文本（左侧按钮列表项用）
// ============================================================
// 【C++ 语法】const 成员函数 + 参数 const KeyMapping* m：指向映射对象的常量指针（不通过它修改对象）
QString LayerEditDialog::mappingDesc(const KeyMapping* m) const {
    if (!m)   // 映射为空指针（无映射）
        return QStringLiteral("—");   // 【Qt】QStringLiteral：编译期构造 QString 字面量；返回破折号占位符
    QString desc = m->describe();   // 调用映射对象自身的 describe() 生成默认描述文本
    // SwitchLayer：将 layer id 解析为显示名
    if (m->action.type == MappedAction::Type::SwitchLayer) {   // 是切换层动作
        const OperationLayer* target = profile_->findLayer(m->action.layerName);   // 按目标层 id 查找操作层
        if (target)   // 层存在
            desc = QStringLiteral("切换→%1").arg(target->name);   // 用"切换→层显示名"替换描述文本
    }
    return desc;   // 返回最终描述文本
}

// ============================================================
// updateButtonListItem：按副本刷新左侧指定按钮列表项的文本
// ============================================================
// 右侧表单改动写回副本后调用，保证左侧列表与右侧动作即时一致，
// 无需等重新打开对话框才看到最新描述。文本直接更新到对应 item widget 标签上。
// 【C++ 语法】成员函数定义：void 无返回值
void LayerEditDialog::updateButtonListItem(ControllerButton button) {
    if (!buttonList_) return;   // 列表为空直接返回（单行 if）
    auto it = buttonLabels_.constFind(button);   // 【C++ 语法】auto 推导迭代器类型；constFind 在哈希表查按键，返回只读迭代器
    if (it != buttonLabels_.cend() && it.value()) {   // 【C++ 语法】迭代器比较（未到末尾 cend）且值（标签指针）非空
        it.value()->setText(QStringLiteral("%1   %2").arg(   // 【Qt】setText 更新标签文本
            controllerButtonDisplayName(button), mappingDesc(copy_.getMapping(button))));   // 续行：按键显示名 + 最新映射描述
    }
}

// ============================================================
// itemStyle：生成单个列表项标签的样式
// ============================================================
// 高亮态（当前选中 或 手柄正按下）用与主界面操作层激活按钮一致的
// 青绿色背景 + 白字加粗；普通态透明背景 + 浅灰文字。
// 【C++ 语法】const 成员函数：返回 QString；参数为两个 bool
QString LayerEditDialog::itemStyle(bool selected, bool pressed) const {
    if (selected || pressed)   // 【C++ 语法】|| 逻辑或：当前选中 或 手柄正按下
        return QStringLiteral(   // 返回高亮样式（青绿背景 + 白字加粗）
            "QLabel { background-color: #22958c; color: #ffffff; font-weight: bold;"
            " border-radius: 4px; padding: 5px 8px; }");
    return QStringLiteral(   // 否则返回普通样式（透明背景 + 浅灰文字）
        "QLabel { background-color: transparent; color: #e8eaee;"
        " border-radius: 4px; padding: 5px 8px; }");
}

// ============================================================
// refreshItemStyles：按当前选中项与按下集合刷新全部列表项样式
// ============================================================
// 在选中切换、手柄按键按下/松开时调用，保证每个列表项背景/文字
// 与最新状态一致（item widget 的 QSS 直接生效，不再被 QListWidget 覆盖）。
// 【C++ 语法】成员函数定义：void 无返回值
void LayerEditDialog::refreshItemStyles() {
    if (!buttonList_) return;   // 列表为空直接返回
    // 【C++ 语法】三目运算符 ? : 条件表达式：有选中项则取该项按键的 int 值，否则取 -1；C++17 起 if 初始化语句
    const int selectedId = buttonList_->currentItem()
                               ? buttonList_->currentItem()->data(Qt::UserRole).toInt()
                               : -1;   // 续行：无选中项时取 -1
    // 【C++ 语法】for + 迭代器：从哈希表头 cbegin() 遍历到尾部 cend()；it 为只读迭代器，++it 递增
    for (auto it = buttonLabels_.cbegin(); it != buttonLabels_.cend(); ++it) {
        const ControllerButton b = it.key();   // 【C++ 语法】迭代器访问：key() 取哈希表键（手柄按键）
        QLabel* label = it.value();   // value() 取哈希表值（标签指针）
        if (!label) continue;   // 标签为空则跳过本次循环（continue 提前进入下一次迭代）
        label->setStyleSheet(itemStyle(static_cast<int>(b) == selectedId,   // 参数1：该按键是否为当前选中项
                                       pressedButtons_.contains(b)));   // 参数2：该按键是否在手柄按下集合中（QSet::contains）
    }
}

// ============================================================
// updateParamPage：切换参数页（并保存当前按钮表单）
// ============================================================
// 由动作类型下拉框触发；若是用户操作（非 loading 加载中），
// 则先把当前按钮表单写入副本，保证类型切换不丢数据。
// 【C++ 语法】成员函数定义（槽函数）：参数为 int 动作类型索引
void LayerEditDialog::updateParamPage(int typeIndex) {
    paramStack_->setCurrentIndex(typeIndex);   // 【Qt】setCurrentIndex：堆叠面板切换到指定页
    if (loading_)   // 若正在加载（初始化填充控件阶段），不做保存
        return;
    saveFormFor(currentButton());   // 用户操作：先保存当前按钮表单到副本，避免切换类型丢数据
}

// ============================================================
// accept：确定并回写
// ============================================================
// 1) 保存当前按钮表单；2) 把编辑框里的新层名写入副本；
// 3) 整个副本回写真实层（layer_）并关闭对话框。
// 【C++ 语法】override 关键字：声明覆盖基类 QDialog 的虚函数 accept()，编译器会检查确为重写基类虚函数
void LayerEditDialog::accept() {
    saveFormFor(currentButton());   // 先把当前按钮表单保存进副本
    // 更新层名称
    copy_.name = layerNameEdit_->text().trimmed();   // 【Qt】text() 取输入框文本，trimmed() 去除首尾空白后写入副本层名
    *layer_ = copy_;   // 【C++ 语法】*layer_ 解引用指针：把整个副本赋值回真实层对象（整体写回）
    QDialog::accept();   // 【C++ 语法】调用基类 QDialog 的 accept()：标记对话框结果为确定并关闭
}
