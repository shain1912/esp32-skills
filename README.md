# esp32-skills

**Agent skills that let any AI coding agent (Claude Code, Cursor, Codex, and 70+ more) drive a Seeed XIAO ESP32S3 board — flash sketches, build camera web apps, and train TinyML models — without the usual debugging spiral.**

Every command, pin number, template, and pitfall in these skills was executed and verified on real hardware before being written down. In our benchmark, a small model (Claude Haiku) completed a full modify → compile → flash → verify loop on the first try using only these skills.

```bash
npx skills add shain1912/esp32-skills
```

---

## Why

Working with embedded boards through an AI agent usually fails in the same handful of places: the wrong serial monitor command hangs the shell, deep sleep makes the COM port "disappear", PSRAM is silently off, the Edge Impulse library crashes on boot with a cryptic tensor-arena error. Each one costs a long debugging session the first time you hit it.

These skills encode the answers so your agent never has to rediscover them. They are written for the *weakest* model that might use them: verified commands to copy, working `.ino` templates with placeholders to fill, and a numbered pitfall list with symptoms and fixes.

## Install

Requires [skills CLI](https://github.com/vercel-labs/skills) (runs via `npx`, no global install).

```bash
# install every skill into your project
npx skills add shain1912/esp32-skills

# or pick specific skills
npx skills add shain1912/esp32-skills --skill xiao-esp32s3
npx skills add shain1912/esp32-skills --skill xiao-webcam-ap --skill xiao-edgeimpulse-train

# list what's inside without installing
npx skills add shain1912/esp32-skills --list

# target a specific agent (Claude Code, Cursor, Codex, ...)
npx skills add shain1912/esp32-skills -a claude-code -a cursor
```

The CLI auto-detects your installed coding agents and places the skills where each one looks for them (`.claude/skills/`, `.agents/skills/`, etc.).

## Skills

### 🔧 [`xiao-esp32s3`](skills/xiao-esp32s3/SKILL.md) — board fundamentals

The base skill. Verified arduino-cli workflow for Windows (compile / upload / bounded serial read), the full pin map, and **9 numbered pitfalls** with symptoms and fixes — DTR-reset behavior, deep-sleep USB drop, the mandatory PSRAM flag, reset-stuck recovery, stale library caches, and more.

Bundled: 11 hardware-tested example sketches (blink, serial echo, touch, ADC, PWM, WiFi scan, SoftAP + web server, BLE scan/server, deep sleep, chip info) plus PowerShell helpers for timeout-safe serial reading and deep-sleep verification.

> "Upload a sketch that blinks the LED twice per second" · "Read what the board is printing" · "Why did COM4 disappear?"

### 🖥️ [`xiao-serial-monitor`](skills/xiao-serial-monitor/SKILL.md) — serial monitoring, both modes

Serial output in the two shapes it actually takes: a **bounded read** the agent runs to debug its own change (returns an exit code, never hangs the session), and a **persistent terminal monitor** handed to the user with the exact command for checking output later. One script does both — add `-Seconds 10` and it stops on its own.

The monitor reattaches automatically when the port drops, which on a USB-Serial/JTAG board is every single reset and re-upload, so one invocation covers a whole debugging session instead of dying at the first reset. Port detection matches on **USB vendor ID** rather than taking the first `Serial Port (USB)` row — on a machine with a CP210x or CH340 permanently attached, the naive pick lands on the wrong device and you get a silent, empty window with no error. Ships `mon` / `flash` / `boards` shortcuts for the PowerShell profile.

**8 numbered pitfalls**, including the wrong-port trap, COM numbers changing on replug, an open monitor blocking every upload, why attaching to an already-running board shows nothing (press Reset — the boot banner is long gone), and PowerShell 5.1 turning non-ASCII into mojibake unless the `.ps1` is saved as UTF-8 **with BOM**.

> "Open the serial monitor" · "시리얼 모니터 띄워줘" · "the monitor keeps disconnecting" · "why is COM3 showing nothing?"

### 🌡️ [`xiao-i2c-sensors`](skills/xiao-i2c-sensors/SKILL.md) — I2C sensor bring-up

BH1750 light, BME280 temperature/pressure/humidity, and BNO055 9-DOF orientation: a unit test per sensor, plus all three streaming together on one bus at 1 Hz. Five sketches, all run on real hardware — the unit tests pass 5/5, 8/8, and 10/10.

The tests bounds-check every reading against the datasheet, because the failure that actually happens is a dead bus returning `0` or `NaN`, not a part that drifted 2 %. They also keep anything needing a human hand — covering the light sensor, breathing on the humidity sensor, moving the IMU — out of the scored section, after an early version asserted "consecutive readings differ" and failed on working hardware whenever nobody was waving at it.

**6 numbered pitfalls** covering the traps that make a healthy-looking sensor return nothing: a BNO055 at 0x29 that the default 0x28 driver cannot reach, `setExtCrystalUse(true)` parking the chip in idle with every output stuck at `0.00` while self-test still reports `0x0F`, and 0x76 being a BMP280 (no humidity) as often as a BME280 — read chip-ID `0xD0`, not the silkscreen.

> "센서 값 읽어줘" · "BNO055 orientation is always 0" · "is this a BME280 or a BMP280?" · "unit test my I2C sensors"

### 🎯 [`xiao-imu-mpu-6050`](skills/xiao-imu-mpu-6050/SKILL.md) — MPU-6050/6500 motion sensing

Accelerometer + gyroscope over I2C with boot-time calibration and a moving/still decision driven to the onboard LED. Three sketches, all run on real hardware; the driver uses **no IMU library** because the obvious one can't drive the part most people actually have.

Modules sold as "MPU-6050" are frequently **MPU-6500**. Both answer at `0x68` with an identical data-register layout, so an I2C scan cannot tell them apart — and `Adafruit_MPU6050`'s `begin()` checks `WHO_AM_I`, sees `0x70`, and reports `MPU6050 not found - check wiring` on a perfectly wired module. Read register `0x75`, not the silkscreen.

Calibration exists because the theoretical numbers are wrong by margins that swamp the measurement: the reference unit's gyro sat 3.3 °/s off zero (≈200°/min of drift if you integrate it), and its accelerometer magnitude read 10.75 m/s² at rest instead of 9.81 — 9 % high, which eats most of a sensible motion threshold before the board has moved. So the sketch subtracts a *measured* resting baseline rather than the constant.

**8 numbered pitfalls**, including the temperature formula differing between the two chips (wrong one = silently off by ~15 °C, the one channel with no obvious sanity check), `ACCEL_CONFIG2` existing only on the 6500, why axes must be read in one 14-byte burst, and how draining the serial buffer hides the one-shot `setup()` error that explains everything.

> "움직임 감지해줘" · "MPU6050 not found but the scan sees it" · "my gyro drifts" · "is this a 6050 or a 6500?"

### 📸 [`xiao-webcam-ap`](skills/xiao-webcam-ap/SKILL.md) — camera web apps, hotspot mode

The board becomes its own WiFi hotspot serving two pages at `http://192.168.4.1`:

- **Dataset collector** — Teachable-Machine-style UI: live preview, labeled capture/burst, thumbnail gallery with per-shot delete, one-click ZIP export named ready for Edge Impulse (`label.1.jpg`, `label.2.jpg`, …)
- **Live inference viewer** — camera stream with a colored prediction box, top label, and per-class confidence bars

Built for classrooms: no router needed, one board per student, numbered SSIDs, and channel spreading (1/6/11) so twenty boards in one room don't jam each other. The skill instructs the agent to **ask the user** for the AP name/password — nothing is invented.

> "Make a data collection page for my class, no router" · "AP 모드로 수집기 만들어줘"

### 🌐 [`xiao-webcam-sta`](skills/xiao-webcam-sta/SKILL.md) — camera web apps, router mode

Same two apps, but the board joins your WiFi router: every device keeps its internet connection and reaches the board at `http://<name>.local` (mDNS) or its LAN IP. Falls back to a hotspot automatically if the router is unreachable. The skill asks the user for 2.4 GHz credentials (and knows ESP32 can't see 5 GHz networks), then verifies end-to-end over HTTP without ever opening the serial port.

> "I want the camera page without losing internet" · "인터넷 안 끊기게 해줘"

### 📊 [`xiao-esp32s3-mqtt-dashboard`](skills/xiao-esp32s3-mqtt-dashboard/SKILL.md) — multi-board MQTT dashboard

Any number of boards join your WiFi (STA mode), publish their touch sensor value over MQTT, and accept LED on/off commands — while a browser dashboard shows every board live as it connects and lets you toggle each LED. Every board runs the *identical* compiled firmware: each one derives a MAC-based ID at boot, so topics never collide and there is nothing to edit per board.

Bundled: a verified `.ino` template, a static `mqtt.js` dashboard (no build step, no baked-in IP — you type the broker address into the page), and two PowerShell setup scripts for the Mosquitto broker side. **8 numbered pitfalls** cover the parts that don't show up until you actually try it on Windows: Mosquitto 2.x's loopback-only default, the elevation the Windows Mosquitto *service* requires for any config change, Windows Firewall silently blocking LAN devices even after the listener is open (with a documented false-positive self-test to avoid), why `PubSubClient` error `rc=-2` is never a credentials problem, and why re-opening the serial port on an already-running board can make a healthy deployment look broken.

> "여러 esp32s3 대시보드 만들어줘" · "MQTT로 터치센서 값 여러 보드에서 모아줘" · "Mosquitto로 ESP32 여러 개 연결하고 싶어"

### 🧠 [`xiao-edgeimpulse-train`](skills/xiao-edgeimpulse-train/SKILL.md) — TinyML training pipeline

Train and deploy audio (keyword spotting) or vision (image classification) models using **only the Edge Impulse REST API** — no `edge-impulse-cli`, which fails to build on modern Node/Windows. Covers the whole loop:

1. Dataset upload via the ingestion API (label-per-file)
2. Impulse creation with working JSON bodies — including the `implementationVersion: 4` MFCC fix that prevents the "mel filterbank contains all zeros" failure
3. Feature generation and training jobs with polling patterns
4. Arduino library download + install into the sketchbook
5. **The three on-device fixes that make vision models actually boot on the ESP32-S3**: patching the hard-coded `EI_MAX_OVERFLOW_BUFFER_COUNT`, overriding the SDK allocators to place the tensor arena in PSRAM, and `--clean` after any library swap
6. Sketch integration: static-buffer inference, RGB888 feature packing, camera rotation compensation, AWB warmup

> "Retrain the model with my new photos" · "Edge Impulse로 키워드 인식 훈련해줘" · "Why does run_classifier crash on boot?"

## Compatibility

| | |
|---|---|
| Board | Seeed Studio XIAO ESP32S3 / XIAO ESP32S3 Sense (camera/mic skills need Sense) |
| Host OS | Windows + PowerShell 5.1 (commands are PowerShell-flavored; concepts port to macOS/Linux) |
| Toolchain | arduino-cli ≥ 1.x, esp32 core 3.x (`esp32:esp32:XIAO_ESP32S3`) |
| Agents | Anything the [skills CLI](https://github.com/vercel-labs/skills) supports: Claude Code, Cursor, Codex, Copilot, Gemini CLI, OpenCode, Zed, … |

## How these skills were made

Built by running the entire workflow on real hardware with [Claude Code](https://claude.com/claude-code), then distilling every verified command and every mistake into skill form. The distinctive content — the pitfall lists, the exact API bodies, the on-device patches — exists because each one cost a real debugging session once, so your agent doesn't pay that cost again.

## Contributing

Issues and PRs welcome. If you hit a new pitfall on this board, that's exactly the kind of thing that belongs here — include the symptom, the cause, and the verified fix.

## Author

**[shain1912](https://github.com/shain1912)**

## License

[MIT](LICENSE)
