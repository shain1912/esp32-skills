---
name: xiao-supabase-tailscale-dashboard
description: Build a XIAO ESP32S3 Sense control dashboard that works on the same Wi-Fi AND from anywhere — LED on/off and live touch-sensor state on a board-hosted web page, remote control through a Supabase table (REST + Realtime) with a static Netlify page, and live camera MJPEG streaming on the LAN and over Tailscale (Tailscale Serve on a LAN PC, since ESP32 cannot run Tailscale). Ships verified firmware, dashboard pages, and SQL, plus the fixes for the traps that break it on phones — esp_http_server's compiled-in 1024-byte header limit (431 "Header fields are too long" behind Tailscale Serve) and a single-task stream server that blocks every other viewer. Use this skill whenever the user wants to control an ESP32S3 from outside the home network, connect an ESP32 to Supabase, stream the XIAO camera remotely or over Tailscale, or says "외부망에서 제어", "supabase 연결", "웹캠 스트리밍", "tailscale로 스트리밍", "ESP32 웹 대시보드", "영상 연결 실패", "Header fields are too long".
---

# XIAO ESP32-S3 Sense: 웹 대시보드 + Supabase 원격 제어 + 카메라 스트리밍

## 완성되는 것

```
[같은 Wi-Fi]  http://<보드IP>/          LED 켜기/끄기 · 터치 상태 · 카메라 영상 (보드가 직접 서빙)
              http://<보드IP>:81/stream  MJPEG,  /capture  JPEG 한 장

[외부망, 누구나]  https://<site>.netlify.app      LED 제어 · 터치 상태 (카메라 없음)
      │  supabase-js (REST + Realtime)
      ▼
[Supabase]  public.xiao_devices  (한 행 = 보드 하나)
      ▲  보드: 0.2초마다 led GET, 터치 변화 시·3초마다 PATCH (HTTPS)
[XIAO ESP32-S3 Sense]

[외부망, tailnet 기기만]  https://<pc>.<tailnet>.ts.net/       로컬 대시보드 전체 (카메라 포함)
      │  Tailscale Serve:  /  → 보드:80,   /cam → 보드:81
      ▼
[같은 Wi-Fi의 PC (Tailscale 설치)] ──LAN──▶ [보드]
```

- 기존 `xiao-esp32s3` 스킬(같은 저장소)의 기본 arduino-cli 작업 흐름과 함정을 먼저 읽을 것. 이 스킬은 그 위에 네트워크 계층을 얹는다.
- 카메라 기능은 **Sense 모델(카메라 보드 장착)** 이 필요하다. 카메라가 없으면 나머지는 그대로 동작하고 대시보드에 "카메라 없음"이 뜬다.

## 스킬 파일

| 경로 | 내용 |
|---|---|
| `templates/WebDashboard/WebDashboard.ino` | 메인 펌웨어: Wi-Fi, 로컬 웹서버(80), 터치 읽기, Supabase 동기화, 카메라 시작 |
| `templates/WebDashboard/camera_stream.h` | 카메라 초기화 + **자체 HTTP 서버(81)**: 클라이언트별 작업, 헤더 길이 무제한 |
| `templates/WebDashboard/dashboard_html.h` | 로컬 대시보드 페이지 (HTTP면 `:81`, HTTPS(Tailscale)면 `/cam`으로 영상 주소 자동 선택) |
| `templates/WebDashboard/secrets.h.tmpl` | Wi-Fi·Supabase 설정 템플릿 → `secrets.h`로 복사해 채움 |
| `templates/cloud-dashboard/index.html` | 외부용 정적 대시보드 (Netlify 등) |
| `templates/supabase/schema.sql` | `xiao_devices` 테이블, 열 단위 권한, RLS, Realtime |

템플릿에는 비밀값이 없다. `{{...}}` 자리표시자를 사용자 값으로 바꿔서 쓴다.

## 하드웨어 사실 (틀리기 쉬움)

| 항목 | 값 |
|---|---|
| 유저 LED | GPIO21 (`LED_BUILTIN`), **active-low**: `LOW` = 켜짐 |
| 터치 | D0 = GPIO1 (TOUCH1). ESP32-S3는 터치하면 `touchRead()` 값이 **올라간다** (원조 ESP32는 내려감). 비터치 약 17,000 |
| 카메라 핀 | XCLK 10, SIOD 40, SIOC 39, Y2~Y9 = 15,17,18,16,14,12,11,48, VSYNC 38, HREF 47, PCLK 13, PWDN/RESET 없음 |
| PSRAM | 8MB OPI. 카메라에 필수 → FQBN `PSRAM=opi` |
| 겹치는 핀 | GPIO21은 Sense 확장보드의 SD 카드 CS와 같다. SD 카드를 함께 쓰려면 LED 제어를 옮길 것 |

## 진행 절차

### 0. 사용자에게 받을 정보

한 번에 묻는다. 값을 지어내지 말 것.

1. **Wi-Fi 이름·비밀번호.** ESP32는 2.4GHz만 된다. 사용자가 파일(예: `WIFI.md`)에 적어 두었다면 그것을 읽는다.
2. **Supabase 프로젝트.** 새로 만들지, 기존 프로젝트에 테이블만 추가할지 묻는다. 기존 프로젝트면 다른 서비스와 공유한다는 점을 알리고 `xiao_devices` 외에는 건드리지 않는다.
3. **외부 대시보드 접근 제한.** 이 템플릿은 "제한 없음"이다. 공개 키를 가진 누구나 LED를 바꿀 수 있다. 로그인이 필요하면 Supabase Auth + 권한 정책을 따로 설계해야 한다고 알린다.
4. **외부 대시보드를 둘 곳.** 기본은 Netlify 정적 배포다.
5. **Tailscale.** 보드와 같은 Wi-Fi에 항상 켜 둘 PC가 있고 Tailscale이 깔려 있는지 확인한다. 외부망 카메라에 필요하다.

### 1. 사전 확인

```bash
arduino-cli version
arduino-cli core list          # esp32:esp32 3.x 필요 (검증: 3.2.0 = ESP-IDF 5.4)
arduino-cli board list         # XIAO 포트 확인
```

esp32 코어 3.x는 `esp_camera.h`, `WiFiClientSecure`, `HTTPClient`, `ESPmDNS`를 기본 포함한다. 추가 라이브러리 설치는 없다.

### 2. Supabase 준비

1. `templates/supabase/schema.sql`의 `{{DEVICE_ID}}`(예: `xiao-01`)를 바꿔서 적용한다. Supabase MCP가 있으면 `apply_migration`을 쓰고, 없으면 대시보드 SQL Editor에 붙여 넣는다.
   - 같은 프로젝트에 `xiao_devices`가 이미 있으면 `create table`이 실패한다. 기존 테이블을 지우지 말고 사용자에게 묻는다.
2. 프로젝트 URL과 **publishable 키**(`sb_publishable_...`)를 얻는다. MCP라면 `get_project_url`, `get_publishable_keys`를 쓴다. `service_role`·secret 키는 절대 펌웨어나 웹페이지에 넣지 말 것.
3. 권한을 확인한다. 조회·수정은 되고 추가·삭제는 막혀야 한다.

```bash
U="<SUPABASE_URL>/rest/v1/xiao_devices"; K="<publishable key>"
curl -s "$U?id=eq.<DEVICE_ID>&select=*" -H "apikey: $K"                          # 행 1개
curl -s -X POST "$U" -H "apikey: $K" -H "Content-Type: application/json" -d '{"id":"x"}'   # 42501 permission denied
curl -s -X DELETE "$U?id=eq.<DEVICE_ID>" -H "apikey: $K"                          # 42501 permission denied
```

4. 스키마를 바꿨으면 `get_advisors`(security)로 점검한다. 공유 프로젝트라면 다른 서비스의 경고가 함께 나올 수 있다. 그 경고는 보고만 하고 고치지 않는다.

### 3. 펌웨어 업로드

1. `templates/WebDashboard/`를 사용자 작업 폴더로 복사한다. 폴더 이름은 `.ino` 이름과 같은 `WebDashboard`여야 한다.
2. `secrets.h.tmpl`을 `secrets.h`로 복사해 값을 채운다. `secrets.h`는 `.gitignore`에 넣는다.
3. 빌드하고 업로드한다.

```bash
FQBN=esp32:esp32:XIAO_ESP32S3:PSRAM=opi      # PSRAM=opi 필수
arduino-cli compile --fqbn $FQBN WebDashboard
arduino-cli upload -p <포트> --fqbn $FQBN WebDashboard
```

4. 시리얼 로그(115200)로 IP를 확인한다. Windows PowerShell 예:

```powershell
$p = New-Object System.IO.Ports.SerialPort <포트>,115200; $p.Open(); Start-Sleep -Seconds 12; $p.ReadExisting(); $p.Close()
```

정상 로그:

```
Camera ready, sensor PID: 0x3660        (OV3660. OV2640=0x26, OV5640=0x5640)
Connected. IP: 192.168.x.x
Camera stream: http://192.168.x.x:81/stream
IP: 192.168.x.x  touch: 17xxx  led: off  cloud: ok      ← 5초마다
```

### 4. 같은 Wi-Fi에서 확인

```bash
B=http://<보드IP>
curl -s $B/api/status                      # "camera":true, "cloud":true
curl -s "$B/api/led?state=on"              # "led":true (실제 LED 점등은 사용자에게 확인 요청)
curl -s -o /dev/null -w "%{http_code} %{size_download}B\n" $B:81/capture   # 200, 10KB 이상
```

### 5. 외부 대시보드 배포 (Netlify)

1. `templates/cloud-dashboard/index.html`을 **빈 폴더**에 복사하고 `{{SUPABASE_URL}}`, `{{SUPABASE_KEY}}`, `{{DEVICE_ID}}`를 채운다.
2. **그 폴더 안에서** 배포한다. 프로젝트 루트에서 배포하면 `secrets.h`와 Wi-Fi 정보 파일이 공개 사이트에 올라간다.
   - Netlify MCP: `create-new-project` → `deploy-site`가 돌려주는 `npx @netlify/mcp ...` 명령을 그 폴더에서 실행한다.
   - CLI: `cd cloud-dashboard && netlify deploy --prod --dir .`
3. 브라우저에서 확인할 것:
   - 우상단이 "보드 온라인"인지 확인한다. 보드가 3초마다 `updated_at`을 갱신하고, 10초 넘게 갱신이 없으면 오프라인으로 표시된다.
   - '켜기'를 누르면 보드의 `/api/status`에서 `"led":true`가 되는지 확인한다.

### 6. 외부망 카메라 (Tailscale Serve)

ESP32에서는 Tailscale을 돌릴 수 없다. 보드와 같은 Wi-Fi에 있는 PC를 중계 노드로 쓴다.

```powershell
$ts = "C:\Program Files\Tailscale\tailscale.exe"     # macOS/Linux: tailscale
& $ts status                                          # 이 PC와 사용자 휴대폰이 tailnet에 있는지
& $ts serve --bg http://<보드IP>:80
& $ts serve --bg --set-path /cam http://<보드IP>:81
& $ts serve status                                    # https://<pc>.<tailnet>.ts.net (tailnet only)
```

- Serve는 `/cam` 접두사를 떼고 보드로 넘긴다. `/cam/stream`은 보드의 `/stream`이 된다.
- Funnel(인터넷 공개)은 사용자가 명시적으로 원할 때만 쓴다. 그 경우 카메라가 인터넷에 공개된다.
- 첫 HTTPS 요청은 인증서 발급 때문에 실패(`000`)할 수 있다. 몇 초 뒤 다시 시도한다.
- 해제: `tailscale serve --https=443 off`

### 7. 최종 검증 (반드시 긴 헤더로)

휴대폰 + Tailscale 조합은 헤더가 길다. 짧은 헤더의 PC curl만으로 통과시키지 말 것(함정 1).

```bash
B=http://<보드IP>; H=https://<pc>.<tailnet>.ts.net
LONG="Tailscale-User-Profile-Pic: https://x/$(head -c 3000 /dev/zero | tr '\0' 'A')"
curl -s -o /dev/null -w "%{http_code}\n" -H "$LONG" $B:81/capture              # 200 (431이면 함정 1)
curl -s -m 5 -o s.bin -H "$LONG" $B:81/stream; grep -ac -- '--xiaoframe' s.bin  # 혼자 보면 5초에 100 전후
curl -s -o /dev/null -w "%{http_code}\n" $H/cam/capture                        # Tailscale 경유 200
# 동시 시청: 3개 동시 → 모두 프레임 수신, 4번째 503, 그 사이 /capture 200
for n in 1 2 3; do curl -s -m 6 -o s$n.bin $H/cam/stream & done; sleep 1
curl -s -m 4 -o /dev/null -w "4th: %{http_code}\n" $B:81/stream
curl -s -m 4 -o /dev/null -w "capture: %{http_code}\n" $H/cam/capture; wait
```

LED 점등, 터치 반응, 휴대폰 화면처럼 에이전트가 직접 볼 수 없는 것은 "확인하지 못했다"고 밝히고 사용자에게 확인을 요청한다.

## 함정 (증상 → 원인 → 조치)

1. **휴대폰에서 '사진 찍기' 시 `Header fields are too long`, 영상은 검은 화면이 잠깐 보였다가 바로 끊김.** PC에서는 정상.
   원인: `esp_http_server`의 요청 헤더 제한 `CONFIG_HTTPD_MAX_REQ_HDR_LEN` = 1024바이트가 **미리 컴파일된 코어 라이브러리**에 들어 있다. Tailscale Serve가 `Tailscale-User-Profile-Pic` 같은 헤더를 붙이고 모바일 브라우저 헤더도 길어서 1KB를 넘고 431이 난다. 스케치의 `#define`이나 `httpd_config_t`로는 바꿀 수 없다.
   조치: `camera_stream.h`의 `WiFiServer` 기반 서버를 쓴다. 요청 첫 줄만 읽고 나머지 헤더는 길이와 상관없이 버린다. **httpd로 되돌리지 말 것.** 80번 포트의 Arduino `WebServer` 라이브러리는 이 제한이 없어서 대시보드 페이지만 열리는 증상이 같이 나타난다.

2. **영상을 한 명이 보고 있으면 두 번째 시청자는 무한 대기 후 실패, `/capture`도 멈춤.**
   원인: CameraWebServer 예제식 스트림 핸들러는 서버 작업 하나에서 무한 루프를 돈다. 그동안 같은 서버의 다른 요청은 하나도 처리되지 않는다. 끊긴 탭이나 프록시에 남은 연결 하나만으로도 다른 사람이 막힌다.
   조치: 클라이언트마다 FreeRTOS 작업을 만든다(현재 템플릿). `MAX_STREAMS` = 3, `MAX_CLIENTS` = 6, `fb_count` = 3이다. 시청자를 늘리면 `fb_count`와 PSRAM 사용량도 함께 늘린다. 동시 시청 시 카메라 프레임을 나눠 쓴다(3명이면 각 약 9fps).

3. **카메라 초기화 실패 또는 "PSRAM not found".**
   원인: FQBN 기본값은 `PSRAM=disabled`다. 조치: `esp32:esp32:XIAO_ESP32S3:PSRAM=opi`로 빌드한다. 그래도 `Camera init failed: 0x...`이면 Sense 카메라 보드의 B2B 커넥터가 제대로 끼워졌는지 확인한다.

4. **LED가 반대로 동작한다(꺼라 했는데 켜짐).**
   원인: 유저 LED는 active-low다. 조치: 켜짐 = `LOW`. `setLed()` 한 곳에서만 핀을 다룬다.

5. **터치 판정이 항상 참이거나 항상 거짓이다.**
   원인: S3는 터치 시 값이 올라간다. 또 부팅 중 D0를 만지면 기준값이 높게 잡힌다. 조치: `value > baseline * 1.3`으로 판정하고 부팅 0.5초 동안 손을 떼게 안내한다. 둔하거나 예민하면 `TOUCH_RATIO`를 1.1~1.5 사이에서 조정한다.

6. **외부 대시보드가 꺼진 보드도 "온라인"으로 표시한다.**
   원인: `updated_at`을 보드가 아닌 쪽(대시보드, DB 트리거)이 갱신했다. 조치: `updated_at`은 보드만 NTP 시각으로 쓴다. 대시보드의 LED 변경은 `led` 열만 PATCH한다. `updated_at` 자동 갱신 트리거를 달지 말 것.

7. **로컬 대시보드에서 켠 LED가 0.2초 뒤 다시 꺼진다.**
   원인: 보드가 클라우드의 옛 `led` 값을 받아와 덮어썼다. 조치: 로컬 변경 시 `ledPending`을 세우고, PATCH가 성공하기 전에는 클라우드 값을 적용하지 않는다(템플릿에 구현됨). 동기화 순서를 바꾸지 말 것.

8. **Tailscale 주소로 대시보드는 열리는데 영상만 안 나온다.**
   원인: 페이지가 HTTPS인데 영상 주소가 `http://<host>:81`이라 mixed content로 막혔다. 또는 Serve에 `/cam` 경로가 없다. 조치: 템플릿 JS는 `location.protocol === 'https:'`이면 `origin + '/cam'`을 쓴다. `tailscale serve status`에 `/cam` 줄이 있는지 확인한다.

9. **며칠 뒤 갑자기 외부 접속이 안 된다.**
    원인: 공유기가 보드 IP를 바꿨다. Serve는 옛 IP를 가리킨다. 조치: 시리얼 로그에서 새 IP를 확인하고 Serve를 다시 설정한다. 공유기 DHCP 예약을 권한다.

10. **배포된 Netlify 사이트에 `secrets.h`나 Wi-Fi 메모가 들어 있다.**
    원인: 프로젝트 루트에서 배포했다. 조치: 사이트에서 파일을 내리고 Wi-Fi 비밀번호를 바꾼다. 이후에는 `index.html`만 있는 폴더에서 배포한다.

11. **C 문자열을 스크립트로 수정한 뒤 `missing terminating " character` 컴파일 오류가 난다.**
    원인: Python·sed 치환이 `"...\n"`의 `\n`을 실제 줄바꿈으로 풀었다. 조치: 문자열이 들어간 수정은 에디터 도구로 하고, 수정한 줄을 직접 확인한다.

12. **시리얼 로그를 읽으려고 포트를 열면 보드가 재부팅된다. 업로드는 "port busy"로 실패한다.**
    원인: 포트를 열 때 보드가 리셋될 수 있다. 열어 둔 리더는 업로드를 막는다. 조치: 부팅 메시지(`ESP-ROM...`)가 섞여도 정상이다. 업로드 전에는 모든 리더를 닫는다. Git Bash에서 `timeout arduino-cli monitor`는 출력이 비어 나오므로 PowerShell `SerialPort`로 읽는다.

## 보안 메모

- **누구나 제어 가능:** 템플릿은 접근 제한이 없다. 공개 키로 `xiao_devices`의 상태 열 조회·수정만 가능하고, 행 추가·삭제와 다른 테이블 접근은 막혀 있다.
- **인증서 검증 생략:** 보드는 Supabase TLS 인증서를 검증하지 않는다(`tlsClient.setInsecure()`). 암호화는 되지만 서버 신원은 확인하지 않는다. 시연용 설정이다.
- **카메라 노출 범위:** 카메라는 LAN과 tailnet에만 노출된다. Netlify 페이지에 tailnet 주소를 넣으면 공개 페이지에 내부 주소가 드러나므로 사용자에게 먼저 묻는다.
- **발열:** 영상을 계속 보내면 보드가 뜨거워진다. 장시간 운용 시 방열판을 권한다.

## 검증 범위

참조 하드웨어(XIAO ESP32-S3 Sense, OV3660, esp32 코어 3.2.0, Windows 11)에서 다음을 확인했다.
- Supabase 권한(조회·수정 허용, 추가·삭제·타 테이블 거부)
- 클라우드와 보드 사이 LED 양방향 동기화, Realtime 이벤트 수신
- Netlify 페이지에서 LED 켜기·끄기
- LAN·Tailscale 경유 영상(약 25fps)과 사진
- 3KB 헤더 요청 처리, 동시 시청 3명과 4번째 거절

**휴대폰 실기기에서 함정 1 수정 후 영상이 나오는 것은 아직 확인 전이다.** PC에서 긴 헤더를 흉내 내 검증했다.

## 커스터마이즈 포인트

- 터치 핀·민감도: `WebDashboard.ino`의 `TOUCH_PIN`, `TOUCH_RATIO`
- 동기화 주기: `SYNC_INTERVAL_MS`(LED 반응 속도), `HEARTBEAT_MS`(온라인 표시 주기. 대시보드의 `OFFLINE_AFTER_MS`는 이 값의 3배 이상으로)
- 해상도·화질: `camera_stream.h`의 `frame_size`(기본 VGA), `jpeg_quality`(낮을수록 고화질·고대역폭)
- 보드 여러 대: `schema.sql`에 행을 추가하고 보드마다 `DEVICE_ID`를 다르게 한다. 대시보드 페이지는 보드마다 하나씩 두거나 목록 UI로 확장한다
