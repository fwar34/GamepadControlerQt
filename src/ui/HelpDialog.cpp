// ============================================================
// HelpDialog.cpp
// 使用说明对话框：展示程序的使用方式与注意点。
// 深色主题（灰底 + 青绿强调），与主窗口风格一致。
// ============================================================

#include "HelpDialog.h"
#include "DarkTitleBar.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QTextBrowser>
#include <QVBoxLayout>

HelpDialog::HelpDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("使用说明"));
    setMinimumSize(560, 520);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* title = new QLabel(tr("使用说明"), this);
    title->setStyleSheet(QStringLiteral(
        "font-size: 17px; font-weight: bold; font-family: \"DengXian\"; color: #7fc9c4;"));
    root->addWidget(title);

    auto* browser = new QTextBrowser(this);
    browser->setOpenExternalLinks(false);
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
)"));
    root->addWidget(browser, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &HelpDialog::reject);
    root->addWidget(buttons);

    // ---- 深色主题（与主窗口一致的灰底 + 青绿强调） ----
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
    )");

    // ---- 标题栏深色化，与主窗口一致 ----
    enableDarkTitleBar(this);
}
