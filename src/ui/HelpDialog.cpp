// ============================================================
// HelpDialog.cpp
// 使用说明对话框：展示程序的使用方式与注意点。
// 深色主题（灰底 + 青绿强调），与主窗口风格一致。
// ============================================================

// 【C++ 语法】#include 预处理指令：将本类声明的头文件内容包含进本文件
#include "HelpDialog.h"
// 【C++ 语法】包含本模块自定义的深色标题栏工具函数头文件
#include "DarkTitleBar.h"

// 【Qt】以下头文件来自 Qt Widgets 模块：按钮盒、标签、富文本浏览器、垂直布局
#include <QDialogButtonBox>
#include <QLabel>
#include <QTextBrowser>
#include <QVBoxLayout>

// 【C++ 语法】构造函数定义：HelpDialog::HelpDialog 表示定义 HelpDialog 类的成员函数
HelpDialog::HelpDialog(QWidget* parent)
    : QDialog(parent) {   // 【C++ 语法】冒号后为初始化列表：进入函数体前先调用基类 QDialog 的构造函数，并把父窗口指针 parent 传给它
    setWindowTitle(tr("使用说明"));   // 【Qt】设置窗口标题；tr() 是 Qt 的翻译函数，便于后续国际化
    setMinimumSize(560, 520);   // 【Qt】设置窗口最小尺寸：宽 560、高 520 像素

    auto* root = new QVBoxLayout(this);   // 【C++ 语法】auto 自动推导类型为 QVBoxLayout*；new 在堆上创建对象；【Qt】创建垂直布局管理器，parent 传 this 使其随窗口一起析构
    root->setContentsMargins(16, 16, 16, 16);   // 【Qt】设置布局的内边距（依次为左、上、右、下），单位像素
    root->setSpacing(12);   // 【Qt】设置布局内相邻子控件之间的间距为 12 像素

    auto* title = new QLabel(tr("使用说明"), this);   // 【Qt】创建 QLabel 标签控件，文本为“使用说明”，父对象为 this
    title->setStyleSheet(QStringLiteral(   // 【Qt】为标签设置样式表 QSS；QStringLiteral 在编译期构造字符串，避免运行时重复分配
        "font-size: 17px; font-weight: bold; font-family: \"Microsoft YaHei\"; color: #7fc9c4;"));   // QSS 内容：17 像素加粗微软雅黑字体、青绿色（#7fc9c4）文字
    root->addWidget(title);   // 【Qt】把标题标签加入垂直布局

    auto* browser = new QTextBrowser(this);   // 【Qt】创建 QTextBrowser 富文本浏览器控件，用于展示 HTML 帮助内容
    browser->setOpenExternalLinks(false);   // 【Qt】禁止点击外部链接时调用系统浏览器打开（本帮助没有外链，避免误触发）
    // 【Qt】把帮助正文 HTML 写入浏览器控件；R"(...)" 是 C++11 原始字符串字面量，内部内容不做任何转义
    browser->setHtml(QStringLiteral(R"(
<body style="font-family:'Microsoft YaHei'; font-size:13px; color:#d5d9df;">
  <h3 style="color:#7fc9c4; margin-bottom:4px;">一、快速开始</h3>
  <ul>
    <li>连接手柄后，顶部显示「手柄：已连接」。</li>
    <li>点击「启动映射」开始把手柄输入转换为键盘/鼠标；「停止映射」暂停注入，手柄仍保持轮询，可随时再次启动。</li>
    <li>启停按钮与悬浮窗顶部圆点实时反映映射是否运行。</li>
  </ul>

  <h3 style="color:#7fc9c4; margin-bottom:4px;">二、操作集与层映射</h3>
  <ul>
    <li><b>操作集</b>：最顶层的配置容器，一组完整的映射方案（1 个公共层 + 最多 10 个操作层）。切换操作集时其下所有层整体切换，各操作集之间互不影响。适合为不同场景/角色各配一套方案。</li>
    <li>主窗口左侧「操作集」下拉框可<b>切换</b>当前操作集；【添加】新建空操作集、【复制】复制当前集（可直接改名）、【重命名】、【删除】管理操作集（至少保留一个）。</li>
    <li><b>公共层（Common）</b>：当前操作集内始终生效的兜底层。</li>
    <li><b>操作层</b>：点击层按钮打开该层的映射编辑对话框，配置该层各按键的动作。</li>
    <li>编辑映射：左侧选手柄按键，右侧选动作类型（键盘、鼠标点击/长按、切换层、切换映射、切换屏幕键盘、切换悬浮窗等），可组合最多 3 个修饰键。</li>
    <li><b>切换层</b>：在公共层（或其他层）为某按键设置「切换层」动作，按住即临时进入目标操作层，松开回到公共层。</li>
  </ul>

  <h3 style="color:#7fc9c4; margin-bottom:4px;">三、悬浮窗与托盘</h3>
  <ul>
    <li>悬浮窗：显示当前<b>操作集</b>、当前层与按下的按键；右键展开/收起按键映射列表，左键拖拽移动位置。</li>
    <li>最小化窗口即隐藏到系统托盘；右键托盘图标可显示主界面、启用/停止映射或退出程序。</li>
  </ul>

  <h3 style="color:#7fc9c4; margin-bottom:4px;">四、设置</h3>
  <ul>
    <li>滑块调整右摇杆的<b>死区 / 灵敏度 / 平滑 / 加速度</b>，可翻转视角方向。</li>
    <li>可开启「前台切换时释放按键」：切换窗口时自动释放已注入按键，防止卡键。</li>
  </ul>

  <h3 style="color:#7fc9c4; margin-bottom:4px;">五、注意点</h3>
  <ul>
    <li>程序以<b>管理员权限</b>运行（自动请求）：否则无法向以管理员权限运行的游戏注入输入。</li>
    <li>配置文件位于程序目录下的 <b>steamlike_config.json</b>，修改会自动保存，可自行备份。</li>
    <li>切换层时系统按「已注入状态」精确释放按键，避免层切换导致按键卡住。</li>
    <li>键盘映射需要目标程序窗口处于前台才能收到输入。</li>
  </ul>
</body>
)"));   // 【C++ 语法】结束原始字符串字面量，并结束 setHtml(...) 语句
    root->addWidget(browser, 1);   // 【Qt】把浏览器加入布局，第二个参数 1 是拉伸因子：多出的垂直空间优先分配给浏览器

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);   // 【Qt】创建标准按钮盒，只含“关闭”（Close）按钮；父对象为 this
    connect(buttons, &QDialogButtonBox::rejected, this, &HelpDialog::reject);   // 【Qt】信号槽连接：按钮盒发出 rejected 信号（点击关闭）时，调用本对话框的 reject() 槽关闭窗口
    root->addWidget(buttons);   // 【Qt】把按钮盒加入垂直布局

    // ---- 深色主题（与主窗口一致的灰底 + 青绿强调） ----
    // 【Qt】setStyleSheet 为整个对话框设置 QSS 样式表（内容见下方，字符串内部不插入注释）
    setStyleSheet(R"(
        QDialog {
            background-color: #2b2d31;
        }
        QLabel {
            background-color: transparent;
        }
        QTextBrowser {
            background-color: #2b2d31;
            color: #d5d9df;
            border: 1px solid #40434a;
            border-radius: 8px;
            padding: 8px;
        }
        QPushButton {
            background-color: #3d4147;
            color: #e8eaee;
            border: 1px solid #4a4e55;
            border-radius: 6px;
            padding: 5px 16px;
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
    )");   // 【C++ 语法】结束原始字符串字面量，并结束 setStyleSheet 语句

    // ---- 标题栏深色化，与主窗口一致 ----
    enableDarkTitleBar(this);   // 【Windows API】调用工具函数：通过 DWM 把本对话框的标题栏设置为深色
}
