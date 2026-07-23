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

### 📸 [`xiao-webcam-ap`](skills/xiao-webcam-ap/SKILL.md) — camera web apps, hotspot mode

The board becomes its own WiFi hotspot serving two pages at `http://192.168.4.1`:

- **Dataset collector** — Teachable-Machine-style UI: live preview, labeled capture/burst, thumbnail gallery with per-shot delete, one-click ZIP export named ready for Edge Impulse (`label.1.jpg`, `label.2.jpg`, …)
- **Live inference viewer** — camera stream with a colored prediction box, top label, and per-class confidence bars

Built for classrooms: no router needed, one board per student, numbered SSIDs, and channel spreading (1/6/11) so twenty boards in one room don't jam each other. The skill instructs the agent to **ask the user** for the AP name/password — nothing is invented.

> "Make a data collection page for my class, no router" · "AP 모드로 수집기 만들어줘"

### 🌐 [`xiao-webcam-sta`](skills/xiao-webcam-sta/SKILL.md) — camera web apps, router mode

Same two apps, but the board joins your WiFi router: every device keeps its internet connection and reaches the board at `http://<name>.local` (mDNS) or its LAN IP. Falls back to a hotspot automatically if the router is unreachable. The skill asks the user for 2.4 GHz credentials (and knows ESP32 can't see 5 GHz networks), then verifies end-to-end over HTTP without ever opening the serial port.

> "I want the camera page without losing internet" · "인터넷 안 끊기게 해줘"

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
