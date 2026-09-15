---
name: xiao-rf-remote-firebase
description: Turn an existing RF/IR remote control into an internet-controlled IoT button with a XIAO ESP32S3 — an NPN transistor (2N2222A) presses the remote's button pad, and a Firebase-hosted web page (Realtime Database + Hosting + Google sign-in, free Spark plan) lets allowed Google accounts press it from anywhere, with delivery acknowledgement and online status. Sets everything up from scratch on the user's own Wi-Fi and own Firebase project via one provisioning script (creates the default RTDB that the Firebase CLI refuses to create, deploys hosting/auth/rules, creates the device account). Use this skill whenever the user wants to hack/automate a remote control, press a physical button over the internet, "리모컨 해킹", "리모컨 IoT", "리모컨 원격 제어", "firebase 리모컨", "버튼을 원격으로 누르기", or asks how to wire a transistor across a remote's button pad.
---

# RF 리모컨 → Firebase IoT 구축

## 완성되는 것

```
[폰/PC 브라우저]  https://<project>.web.app  (구글 로그인, 허용된 계정만)
      │  전원 버튼 클릭 → /remote/cmd = 서버시각(ms)
      ▼
[Firebase Realtime Database]  보안 규칙: 허용 구글 계정 + ESP32 기기 계정만 /remote 접근
      │  스트림(SSE)
      ▼
[XIAO ESP32-S3]  D3 HIGH 0.4초 → 2N2222A → 리모컨 버튼 눌림
      │  /remote/ack = 같은 값,  30초마다 /remote/device {lastSeen, rssi, ip}
      ▼
[브라우저]  "리모컨 눌림 확인", 기기 온라인/오프라인 표시
```

- 15초보다 오래된 명령은 무시한다 → ESP32가 꺼졌다 켜져도 예전 명령이 실행되지 않음
- 과금 없는 Spark(무료) 요금제로 동작한다

## 스킬 파일

| 경로 | 내용 |
|---|---|
| `hardware.md` | 부품, **버튼 패드 구조 확인법**, 결선도, 트랜지스터 핀 순서, 하드웨어 문제 해결 |
| `scripts/setup.js` | DB 생성 · 파일 생성 · 배포 · ESP32 기기 계정 생성을 한 번에 (재실행 안전) |
| `templates/firebase/` | 웹페이지(`public/index.html`), `firebase.json`, 보안 규칙 템플릿 |
| `templates/RemoteFirebase/` | ESP32 스케치, `secrets.h` 템플릿 |

템플릿에는 비밀값이 없다. `secrets.h`와 보안 규칙은 `setup.js`가 사용자 값으로 생성한다.

## 진행 절차

사용자가 초보일 수 있으므로 단계마다 결과를 확인하고 다음으로 넘어간다. 명령은 Claude가 직접 실행하되,
브라우저 로그인·하드웨어 측정처럼 사람이 해야 하는 일은 명확히 요청한다.

### 0. 사용자에게 받을 정보

한 번에 물어본다.

1. **Wi-Fi 이름과 비밀번호** — ESP32-S3는 **2.4GHz만** 된다. 5GHz 전용 공유기면 연결 안 됨
2. **웹에서 버튼을 누를 수 있게 허용할 구글 계정** (여러 개 가능, 쉼표로)
3. **하드웨어 준비 상태** — 버튼 패드 확인과 결선을 했는지. 안 했으면 `hardware.md`를 따라 먼저 진행
4. (선택) DB 리전 — 기본 `asia-southeast1`(싱가포르, 한국에서 가장 가까움). `us-central1`, `europe-west1` 가능

프로젝트 ID는 전 세계에서 유일해야 하므로 `rf-remote-<무작위 4~6자>`로 Claude가 정한다 (소문자·숫자·하이픈, 6~30자).

### 1. 사전 준비 확인

```bash
node --version            # 18 이상 (fetch 사용)
firebase --version        # 없으면: npm i -g firebase-tools
firebase login:list       # "No authorized accounts" 면 사용자에게 `! firebase login` 실행 요청
arduino-cli version       # 없으면 Arduino CLI 설치 안내
arduino-cli core list     # esp32:esp32 가 없으면 아래
arduino-cli lib list      # FirebaseClient 가 없으면 아래
arduino-cli board list    # XIAO 포트 확인 (ESP32 Family Device / USB 직렬 장치)
```

없을 때 설치:

```bash
arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install FirebaseClient     # mobizt/FirebaseClient 2.x
```

`firebase login`은 브라우저가 열리는 대화형 명령이라 Claude가 대신 못 한다. 사용자에게 프롬프트에 `! firebase login`을 입력하게 한다.
`firebase login`의 구글 계정이 곧 프로젝트 소유자다. 웹 허용 계정과 달라도 된다.

### 2. Firebase 프로젝트와 웹 앱 만들기

```bash
firebase projects:create <project-id> --display-name "RF Remote"
firebase apps:create WEB rf-remote-web --project <project-id>
```

- 이미 쓰고 있는 프로젝트에 붙여도 되지만, 보안 규칙(`database.rules.json`)을 **통째로 덮어쓰므로** 기존에 Realtime Database를 쓰는 프로젝트라면 새 프로젝트를 권한다.
- 실패 원인 대부분: 이 구글 계정으로 Firebase 콘솔을 한 번도 안 열어서 약관 미동의 → 사용자에게 https://console.firebase.google.com 을 한 번 열어 동의하게 한다. 프로젝트 개수 한도 초과 → 안 쓰는 프로젝트 삭제 요청.

### 3. setup.js 실행

출력 폴더는 사용자 작업 폴더 아래로 한다 (예: `./rf-remote`).
`<SKILL_DIR>`은 이 SKILL.md가 있는 폴더의 절대 경로다. 설치 방식에 따라 `.claude/skills/rf-remote-firebase`,
`~/.claude/skills/rf-remote-firebase`, `.agents/skills/rf-remote-firebase` 등으로 다르니 실제 위치를 확인해서 넣는다.

```bash
node <SKILL_DIR>/scripts/setup.js \
  --project <project-id> \
  --owner "me@gmail.com,family@gmail.com" \
  --ssid "<Wi-Fi 이름>" --wifi-pass "<Wi-Fi 비밀번호>" \
  --out ./rf-remote
```

스크립트가 하는 일 (순서가 중요해서 스크립트로 묶었다):

1. 웹 앱 설정에서 `apiKey` 조회
2. **기본 Realtime Database 생성** — `firebase database:instances:create`는 첫 기본 DB 생성을 거부하므로("run firebase init database") CLI 내부 API로 만든다
3. `./rf-remote/firebase/`(배포용)와 `./rf-remote/RemoteFirebase/`(스케치, `secrets.h` 포함) 생성
4. `firebase deploy --only hosting,auth,database` — 웹페이지 게시, Email/Password·구글 로그인 활성화, 보안 규칙 적용
5. ESP32 기기 계정(`xiao@<project-id>.local`) 생성 — Email/Password 로그인이 4번에서 켜지므로 반드시 배포 뒤

다시 실행해도 안전하다. 기존 `secrets.h`의 기기 비밀번호를 재사용하고, 계정이 이미 있으면 비밀번호를 맞춘다.
**허용 계정 추가/변경, Wi-Fi 변경도 같은 명령을 값만 바꿔 다시 실행**하면 된다 (Wi-Fi 변경 시 스케치 재업로드 필요).

PowerShell에서는 줄바꿈 `\` 대신 한 줄로 쓰거나 백틱(`` ` ``)을 쓴다.

### 4. ESP32 업로드

```bash
arduino-cli compile --upload -p <포트> --fqbn esp32:esp32:XIAO_ESP32S3 ./rf-remote/RemoteFirebase
```

- **FQBN에 `:CDCOnBoot=cdc`를 붙이지 말 것.** 이름과 반대로 이 값이 USB 시리얼을 *끄는* 옵션이라 로그가 안 나온다. 기본값이 켜짐이다.
- 포트가 안 보이거나 업로드 실패: XIAO의 **BOOT 버튼을 누른 채 USB를 꽂고** 다시 업로드.

### 5. 동작 확인

시리얼 로그(115200)를 30~40초 읽는다. Windows PowerShell 예:

```powershell
$p = New-Object System.IO.Ports.SerialPort <포트>,115200; $p.DtrEnable=$true; $p.Open()
$end=(Get-Date).AddSeconds(40); $b=''; while((Get-Date) -lt $end){ Start-Sleep -Milliseconds 250; $b += $p.ReadExisting() }; $p.Close(); $b
```

macOS/Linux: `arduino-cli monitor -p <포트> -c baudrate=115200` (Ctrl+C로 종료, Claude는 백그라운드로 실행)

정상 로그:

```
=== XIAO RF Remote (Firebase) ===
[wifi] IP 192.168.x.x  RSSI -5x dBm
[firebase] 인증 완료, /remote/cmd 스트림 시작
```

그다음 사용자에게:
1. 폰에서 `https://<project-id>.web.app` 열기 → 허용한 구글 계정으로 로그인
2. 상단이 "기기 온라인"(초록 점)인지 확인
3. 빨간 전원 버튼 누르기 → 시리얼에 `[cmd] ... -> 버튼 누름`, 웹에 "리모컨 눌림 확인", 실제 기기 반응

## 문제 해결

| 증상 | 원인 / 조치 |
|---|---|
| 시리얼에 아무것도 안 나옴 | FQBN에 `CDCOnBoot=cdc`를 붙였음 → 빼고 재업로드 |
| `[wifi] 실패` 반복 | SSID/비밀번호 오타, 5GHz 네트워크, 신호 약함. 값 고쳐 `setup.js` 재실행 후 재업로드 |
| `authTask` 오류 `INVALID_LOGIN_CREDENTIALS` / 400 | `secrets.h` 비밀번호와 계정 불일치 → `setup.js` 재실행(비밀번호 맞춤) 후 재업로드 |
| `authTask` 오류 `OPERATION_NOT_ALLOWED` | Email/Password 로그인 꺼짐 → `--skip-deploy` 없이 `setup.js` 재실행 |
| `[stream] 오류` 401 / Permission denied | 보안 규칙의 기기 이메일과 `secrets.h`의 `DEVICE_EMAIL` 불일치 → 같은 `--out`으로 재실행 |
| 웹에 "권한 없음: 허용되지 않은 계정" | 로그인한 구글 계정이 `--owner`에 없음 → 추가해서 재실행 |
| 웹 로그인 시 `auth/unauthorized-domain` | `web.app`/`firebaseapp.com`이 아닌 도메인에서 열었음 → 콘솔 > Authentication > 설정 > 승인된 도메인에 추가 |
| 웹 "기기 응답 없음" | ESP32 전원/Wi-Fi 확인. 방금 부팅했으면 인증 완료 로그까지 기다린 뒤 다시 누름 |
| `[cmd] ... 무시 (xxx ms 전 명령)` | 부팅 직후 DB에 남아 있던 옛 명령이라 정상. 새로 누르면 동작 |
| 로그는 "버튼 누름"인데 기기 반응 없음 | 하드웨어 문제 → `hardware.md`의 문제 해결 (패드 전압 측정) |
| `firebase ...` 명령 끝에 `Assertion failed: !(handle->flags & UV_HANDLE_CLOSING)` | Windows용 CLI 종료 시 뜨는 알려진 메시지. 앞의 출력이 성공이면 무시 |

## 커스터마이즈 포인트

- 버튼 핀: `RemoteFirebase.ino`의 `#define BTN D3`
- 누름 시간: `pressPower()`의 `delay(400)` — 수신기가 인식 못 하면 800까지 늘림
- 버튼 여러 개: `/remote/cmd` 대신 `/remote/<버튼이름>/cmd`로 경로를 나누고 핀·스트림을 추가. 보안 규칙은 `/remote` 하위 전체에 적용되므로 수정 불필요
- 템플릿을 고치면 이 스킬의 `templates/`를 고친 뒤 `setup.js`를 재실행해야 출력 폴더에 반영된다 (출력 폴더를 직접 고치면 재실행 시 덮어써짐)
