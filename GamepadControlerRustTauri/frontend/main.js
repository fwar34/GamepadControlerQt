/* =====================================================================
   main.js —— 主窗口前端逻辑（原生 JS + Tauri invoke）
   每 50ms 轮询后端快照刷新界面；数据无变化时不重建 DOM 避免闪烁。
   ===================================================================== */

const { invoke } = window.__TAURI__.core;

// ---------------------------------------------------------------------
// 常量（与后端 core/input_types.rs android_key 一致）
// ---------------------------------------------------------------------
const COMMON_KEYS = [
  [51, 'W'], [29, 'A'], [47, 'S'], [32, 'D'], [45, 'Q'], [33, 'E'],
  [62, 'Space'], [66, 'Enter'], [61, 'Tab'], [111, 'Esc'],
  [59, 'Shift'], [113, 'Ctrl'], [57, 'Alt'],
  [8, '1'], [9, '2'], [10, '3'], [11, '4'], [12, '5'],
  [131, 'F1'], [132, 'F2'], [133, 'F3'], [134, 'F4'], [135, 'F5'],
  [136, 'F6'], [137, 'F7'], [138, 'F8'], [139, 'F9'], [140, 'F10'],
];

const KINDS = [
  ['keyboard', '键盘'],
  ['mouse', '鼠标点击'],
  ['mousetoggle', '鼠标长按'],
  ['wheelup', '滚轮上'],
  ['wheeldown', '滚轮下'],
  ['switchlayer', '切层'],
  ['lookaround', '视角控制'],
  ['mousemove', '鼠标移动'],
];

const MOUSE_BUTTONS = [
  ['LEFT', '鼠标左键'],
  ['RIGHT', '鼠标右键'],
  ['MIDDLE', '鼠标中键'],
  ['FORWARD', '鼠标前进键'],
  ['BACK', '鼠标后退键'],
];

const NO_TARGET_KINDS = ['wheelup', 'wheeldown', 'lookaround', 'mousemove'];

const HELP_SECTIONS = [
  ['一、快速上手',
   '1. 连接手柄，点击「开始映射」。\n' +
   '2. 默认配置已含一个「默认操作集」，公共层绑定基础按键（A=空格、B=右键、X=左键、Y=I、菜单键=Esc、视图键=M）。\n' +
   '3. 右摇杆 = 视角控制，左摇杆 = WASD 移动。'],
  ['二、操作集与层映射',
   '操作集是最高层容器，每个操作集内包含 1 个公共层 + 最多 10 个操作层。\n' +
   '切换操作集时，其下所有层整体切换（适合不同游戏/场景一键切换整套配置）。\n' +
   '• 添加：新建空操作集（默认名可再改）。\n' +
   '• 复制：把当前操作集整体复制为新操作集，可直接改名。\n' +
   '• 重命名：修改当前操作集的自定义名字。\n' +
   '• 删除：至少保留一个操作集。\n' +
   '悬浮窗始终显示当前操作集名称。'],
  ['三、层切换机制',
   '操作层由公共层的「切层」(SwitchLayer) 映射驱动：按住切层键激活目标层，松开自动回退。\n' +
   '按键查询顺序：最后激活的操作层 → 较早的操作层 → 公共层（兜底）。'],
  ['四、悬浮窗',
   '「显示悬浮窗」打开置顶透明信息窗，实时显示：当前操作集、当前层、连接状态、按下的手柄按键。\n' +
   '当鼠标长按锁存（MouseToggle）激活时，悬浮窗边框变橙色并显示警示。'],
  ['五、配置文件',
   '配置文件 steamlike_config.json 位于程序同目录，绿色便携。与安卓版格式兼容（version=2）。'],
];

// ---------------------------------------------------------------------
// 前端状态
// ---------------------------------------------------------------------
let view = 'main';             // 'main' | 'edit' | 'help'
let selectedButton = 'A';
let editLayerId = 'Common';
let editKind = 'keyboard';
let overlayVisible = false;
let renameMode = null;         // 'rename' | 'copy' | null
let renameSetId = null;
let renameValue = '';
let activeSetId = '';
let activeSetName = '';
let running = false;
let overlayOpacity = 0.85;   // 悬浮窗透明度（0.2 ~ 1.0），与后端一致

let prevMain = null;
let prevEdit = null;

// ---------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------
function esc(s) {
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function $(id) { return document.getElementById(id); }

function showView(v) {
  view = v;
  $('view-main').style.display = v === 'main' ? 'flex' : 'none';
  $('view-edit').style.display = v === 'edit' ? 'flex' : 'none';
  $('view-help').style.display = v === 'help' ? 'flex' : 'none';
  prevMain = null;
  prevEdit = null;
  if (v === 'help') renderHelp();
}

// ---------------------------------------------------------------------
// 轮询
// ---------------------------------------------------------------------
let ticking = false;
async function tick() {
  if (ticking) return;
  ticking = true;
  try {
    if (view === 'main') {
      const snap = await invoke('get_snapshot');
      const key = JSON.stringify(snap);
      if (key !== prevMain) {
        prevMain = key;
        renderMain(snap);
      }
    } else if (view === 'edit') {
      const info = await invoke('get_layer_edit_snapshot', { layerId: editLayerId });
      const map = await invoke('get_mapping', { layerId: editLayerId, button: selectedButton });
      const key = JSON.stringify(info) + '|' + JSON.stringify(map);
      if (key !== prevEdit) {
        prevEdit = key;
        renderEdit(info, map);
      }
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
    'onclick="App.switchSet(' + JSON.stringify(s.id) + ')">' + esc(s.name) + '</div>'
  ).join('');

  // 层列表
  const layers = [{ id: 'Common', name: '公共层', active: false }].concat(snap.layers);
  $('layer-list').innerHTML = layers.map((l) =>
    '<div class="layer-btn' + (l.active ? ' active' : '') + '" ' +
    'onclick="App.openEdit(' + JSON.stringify(l.id) + ')">' + esc(l.name) + '</div>'
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
// 层编辑视图渲染
// ---------------------------------------------------------------------
function renderEdit(info, map) {
  const btnDisplay = (info.buttons.find((b) => b.name === selectedButton) || {}).display || selectedButton;
  $('edit-title').textContent = '编辑层: ' + info.layer_name;
  $('edit-current').textContent = '当前: ' + btnDisplay + ' (' + map.desc + ')';

  // 手柄按钮网格
  $('edit-grid').innerHTML = info.buttons.map((b) =>
    '<div class="pad-btn' +
    (b.name === selectedButton ? ' sel' : '') +
    (b.pressed && b.name !== selectedButton ? ' pressed' : '') + '" ' +
    'onclick="App.selectButton(' + JSON.stringify(b.name) + ')">' + esc(b.display) + '</div>'
  ).join('');

  // 动作类型 chips
  $('edit-kinds').innerHTML = KINDS.map(([k, label]) =>
    '<div class="chip' + (k === editKind ? ' active' : '') + '" ' +
    'onclick="App.setKind(' + JSON.stringify(k) + ')">' + label + '</div>'
  ).join('');

  // 目标区
  $('edit-target').innerHTML = targetHTML(info);

  // 子命令
  const subSet = new Set(map.subs);
  $('edit-subs').innerHTML = COMMON_KEYS.map(([code, label]) =>
    '<div class="chip sub' + (subSet.has(label) ? ' active' : '') + '" ' +
    'onclick="App.toggleSub(' + code + ')">' + label + '</div>'
  ).join('');
  $('edit-subs-label').textContent =
    '子命令（最多3个，当前: ' + (map.subs.length ? map.subs.join(' + ') : '无子命令') + '）';
}

function targetHTML(info) {
  if (editKind === 'keyboard') {
    return '<div class="dim">选择按键</div><div class="chips-row">' +
      COMMON_KEYS.map(([code, label]) =>
        '<div class="chip" onclick="App.setKey(' + code + ')">' + label + '</div>'
      ).join('') + '</div>';
  }
  if (editKind === 'mouse' || editKind === 'mousetoggle') {
    return '<div class="dim">选择鼠标键</div><div class="chips-row">' +
      MOUSE_BUTTONS.map(([name, label]) =>
        '<div class="chip" onclick="App.setMouse(' + JSON.stringify(name) + ')">' + label + '</div>'
      ).join('') + '</div>';
  }
  if (editKind === 'switchlayer') {
    return '<div class="dim">选择目标层</div><div class="chips-row">' +
      info.switch_targets.map((t) =>
        '<div class="chip" onclick="App.setLayer(' + JSON.stringify(t.name) + ')">' + esc(t.display) + '</div>'
      ).join('') + '</div>';
  }
  return '<div class="faint">该动作无需额外目标，点击上方类型即生效</div>';
}

// ---------------------------------------------------------------------
// 帮助视图
// ---------------------------------------------------------------------
function renderHelp() {
  $('help-sections').innerHTML = HELP_SECTIONS.map(([title, body]) =>
    '<div class="help-section">' +
    '<div class="help-section-title">' + esc(title) + '</div>' +
    '<div class="help-section-body">' + esc(body) + '</div>' +
    '</div>'
  ).join('');
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
  toggleMapping() {
    invoke(running ? 'stop_mapping' : 'start_mapping');
  },
  saveConfig() { invoke('save_config'); },
  resetConfig() { invoke('reset_config'); },

  // ---- 悬浮窗 / 退出 ----
  toggleOverlay() {
    invoke('toggle_overlay').then((v) => { overlayVisible = v; });
  },
  quit() { invoke('quit_app'); },

  // ---- 设置 ----
  adjust(key, delta) { invoke('adjust_setting', { key, delta }); },

  // ---- 悬浮窗 ----
  adjustOpacity(delta) {
    overlayOpacity = Math.min(1, Math.max(0.2, overlayOpacity + delta));
    const v = $('opacity-val');
    if (v) v.textContent = Math.round(overlayOpacity * 100) + '%';
    invoke('set_overlay_opacity', { opacity: overlayOpacity });
  },

  // ---- 视图切换 ----
  openEdit(id) {
    editLayerId = id;
    selectedButton = 'A';
    editKind = 'keyboard';
    showView('edit');
    // 读取选中按钮的现有映射类型
    invoke('get_mapping', { layerId: id, button: 'A' }).then((map) => {
      if (map.has_mapping) editKind = map.kind;
      prevEdit = null;
    });
  },
  backToMain() { showView('main'); },
  openHelp() { showView('help'); },

  // ---- 层编辑 ----
  selectButton(name) {
    selectedButton = name;
    invoke('get_mapping', { layerId: editLayerId, button: name }).then((map) => {
      editKind = map.has_mapping ? map.kind : 'keyboard';
      prevEdit = null;
    });
  },
  setKind(k) {
    editKind = k;
    if (NO_TARGET_KINDS.includes(k)) {
      invoke('set_mapping', { layerId: editLayerId, button: selectedButton, kind: k, keyCode: null, mouseButton: null, layerName: null });
    }
    prevEdit = null;
  },
  setKey(code) {
    invoke('set_mapping', { layerId: editLayerId, button: selectedButton, kind: 'keyboard', keyCode: code, mouseButton: null, layerName: null });
  },
  setMouse(name) {
    invoke('set_mapping', { layerId: editLayerId, button: selectedButton, kind: editKind, mouseButton: name, keyCode: null, layerName: null });
  },
  setLayer(name) {
    invoke('set_mapping', { layerId: editLayerId, button: selectedButton, kind: 'switchlayer', layerName: name, keyCode: null, mouseButton: null });
  },
  clearMapping() {
    invoke('clear_mapping', { layerId: editLayerId, button: selectedButton });
  },
  toggleSub(code) {
    invoke('toggle_sub', { layerId: editLayerId, button: selectedButton, keyCode: code });
  },
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
$('edit-back').addEventListener('click', () => App.backToMain());
$('edit-clear').addEventListener('click', () => App.clearMapping());
$('help-back').addEventListener('click', () => App.backToMain());

// 首次渲染
showView('main');
