# esp32-skills

Agent skills for working with ESP32 boards. Verified on real hardware.

## Install

```bash
npx skills add <owner>/esp32-skills
# or a specific skill:
npx skills add <owner>/esp32-skills --skill xiao-esp32s3
```

## Skills

| Skill | What it does |
|-------|--------------|
| [xiao-esp32s3](skills/xiao-esp32s3/SKILL.md) | Operate a Seeed XIAO ESP32S3 with arduino-cli on Windows without troubleshooting: verified compile/upload/serial commands, pin map, 11 tested example sketches, bundled serial-reader scripts, and the 9 pitfalls that waste the most debugging time (DTR reset, deep-sleep USB drop, PSRAM flag, ...). |
| [xiao-webcam-ap](skills/xiao-webcam-ap/SKILL.md) | Camera web apps in standalone hotspot (AP) mode for classrooms/demos — Teachable-Machine-style dataset collector (gallery, delete, ZIP export) and live inference viewer at http://192.168.4.1. Verified .ino templates included; asks the user for AP name/password, spreads channels for multi-board rooms. |
| [xiao-webcam-sta](skills/xiao-webcam-sta/SKILL.md) | Same camera web apps in router (STA) mode — devices keep internet, page at http://\<name\>.local via mDNS, AP fallback if the router is unreachable. Asks the user for 2.4 GHz WiFi credentials; end-to-end HTTP verification without touching serial. |
| [xiao-edgeimpulse-train](skills/xiao-edgeimpulse-train/SKILL.md) | Train & deploy TinyML (audio keyword spotting / image classification) for the XIAO ESP32S3 using only the Edge Impulse REST API — no edge-impulse-cli. Dataset upload, impulse JSON (with the MFCC v4 fix), training jobs, Arduino library install, and the three on-device fixes (arena overflow patch, PSRAM allocators, --clean) that make vision models actually boot. |

Battle-tested: every command and example in these skills was run on a real
board before being written down. A small model (Haiku) completed a full
modify-compile-flash-verify loop on the first try using only the skill.

## License

MIT
