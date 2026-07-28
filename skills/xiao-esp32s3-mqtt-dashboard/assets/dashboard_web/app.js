const BROKER_HOST_KEY = 'esp32-dashboard-broker-host';
const WS_PORT = 9001;
const TOUCH_SCALE_MAX = 80000;
const TOUCH_THRESHOLD = 40000;
const STALE_MS = 15000;

let client = null;
const devices = new Map(); // deviceId -> { touch, ledState, status, lastSeen }

const els = {
  brokerInput: document.getElementById('brokerHost'),
  connectBtn: document.getElementById('connectBtn'),
  connStatus: document.getElementById('connStatus'),
  grid: document.getElementById('deviceGrid'),
  emptyState: document.getElementById('emptyState'),
};

function loadBrokerHost() {
  return localStorage.getItem(BROKER_HOST_KEY) || '';
}

function saveBrokerHost(host) {
  localStorage.setItem(BROKER_HOST_KEY, host);
}

function setConnStatus(state, text) {
  els.connStatus.textContent = text;
  els.connStatus.dataset.state = state;
}

function connect(host) {
  if (!host) return;
  if (client) {
    client.end(true);
  }
  const url = `ws://${host}:${WS_PORT}`;
  setConnStatus('connecting', `연결 중... (${url})`);

  client = mqtt.connect(url, { reconnectPeriod: 2000, connectTimeout: 5000 });

  client.on('connect', () => {
    setConnStatus('connected', `연결됨: ${url}`);
    client.subscribe(['xiao/+/touch', 'xiao/+/led/state', 'xiao/+/status'], { qos: 0 });
  });
  client.on('reconnect', () => setConnStatus('connecting', '재연결 시도 중...'));
  client.on('close', () => setConnStatus('error', '연결 끊김'));
  client.on('error', (err) => setConnStatus('error', '연결 오류: ' + err.message));

  client.on('message', (topic, payloadBuf) => {
    const parts = topic.split('/'); // xiao / <id> / <rest...>
    const id = parts[1];
    const kind = parts.slice(2).join('/');
    const msg = payloadBuf.toString();

    const dev = devices.get(id) || { id, touch: 0, ledState: false, status: 'unknown', lastSeen: 0 };

    if (kind === 'touch') {
      dev.touch = parseInt(msg, 10) || 0;
      dev.lastSeen = Date.now();
    } else if (kind === 'led/state') {
      dev.ledState = msg.trim().toUpperCase() === 'ON';
    } else if (kind === 'status') {
      dev.status = msg.trim();
      dev.lastSeen = Date.now();
    }
    devices.set(id, dev);
    render();
  });
}

function toggleLed(id, currentlyOn) {
  if (!client || !client.connected) return;
  client.publish(`xiao/${id}/led/set`, currentlyOn ? 'OFF' : 'ON');
}

function render() {
  const ids = Array.from(devices.keys()).sort();
  els.emptyState.classList.toggle('hidden', ids.length > 0);
  els.grid.innerHTML = ids.map((id) => renderCard(devices.get(id))).join('');
}

function renderCard(dev) {
  const now = Date.now();
  const stale = now - dev.lastSeen > STALE_MS;
  const online = dev.status === 'online' && !stale;
  const pct = Math.max(0, Math.min(100, (dev.touch / TOUCH_SCALE_MAX) * 100));
  const thresholdPct = (TOUCH_THRESHOLD / TOUCH_SCALE_MAX) * 100;
  const touched = dev.touch > TOUCH_THRESHOLD;
  const lastSeenText = dev.lastSeen ? new Date(dev.lastSeen).toLocaleTimeString('ko-KR') : '-';

  return `
    <div class="device-card ${touched ? 'is-touched' : ''}">
      <div class="card-top">
        <span class="device-id">xiao-${dev.id}</span>
        <span class="status-chip ${online ? 'status-good' : 'status-critical'}">
          <span class="status-dot"></span>${online ? '온라인' : '오프라인'}
        </span>
      </div>

      <div class="touch-value">${dev.touch.toLocaleString()}</div>
      <div class="touched-badge ${touched ? '' : 'placeholder'}">✋ 터치됨</div>

      <div class="meter-track">
        <div class="meter-fill" style="width:${pct}%"></div>
        <div class="meter-threshold" style="left:${thresholdPct}%"></div>
      </div>

      <button class="led-toggle ${dev.ledState ? 'on' : 'off'}" data-id="${dev.id}" data-on="${dev.ledState}">
        <span class="toggle-track"><span class="toggle-knob"></span></span>
        <span class="toggle-label">LED ${dev.ledState ? 'ON' : 'OFF'}</span>
      </button>

      <div class="last-seen">마지막 갱신: ${lastSeenText}</div>
    </div>
  `;
}

document.addEventListener('click', (e) => {
  const btn = e.target.closest('.led-toggle');
  if (!btn) return;
  toggleLed(btn.dataset.id, btn.dataset.on === 'true');
});

setInterval(render, 3000); // re-render periodically so staleness/offline updates even without new messages

els.brokerInput.value = loadBrokerHost();
els.connectBtn.addEventListener('click', () => {
  const host = els.brokerInput.value.trim();
  if (!host) return;
  saveBrokerHost(host);
  connect(host);
});

const savedHost = loadBrokerHost();
if (savedHost) connect(savedHost);
