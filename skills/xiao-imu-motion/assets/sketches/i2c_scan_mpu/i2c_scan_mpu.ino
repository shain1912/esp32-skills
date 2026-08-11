// I2C 버스 스캐너 — 연결된 장치의 주소를 찾는다.
//
// 근거: docs/Pin Multiplexing with Seeed Studio XIAO ESP32-S3 (Sense).md "## IIC"
//   XIAO ESP32-S3의 I2C 핀: D4 = GPIO5 (SDA), D5 = GPIO6 (SCL)
//   Wire.begin(D4, D5)로 핀을 명시한다. 인자 없는 형태는 코어 버전에 따라
//   기본 핀이 달라질 수 있으므로 의존하지 않는다.
//
// MPU-6050 예상 주소: AD0=LOW이면 0x68, AD0=HIGH이면 0x69

#include <Wire.h>

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { }  // USB CDC 연결 대기 (최대 3초)

  Wire.begin(D4, D5);  // SDA=D4(GPIO5), SCL=D5(GPIO6) - 명시적으로 지정
  Serial.println("\n=== I2C Scanner ===");
  Serial.printf("SDA=GPIO%d, SCL=GPIO%d\n", SDA, SCL);
}

void loop() {
  int found = 0;

  Serial.println("\nScanning...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device found at 0x%02X", addr);
      if (addr == 0x68 || addr == 0x69) {
        Serial.print("  <- MPU-6050 예상 주소");
      }
      Serial.println();
      found++;
    }
  }

  if (found == 0) {
    Serial.println("  no I2C devices found");
  } else {
    Serial.printf("  total: %d device(s)\n", found);
  }

  delay(3000);
}
