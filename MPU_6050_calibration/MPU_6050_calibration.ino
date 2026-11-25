// MPU6050 calibration helper for the Digital Fish Tank
// XIAO ESP32-S3: MPU6050 SDA -> D4, SCL -> D5, VCC -> 3V3, GND -> GND
//
// Open Serial Monitor at 115200 baud, line ending "No line ending" or "Newline".
//   send 'a' = guided calibration (about 1.5 minutes)
//   send 'p' = live plotter mode (open Tools > Serial Plotter to see ax, ay, az)
//
// Mount the MPU6050 exactly as it will be in the final device, chip facing up.
// "Left" and "right" are as you look at the OLED screen.

#include <Arduino.h>
#include <Wire.h>

constexpr uint8_t MPU_ADDR = 0x68;           // 0x69 if AD0 is pulled high
constexpr int NTH = 5;
const float TH[NTH] = {0.3f, 0.45f, 0.6f, 0.8f, 1.0f};   // shake thresholds to test (g)

// ---------------- sensor ----------------
float gSlow[3] = {0, 0, 1};
float aPrev[3] = {0, 0, 1};

struct Sample { float a[3]; float d[3]; float hp; float jump; };

bool imuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool initImu() {
  if (!imuWrite(0x6B, 0x00)) return false;
  delay(50);
  imuWrite(0x1C, 0x10);   // +-8 g, 4096 counts per g (same as the fish tank)
  imuWrite(0x1A, 0x02);   // ~94 Hz filter (same as the fish tank)
  return true;
}

bool readAccel(float a[3]) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, (uint8_t)6) != 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  a[0] = (int16_t)((b[0] << 8) | b[1]) / 4096.0f;
  a[1] = (int16_t)((b[2] << 8) | b[3]) / 4096.0f;
  a[2] = (int16_t)((b[4] << 8) | b[5]) / 4096.0f;
  return true;
}

// One reading every >= 5 ms. Same maths as updateImu() in the fish tank.
bool takeSample(Sample &s) {
  static unsigned long last = 0;
  unsigned long now = millis();
  if (now - last < 5) return false;
  float dt = (now - last) / 1000.0f;
  last = now;
  if (dt > 0.2f) dt = 0.2f;
  if (!readAccel(s.a)) return false;
  float k = dt / (0.40f + dt);
  float hp2 = 0, j2 = 0;
  for (int i = 0; i < 3; i++) {
    gSlow[i] += (s.a[i] - gSlow[i]) * k;
    s.d[i] = s.a[i] - gSlow[i];
    hp2 += s.d[i] * s.d[i];
    float dj = s.a[i] - aPrev[i];
    j2 += dj * dj;
    aPrev[i] = s.a[i];
  }
  s.hp = sqrtf(hp2);
  s.jump = sqrtf(j2);
  return true;
}

// ---------------- results ----------------
float zeroMean[3], zeroStd[3];
float leftMean[3], rightMean[3], flipMean[3];
float stillHpMax = 0, stillJumpMax = 0;
float knockPeaks[20];
int   nKnock = 0;
int   shakeSwings[NTH];
float shakeHpMax = 0;
int   wiggleSwings[NTH];
float wiggleJumpMax = 0, wiggleHpMax = 0;

// counts back-and-forth swings per threshold (same logic as the fish tank)
struct SwingCounter {
  int sign[NTH][3];
  int count[NTH];
  unsigned long last[NTH];
  void reset() {
    memset(sign, 0, sizeof(sign));
    memset(count, 0, sizeof(count));
    memset(last, 0, sizeof(last));
  }
  void feed(const float d[3], unsigned long now) {
    for (int t = 0; t < NTH; t++) {
      for (int i = 0; i < 3; i++) {
        int sg = (d[i] > TH[t]) ? 1 : (d[i] < -TH[t]) ? -1 : 0;
        if (sg == 0) continue;
        if (sign[t][i] != 0 && sg != sign[t][i]) count[t]++;
        sign[t][i] = sg;
        last[t] = now;
      }
      if (now - last[t] > 600) for (int i = 0; i < 3; i++) sign[t][i] = 0;
    }
  }
};

// ---------------- phases ----------------
void getReady(const char *msg, int sec) {
  Serial.printf("\n>>> %s\n", msg);
  for (int s = sec; s > 0; s--) {
    Serial.printf("    starting in %d...\n", s);
    unsigned long t0 = millis();
    Sample sm;
    while (millis() - t0 < 1000) takeSample(sm);   // keep filters running
  }
  Serial.println("    RECORDING - go!");
}

// Hold the device in one position and average it.
void holdPhase(const char *msg, float mean[3], float stdv[3], int recSec, bool trackNoise) {
  getReady(msg, 4);
  double sum[3] = {0, 0, 0}, sum2[3] = {0, 0, 0};
  long n = 0;
  unsigned long t0 = millis();
  Sample s;
  while (millis() - t0 < (unsigned long)recSec * 1000UL) {
    if (!takeSample(s)) continue;
    for (int i = 0; i < 3; i++) { sum[i] += s.a[i]; sum2[i] += (double)s.a[i] * s.a[i]; }
    n++;
    if (trackNoise) {
      if (s.hp > stillHpMax) stillHpMax = s.hp;
      if (s.jump > stillJumpMax) stillJumpMax = s.jump;
    }
  }
  for (int i = 0; i < 3; i++) {
    mean[i] = n ? sum[i] / n : 0;
    if (stdv) {
      double v = n ? sum2[i] / n - (double)mean[i] * mean[i] : 0;
      stdv[i] = v > 0 ? sqrt(v) : 0;
    }
  }
  Serial.printf("    mean  X=%.3f  Y=%.3f  Z=%.3f   (%ld samples)\n", mean[0], mean[1], mean[2], n);
}

// Tap the device repeatedly; records the peak "jump" of every knock.
void knockPhase() {
  getReady("KNOCK: tap or bump the device about 8 times, 1 second apart. Use the strength you want to count as a knock.", 4);
  bool inPeak = false;
  unsigned long peakStart = 0, lastEnd = 0, t0 = millis();
  float peakMax = 0;
  Sample s;
  while (millis() - t0 < 12000UL) {
    if (!takeSample(s)) continue;
    unsigned long now = millis();
    if (!inPeak && s.jump > 0.12f && now - lastEnd > 300) {
      inPeak = true; peakStart = now; peakMax = s.jump;
    } else if (inPeak) {
      if (s.jump > peakMax) peakMax = s.jump;
      if (now - peakStart > 120) {
        inPeak = false; lastEnd = now;
        if (nKnock < 20) knockPeaks[nKnock++] = peakMax;
        Serial.printf("    knock #%d  peak jump = %.2f g\n", nKnock, peakMax);
      }
    }
  }
}

// Shake hard; counts swings at several thresholds.
void shakePhase() {
  getReady("SHAKE: shake the device hard, back and forth, for 6 seconds like you mean it.", 4);
  SwingCounter sc; sc.reset();
  unsigned long t0 = millis();
  Sample s;
  while (millis() - t0 < 6000UL) {
    if (!takeSample(s)) continue;
    sc.feed(s.d, millis());
    if (s.hp > shakeHpMax) shakeHpMax = s.hp;
  }
  for (int t = 0; t < NTH; t++) shakeSwings[t] = sc.count[t];
  Serial.printf("    biggest movement = %.2f g\n", shakeHpMax);
}

// Normal handling: slow tilts left/right and gentle carrying. Nothing should trigger here.
void wigglePhase() {
  getReady("GENTLE: tilt slowly left and right and move the device around gently (as if playing with the tank) for 8 seconds. NO knocks, NO shakes.", 4);
  SwingCounter sc; sc.reset();
  unsigned long t0 = millis();
  Sample s;
  while (millis() - t0 < 8000UL) {
    if (!takeSample(s)) continue;
    sc.feed(s.d, millis());
    if (s.jump > wiggleJumpMax) wiggleJumpMax = s.jump;
    if (s.hp > wiggleHpMax) wiggleHpMax = s.hp;
  }
  for (int t = 0; t < NTH; t++) wiggleSwings[t] = sc.count[t];
  Serial.printf("    biggest jump = %.2f g, biggest movement = %.2f g\n", wiggleJumpMax, wiggleHpMax);
}

// ---------------- report ----------------
void report() {
  const char *names[3] = {"X", "Y", "Z"};
  Serial.println("\n\n==================== RESULTS ====================");

  // sanity: gravity at rest
  float g = sqrtf(zeroMean[0] * zeroMean[0] + zeroMean[1] * zeroMean[1] + zeroMean[2] * zeroMean[2]);
  Serial.printf("Level reading: X=%.3f Y=%.3f Z=%.3f  |g|=%.3f  (should be about 1.00)\n",
                zeroMean[0], zeroMean[1], zeroMean[2], g);
  Serial.printf("Noise at rest (std): X=%.4f Y=%.4f Z=%.4f\n", zeroStd[0], zeroStd[1], zeroStd[2]);
  if (zeroMean[2] < 0.8f) Serial.println("WARNING: Z is not close to +1 g at rest. Is the chip facing up and the device flat?");

  // tilt axis and sign
  float diff[3];
  int ax = 0;
  for (int i = 0; i < 3; i++) {
    diff[i] = rightMean[i] - leftMean[i];
    if (fabsf(diff[i]) > fabsf(diff[ax])) ax = i;
  }
  Serial.printf("\nRight minus left:  X=%.3f  Y=%.3f  Z=%.3f\n", diff[0], diff[1], diff[2]);
  int sign = (diff[ax] > 0) ? 1 : -1;
  float rightT = sign * (rightMean[ax] - zeroMean[ax]);
  float leftT  = sign * (leftMean[ax] - zeroMean[ax]);
  float maxTilt = fminf(fabsf(rightT), fabsf(leftT));
  Serial.printf("Tilt axis = %s, sign = %d\n", names[ax], sign);
  Serial.printf("Tilt reached: right = %+.2f, left = %+.2f (fish tank scale -1..+1)\n", rightT, leftT);
  if (ax == 2) Serial.println("WARNING: Z changed most. The fish tank only supports X or Y for tilt. The chip may be mounted vertically.");
  if (fabsf(diff[ax]) < 0.3f) Serial.println("WARNING: left/right difference is small. Tilt further next time.");
  float other = 0;
  for (int i = 0; i < 2; i++) if (i != ax && fabsf(diff[i]) > other) other = fabsf(diff[i]);
  if (other > 0.5f * fabsf(diff[ax])) Serial.println("WARNING: another axis also changed a lot. The chip may be rotated diagonally on the board.");
  if (maxTilt < 0.05f) maxTilt = 0.05f;
  float aimGain = 58.0f / (0.9f * maxTilt);     // aim marker reaches the screen edge at ~90% of your tilt
  float sloshGain = 1.0f / maxTilt;             // full slosh at your full tilt

  // still threshold
  float stillG = fmaxf(0.03f, stillHpMax * 2.5f);

  // knock threshold
  float lowK = 1.5f * fmaxf(stillJumpMax, wiggleJumpMax);
  float knockMin = 99, sumK = 0;
  for (int i = 0; i < nKnock; i++) { if (knockPeaks[i] < knockMin) knockMin = knockPeaks[i]; sumK += knockPeaks[i]; }
  Serial.printf("\nKnocks recorded: %d", nKnock);
  if (nKnock) Serial.printf("  (weakest %.2f g, average %.2f g)", knockMin, sumK / nKnock);
  Serial.printf("\nNoise jump at rest = %.2f g, during gentle handling = %.2f g\n", stillJumpMax, wiggleJumpMax);
  float tapG = 0.45f;
  if (nKnock >= 3) {
    float highK = 0.7f * knockMin;
    if (lowK < highK) tapG = (lowK + highK) / 2.0f;
    else { tapG = lowK; Serial.println("WARNING: knocks are barely above the handling noise. Knock harder or accept missed knocks."); }
  } else {
    Serial.println("WARNING: fewer than 3 knocks detected. TAP_G left at the default. Knock harder and re-run.");
  }

  // shake threshold
  Serial.println("\nShake swings counted (threshold: shake / gentle):");
  int good[NTH], ng = 0;
  for (int t = 0; t < NTH; t++) {
    Serial.printf("   %.2f g :  %3d  /  %3d\n", TH[t], shakeSwings[t], wiggleSwings[t]);
    if (shakeSwings[t] >= 10 && wiggleSwings[t] == 0) good[ng++] = t;
  }
  float shakeG = 0.6f;
  if (ng > 0) shakeG = TH[good[ng / 2]];
  else Serial.println("WARNING: no threshold separates shaking from gentle use. Shake harder or re-run.");

  // flip
  Serial.printf("\nUpside down: Z = %.3f (flat was %.3f)\n", flipMean[2], zeroMean[2]);
  if (flipMean[2] > -0.8f) Serial.println("WARNING: Z did not go close to -1 g when flipped. Turn it fully upside down.");
  else Serial.println("Flip detection (Z < -0.6 for 0.5 s) will work with this mounting.");

  Serial.println("\n============ PASTE INTO THE FISH TANK ============");
  Serial.printf("#define TILT_AXIS   %d      // %s axis\n", ax, names[ax]);
  Serial.printf("#define TILT_SIGN  %d\n", sign);
  Serial.printf("const float TAP_G        = %.2ff;\n", tapG);
  Serial.printf("const float STILL_G      = %.2ff;\n", stillG);
  Serial.printf("const float SHAKE_G      = %.2ff;   // replace the hard-coded 0.6f in updateImu()\n", shakeG);
  Serial.printf("const float AIM_GAIN     = %.0ff;   // replace 120.0f in updateReactions()\n", aimGain);
  Serial.printf("const float SLOSH_GAIN   = %.1ff;   // replace 2.0f in: target = tilt * 2.0f\n", sloshGain);
  Serial.println("(tiltZero is measured at start-up, keep the device level then)");
  Serial.println("==================================================");
  Serial.println("Copy this whole output and send it back to me. Reset the board to run again.");
}

// ---------------- main ----------------
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000) delay(10);
  Wire.begin();          // XIAO ESP32-S3: SDA = D4, SCL = D5
  Wire.setClock(400000);

  if (!initImu()) {
    Serial.println("MPU6050 not found. Check wiring and address 0x68 / 0x69.");
    while (true) delay(1000);
  }
  Serial.println("\nMPU6050 found.");
  Serial.println("Send 'a' for guided calibration, or 'p' for live plotter mode.");

  while (!Serial.available()) delay(10);
  char c = Serial.read();

  if (c == 'p') {
    Sample s;
    while (true) {
      if (takeSample(s)) {
        Serial.printf("ax:%.3f,ay:%.3f,az:%.3f,jump:%.3f,hp:%.3f\n", s.a[0], s.a[1], s.a[2], s.jump, s.hp);
        delay(15);
      }
    }
  }

  Serial.println("\nPlace the device exactly as it will be used. Keep it still at first.");

  // prime filters
  float a[3];
  readAccel(a);
  for (int i = 0; i < 3; i++) { gSlow[i] = a[i]; aPrev[i] = a[i]; }

  holdPhase("1/7 LEVEL: put the device flat and level on the table and DON'T touch it.", zeroMean, zeroStd, 6, true);
  holdPhase("2/7 LEFT: tilt the LEFT side of the screen down as far as you would normally tilt it, and hold.", leftMean, nullptr, 4, false);
  holdPhase("3/7 RIGHT: tilt the RIGHT side of the screen down by the same amount, and hold.", rightMean, nullptr, 4, false);

  Serial.println("\nPut the device flat and still again...");
  delay(2000);
  knockPhase();
  shakePhase();
  wigglePhase();
  holdPhase("7/7 FLIP: turn the device fully UPSIDE DOWN and hold.", flipMean, nullptr, 3, false);

  Serial.println("\nYou can turn it back now.");
  report();
}

void loop() {}