/* =====================================================================
   edit.js —— 独立「编辑层」窗口（WebviewWindow "edit-win"）
   层 id 通过 URL hash 传入（edit.html#<layerId>）；主窗口复用该窗口
   时会广播 'edit-layer' 事件携带新的层 id。每 50ms 轮询渲染。
   ===================================================================== */

const { invoke } = window.__TAURI__.core;
const { getCurrentWindow } = window.__TAURI__.window;
const { listen } = window.__TAURI__.event;

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
// 状态：层 id 取自 URL hash（edit.html#<layerId>）
// ---------------------------------------------------------------------
let editLayerId = decodeURIComponent(location.hash.replace(/^#/, '')) || 'Common';
let selectedButton = 'A';
let editKind = 'keyboard';
let prevEdit = null;

// 主窗口复用本窗口并切换目标层时，通过事件推送新的层 id
listen('edit-layer', (e) => {
  const id = (e.payload && e.payload.layerId) || 'Common';
  editLayerId = id;
  selectedButton = 'A';
  editKind = 'keyboard';
  prevEdit = null;
  // 读取新层 A 键的现有映射类型
  invoke('get_mapping', { layerId: id, button: 'A' }).then((map) => {
    if (map.has_mapping) editKind = map.kind;
    prevEdit = null;
  });
});

// ---------------------------------------------------------------------
// 轮询
// ---------------------------------------------------------------------
let ticking = false;
async function tick() {
  if (ticking) return;
  ticking = true;
  try {
    const info = await invoke('get_layer_edit_snapshot', { layerId: editLayerId });
    const map = await invoke('get_mapping', { layerId: editLayerId, button: selectedButton });
    const key = JSON.stringify(info) + '|' + JSON.stringify(map);
    if (key !== prevEdit) {
      prevEdit = key;
      renderEdit(info, map);
    }
  } catch (e) {
    console.error('edit tick:', e);
  }
  ticking = false;
}
setInterval(tick, 50);

// ---------------------------------------------------------------------
// 渲染
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
    'onclick="App.selectButton(' + q(b.name) + ')">' + esc(b.display) + '</div>'
  ).join('');

  // 动作类型 chips
  $('edit-kinds').innerHTML = KINDS.map(([k, label]) =>
    '<div class="chip' + (k === editKind ? ' active' : '') + '" ' +
    'onclick="App.setKind(' + q(k) + ')">' + label + '</div>'
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
        '<div class="chip" onclick="App.setMouse(' + q(name) + ')">' + label + '</div>'
      ).join('') + '</div>';
  }
  if (editKind === 'switchlayer') {
    return '<div class="dim">选择目标层</div><div class="chips-row">' +
      info.switch_targets.map((t) =>
        '<div class="chip" onclick="App.setLayer(' + q(t.name) + ')">' + esc(t.display) + '</div>'
      ).join('') + '</div>';
  }
  return '<div class="faint">该动作无需额外目标，点击上方类型即生效</div>';
}

// ---------------------------------------------------------------------
// 动作
// ---------------------------------------------------------------------
const App = {
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
  close() { getCurrentWindow().close(); },
};

// ---------------------------------------------------------------------
// 静态按钮绑定 + 点击波纹
// ---------------------------------------------------------------------
$('edit-close').addEventListener('click', () => App.close());
$('edit-clear').addEventListener('click', () => App.clearMapping());

document.addEventListener('pointerdown', (e) => {
  const el = e.target.closest('.btn, .chip, .pad-btn, .mini');
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
