// 0x68 장치의 WHO_AM_I 레지스터를 읽어 실제 칩을 식별한다.
//
// Adafruit_MPU6050::begin()이 실패했으므로, 칩이 진짜 MPU-6050인지 확인한다.
// MPU-6050 계열 WHO_AM_I 레지스터: 0x75
//   0x68 = MPU-6050 (정품)
//   0x70 = MPU-6500
//   0x71 = MPU-9250
//   0x73 = MPU-9255
//   0x74/0x75/0x77/0x78 = MPU-6880 등 클론

#include <Wire.h>

const uint8_t ADDR = 0x68;
const uint8_t REG_WHO_AM_I = 0x75;
const uint8_t REG_PWR_MGMT_1 = 0x6B;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { }
  Wire.begin(D4, D5);  // SDA=D4(GPIO5), SCL=D5(GPIO6) - 명시적으로 지정
  delay(100);
}

void loop() {
  Serial.println("\n--- chip id probe ---");

  // 슬립 해제. 전원 인가 직후 MPU 계열은 슬립 상태다.
  Wire.beginTransmission(ADDR);
  Wire.write(REG_PWR_MGMT_1);
  Wire.write(0x00);
  uint8_t wakeResult = Wire.endTransmission();
  Serial.printf("wake write result: %d (0=ok)\n", wakeResult);
  delay(100);

  // WHO_AM_I 읽기
  Wire.beginTransmission(ADDR);
  Wire.write(REG_WHO_AM_I);
  uint8_t txResult = Wire.endTransmission(false);  // repeated start
  Serial.printf("reg select result: %d (0=ok)\n", txResult);

  uint8_t n = Wire.requestFrom((uint8_t)ADDR, (uint8_t)1);
  Serial.printf("bytes returned: %d\n", n);

  if (n == 1) {
    uint8_t id = Wire.read();
    Serial.printf("WHO_AM_I (0x75) = 0x%02X\n", id);

    switch (id) {
      case 0x68: Serial.println("  -> MPU-6050 (genuine)"); break;
      case 0x70: Serial.println("  -> MPU-6500"); break;
      case 0x71: Serial.println("  -> MPU-9250"); break;
      case 0x73: Serial.println("  -> MPU-9255"); break;
      default:   Serial.println("  -> unknown / clone variant"); break;
    }
  } else {
    Serial.println("read failed");
  }

  delay(3000);
}
