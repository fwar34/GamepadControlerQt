/* =====================================================================
   overlay.js —— 悬浮窗前端逻辑（原生 JS + Tauri invoke）
   每 50ms 轮询 get_overlay_snapshot 渲染；点击标题或按钮切换展开，
   展开时调用 setSize 加高窗口容纳映射列表。
   ===================================================================== */

const { invoke } = window.__TAURI__.core;
const { getCurrentWindow, LogicalSize } = window.__TAURI__.window;

// ---------------------------------------------------------------------
// 前端状态
// ---------------------------------------------------------------------
let expanded = false;
let prevKey = null;

const COLLAPSED_H = 220;
const EXPANDED_H = 480;
const W = 320;

// ---------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------
function esc(s) {
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function $(id) { return document.getElementById(id); }

// ---------------------------------------------------------------------
// 轮询
// ---------------------------------------------------------------------
let ticking = false;
async function tick() {
  if (ticking) return;
  ticking = true;
  try {
    const snap = await invoke('get_overlay_snapshot');
    const key = JSON.stringify(snap) + '|' + expanded;
    if (key !== prevKey) {
      prevKey = key;
      render(snap);
    }
  } catch (e) {
    console.error('overlay tick:', e);
  }
  ticking = false;
}
setInterval(tick, 50);

// ---------------------------------------------------------------------
// 渲染
// ---------------------------------------------------------------------
function render(snap) {
  // 标题
  $('ov-set').textContent = '操作集: ' + esc(snap.set_name);
  $('ov-layer').textContent = '当前层: ' + esc(snap.layer_name);
  $('ov-toggle').textContent = expanded ? '收起' : '展开';

  // 连接状态
  $('ov-dot').className = 'dot ' + (snap.connected ? 'ok' : 'off');
  $('ov-conn').textContent = snap.connected ? '手柄已连接' : '手柄未连接';

  // 按下按键
  if (snap.pressed.length === 0) {
    $('ov-pressed').innerHTML = '<span class="overlay-faint">无按键按下</span>';
  } else {
    $('ov-pressed').innerHTML = snap.pressed.map((p) =>
      '<span class="press-chip">' + esc(p) + '</span>'
    ).join('');
  }

  // L3 锁存警示（边框变橙 + 警示条）
  $('ov-warn').style.display = snap.mouse_toggle ? 'block' : 'none';
  $('overlay-card').style.borderColor = snap.mouse_toggle ? '#f0a34a' : '#7fc9c4';

  // 展开：映射列表
  $('ov-mappings').style.display = expanded ? 'flex' : 'none';
  if (expanded) {
    $('ov-map-title').textContent = '当前层映射: ' + esc(snap.layer_name);
    if (snap.mappings.length === 0) {
      $('ov-map-list').innerHTML = '<span class="overlay-faint">（无映射）</span>';
    } else {
      $('ov-map-list').innerHTML = snap.mappings.map((m) =>
        '<div class="overlay-row">' +
        '<span class="ov-acc' + (m.held ? ' held' : '') + '">' + esc(m.button) + '</span>' +
        '<span class="ov-dim">→</span>' +
        '<span class="ov-text">' + esc(m.desc) + '</span>' +
        '</div>'
      ).join('');
    }
  }
}

// ---------------------------------------------------------------------
// 展开 / 收起（同时调整窗口大小）
// ---------------------------------------------------------------------
async function setExpanded(v) {
  expanded = v;
  const win = getCurrentWindow();
  await win.setSize(new LogicalSize(W, expanded ? EXPANDED_H : COLLAPSED_H));
  prevKey = null;
  try {
    const snap = await invoke('get_overlay_snapshot');
    render(snap);
  } catch (e) {
    console.error('overlay render:', e);
  }
}

// ---------------------------------------------------------------------
// 事件绑定
// ---------------------------------------------------------------------
$('ov-toggle').addEventListener('click', (e) => {
  e.stopPropagation();
  setExpanded(!expanded);
});
// 点击标题区域切换展开/收起（不影响拖动）
$('ov-title').addEventListener('click', (e) => {
  if (e.target === $('ov-toggle')) return;
  setExpanded(!expanded);
});
