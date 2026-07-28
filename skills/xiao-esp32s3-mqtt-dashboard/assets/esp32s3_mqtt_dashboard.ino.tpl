// Seeed XIAO ESP32S3 - WiFi STA + MQTT: touch sensor publish + LED control
// Multiple boards can run this exact sketch unmodified after templating:
// each one derives a unique device ID from its own MAC address, so topics
// never collide even when the same firmware is flashed to many boards.
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

const char* WIFI_SSID = "__WIFI_SSID__";
const char* WIFI_PASSWORD = "__WIFI_PASS__";

// LAN IP of the machine running the MQTT broker (Mosquitto). DHCP-assigned
// IPs can change on reboot — use a router DHCP reservation if possible.
const char* MQTT_HOST = "__MQTT_HOST__";
const int MQTT_PORT = 1883;

#define LED_PIN LED_BUILTIN   // GPIO21, inverted: LOW = on, HIGH = off
#define TOUCH_PIN T2          // GPIO2 / D1
#define TOUCH_THRESHOLD 40000 // touch value RISES above this when touched

WiFiClient net;
PubSubClient mqtt(net);

char deviceId[13];
char topicTouch[32];
char topicLedSet[32];
char topicLedState[32];
char topicStatus[32];

bool ledState = false;
unsigned long lastTouchPublish = 0;
const unsigned long TOUCH_INTERVAL_MS = 500;

void setLed(bool on, bool publishState) {
  ledState = on;
  digitalWrite(LED_PIN, on ? LOW : HIGH);
  if (publishState) {
    mqtt.publish(topicLedState, on ? "ON" : "OFF", true);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  msg.toUpperCase();

  if (String(topic) == topicLedSet) {
    if (msg == "ON" || msg == "1" || msg == "TRUE") setLed(true, true);
    else if (msg == "OFF" || msg == "0" || msg == "FALSE") setLed(false, true);
    else if (msg == "TOGGLE") setLed(!ledState, true);
  }
}

void buildTopics() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(deviceId, sizeof(deviceId), "%02x%02x%02x", mac[3], mac[4], mac[5]);
  snprintf(topicTouch, sizeof(topicTouch), "xiao/%s/touch", deviceId);
  snprintf(topicLedSet, sizeof(topicLedSet), "xiao/%s/led/set", deviceId);
  snprintf(topicLedState, sizeof(topicLedState), "xiao/%s/led/state", deviceId);
  snprintf(topicStatus, sizeof(topicStatus), "xiao/%s/status", deviceId);
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi \"");
  Serial.print(WIFI_SSID);
  Serial.print("\"");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
}

void connectMqtt() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT broker ");
    Serial.print(MQTT_HOST);
    Serial.print(" ...");
    String clientId = "xiao-" + String(deviceId);
    if (mqtt.connect(clientId.c_str(), NULL, NULL, topicStatus, 1, true, "offline")) {
      Serial.println(" connected");
      mqtt.subscribe(topicLedSet);
      mqtt.publish(topicStatus, "online", true);
      mqtt.publish(topicLedState, ledState ? "ON" : "OFF", true);
    } else {
      Serial.print(" failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" retrying in 2s");
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // off

  connectWiFi();
  buildTopics();
  Serial.print("Device ID: ");
  Serial.println(deviceId);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  connectMqtt();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  if (!mqtt.connected()) {
    connectMqtt();
  }
  mqtt.loop();

  unsigned long now = millis();
  if (now - lastTouchPublish >= TOUCH_INTERVAL_MS) {
    lastTouchPublish = now;
    uint32_t touchVal = touchRead(TOUCH_PIN);
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)touchVal);
    mqtt.publish(topicTouch, buf);
  }
}
