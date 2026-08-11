// MPU-6050 / MPU-6500 6축 IMU — 가속도/각속도 측정 및 움직임 감지
//
// 칩 자동 판별: "MPU-6050"으로 판매되는 모듈 상당수가 실제로는 MPU-6500이다.
//       I2C 주소(0x68)와 데이터 레지스터 배치가 같아 스캔으로는 구분되지 않고,
//       WHO_AM_I(0x75)를 읽어야 드러난다. 0x68=6050, 0x70=6500.
//       (개발에 쓴 개체는 0x70 = MPU-6500이었다. 그래서 Adafruit_MPU6050 라이브러리는
//        begin()에서 거부한다 — 이 스케치가 레지스터를 직접 다루는 이유다.)
//
//       가속도/자이로 스케일 계수는 두 칩이 같지만 온도 변환식이 다르므로
//       판별 결과에 따라 식을 바꾼다.
//
// 배선 (근거: docs/Pin Multiplexing ... "## IIC")
//   VCC -> XIAO 3V3 / GND -> GND / SDA -> D4(GPIO5) / SCL -> D5(GPIO6)
//   I2C 주소 0x68 (AD0=LOW)
//
// 사용자 LED는 액티브 로우 (docs/Getting Started ... "### Run your first Blink program")

#include <Wire.h>

const uint8_t IMU_ADDR = 0x68;

// 레지스터
const uint8_t REG_SMPLRT_DIV   = 0x19;
const uint8_t REG_CONFIG       = 0x1A;
const uint8_t REG_GYRO_CONFIG  = 0x1B;
const uint8_t REG_ACCEL_CONFIG = 0x1C;
const uint8_t REG_ACCEL_CONFIG2= 0x1D;
const uint8_t REG_ACCEL_XOUT_H = 0x3B;
const uint8_t REG_PWR_MGMT_1   = 0x6B;
const uint8_t REG_WHO_AM_I     = 0x75;

// 측정 범위에 대응하는 스케일. 아래 setup()의 설정과 반드시 함께 바꿀 것.
const float ACCEL_SCALE = 4096.0;   // ±8g  -> 4096 LSB/g
const float GYRO_SCALE  = 65.5;     // ±500 dps -> 65.5 LSB/(deg/s)
const float G_TO_MS2    = 9.80665;

// 움직임 판정 문턱값. 정지 시 실측 노이즈(가속도 ~0.05, 자이로 ~0.5)보다
// 충분히 크되 과하지 않게 잡는다.
const float ACCEL_THRESHOLD = 0.5;  // m/s^2 (기준선 대비 편차)
const float GYRO_THRESHOLD  = 5.0;  // deg/s

const int USER_LED = 21;

// 자이로 영점 보정값 (정지 상태에서 측정)
float gyroBiasX = 0, gyroBiasY = 0, gyroBiasZ = 0;

// 감지된 칩 종류. WHO_AM_I 값에 따라 온도 변환식이 달라진다.
enum ChipType { CHIP_UNKNOWN, CHIP_MPU6050, CHIP_MPU6500 };
ChipType chip = CHIP_UNKNOWN;

// 정지 시 가속도 크기의 실측 기준선.
// 이론상 9.80665여야 하지만 이 칩은 실측 ~10.7로 약 9% 높다 (영점 오프셋/스케일 편차).
// 이론값을 쓰면 정지 상태에서도 문턱값을 절반 이상 소모하므로 실측값을 쓴다.
float gravityBaseline = G_TO_MS2;

void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)IMU_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

// 가속도 3축 + 온도 + 자이로 3축을 한 번에 읽는다 (연속 14바이트).
// 한 번의 전송으로 읽어야 축 간 시간차가 생기지 않는다.
bool readAll(int16_t* ax, int16_t* ay, int16_t* az,
             int16_t* t,
             int16_t* gx, int16_t* gy, int16_t* gz) {
  Wire.beginTransmission(IMU_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom((uint8_t)IMU_ADDR, (uint8_t)14) != 14) return false;

  *ax = (Wire.read() << 8) | Wire.read();
  *ay = (Wire.read() << 8) | Wire.read();
  *az = (Wire.read() << 8) | Wire.read();
  *t  = (Wire.read() << 8) | Wire.read();
  *gx = (Wire.read() << 8) | Wire.read();
  *gy = (Wire.read() << 8) | Wire.read();
  *gz = (Wire.read() << 8) | Wire.read();
  return true;
}

// 정지 상태를 가정하고 (1) 자이로 오프셋과 (2) 중력 크기 기준선을 측정한다.
// MPU-6500은 정지 상태에서도 축마다 수 deg/s의 자이로 오프셋이 있다.
void calibrate(int samples) {
  Serial.printf("calibrating (%d samples) - keep still...\n", samples);
  float sx = 0, sy = 0, sz = 0, sMag = 0;
  int n = 0;
  int16_t ax, ay, az, t, gx, gy, gz;

  for (int i = 0; i < samples; i++) {
    if (readAll(&ax, &ay, &az, &t, &gx, &gy, &gz)) {
      sx += gx; sy += gy; sz += gz;

      float fax = ax / ACCEL_SCALE * G_TO_MS2;
      float fay = ay / ACCEL_SCALE * G_TO_MS2;
      float faz = az / ACCEL_SCALE * G_TO_MS2;
      sMag += sqrt(fax * fax + fay * fay + faz * faz);
      n++;
    }
    delay(5);
  }

  if (n == 0) {
    Serial.println("calibration failed - no samples");
    return;
  }

  gyroBiasX = sx / n;
  gyroBiasY = sy / n;
  gyroBiasZ = sz / n;
  gravityBaseline = sMag / n;

  Serial.printf("gyro bias (LSB): %.1f %.1f %.1f\n", gyroBiasX, gyroBiasY, gyroBiasZ);
  Serial.printf("gravity baseline: %.2f m/s^2 (theoretical %.2f)\n",
                gravityBaseline, G_TO_MS2);
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { }

  pinMode(USER_LED, OUTPUT);
  digitalWrite(USER_LED, HIGH);  // 액티브 로우 -> 꺼짐

  Wire.begin(D4, D5);  // SDA=D4(GPIO5), SCL=D5(GPIO6) - 명시적으로 지정
  Wire.setClock(400000);  // 400kHz fast mode

  uint8_t id = readReg(REG_WHO_AM_I);
  Serial.printf("WHO_AM_I = 0x%02X -> ", id);
  switch (id) {
    case 0x68:
      chip = CHIP_MPU6050;
      Serial.println("MPU-6050");
      break;
    case 0x70:
      chip = CHIP_MPU6500;
      Serial.println("MPU-6500");
      break;
    default:
      chip = CHIP_UNKNOWN;
      Serial.println("UNKNOWN chip - continuing with MPU-6500 settings");
      Serial.println("  (accel/gyro may still work; temperature likely wrong)");
      break;
  }

  writeReg(REG_PWR_MGMT_1, 0x80);  // 디바이스 리셋
  delay(100);
  writeReg(REG_PWR_MGMT_1, 0x01);  // 슬립 해제, 자이로 X축 PLL을 클럭원으로 사용
  delay(50);

  writeReg(REG_CONFIG, 0x03);         // DLPF ~41Hz (자이로)
  writeReg(REG_SMPLRT_DIV, 0x04);     // 1kHz / (1+4) = 200Hz 샘플레이트
  writeReg(REG_GYRO_CONFIG, 0x08);    // FS_SEL=1 -> ±500 dps
  writeReg(REG_ACCEL_CONFIG, 0x10);   // AFS_SEL=2 -> ±8g

  // ACCEL_CONFIG2(0x1D)는 MPU-6500에만 있는 가속도 DLPF 설정이다.
  // MPU-6050에는 없는 레지스터이므로 건드리지 않는다.
  if (chip != CHIP_MPU6050) {
    writeReg(REG_ACCEL_CONFIG2, 0x03);  // DLPF ~41Hz (가속도)
  }
  delay(50);

  calibrate(200);

  Serial.println("ready");
  Serial.println("accel(m/s^2) x y z | gyro(deg/s) x y z | temp | state");
}

void loop() {
  int16_t rax, ray, raz, rt, rgx, rgy, rgz;

  if (!readAll(&rax, &ray, &raz, &rt, &rgx, &rgy, &rgz)) {
    Serial.println("read failed");
    delay(500);
    return;
  }

  float ax = rax / ACCEL_SCALE * G_TO_MS2;
  float ay = ray / ACCEL_SCALE * G_TO_MS2;
  float az = raz / ACCEL_SCALE * G_TO_MS2;

  float gx = (rgx - gyroBiasX) / GYRO_SCALE;
  float gy = (rgy - gyroBiasY) / GYRO_SCALE;
  float gz = (rgz - gyroBiasZ) / GYRO_SCALE;

  // 온도 변환식은 칩마다 다르다. 잘못 쓰면 온도만 조용히 ~15도 틀린다.
  float tempC = (chip == CHIP_MPU6050) ? (rt / 340.0 + 36.53)   // MPU-6050
                                       : (rt / 333.87 + 21.0);  // MPU-6500

  // 중력을 뺀 순수 운동 성분. 이론값이 아니라 정지 시 실측 기준선을 뺀다.
  float accelMag = sqrt(ax * ax + ay * ay + az * az);
  float accelDelta = fabs(accelMag - gravityBaseline);
  float gyroMag = sqrt(gx * gx + gy * gy + gz * gz);

  bool moving = (accelDelta > ACCEL_THRESHOLD) || (gyroMag > GYRO_THRESHOLD);
  digitalWrite(USER_LED, moving ? LOW : HIGH);  // 움직이면 점등

  Serial.printf("A %6.2f %6.2f %6.2f | G %7.1f %7.1f %7.1f | %4.1fC | %s\n",
                ax, ay, az, gx, gy, gz, tempC,
                moving ? "MOVING" : "still");

  delay(100);
}
