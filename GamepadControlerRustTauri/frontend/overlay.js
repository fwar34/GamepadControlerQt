/* =====================================================================
   overlay.js —— 悬浮窗前端逻辑（原生 JS + Tauri invoke）
   每 50ms 轮询 get_overlay_snapshot 渲染；点击标题或按钮切换展开，
   展开时调用 setSize 加高窗口容纳映射列表。
   ===================================================================== */

// 解构出 Tauri invoke（调用后端 Rust 命令）
const { invoke } = window.__TAURI__.core;
// 解构出 getCurrentWindow（调整悬浮窗大小）与 LogicalSize（逻辑像素尺寸）
const { getCurrentWindow, LogicalSize } = window.__TAURI__.window;

// ---------------------------------------------------------------------
// 前端状态
// ---------------------------------------------------------------------
let expanded = false; // 悬浮窗是否处于展开状态
let prevKey = null; // 上一次渲染内容对比串，避免重复渲染
let lastFitH = 0; // 上次贴合高度，高度没变就不 setSize，避免频繁调整

const MIN_H = 60;  // 收起最小高度（防御下限；实际由 fitHeight 精确贴合卡片，四边透明带均为 4px）
const MAX_H = 480; // 展开最大高度（超过则映射区内部滚动）
const W = 340;     // 悬浮窗固定宽度

// ---------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------
// esc：HTML 转义，防止特殊字符破坏页面结构
function esc(s) {
  // 依次替换 & < > " 为对应 HTML 实体
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

// $：按 id 获取 DOM 元素的简写
function $(id) { return document.getElementById(id); }

// 字段级更新辅助：值没变就不写 DOM，避免轮询渲染时无意义重绘导致文字闪烁
function setText(id, v) { // 设置文本：相同则跳过
  const el = $(id);
  if (el.textContent !== v) el.textContent = v;
}
function setHTML(id, v) { // 设置 HTML：相同则跳过（避免重建 DOM 闪烁）
  const el = $(id);
  if (el.innerHTML !== v) el.innerHTML = v;
}
function setStyle(id, prop, v) { // 设置内联样式：相同则跳过
  const el = $(id);
  if (el.style[prop] !== v) el.style[prop] = v;
}

// ---------------------------------------------------------------------
// 轮询
// ---------------------------------------------------------------------
let ticking = false; // 轮询重入锁，防止上一次 invoke 未返回时又发起一次
async function tick() {
  if (ticking) return; // 上一次轮询未结束则跳过
  ticking = true; // 标记轮询进行中
  try {
    const snap = await invoke('get_overlay_snapshot'); // 调用后端命令获取悬浮窗快照
    const key = JSON.stringify(snap) + '|' + expanded; // 组合对比串（含展开状态）
    if (key !== prevKey) { // 有变化才渲染
      prevKey = key; // 记录本次内容
      render(snap); // 渲染悬浮窗
    }
  } catch (e) {
    console.error('overlay tick:', e); // 出错打印日志（不中断定时器）
  }
  ticking = false; // 轮询结束，解除锁
}
setInterval(tick, 50); // 每 50ms 轮询一次

// ---------------------------------------------------------------------
// 渲染
// ---------------------------------------------------------------------
function render(snap) {
  // 标题（值相同则跳过，按键时不会闪）
  setText('ov-set', '操作集: ' + esc(snap.set_name)); // 当前操作集名称
  setText('ov-layer', '当前层: ' + esc(snap.layer_name)); // 当前层名称
  setText('ov-toggle', expanded ? '收起' : '展开'); // 展开/收起按钮文案

  // 连接状态
  const dotCls = 'dot ' + (snap.connected ? 'ok' : 'off'); // 状态圆点类名
  if ($('ov-dot').className !== dotCls) $('ov-dot').className = dotCls; // 有变化才改
  setText('ov-conn', snap.connected ? '手柄已连接' : '手柄未连接'); // 连接状态文字

  // 按下按键（仅按键内容变化才重建 chips）
  setHTML('ov-pressed', snap.pressed.length === 0
    ? '<span class="overlay-faint">无按键按下</span>' // 无按键时显示提示
    : snap.pressed.map((p) => '<span class="press-chip">' + esc(p) + '</span>').join(''));

  // L3 锁存警示（边框变橙 + 警示条）；类切换触发边框/光晕过渡动画
  const mouseToggle = !!snap.mouse_toggle; // 是否有鼠标长按锁存
  setStyle('ov-warn', 'display', mouseToggle ? 'block' : 'none'); // 显示/隐藏警示条
  $('overlay-shell').classList.toggle('mouse-toggle', mouseToggle); // 切换外框橙色光晕样式

  // 外框/卡片背景透明度跟随设置（透明度越低越透明，配合透明窗口）
  const op = typeof snap.opacity === 'number' ? snap.opacity : 0.85; // 取快照中的透明度，缺省 0.85
  const bg = 'rgba(43, 45, 49, ' + op + ')'; // 深色背景色（外框与卡片同色，深色区域统一）
  if (document.body.style.background !== bg) document.body.style.background = bg; // body 兜底背景
  setStyle('overlay-shell', 'background', bg); // 外层容器背景（与卡片同色）
  setStyle('overlay-card', 'background', bg); // 内层卡片背景

  // 展开：映射列表
  setStyle('ov-mappings', 'display', expanded ? 'flex' : 'none'); // 展开时显示映射区
  if (expanded) { // 仅展开时渲染映射列表
    setText('ov-map-title', '当前层映射: ' + esc(snap.layer_name)); // 映射列表标题
    setHTML('ov-map-list', snap.mappings.length === 0 // 生成映射行（内容不变则跳过重建）
      ? '<span class="overlay-faint">（无映射）</span>' // 无映射时显示空提示
      : snap.mappings.map((m) => // 遍历每条映射生成行
        '<div class="overlay-row">' + // 一行容器
        '<span class="ov-acc' + (m.held ? ' held' : '') + '">' + esc(m.button) + '</span>' + // 手柄按键名（按住中变橙色）
        '<span class="ov-dim">→</span>' + // 箭头分隔
        '<span class="ov-text">' + esc(m.desc) + '</span>' + // 映射动作描述
        '</div>'
      ).join(''));
  }
  fitHeight(); // 内容变化后贴合窗口高度（有守卫，高度没变不会重复 setSize）
}

async function fitHeight() {
  await new Promise((r) => requestAnimationFrame(r)); // 等布局完成再测量
  const card = $('overlay-card');
  // 卡片高 + 外层 shell 上下深色间距（padding 8px*2 = 16px），窗口高度才贴合
  const h = card.offsetHeight + 16;
  const target = Math.max(MIN_H, Math.min(h, MAX_H)); // 夹在上下限之间
  if (target === lastFitH) return; // 高度没变则跳过
  lastFitH = target;               // 记录本次高度
  await getCurrentWindow().setSize(new LogicalSize(W, target));
}

// ---------------------------------------------------------------------
// 展开 / 收起（同时调整窗口大小）
// ---------------------------------------------------------------------
async function setExpanded(v) {
  expanded = v;             // 更新展开状态
  prevKey = null;           // 强制重绘
  $('ov-mappings').style.opacity = '0'; // 先隐藏映射区（仍占位，可测量高度）
  try {
    const snap = await invoke('get_overlay_snapshot'); // 获取快照
    render(snap);           // 渲染内容（占位，参与布局）
  } catch (e) { console.error('overlay render:', e); }
  await fitHeight();        // 一次性调整到实际内容高度，避免"先拉高再收缩"的底部透明截
  if (expanded) $('ov-mappings').style.opacity = '1'; // 窗口到位后淡入
}

// ---------------------------------------------------------------------
// 事件绑定
// ---------------------------------------------------------------------
$('ov-toggle').addEventListener('click', (e) => { // 展开/收起按钮
  e.stopPropagation(); // 阻止事件冒泡到标题，避免重复触发
  setExpanded(!expanded); // 切换展开状态
});
// 点击标题区域切换展开/收起（不影响拖动）
$('ov-title').addEventListener('click', (e) => { // 标题行点击
  if (e.target === $('ov-toggle')) return; // 点的是展开按钮则跳过（避免重复）
  setExpanded(!expanded); // 切换展开状态
});

$('openApp').addEventListener('click', (e) => {
  e.stopPropagation(); // 阻止事件冒泡到标题，避免重复触发
  invoke('open_app'); // 打开应用
});
