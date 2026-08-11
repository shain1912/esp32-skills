---
name: xiao-imu-mpu-6050
description: >
  Motion sensing on a XIAO ESP32S3 with an MPU-6050 / MPU-6500 6-axis IMU:
  accelerometer and gyroscope over I2C, startup calibration, and a moving/still
  decision driven to the onboard LED. Use this skill WHENEVER the task involves
  an MPU-60x0 module on an ESP32S3 — "움직임 감지해줘", wiring one up, an
  Adafruit_MPU6050 sketch whose begin() returns "MPU6050 not found" even though
  the scan shows 0x68, a gyro that drifts hundreds of degrees per minute, an
  accelerometer whose magnitude sits well off 9.8 m/s² at rest, or deciding
  whether a module labelled MPU-6050 is actually an MPU-6500. Carries a
  hardware-verified library-free driver and the identity trap that makes a
  perfectly healthy module look broken.
license: MIT
compatibility: Windows + PowerShell 5.1, arduino-cli >= 1.x, esp32 core 3.x
---

# MPU-6050 / MPU-6500 motion sensing on the XIAO ESP32S3

Bus pins are fixed by the board: **SDA = D4 = GPIO5**, **SCL = D5 = GPIO6**.
`Wire.begin(D4, D5)` — pass the pins explicitly.

Read the `xiao-esp32s3` skill for the compile/upload workflow, `xiao-serial-monitor`
for watching the output, and `xiao-i2c-sensors` for the general scan-first method.
This skill is the MPU-60x0 instance of that method, plus the traps specific to it.

## The module is probably not the chip on the label

Modules sold as "MPU-6050" are frequently **MPU-6500**. Both answer at `0x68` and
share the same data-register layout, so **an I2C scan cannot tell them apart** —
the scan succeeds and the part still refuses to work with a 6050 driver.

`WHO_AM_I` (register `0x75`) is the only thing that settles it:

| Value | Chip |
|---|---|
| `0x68` | MPU-6050 |
| `0x70` | MPU-6500 |
| `0x71` | MPU-9250 |
| `0x73` | MPU-9255 |

Verified on the reference board:

```
WHO_AM_I = 0x70 -> MPU-6500
```

`imu_motion` reads this at boot and adapts. Run `whoami_mpu` first if a driver is
failing and you want the answer in isolation.

## Wiring

| MPU module | XIAO ESP32S3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | D4 (GPIO5) |
| SCL | D5 (GPIO6) |

AD0 unconnected or low → `0x68`. AD0 high → `0x69` (change `IMU_ADDR` in the sketch).

## Sketches

| Sketch | What it does |
|---|---|
| `assets/sketches/i2c_scan_mpu` | scan the bus, flag `0x68`/`0x69` as a likely MPU |
| `assets/sketches/whoami_mpu` | read `WHO_AM_I` and name the chip |
| `assets/sketches/imu_motion` | accel + gyro + temperature, auto chip detection, calibration, moving/still |

Copy the one you need into the user's workspace (folder name must match the
`.ino` name) rather than writing a driver from scratch. All three were run on real
hardware against an MPU-6500.

`imu_motion` needs **no library** — it talks to registers directly. That is not
purism: the obvious choice, `Adafruit_MPU6050`, cannot drive an MPU-6500 at all
(pitfall 1).

Verified output:

```
WHO_AM_I = 0x70 -> MPU-6500
calibrating (200 samples) - keep still...
gyro bias (LSB): 215.9 111.1 -38.6
gravity baseline: 10.75 m/s^2 (theoretical 9.81)
ready
accel(m/s^2) x y z | gyro(deg/s) x y z | temp | state
A   0.85   0.32  10.69 | G     0.0     0.0     0.0 | 28.2C | still
```

Move the sensor → `MOVING`, and the onboard LED (GPIO21, active **low**) lights.
Thresholds are at the top of the sketch: `ACCEL_THRESHOLD` 0.5 m/s², `GYRO_THRESHOLD` 5 °/s.
At rest the reference board held `still` across 70 consecutive samples with no false positives.

## Calibrate at boot, and measure the baseline rather than assuming it

Two corrections run in the first second. Both exist because the theoretical values
are wrong on real parts by margins that swamp the measurement.

**Gyro zero offset.** These chips read several deg/s while sitting still — the
reference unit showed 215.9 LSB on X, about 3.3 °/s. Integrate that for angle and
it drifts ~200° per minute. Averaging a few hundred stationary samples and
subtracting brings the resting reading to ±0.3 °/s.

**Gravity baseline.** At rest the accelerometer magnitude must equal 9.81 m/s²
whatever the orientation — magnitude is rotation-invariant, so tilt cannot explain
a deviation. The reference unit reads **10.75**, about 9 % high, which is ordinary
for clone parts (zero-g offset plus sensitivity tolerance). Subtracting the
theoretical 9.81 leaves 0.94 of standing error, consuming most of a sensible motion
threshold before the sensor has moved at all. So the sketch measures the resting
magnitude at boot and subtracts *that*:

```cpp
float accelDelta = fabs(accelMag - gravityBaseline);  // not - 9.80665
```

The cost is that the baseline is orientation-specific enough to matter if the
module is remounted; reset the board to re-measure. Say so when handing the stream
to the user — motion during calibration is silently baked in as the zero point,
and opening a serial monitor resets the board, so calibration reruns every time.

## Pitfalls

These each cost a debugging session once.

1. **`Adafruit_MPU6050` cannot drive an MPU-6500.** `begin()` checks `WHO_AM_I`,
   sees `0x70` instead of `0x68`, and returns false — so a correctly wired module
   reports `MPU6050 not found - check wiring` while an I2C scan shows `0x68`
   plainly. The wiring is fine; the library is simply refusing the part. Use the
   register-level sketch here, and read `WHO_AM_I` before blaming the bus.

2. **The temperature formula differs between the two chips.** MPU-6050 is
   `raw/340 + 36.53`; MPU-6500 is `raw/333.87 + 21.0`. Everything else about the
   data registers is identical, so using the wrong one produces plausible-looking
   temperature that is off by roughly 15 °C — the one channel with no obvious
   sanity check. `imu_motion` picks the formula from the detected chip.

3. **`ACCEL_CONFIG2` (`0x1D`) exists only on the MPU-6500.** It sets the
   accelerometer DLPF. On an MPU-6050 that address is not a documented register,
   so writing the accel filter config unconditionally pokes at something undefined.
   Gate it on the detected chip.

4. **Wake the part before reading anything.** MPU-60x0 devices boot into sleep and
   return stale or zero data until `PWR_MGMT_1` (`0x6B`) is cleared. The sketch
   resets (`0x80`), waits, then selects the gyro X PLL as clock source (`0x01`),
   which is more stable than the internal oscillator.

5. **Read all 14 data bytes in one burst.** `ACCEL_XOUT_H` (`0x3B`) through the
   gyro registers is one contiguous block. Reading axes in separate transactions
   samples them at different instants, which shows up as phantom rotation during
   fast motion.

6. **A message printed once in `setup()` is easy to lose.** Draining the serial
   buffer before reading — the usual way to skip stale output — also discards the
   boot banner and any one-shot error line, so a sketch stuck in an error branch
   looks like a board producing no output at all. When debugging startup, read
   without discarding. This is how pitfall 1 first presented: zero bytes, no clue.

7. **Distinguish "stuck" from "crash-looping" with `arduino-cli board list`.**
   Port still enumerating → the sketch is running and stuck or silent. Port gone or
   flickering → the board is resetting in a loop. Note that ROM bootloader messages
   never reach USB in `hwcdc` mode (they go to UART0 on D6/D7), so their absence is
   normal and not evidence of a boot problem.

8. **Keep serial output ASCII.** The Windows console reads the port as the ANSI
   codepage, so Korean text in `Serial.print` comes back as `??????` in captured
   logs. Explanations belong in comments; printed strings stay English.

## Handing the stream to the user

Motion data is something the user watches while moving the board — a capture taken
while it sat still on a desk shows nothing interesting. Verify with a bounded read
yourself, then hand over the live command (see `xiao-serial-monitor`):

```powershell
.\scripts\mon.ps1 -Seconds 15    # your check; returns
mon                              # theirs; runs until Ctrl+C
```

Never launch the interactive monitor from a tool call — it never returns and the
session hangs. And remind them that an open monitor holds the port, so it has to be
closed before the next upload.

## Adding another IMU

The method generalises: scan for the address, identify the part from its ID
register before choosing a driver, wake it explicitly, read its data block in one
burst, and calibrate against what the part actually reports at rest rather than
what the datasheet says it should. The three sketches here are just instances of it.
