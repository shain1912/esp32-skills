// Dashboard page served at "/"
#pragma once

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>XIAO Dashboard</title>
<style>
  :root {
    --bg: #f4f5f7; --card: #ffffff; --text: #1c1e21; --muted: #6b7280;
    --border: #e3e5e8; --accent: #f59e0b; --accent-soft: #fef3c7;
    --ok: #10b981; --ok-soft: #d1fae5; --off: #d1d5db; --bad: #ef4444;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --bg: #111315; --card: #1b1e21; --text: #e8eaed; --muted: #9aa0a6;
      --border: #2c3035; --accent: #fbbf24; --accent-soft: #3a2f12;
      --ok: #34d399; --ok-soft: #12332a; --off: #3c4046; --bad: #f87171;
    }
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; background: var(--bg); color: var(--text);
    font-family: system-ui, -apple-system, "Segoe UI", "Malgun Gothic", sans-serif;
    padding: 24px 16px;
  }
  main { max-width: 560px; margin: 0 auto; }
  header { display: flex; justify-content: space-between; align-items: baseline; margin-bottom: 20px; }
  h1 { font-size: 20px; margin: 0; }
  .conn { font-size: 13px; color: var(--muted); display: flex; align-items: center; gap: 6px; }
  .conn i { width: 8px; height: 8px; border-radius: 50%; background: var(--bad); display: inline-block; }
  .conn.live i { background: var(--ok); }
  .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
  @media (max-width: 440px) { .grid { grid-template-columns: 1fr; } }
  .card {
    background: var(--card); border: 1px solid var(--border); border-radius: 14px;
    padding: 18px; display: flex; flex-direction: column; gap: 14px;
  }
  .label { font-size: 13px; color: var(--muted); }
  .state { font-size: 28px; font-weight: 600; }
  .dot {
    width: 64px; height: 64px; border-radius: 50%; background: var(--off);
    transition: background .15s, box-shadow .15s; align-self: center;
  }
  .led.on .dot { background: var(--accent); box-shadow: 0 0 24px var(--accent); }
  .touch.on .dot { background: var(--ok); box-shadow: 0 0 24px var(--ok); }
  .row { display: flex; gap: 8px; }
  button {
    flex: 1; font: inherit; font-size: 15px; padding: 10px; border-radius: 10px; cursor: pointer;
    border: 1px solid var(--border); background: var(--bg); color: var(--text);
  }
  button.primary { background: var(--accent); border-color: var(--accent); color: #1c1e21; font-weight: 600; }
  button:disabled { opacity: .5; cursor: default; }
  .meter { height: 6px; border-radius: 3px; background: var(--off); overflow: hidden; position: relative; }
  .meter b { position: absolute; inset: 0 auto 0 0; background: var(--ok); width: 0; transition: width .1s; }
  .meter s { position: absolute; top: -3px; bottom: -3px; width: 2px; background: var(--text); }
  .cam { grid-column: 1 / -1; }
  .cam-view {
    position: relative; aspect-ratio: 4 / 3; border-radius: 10px; overflow: hidden;
    background: #000; display: flex; align-items: center; justify-content: center;
  }
  .cam-view img { width: 100%; height: 100%; object-fit: contain; display: none; }
  .cam-view.playing img { display: block; }
  .cam-view span { color: #9aa0a6; font-size: 14px; }
  .cam-view.playing span { display: none; }
  .cam-head { display: flex; justify-content: space-between; align-items: center; }
  a.btn { text-decoration: none; text-align: center; }
  .small { font-size: 12px; color: var(--muted); font-variant-numeric: tabular-nums; }
</style>
</head>
<body>
<main>
  <header>
    <h1>XIAO ESP32-S3</h1>
    <span class="conn" id="conn"><i></i><span id="connText">연결 중</span></span>
  </header>
  <div class="grid">
    <section class="card led" id="ledCard">
      <span class="label">사용자 LED (GPIO21)</span>
      <div class="dot"></div>
      <span class="state" id="ledState">-</span>
      <div class="row">
        <button id="onBtn" class="primary">켜기</button>
        <button id="offBtn">끄기</button>
      </div>
    </section>
    <section class="card touch" id="touchCard">
      <span class="label">터치 센서 (D0 / GPIO1)</span>
      <div class="dot"></div>
      <span class="state" id="touchState">-</span>
      <div class="meter"><b id="bar"></b><s id="mark"></s></div>
      <span class="small" id="touchValue">값 - / 기준 -</span>
    </section>
    <section class="card cam" id="camCard">
      <div class="cam-head">
        <span class="label">카메라 (같은 와이파이에서만)</span>
        <span class="small" id="camInfo"></span>
      </div>
      <div class="cam-view" id="camView">
        <img id="camImg" alt="카메라 영상">
        <span id="camMsg">정지됨</span>
      </div>
      <div class="row">
        <button id="camBtn" class="primary">영상 보기</button>
        <a id="snapBtn" class="btn" target="_blank"><button>사진 찍기</button></a>
      </div>
    </section>
  </div>
  <p class="small" id="rssi"></p>
</main>
<script>
  const $ = id => document.getElementById(id);
  let busy = false;

  function render(s) {
    $('ledCard').classList.toggle('on', s.led);
    $('ledState').textContent = s.led ? '켜짐' : '꺼짐';
    $('touchCard').classList.toggle('on', s.touched);
    $('touchState').textContent = s.touched ? '터치됨' : '대기';
    const max = s.threshold * 2;
    $('bar').style.width = Math.min(100, s.touch / max * 100) + '%';
    $('mark').style.left = (s.threshold / max * 100) + '%';
    $('touchValue').textContent = '값 ' + s.touch + ' / 기준 ' + s.threshold;
    $('rssi').textContent = 'Wi-Fi 신호 ' + s.rssi + ' dBm';
    if (s.camera === false && camAvailable) {
      camAvailable = false;
      $('camBtn').disabled = true;
      setCam(false);
    }
  }

  // Camera stream runs on port 81 of the board; via Tailscale Serve (https) it is mounted at /cam
  const camBase = location.protocol === 'https:'
    ? location.origin + '/cam'
    : 'http://' + location.hostname + ':81';
  let camPlaying = false;
  let camAvailable = true;
  $('snapBtn').href = camBase + '/capture';
  $('camInfo').textContent = camBase + '/stream';

  function setCam(play) {
    camPlaying = play && camAvailable;
    $('camView').classList.toggle('playing', camPlaying);
    $('camImg').src = camPlaying ? camBase + '/stream?t=' + Date.now() : '';
    $('camBtn').textContent = camPlaying ? '영상 정지' : '영상 보기';
    if (!camPlaying) $('camMsg').textContent = camAvailable ? '정지됨' : '카메라 없음 (Sense 카메라 보드 확인)';
  }
  $('camBtn').onclick = () => setCam(!camPlaying);
  // Retry a few times before giving up (stream can drop on flaky mobile links)
  let camRetries = 0;
  $('camImg').onload = () => { camRetries = 0; };
  $('camImg').onerror = () => {
    if (!camPlaying) return;
    if (camRetries < 3) {
      camRetries++;
      setTimeout(() => { if (camPlaying) $('camImg').src = camBase + '/stream?t=' + Date.now(); }, 1000);
      return;
    }
    camRetries = 0;
    setCam(false);
    $('camMsg').textContent = '영상 연결 실패 (동시 시청 최대 3명)';
  };

  function setConn(live) {
    $('conn').classList.toggle('live', live);
    $('connText').textContent = live ? '연결됨' : '연결 끊김';
  }

  async function poll() {
    try {
      const r = await fetch('/api/status', { cache: 'no-store' });
      render(await r.json());
      setConn(true);
    } catch (e) {
      setConn(false);
    }
    setTimeout(poll, 200);
  }

  async function setLed(state) {
    if (busy) return;
    busy = true;
    $('onBtn').disabled = $('offBtn').disabled = true;
    try {
      const r = await fetch('/api/led?state=' + state, { cache: 'no-store' });
      render(await r.json());
    } catch (e) {
      setConn(false);
    }
    busy = false;
    $('onBtn').disabled = $('offBtn').disabled = false;
  }

  $('onBtn').onclick = () => setLed('on');
  $('offBtn').onclick = () => setLed('off');
  poll();
  setCam(true);
</script>
</body>
</html>
)rawliteral";
