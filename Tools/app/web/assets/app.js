import {RosClient} from './ros-client.js';
const $ = id => document.getElementById(id);
const client = new RosClient();
const boards = {esp32s3:{name:'ESP32-S3', label:'ESPRESSIF / MCU 01'}, stm32:{name:'STM32', label:'ST MICROELECTRONICS / MCU 02'}};
const activeGoals = new Map();
const logLines = [];
let ready = false, connecting = false, connectTimer;
let agent = {state:'unchecked', owned:false};
let graphTopics = [];
const descriptions = {overview:['设备与控制','查看设备状态，直接与每块板卡交互。'], sensors:['传感器','订阅标准传感器话题，查看实时数据。'], graph:['ROS 图','检查已发现的节点、话题和服务。'], logs:['事件记录','查看连接、请求和设备通信的运行记录。']};

for (const [key, board] of Object.entries(boards)) {
  const card = document.createElement('article'); card.className = 'device-card';
  card.innerHTML = `<div class="device-top"><div><div class="device-label">${board.label}</div><div class="device-name">${board.name}</div></div><span class="badge" id="${key}-state">等待心跳</span></div><div class="device-stats">${[['heartbeat','HEARTBEAT'],['echo','ECHO'],['peer_received','PEER RX'],['roundtrip','ROUNDTRIP']].map(([field,label]) => `<div><div class="stat-name">${label}</div><div class="stat-value" id="${key}-${field}">—</div></div>`).join('')}</div><div class="device-diagnostics" id="${key}-diagnostics">Service — · Action —</div>`;
  $('devices').append(card);
}
try { $('endpoint').value = localStorage.getItem('ros-endpoint') || ''; } catch {}
if (!$('endpoint').value) $('endpoint').value = `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`;

function log(message) {
  logLines.push(`${new Date().toLocaleTimeString('zh-CN', {hour12:false})}  ${message}`);
  if (logLines.length > 200) logLines.shift();
  $('log-view').textContent = logLines.join('\n');
}
function notice(message) { $('notice').textContent = message; $('notice').hidden = !message; }
function updateControls() {
  document.querySelectorAll('[data-ros]').forEach(button => button.disabled = !ready);
  $('agent-stop').disabled = !ready || !agent.owned || agent.state !== 'running';
  const goal = activeGoals.get($('board').value);
  $('action-send').disabled = !ready || !!goal;
  $('cancel').disabled = !ready || !goal?.accepted;
}
function connectionState(text, online = false) {
  $('connection-state').textContent = text; $('connection-state').className = `badge ${online?'online':''}`;
  $('sidebar-status').textContent = text; $('sidebar-dot').className = `dot ${online?'online':''}`;
  $('connection-note').textContent = online ? 'ROS 后端已就绪' : text;
  $('connect').textContent = ready || connecting ? '断开连接' : '连接工作台';
  updateControls();
}
function integer(id, bits) {
  const value = $(id).value.trim();
  if (!/^[+-]?\d+$/.test(value)) throw new Error('请输入十进制整数');
  const number = BigInt(value), limit = 1n << BigInt(bits-1);
  if (number < -limit || number >= limit) throw new Error(`数值超出 Int${bits} 范围`);
  return number.toString();
}
async function request(op, values = {}) { return await client.request(op, values).promise; }
async function run(task) { try { await task(); } catch (error) { notice(error.message); log(`错误 · ${error.message}`); } }
function reset() {
  ready = false; connecting = false; clearTimeout(connectTimer);
  activeGoals.clear(); agent = {state:'unchecked', owned:false};
  for (const [key,board] of Object.entries(boards)) {
    board.lastHeartbeat = 0; board.fields = {};
    for (const field of ['heartbeat','echo','peer_received','roundtrip']) $(`${key}-${field}`).textContent = '—';
    $(`${key}-diagnostics`).textContent = 'Service — · Action —';
  }
  $('agent-state').textContent = '未检查'; $('agent-state').className = 'badge';
  $('agent-detail').textContent = '连接后检查通信服务';
  $('domain-info').textContent = '等待后端连接';
  $('sensor-view').textContent = '等待传感器数据'; $('sensor-meta').textContent = '尚未订阅';
  graphTopics = []; renderGraph({nodes:[], topics:[], services:[]});
  $('action-result').textContent = '等待目标';
  connectionState('未连接'); renderOnline();
}
$('connect').addEventListener('click', () => {
  if (ready || connecting) { client.disconnect(); return; }
  run(async () => {
    const url = new URL($('endpoint').value);
    if (!['ws:','wss:'].includes(url.protocol)) throw new Error('服务地址必须以 ws:// 或 wss:// 开头');
    notice(''); connecting = true; connectionState('正在连接');
    try { localStorage.setItem('ros-endpoint', url.href); } catch {}
    client.connect(url.href);
    connectTimer = setTimeout(() => { client.disconnect(); notice('连接超时，请检查后端服务和地址'); }, 22000);
  });
});
client.addEventListener('disconnected', () => { reset(); log('连接已断开'); });
client.addEventListener('connection-error', () => { notice('无法连接，请确认后端服务已启动'); });
client.addEventListener('event', ({detail:event}) => {
  if (event.event === 'ready') {
    clearTimeout(connectTimer); ready = true; connecting = false; notice(''); connectionState('已连接', true);
    $('domain-info').textContent = `ROS Domain ${event.domain} · ${event.boards.join(' / ')}`;
    log(`ROS 后端已就绪 · Domain ${event.domain}`);
    run(async () => {
      renderGraph(await request('graph'));
      if ($('auto-agent').checked) await request('agent_start', {port:Number($('agent-port').value)});
      else await request('agent_status');
    });
  } else if (event.event === 'telemetry') {
    const board = boards[event.board]; if (!board) return;
    board.fields ||= {}; board.fields[event.field] = event.value;
    if (event.field === 'heartbeat') board.lastHeartbeat = Date.now();
    const value = $(`${event.board}-${event.field}`); if (value) value.textContent = event.value;
    $(`${event.board}-diagnostics`).textContent = `Service ${board.fields.service_result??'—'} · Action FB ${board.fields.action_feedback??'—'} · 值 ${board.fields.action_result??'—'} · 状态 ${board.fields.action_status??'—'}`;
    renderOnline();
  } else if (event.event === 'agent') {
    agent = event;
    const names = {unchecked:'未检查',starting:'启动中',running:'运行中',stopping:'停止中',stopped:'已停止',error:'异常'};
    $('agent-state').textContent = names[event.state] || event.state;
    $('agent-state').className = `badge ${event.state==='running'?'online':event.state==='error'?'error':''}`;
    const ownership = event.owned?'后端创建':event.verified?'复用外部实例':'尚未确认运行实例';
    $('agent-detail').textContent = `UDP ${event.port} · ${ownership}${event.error?' · '+event.error:''}`;
    updateControls(); if (event.error) notice(event.error);
    log(`Agent · ${names[event.state]} · UDP ${event.port}`);
  } else if (event.event === 'sensor') {
    $('sensor-meta').textContent = `${event.topic} · ${event.type} · ${new Date().toLocaleTimeString()}`;
    $('sensor-view').textContent = JSON.stringify(event.data, null, 2);
  } else if (event.event === 'action_accepted' || event.event === 'action_feedback') {
    const goal = [...activeGoals.values()].find(goal => goal.id === event.id);
    if (goal) {
      goal.accepted = true;
      if (goal.board === $('board').value) $('action-result').textContent = event.event === 'action_accepted' ? '目标已接受' : `反馈：${event.sequence.join(', ')}`;
      updateControls();
    }
  } else if (event.event === 'backend_error') {
    reset(); client.disconnect(); notice(event.error); log(event.error);
  } else if (event.event === 'agent_log' || event.event === 'log') { log(event.message); }
});
function renderOnline() {
  for (const [key, board] of Object.entries(boards)) {
    const online = ready && Date.now() - (board.lastHeartbeat || 0) < 3500;
    $(`${key}-state`).textContent = online ? '在线' : '等待心跳';
    $(`${key}-state`).className = `badge ${online?'online':''}`;
  }
}
setInterval(() => { renderOnline(); $('clock').textContent = new Date().toLocaleString('zh-CN',{hour12:false}); }, 500);
document.querySelectorAll('[data-page]').forEach(button => button.addEventListener('click', () => {
  const page = button.dataset.page;
  document.querySelectorAll('.page').forEach(section => section.hidden = section.id !== page);
  document.querySelectorAll('[data-page]').forEach(item => item.classList.toggle('selected', item === button));
  $('page-label').textContent = $('page-title').textContent = descriptions[page][0];
  $('page-description').textContent = descriptions[page][1];
}));
$('board').addEventListener('change', () => { $('action-result').textContent = activeGoals.has($('board').value)?'任务进行中':'等待目标'; updateControls(); });
function form(id, task) { $(id).addEventListener('submit', event => { event.preventDefault(); run(task); }); }
form('publish-form', async () => {
  const board = $('board').value, value = integer('command-value',32);
  await request('publish', {board,value}); $('publish-result').textContent = `${board} · 已发布 ${value}`; log(`${board} · command = ${value}`);
});
form('service-form', async () => {
  const board = $('board').value;
  $('service-result').textContent = '请求中…';
  try { const result = await request('service',{board,a:integer('operand-a',64),b:integer('operand-b',64)}); $('service-result').textContent = `${board} · ${result.sum}`; log(`${board} · Service 求和 = ${result.sum}`); }
  catch (error) { $('service-result').textContent = error.message; throw error; }
});
form('action-form', async () => {
  const board = $('board').value;
  if (activeGoals.has(board)) throw new Error('当前板卡已有任务');
  const outgoing = client.request('action',{board,order:Number($('order').value)});
  const goal = {id:outgoing.id,board,accepted:false}; activeGoals.set(board, goal);
  $('action-result').textContent = '等待接受…'; updateControls();
  try {
    const result = await outgoing.promise;
    const text = !result.accepted?'目标被拒绝':`${({4:'成功',5:'已取消',6:'已中止'})[result.status]||'任务结束'} · [${result.sequence.join(', ')}]`;
    if ($('board').value === board) $('action-result').textContent = text;
    log(`${board} · Action ${text}`);
  } catch (error) { if ($('board').value === board) $('action-result').textContent = error.message; throw error; }
  finally { if (activeGoals.get(board) === goal) activeGoals.delete(board); updateControls(); }
});
$('cancel').addEventListener('click', () => run(async () => {
  const board = $('board').value, goal = activeGoals.get(board);
  if (goal) { const result = await request('cancel',{board,goal_id:goal.id}); $('action-result').textContent = result.cancel_requested?'已请求取消':'取消未被接受'; }
}));
$('agent-start').addEventListener('click', () => run(() => request('agent_start',{port:Number($('agent-port').value)})));
$('agent-stop').addEventListener('click', () => run(() => request('agent_stop')));
function renderGraph(graph) {
  graphTopics = graph.topics;
  $('graph-summary').textContent = `${graph.nodes.length} 个节点 · ${graph.topics.length} 个话题 · ${graph.services.length} 个服务`;
  $('nodes').replaceChildren(...graph.nodes.map(name => { const item = document.createElement('span'); item.textContent = name; return item; }));
  for (const [id,rows] of [['topics',graph.topics.map(topic=>[topic.name,topic.types])],['services',graph.services]]) {
    $(id).replaceChildren(...rows.map(([name,types]) => { const row = document.createElement('tr'); for (const text of [name,types.join(', ')]) { const cell = document.createElement('td'); cell.textContent = text; row.append(cell); } return row; }));
  }
  const previous = $('sensor-topic').value;
  const options = graphTopics.flatMap(topic => topic.types.filter(type=>type.startsWith('sensor_msgs/msg/')).map(type => { const option = document.createElement('option'); option.value = JSON.stringify({topic:topic.name,type}); option.textContent = `${topic.name} · ${type.split('/').at(-1)}`; return option; }));
  if (!options.length) { const empty = document.createElement('option'); empty.value = ''; empty.textContent = '暂无传感器话题'; options.push(empty); }
  $('sensor-topic').replaceChildren(...options);
  if (options.some(option=>option.value===previous)) $('sensor-topic').value = previous;
}
for (const id of ['graph-refresh','sensor-refresh']) $(id).addEventListener('click', () => run(async () => renderGraph(await request('graph'))));
$('subscribe').addEventListener('click', () => run(async () => {
  if (!$('sensor-topic').value) throw new Error('请先选择传感器话题');
  const selection = JSON.parse($('sensor-topic').value); await request('subscribe', selection);
  $('sensor-meta').textContent = `${selection.topic} · 等待数据`; $('sensor-view').textContent = '等待传感器数据'; log(`订阅 ${selection.topic}`);
}));
$('clear-log').addEventListener('click', () => { logLines.length = 0; $('log-view').textContent = '尚无事件'; });
$('export-log').addEventListener('click', () => { const url = URL.createObjectURL(new Blob([logLines.join('\n')],{type:'text/plain;charset=utf-8'})); const link = document.createElement('a'); link.href = url; link.download = 'ros-events.txt'; link.click(); setTimeout(()=>URL.revokeObjectURL(url),1000); });
window.addEventListener('beforeunload', () => client.disconnect());
reset();
