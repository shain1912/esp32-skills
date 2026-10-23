// XIAO ESP32-S3 Web Dashboard
// - Web UI to turn the user LED on/off
// - Live touch sensor state on D0 (GPIO1, TOUCH1)
// - Syncs with Supabase table public.xiao_devices for remote (internet) control
// - Camera MJPEG stream on port 81 (same network only, Sense board)
// User LED: GPIO21 (LED_BUILTIN), active-low (LOW = on, HIGH = off)
// Local: http://<board IP>/   Remote: Netlify dashboard via Supabase

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include "secrets.h"
#include "dashboard_html.h"
#include "camera_stream.h"

const int TOUCH_PIN = D0;
const float TOUCH_RATIO = 1.3;  // touched when value > baseline * ratio

const uint32_t SYNC_INTERVAL_MS = 200;    // poll LED command from Supabase
const uint32_t HEARTBEAT_MS = 3000;       // push status even if nothing changed

WebServer server(80);
WiFiClientSecure tlsClient;
HTTPClient http;
const String deviceUrl = String(SUPABASE_URL) + "/rest/v1/xiao_devices?id=eq." + DEVICE_ID;

uint32_t baseline = 0;
uint32_t touchValue = 0;
bool touched = false;
bool ledOn = false;

bool ledPending = false;     // LED changed locally, push to Supabase
bool lastSentTouched = false;
uint32_t lastPush = 0;
bool cloudOk = false;
bool cameraOk = false;

void setLed(bool on) {
  ledOn = on;
  digitalWrite(LED_BUILTIN, on ? LOW : HIGH);
}

// ---------- Supabase ----------

String isoNow() {
  time_t now = time(nullptr);
  if (now < 1700000000) return "";  // NTP not synced yet
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
  return buf;
}

// Applies "led" from a PostgREST row array response, e.g. [{"led":true,...}]
void applyCloudLed(const String& body) {
  if (ledPending) return;  // local change not pushed yet, keep it
  if (body.indexOf("\"led\":true") >= 0) setLed(true);
  else if (body.indexOf("\"led\":false") >= 0) setLed(false);
}

bool cloudRequest(const char* method, const String& url, const String& payload, String& response) {
  http.begin(tlsClient, url);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  if (payload.length()) http.addHeader("Prefer", "return=representation");
  int code = http.sendRequest(method, payload);
  response = code > 0 ? http.getString() : "";
  http.end();
  if (code != 200) Serial.printf("Supabase %s failed: %d %s\n", method, code, response.c_str());
  return code == 200;
}

void syncCloud() {
  String response;
  bool needPush = ledPending || touched != lastSentTouched || millis() - lastPush >= HEARTBEAT_MS;

  if (needPush) {
    String ts = isoNow();
    String payload = "{\"touched\":" + String(touched ? "true" : "false") +
                     ",\"touch_value\":" + touchValue +
                     ",\"threshold\":" + (uint32_t)(baseline * TOUCH_RATIO) +
                     ",\"rssi\":" + WiFi.RSSI();
    if (ledPending) payload += ",\"led\":" + String(ledOn ? "true" : "false");
    if (ts.length()) payload += ",\"updated_at\":\"" + ts + "\"";
    payload += "}";

    bool sentLed = ledPending;
    cloudOk = cloudRequest("PATCH", deviceUrl, payload, response);
    if (cloudOk) {
      lastSentTouched = touched;
      lastPush = millis();
      if (sentLed) ledPending = false;
      applyCloudLed(response);
    }
  } else {
    cloudOk = cloudRequest("GET", deviceUrl + "&select=led", "", response);
    if (cloudOk) applyCloudLed(response);
  }
}

// ---------- Local web server ----------

void sendStatus() {
  char json[192];
  snprintf(json, sizeof(json),
           "{\"led\":%s,\"touched\":%s,\"touch\":%u,\"threshold\":%u,\"rssi\":%d,\"cloud\":%s,\"camera\":%s}",
           ledOn ? "true" : "false", touched ? "true" : "false", touchValue,
           (uint32_t)(baseline * TOUCH_RATIO), WiFi.RSSI(), cloudOk ? "true" : "false",
           cameraOk ? "true" : "false");
  server.send(200, "application/json", json);
}

void handleLed() {
  String state = server.arg("state");
  if (state == "on") setLed(true);
  else if (state == "off") setLed(false);
  else if (state == "toggle") setLed(!ledOn);
  else {
    server.send(400, "text/plain", "state must be on, off or toggle");
    return;
  }
  ledPending = true;
  sendStatus();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  setLed(false);

  // Calibrate untouched baseline (do not touch the pin during boot)
  delay(500);
  uint64_t sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += touchRead(TOUCH_PIN);
    delay(10);
  }
  baseline = sum / 20;

  cameraOk = startCamera();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());

  configTime(0, 0, "pool.ntp.org", "time.google.com");
  tlsClient.setInsecure();  // skip certificate check (demo); traffic is still encrypted
  http.setReuse(true);
  http.setTimeout(3000);

  if (MDNS.begin("xiao")) {
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/", []() { server.send_P(200, "text/html", DASHBOARD_HTML); });
  server.on("/api/status", sendStatus);
  server.on("/api/led", handleLed);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();

  if (cameraOk) {
    startStreamServer();
    Serial.printf("Camera stream: http://%s:81/stream\n", WiFi.localIP().toString().c_str());
  }
}

void loop() {
  server.handleClient();

  static uint32_t lastRead = 0;
  if (millis() - lastRead >= 50) {
    lastRead = millis();
    touchValue = touchRead(TOUCH_PIN);
    touched = touchValue > baseline * TOUCH_RATIO;
  }

  static uint32_t lastSync = 0;
  if (WiFi.status() == WL_CONNECTED && millis() - lastSync >= SYNC_INTERVAL_MS) {
    syncCloud();
    lastSync = millis();
  }

  static uint32_t lastLog = 0;
  if (millis() - lastLog >= 5000) {
    lastLog = millis();
    Serial.printf("IP: %s  touch: %u  led: %s  cloud: %s\n", WiFi.localIP().toString().c_str(),
                  touchValue, ledOn ? "on" : "off", cloudOk ? "ok" : "error");
  }
}
