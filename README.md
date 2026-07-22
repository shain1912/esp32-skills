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
| [xiao-esp32s3](skills/xiao-esp32s3/SKILL.md) | Operate a Seeed XIAO ESP32S3 with arduino-cli on Windows without troubleshooting: verified compile/upload/serial commands, pin map, 11 tested example sketches, bundled serial-reader scripts, and the 7 pitfalls that waste the most debugging time (DTR reset, deep-sleep USB drop, PSRAM flag, ...). |

Battle-tested: every command and example in these skills was run on a real
board before being written down. A small model (Haiku) completed a full
modify-compile-flash-verify loop on the first try using only the skill.

## License

MIT
