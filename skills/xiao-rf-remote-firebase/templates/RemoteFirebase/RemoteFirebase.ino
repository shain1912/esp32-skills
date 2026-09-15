/*
 * XIAO ESP32-S3 - Firebase RF 리모컨 노드
 *
 * 웹(remote-firebase/public) -> /remote/cmd 에 서버 시각(ms) 기록
 * 이 노드   -> /remote/cmd 스트림 수신 -> 버튼 누름 -> /remote/ack 에 같은 값 기록
 *           -> 30초마다 /remote/device {lastSeen, rssi, ip}
 *
 * 회로: D3 --4.7k-- 2N2222A Base, Base --10k-- GND, Emitter-GND, Collector-버튼 안쪽 링
 * Board: esp32:esp32:XIAO_ESP32S3
 */

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <WiFi.h>
#include <FirebaseClient.h>
#include "ExampleFunctions.h"   // SSL_CLIENT, set_ssl_client_insecure_and_buffer, auth_debug_print
#include "secrets.h"

#define BTN D3
#define LED LED_BUILTIN          // active LOW

const int64_t  STALE_MS     = 15 * 1000;   // 이보다 오래된 명령은 무시 (부팅/재접속 시 재실행 방지)
const uint32_t HEARTBEAT_MS = 30 * 1000;

SSL_CLIENT ssl_client, stream_ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client), streamClient(stream_ssl_client);

UserAuth user_auth(API_KEY, DEVICE_EMAIL, DEVICE_PASSWORD, 3000);
FirebaseApp app;
RealtimeDatabase Database;

uint64_t lastCmd = 0;
uint64_t pendingCmd = 0;
uint32_t lastBeat = 0;
bool streamStarted = false;

/* ---------- 버튼 ---------- */

void pressPower() {
  digitalWrite(LED, LOW);
  digitalWrite(BTN, HIGH);
  delay(400);          // RF 반복 송신 여유
  digitalWrite(BTN, LOW);
  digitalWrite(LED, HIGH);
}

/* ---------- Firebase 콜백 ---------- */

void onCmd(const String &raw) {
  uint64_t cmd = strtoull(raw.c_str(), nullptr, 10);
  if (cmd == 0 || cmd == lastCmd) return;
  bool first = (lastCmd == 0);
  lastCmd = cmd;

  time_t now = time(nullptr);
  if (now > 1700000000) {
    int64_t age = (int64_t)now * 1000 - (int64_t)cmd;
    if (age > STALE_MS || age < -STALE_MS) {
      Serial.printf("[cmd] %s 무시 (%lld ms 전 명령)\n", raw.c_str(), age);
      return;
    }
  } else if (first) {
    Serial.printf("[cmd] %s 무시 (시간 미동기화 상태의 첫 값)\n", raw.c_str());
    return;
  }
  pendingCmd = cmd;
}

void processStream(AsyncResult &aResult) {
  if (!aResult.isResult()) return;

  if (aResult.isError())
    Firebase.printf("[stream] 오류: %s (%d)\n", aResult.error().message().c_str(), aResult.error().code());

  if (aResult.available()) {
    RealtimeDatabaseResult &s = aResult.to<RealtimeDatabaseResult>();
    if (s.isStream() && (s.event() == "put" || s.event() == "patch") && s.dataPath() == "/")
      onCmd(s.to<String>());
  }
}

void processData(AsyncResult &aResult) {
  if (!aResult.isResult()) return;
  if (aResult.isError())
    Firebase.printf("[%s] 오류: %s (%d)\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());
}

/* ---------- setup / loop ---------- */

void setup() {
  pinMode(BTN, OUTPUT);
  digitalWrite(BTN, LOW);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(300);
  Serial.println("\n=== XIAO RF Remote (Firebase) ===");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] \"%s\" 접속 중", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) { delay(400); Serial.print("."); }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[wifi] 실패. 5초 후 재부팅.");
    delay(5000);
    ESP.restart();
  }
  Serial.printf("[wifi] IP %s  RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

  configTime(0, 0, "pool.ntp.org", "time.google.com");

  set_ssl_client_insecure_and_buffer(ssl_client);
  set_ssl_client_insecure_and_buffer(stream_ssl_client);

  initializeApp(aClient, app, getAuth(user_auth), auth_debug_print, "authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);

  streamClient.setSSEFilters("get,put,patch,keep-alive,cancel,auth_revoked");
}

void loop() {
  app.loop();

  if (!app.ready()) return;

  if (!streamStarted) {
    streamStarted = true;
    Serial.println("[firebase] 인증 완료, /remote/cmd 스트림 시작");
    Database.get(streamClient, "/remote/cmd", processStream, true, "streamTask");
  }

  if (pendingCmd) {
    uint64_t cmd = pendingCmd;
    pendingCmd = 0;
    Serial.printf("[cmd] %llu -> 버튼 누름\n", cmd);
    pressPower();
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", cmd);
    Database.set<String>(aClient, "/remote/ack", String(buf), processData, "ackTask");
  }

  if (lastBeat == 0 || millis() - lastBeat > HEARTBEAT_MS) {
    lastBeat = millis();
    char json[128];
    snprintf(json, sizeof(json), "{\"lastSeen\":{\".sv\":\"timestamp\"},\"rssi\":%d,\"ip\":\"%s\"}",
             WiFi.RSSI(), WiFi.localIP().toString().c_str());
    Database.set<object_t>(aClient, "/remote/device", object_t(json), processData, "beatTask");
  }
}
