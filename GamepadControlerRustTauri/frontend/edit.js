/* =====================================================================
   edit.js —— 独立「编辑层」窗口（WebviewWindow "edit-win"）
   层 id 通过 URL hash 传入（edit.html#<layerId>）；主窗口复用该窗口
   时会广播 'edit-layer' 事件携带新的层 id。每 50ms 轮询渲染。
   ===================================================================== */

// 解构出 Tauri invoke（调用后端 Rust 命令）
const { invoke } = window.__TAURI__.core;
// 解构出 getCurrentWindow（用于关闭本窗口）
const { getCurrentWindow } = window.__TAURI__.window;
// 解构出 listen（用于接收主窗口广播的层 id）
const { listen } = window.__TAURI__.event;

// ---------------------------------------------------------------------
// 常量（与后端 core/input_types.rs android_key 一致）
// ---------------------------------------------------------------------
// COMMON_KEYS：可映射的键盘键列表，[Android 键码, 显示名]
const COMMON_KEYS = [
  [51, 'W'], [29, 'A'], [47, 'S'], [32, 'D'], [45, 'Q'], [33, 'E'], // 移动键 WASD / Q E
  [62, 'Space'], [66, 'Enter'], [61, 'Tab'], [111, 'Esc'], // 常用功能键
  [59, 'Shift'], [113, 'Ctrl'], [57, 'Alt'], // 修饰键
  [8, '1'], [9, '2'], [10, '3'], [11, '4'], [12, '5'], // 数字键 1~5
  [131, 'F1'], [132, 'F2'], [133, 'F3'], [134, 'F4'], [135, 'F5'], // 功能键 F1~F5
  [136, 'F6'], [137, 'F7'], [138, 'F8'], [139, 'F9'], [140, 'F10'], // 功能键 F6~F10
];

// KINDS：动作类型列表，[后端枚举名, 中文显示名]
const KINDS = [
  ['keyboard', '键盘'], // 键盘按键
  ['mouse', '鼠标点击'], // 鼠标点击
  ['mousetoggle', '鼠标长按'], // 鼠标长按锁存
  ['wheelup', '滚轮上'], // 鼠标滚轮上滚
  ['wheeldown', '滚轮下'], // 鼠标滚轮下滚
  ['switchlayer', '切层'], // 切换层
  ['lookaround', '视角控制'], // 右摇杆视角控制
  ['mousemove', '鼠标移动'], // 鼠标移动
];

// MOUSE_BUTTONS：可映射的鼠标按键列表，[枚举名, 中文显示名]
const MOUSE_BUTTONS = [
  ['LEFT', '鼠标左键'], // 左键
  ['RIGHT', '鼠标右键'], // 右键
  ['MIDDLE', '鼠标中键'], // 中键
  ['FORWARD', '鼠标前进键'], // 前进键（侧键）
  ['BACK', '鼠标后退键'], // 后退键（侧键）
];

// NO_TARGET_KINDS：无需选择额外目标的动作类型，选中即生效
const NO_TARGET_KINDS = ['wheelup', 'wheeldown', 'lookaround', 'mousemove'];

// ---------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------
// esc：HTML 转义，防止特殊字符破坏页面结构
function esc(s) {
  // 依次替换 & < > " 为对应 HTML 实体
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

// 生成单引号包裹的 JS 字符串字面量，用于双引号 onclick 属性内的实参
// （JSON.stringify 会生成双引号，嵌进双引号 onclick 属性会断裂 HTML，故改用单引号）
function q(s) {
  // 先转义反斜杠，再转义单引号，最后用单引号包裹，得到安全的 JS 字符串字面量
  return "'" + String(s).replace(/\\/g, '\\\\').replace(/'/g, "\\'") + "'";
}

// $：按 id 获取 DOM 元素的简写
function $(id) { return document.getElementById(id); }

// ---------------------------------------------------------------------
// 状态：层 id 取自 URL hash（edit.html#<layerId>）
// ---------------------------------------------------------------------
let editLayerId = decodeURIComponent(location.hash.replace(/^#/, '')) || 'Common'; // 当前编辑的层 id，默认公共层
let selectedButton = 'A'; // 当前选中的手柄按键
let editKind = 'keyboard'; // 当前选中的动作类型
let prevEdit = null; // 上一次渲染内容对比串，避免重复渲染

// 主窗口复用本窗口并切换目标层时，通过事件推送新的层 id
listen('edit-layer', (e) => { // 注册 edit-layer 事件监听
  const id = (e.payload && e.payload.layerId) || 'Common'; // 读取事件携带的层 id，缺省为公共层
  editLayerId = id; // 更新当前编辑的层 id
  selectedButton = 'A'; // 重置选中按键为 A
  editKind = 'keyboard'; // 重置动作类型为键盘
  prevEdit = null; // 清空对比串，强制下一轮重新渲染
  // 读取新层 A 键的现有映射类型
  invoke('get_mapping', { layerId: id, button: 'A' }).then((map) => { // 查询 A 键当前映射
    if (map.has_mapping) editKind = map.kind; // 若已有映射则按原类型显示
    prevEdit = null; // 再次强制下一轮重新渲染
  });
});

// ---------------------------------------------------------------------
// 轮询
// ---------------------------------------------------------------------
let ticking = false; // 轮询重入锁，防止上一次 invoke 未返回时又发起一次
async function tick() {
  if (ticking) return; // 上一次轮询未结束则跳过
  ticking = true; // 标记轮询进行中
  try {
    const info = await invoke('get_layer_edit_snapshot', { layerId: editLayerId }); // 获取当前层编辑快照（按键网格/层名等）
    const map = await invoke('get_mapping', { layerId: editLayerId, button: selectedButton }); // 获取当前选中按键的映射
    const key = JSON.stringify(info) + '|' + JSON.stringify(map); // 组合成对比串
    if (key !== prevEdit) { // 有变化才渲染
      prevEdit = key; // 记录本次内容
      renderEdit(info, map); // 渲染编辑界面
    }
  } catch (e) {
    console.error('edit tick:', e); // 出错打印日志（不中断定时器）
  }
  ticking = false; // 轮询结束，解除锁
}
setInterval(tick, 50); // 每 50ms 轮询一次

// ---------------------------------------------------------------------
// 渲染
// ---------------------------------------------------------------------
function renderEdit(info, map) {
  const btnDisplay = (info.buttons.find((b) => b.name === selectedButton) || {}).display || selectedButton; // 当前选中按键的显示名
  $('edit-title').textContent = '编辑层: ' + info.layer_name; // 标题显示层名
  $('edit-current').textContent = '当前: ' + btnDisplay + ' (' + map.desc + ')'; // 显示当前按键与其映射描述

  // 手柄按钮网格
  $('edit-grid').innerHTML = info.buttons.map((b) => // 遍历所有手柄按键生成网格按钮
    '<div class="pad-btn' + // 按键按钮基础样式
    (b.name === selectedButton ? ' sel' : '') + // 选中的按键加 sel 高亮
    (b.pressed && b.name !== selectedButton ? ' pressed' : '') + '" ' + // 物理按下的其他按键加 pressed 样式
    'onclick="App.selectButton(' + q(b.name) + ')">' + esc(b.display) + '</div>' // 点击切换选中按键（单引号实参）
  ).join('');

  // 动作类型 chips
  $('edit-kinds').innerHTML = KINDS.map(([k, label]) => // 遍历动作类型生成 chip
    '<div class="chip' + (k === editKind ? ' active' : '') + '" ' + // 当前类型加 active 高亮
    'onclick="App.setKind(' + q(k) + ')">' + label + '</div>' // 点击切换动作类型
  ).join('');

  // 目标区
  $('edit-target').innerHTML = targetHTML(info); // 按动作类型渲染目标选择区

  // 子命令
  const subSet = new Set(map.subs); // 已勾选的子命令集合（用于判断 active）
  $('edit-subs').innerHTML = COMMON_KEYS.map(([code, label]) => // 遍历可映射按键生成子命令 chip
    '<div class="chip sub' + (subSet.has(label) ? ' active' : '') + '" ' + // 已勾选的子命令加 active
    'onclick="App.toggleSub(' + code + ')">' + label + '</div>' // 点击切换子命令（数字实参无需引号）
  ).join('');
  $('edit-subs-label').textContent = // 子命令标题，显示当前已选数量
    '子命令（最多3个，当前: ' + (map.subs.length ? map.subs.join(' + ') : '无子命令') + '）';
}

// targetHTML：根据当前动作类型返回目标选择区 HTML
function targetHTML(info) {
  if (editKind === 'keyboard') { // 键盘动作 → 显示按键选择
    return '<div class="dim">选择按键</div><div class="chips-row">' +
      COMMON_KEYS.map(([code, label]) => // 遍历按键生成 chip
        '<div class="chip" onclick="App.setKey(' + code + ')">' + label + '</div>' // 点击设置键盘映射（数字实参）
      ).join('') + '</div>';
  }
  if (editKind === 'mouse' || editKind === 'mousetoggle') { // 鼠标动作 → 显示鼠标键选择
    return '<div class="dim">选择鼠标键</div><div class="chips-row">' +
      MOUSE_BUTTONS.map(([name, label]) => // 遍历鼠标键生成 chip
        '<div class="chip" onclick="App.setMouse(' + q(name) + ')">' + label + '</div>' // 点击设置鼠标映射（单引号实参）
      ).join('') + '</div>';
  }
  if (editKind === 'switchlayer') { // 切层动作 → 显示目标层选择
    return '<div class="dim">选择目标层</div><div class="chips-row">' +
      info.switch_targets.map((t) => // 遍历可切换的目标层生成 chip
        '<div class="chip" onclick="App.setLayer(' + q(t.name) + ')">' + esc(t.display) + '</div>' // 点击设置切层映射
      ).join('') + '</div>';
  }
  return '<div class="faint">该动作无需额外目标，点击上方类型即生效</div>'; // 无需目标的动作提示
}

// ---------------------------------------------------------------------
// 动作
// ---------------------------------------------------------------------
// App：编辑窗口动作集合，供内联 onclick 调用
const App = {
  selectButton(name) { // 切换选中手柄按键
    selectedButton = name; // 更新选中按键
    invoke('get_mapping', { layerId: editLayerId, button: name }).then((map) => { // 查询新按键的映射
      editKind = map.has_mapping ? map.kind : 'keyboard'; // 有映射则按其类型显示，否则默认键盘
      prevEdit = null; // 强制下一轮重新渲染
    });
  },
  setKind(k) { // 切换动作类型
    editKind = k; // 更新动作类型
    if (NO_TARGET_KINDS.includes(k)) { // 若该类型无需额外目标则直接生效
      invoke('set_mapping', { layerId: editLayerId, button: selectedButton, kind: k, keyCode: null, mouseButton: null, layerName: null }); // 直接写入映射
    }
    prevEdit = null; // 强制下一轮重新渲染
  },
  setKey(code) { // 设置键盘映射
    invoke('set_mapping', { layerId: editLayerId, button: selectedButton, kind: 'keyboard', keyCode: code, mouseButton: null, layerName: null }); // 写入键盘键码映射
  },
  setMouse(name) { // 设置鼠标按键映射
    invoke('set_mapping', { layerId: editLayerId, button: selectedButton, kind: editKind, mouseButton: name, keyCode: null, layerName: null }); // 写入鼠标键映射
  },
  setLayer(name) { // 设置切层映射
    invoke('set_mapping', { layerId: editLayerId, button: selectedButton, kind: 'switchlayer', layerName: name, keyCode: null, mouseButton: null }); // 写入目标层映射
  },
  clearMapping() { // 清除当前按键映射
    invoke('clear_mapping', { layerId: editLayerId, button: selectedButton });
  },
  toggleSub(code) { // 切换子命令（追加/移除）
    invoke('toggle_sub', { layerId: editLayerId, button: selectedButton, keyCode: code });
  },
  close() { getCurrentWindow().close(); }, // 关闭本窗口
};

// ---------------------------------------------------------------------
// 静态按钮绑定 + 点击波纹
// ---------------------------------------------------------------------
$('edit-close').addEventListener('click', () => App.close()); // 关闭按钮
$('edit-clear').addEventListener('click', () => App.clearMapping()); // 清除映射按钮

document.addEventListener('pointerdown', (e) => { // 监听指针按下（事件委托，适用于动态生成的元素）
  const el = e.target.closest('.btn, .chip, .pad-btn, .mini'); // 命中可点击元素
  if (!el) return; // 未命中则忽略
  const rect = el.getBoundingClientRect(); // 获取元素位置与尺寸
  const d = Math.max(rect.width, rect.height); // 波纹直径取宽高较大值
  const span = document.createElement('span'); // 创建波纹元素
  span.className = 'ripple'; // 应用波纹样式
  span.style.width = d + 'px'; // 波纹宽度
  span.style.height = d + 'px'; // 波纹高度
  span.style.left = (e.clientX - rect.left - d / 2) + 'px'; // 波纹水平位置（以点击点为圆心）
  span.style.top = (e.clientY - rect.top - d / 2) + 'px'; // 波纹垂直位置
  el.appendChild(span); // 挂入波纹节点
  setTimeout(() => span.remove(), 600); // 动画结束后移除
});

// ---------------------------------------------------------------------
// 窗口显示策略：
//   - 带 ?prewarm=1（应用启动时预热创建）：保持隐藏，用户点击后由 main.js 调用 show() 显示
//   - 不带 prewarm（用户点击后才新建）：首帧渲染完成后自动显示，避免打开瞬间白屏闪动
// ---------------------------------------------------------------------
if (!new URLSearchParams(location.search).has('prewarm')) { // 非预热窗口才自动显示
  (async () => {
    try {
      await tick(); // 立即先执行一轮渲染
    } catch (e) {
      console.error('首帧渲染失败:', e); // 渲染出错打印日志
    }
    getCurrentWindow().show(); // 内容就绪后再显示窗口，杜绝白屏
  })();
  // 兜底：若首帧渲染异常导致未显示，1.5 秒后强制显示，避免窗口"打不开"
  setTimeout(() => getCurrentWindow().show(), 1500);
}
