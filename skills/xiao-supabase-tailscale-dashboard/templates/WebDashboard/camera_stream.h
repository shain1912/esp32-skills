// XIAO ESP32-S3 Sense camera + MJPEG stream server
// Stream: http://<board IP>:81/stream, snapshot: /capture
// Each client is served in its own task, so streaming never blocks loop() or other viewers.
// Uses a minimal HTTP server instead of esp_http_server: its 1024-byte request header limit
// is compiled into the core and rejects requests proxied by Tailscale Serve (431).
// Requires PSRAM enabled (FQBN option PSRAM=opi)
#pragma once

#include <atomic>
#include "esp_camera.h"
#include <WiFi.h>

// Camera pins for XIAO ESP32-S3 Sense (see xiao.md "Camera" table)
#define CAM_PIN_PWDN  -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK  10
#define CAM_PIN_SIOD  40
#define CAM_PIN_SIOC  39
#define CAM_PIN_Y9    48
#define CAM_PIN_Y8    11
#define CAM_PIN_Y7    12
#define CAM_PIN_Y6    14
#define CAM_PIN_Y5    16
#define CAM_PIN_Y4    18
#define CAM_PIN_Y3    17
#define CAM_PIN_Y2    15
#define CAM_PIN_VSYNC 38
#define CAM_PIN_HREF  47
#define CAM_PIN_PCLK  13

#define STREAM_BOUNDARY "xiaoframe"

#define MAX_STREAMS 3  // concurrent viewers
#define MAX_CLIENTS 6  // concurrent connections of any kind

static WiFiServer camServer(81);
static std::atomic<int> activeStreams{0};
static std::atomic<int> activeClients{0};

bool startCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_Y2;
  config.pin_d1 = CAM_PIN_Y3;
  config.pin_d2 = CAM_PIN_Y4;
  config.pin_d3 = CAM_PIN_Y5;
  config.pin_d4 = CAM_PIN_Y6;
  config.pin_d5 = CAM_PIN_Y7;
  config.pin_d6 = CAM_PIN_Y8;
  config.pin_d7 = CAM_PIN_Y9;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;  // 640x480
  config.jpeg_quality = 12;           // 0-63, lower = better quality
  config.fb_count = 3;  // one per concurrent viewer
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (!psramFound()) {
    Serial.println("PSRAM not found - build with PSRAM=opi");
    return false;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x (is the Sense camera board attached?)\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  Serial.printf("Camera ready, sensor PID: 0x%x\n", s->id.PID);
  return true;
}

// Reads one CRLF-terminated line; returns false on timeout/disconnect
static bool readLine(WiFiClient& client, String& line, uint32_t deadline, size_t maxLen) {
  line = "";
  while (millis() < deadline && client.connected()) {
    if (!client.available()) {
      vTaskDelay(1);
      continue;
    }
    char c = client.read();
    if (c == '\n') return true;
    if (c != '\r' && line.length() < maxLen) line += c;
  }
  return false;
}

static void sendSimple(WiFiClient& client, const char* status, const char* body) {
  client.printf("HTTP/1.1 %s\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n\r\n%s", status, body);
}

static void sendCapture(WiFiClient& client) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    sendSimple(client, "500 Internal Server Error", "Camera capture failed");
    return;
  }
  client.printf("HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n"
                "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                fb->len);
  client.write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

static void sendStream(WiFiClient& client) {
  if (activeStreams >= MAX_STREAMS) {
    sendSimple(client, "503 Service Unavailable", "Too many viewers");
    return;
  }
  int count = ++activeStreams;
  Serial.printf("Stream opened (%d active)\n", count);

  client.print("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY "\r\n"
               "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
  char header[96];
  while (client.connected()) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      break;
    }
    int headerLen = snprintf(header, sizeof(header),
                             "\r\n--" STREAM_BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                             fb->len);
    bool ok = client.write((const uint8_t*)header, headerLen) == (size_t)headerLen &&
              client.write(fb->buf, fb->len) == fb->len;
    esp_camera_fb_return(fb);
    if (!ok) break;  // client disconnected
  }

  int remaining = --activeStreams;
  Serial.printf("Stream closed (%d active)\n", remaining);
}

static void camClientTask(void* arg) {
  WiFiClient* client = (WiFiClient*)arg;
  uint32_t deadline = millis() + 5000;
  String requestLine, line;

  // Request line, then skip headers of any length until the blank line
  if (readLine(*client, requestLine, deadline, 512)) {
    while (readLine(*client, line, deadline, 64) && line.length() > 0) {
    }
    // "GET /stream?t=123 HTTP/1.1" -> "/stream"
    int start = requestLine.indexOf(' ') + 1;
    int end = requestLine.indexOf(' ', start);
    String path = requestLine.substring(start, end);
    int query = path.indexOf('?');
    if (query >= 0) path = path.substring(0, query);

    if (!requestLine.startsWith("GET ")) sendSimple(*client, "405 Method Not Allowed", "GET only");
    else if (path == "/stream") sendStream(*client);
    else if (path == "/capture") sendCapture(*client);
    else sendSimple(*client, "404 Not Found", "Not found");
  }

  client->stop();
  delete client;
  activeClients--;
  vTaskDelete(nullptr);
}

static void camServerTask(void*) {
  while (true) {
    WiFiClient client = camServer.accept();
    if (!client) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (activeClients >= MAX_CLIENTS) {
      sendSimple(client, "503 Service Unavailable", "Server busy");
      client.stop();
      continue;
    }
    client.setNoDelay(true);
    WiFiClient* heapClient = new WiFiClient(client);
    activeClients++;
    if (xTaskCreate(camClientTask, "camClient", 6144, heapClient, 5, nullptr) != pdPASS) {
      heapClient->stop();
      delete heapClient;
      activeClients--;
    }
  }
}

void startStreamServer() {
  camServer.begin();
  xTaskCreate(camServerTask, "camServer", 4096, nullptr, 5, nullptr);
}
