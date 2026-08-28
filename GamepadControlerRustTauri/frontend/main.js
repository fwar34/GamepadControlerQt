/* =====================================================================
   main.js —— 主窗口前端逻辑（原生 JS + Tauri invoke）
   每 50ms 轮询后端快照刷新界面；数据无变化时不重建 DOM 避免闪烁。
   层编辑、使用说明均为独立窗口（edit.html / help.html），
   由本文件通过 WebviewWindow 创建并复用。
   ===================================================================== */

const { invoke } = window.__TAURI__.core;
const { WebviewWindow } = window.__TAURI__.webviewWindow;
const { emit } = window.__TAURI__.event;

// ---------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------
function esc(s) {
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

// 生成单引号包裹的 JS 字符串字面量，用于双引号 onclick 属性内的实参
function q(s) {
  return "'" + String(s).replace(/\\/g, '\\\\').replace(/'/g, "\\'") + "'";
}

function $(id) { return document.getElementById(id); }

// ---------------------------------------------------------------------
// 前端状态
// ---------------------------------------------------------------------
let overlayVisible = false;
let renameMode = null;         // 'rename' | 'copy' | null
let renameSetId = null;
let renameValue = '';
let activeSetId = '';
let activeSetName = '';
let running = false;
let overlayOpacity = 0.85;     // 悬浮窗透明度（0.2 ~ 1.0），与后端一致

let prevMain = null;

// ---------------------------------------------------------------------
// 轮询
// ---------------------------------------------------------------------
let ticking = false;
async function tick() {
  if (ticking) return;
  ticking = true;
  try {
    const snap = await invoke('get_snapshot');
    const key = JSON.stringify(snap);
    if (key !== prevMain) {
      prevMain = key;
      renderMain(snap);
    }
  } catch (e) {
    console.error('tick:', e);
  }
  ticking = false;
}
setInterval(tick, 50);

// ---------------------------------------------------------------------
// 主视图渲染
// ---------------------------------------------------------------------
function renderMain(snap) {
  running = snap.running;
  activeSetId = snap.active_set_id;
  activeSetName = snap.active_set_name;

  // 状态
  const statusText = snap.connected
    ? (snap.running ? '● 已连接 · 映射运行中' : '● 已连接 · 已停止')
    : '○ 手柄未连接';
  $('status-text').textContent = statusText;
  $('status-dot').className = 'dot ' + (snap.connected ? 'ok' : 'off');

  // 重命名输入行
  renderRenameRow();

  // 操作集 chips
  $('set-chips').innerHTML = snap.sets.map((s) =>
    '<div class="chip' + (s.id === snap.active_set_id ? ' active' : '') + '" ' +
    'onclick="App.switchSet(' + q(s.id) + ')">' + esc(s.name) + '</div>'
  ).join('');

  // 层列表（点击打开独立「编辑层」窗口）
  const layers = [{ id: 'Common', name: '公共层', active: false }].concat(snap.layers);
  $('layer-list').innerHTML = layers.map((l) =>
    '<div class="layer-btn' + (l.active ? ' active' : '') + '" ' +
    'onclick="App.openEdit(' + q(l.id) + ')">' + esc(l.name) + '</div>'
  ).join('');

  // 当前信息
  $('info-col').innerHTML =
    '<div class="info-line">当前操作集: ' + esc(snap.active_set_name) + '</div>' +
    '<div class="info-line">当前层: ' + esc(snap.layer_name) + '</div>' +
    '<div class="info-sub" style="color:' + (snap.mouse_toggle ? '#f0a34a' : '#6c727c') + '">' +
    (snap.mouse_toggle ? esc(snap.mouse_toggle) : '无长按锁存') + '</div>';

  // 全局设置
  const settings = [
    ['死区', snap.deadzone, 'deadzone'],
    ['视角灵敏度', snap.look_sensitivity, 'look_sensitivity'],
    ['视角平滑', snap.look_smoothing, 'look_smoothing'],
    ['视角加速', snap.look_acceleration, 'look_acceleration'],
  ];
  $('settings-col').innerHTML = settings.map(([label, val, key]) =>
    '<div class="slider-row">' +
    '<div class="slider-label">' + label + '</div>' +
    '<button class="mini" onclick="App.adjust(\'' + key + '\',-0.01)">−</button>' +
    '<div class="slider-val">' + val.toFixed(2) + '</div>' +
    '<button class="mini" onclick="App.adjust(\'' + key + '\',0.01)">+</button>' +
    '</div>'
  ).join('');

  // 开始/停止 + 悬浮窗按钮
  const toggle = $('btn-toggle');
  toggle.textContent = snap.running ? '停止映射' : '开始映射';
  toggle.className = 'btn-toggle ' + (snap.running ? 'running' : 'stopped');
  $('btn-overlay').textContent = overlayVisible ? '关闭悬浮窗' : '显示悬浮窗';
}

function renderRenameRow() {
  const rr = $('rename-row');
  if (renameMode) {
    rr.style.display = 'flex';
    $('rename-hint').textContent = renameMode === 'rename' ? '重命名操作集' : '复制为新操作集';
    const inp = $('rename-input');
    if (document.activeElement !== inp) inp.value = renameValue;
  } else {
    rr.style.display = 'none';
  }
}

// ---------------------------------------------------------------------
// 独立窗口：编辑层 / 使用说明
// ---------------------------------------------------------------------
// 编辑层窗口：固定标签 'edit-win'。首次打开通过 URL hash 传入层 id
// （edit.html#<layerId>）；窗口已存在时复用，并用 'edit-layer' 事件
// 推送新的层 id，避免整窗刷新。
function openEditWindow(id) {
  WebviewWindow.getByLabel('edit-win').then((existing) => {
    if (existing) {
      existing.setFocus();
      emit('edit-layer', { layerId: id });
    } else {
      const win = new WebviewWindow('edit-win', {
        url: 'edit.html#' + encodeURIComponent(id),
        title: '编辑层',
        width: 780,
        height: 620,
        minWidth: 680,
        minHeight: 520,
        center: true,
      });
      win.once('tauri://error', (e) => console.error('编辑窗口创建失败:', e));
    }
  });
}

function openHelpWindow() {
  WebviewWindow.getByLabel('help-win').then((existing) => {
    if (existing) {
      existing.setFocus();
      return;
    }
    const win = new WebviewWindow('help-win', {
      url: 'help.html',
      title: '使用说明',
      width: 660,
      height: 740,
      minWidth: 500,
      minHeight: 420,
      center: true,
    });
    win.once('tauri://error', (e) => console.error('使用说明窗口创建失败:', e));
  });
}

// ---------------------------------------------------------------------
// 动作
// ---------------------------------------------------------------------
const App = {
  // ---- 操作集 ----
  switchSet(id) { invoke('switch_operation_set', { setId: id }); },
  addSet() { invoke('add_operation_set'); },
  startCopy() {
    renameMode = 'copy';
    renameSetId = activeSetId;
    renameValue = activeSetName + ' - 副本';
    renderRenameRow();
    $('rename-input').focus();
    $('rename-input').select();
  },
  startRename() {
    renameMode = 'rename';
    renameSetId = activeSetId;
    renameValue = '';
    renderRenameRow();
    $('rename-input').focus();
  },
  commitRename() {
    const name = renameValue.trim();
    if (!name || !renameMode) return;
    if (renameMode === 'rename') {
      invoke('rename_operation_set', { setId: renameSetId, name });
    } else {
      invoke('copy_operation_set', { setId: renameSetId, name });
    }
    renameMode = null;
    renderRenameRow();
  },
  cancelRename() { renameMode = null; renderRenameRow(); },
  deleteSet() { invoke('delete_operation_set', { setId: activeSetId }); },

  // ---- 启停 / 配置 ----
  toggleMapping() { invoke(running ? 'stop_mapping' : 'start_mapping'); },
  saveConfig() { invoke('save_config'); },
  resetConfig() { invoke('reset_config'); },

  // ---- 悬浮窗 / 退出 ----
  toggleOverlay() { invoke('toggle_overlay').then((v) => { overlayVisible = v; }); },
  quit() { invoke('quit_app'); },

  // ---- 设置 ----
  adjust(key, delta) { invoke('adjust_setting', { key, delta }); },
  adjustOpacity(delta) {
    overlayOpacity = Math.min(1, Math.max(0.2, overlayOpacity + delta));
    const v = $('opacity-val');
    if (v) v.textContent = Math.round(overlayOpacity * 100) + '%';
    invoke('set_overlay_opacity', { opacity: overlayOpacity });
  },

  // ---- 独立窗口 ----
  openEdit(id) { openEditWindow(id); },
  openHelp() { openHelpWindow(); },
};

// ---------------------------------------------------------------------
// 静态按钮绑定
// ---------------------------------------------------------------------
$('btn-add').addEventListener('click', () => App.addSet());
$('btn-copy').addEventListener('click', () => App.startCopy());
$('btn-rename').addEventListener('click', () => App.startRename());
$('btn-delete').addEventListener('click', () => App.deleteSet());
$('btn-toggle').addEventListener('click', () => App.toggleMapping());
$('btn-save').addEventListener('click', () => App.saveConfig());
$('btn-reset').addEventListener('click', () => App.resetConfig());
$('btn-overlay').addEventListener('click', () => App.toggleOverlay());
$('btn-help').addEventListener('click', () => App.openHelp());
$('btn-quit').addEventListener('click', () => App.quit());
$('rename-ok').addEventListener('click', () => App.commitRename());
$('rename-cancel').addEventListener('click', () => App.cancelRename());
$('rename-input').addEventListener('input', (e) => { renameValue = e.target.value; });
$('rename-input').addEventListener('keydown', (e) => {
  if (e.key === 'Enter') App.commitRename();
  if (e.key === 'Escape') App.cancelRename();
});

// ---------------------------------------------------------------------
// 点击波纹动效（全局委托：按钮 / chip / 层 / 手柄按键 / 设置小按钮）
// ---------------------------------------------------------------------
document.addEventListener('pointerdown', (e) => {
  const el = e.target.closest('.btn, .chip, .layer-btn, .pad-btn, .mini, .btn-toggle, .ov-toggle');
  if (!el) return;
  const rect = el.getBoundingClientRect();
  const d = Math.max(rect.width, rect.height);
  const span = document.createElement('span');
  span.className = 'ripple';
  span.style.width = d + 'px';
  span.style.height = d + 'px';
  span.style.left = (e.clientX - rect.left - d / 2) + 'px';
  span.style.top = (e.clientY - rect.top - d / 2) + 'px';
  el.appendChild(span);
  setTimeout(() => span.remove(), 600);
});
