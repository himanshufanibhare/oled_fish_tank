// Digital Fish Tank - single file version
// XIAO ESP32-S3 + 1.3" SH1106 OLED (I2C) + touch sensor (e.g. TTP223) + MPU6050 (I2C)
//
// Wiring:
//   OLED  VCC -> 3V3, GND -> GND, SDA -> D4, SCL -> D5
//   MPU6050 VCC -> 3V3, GND -> GND, SDA -> D4, SCL -> D5   (same two I2C wires as the OLED)
//   Touch VCC -> 3V3, GND -> GND, OUT -> D1
//
// Libraries: "Adafruit SH110X" and "Adafruit GFX Library" (Library Manager)

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

constexpr int JUPITER_SCREEN_WIDTH = 128;
constexpr int JUPITER_SCREEN_HEIGHT = 64;
constexpr int JUPITER_OLED_RESET = -1;
constexpr uint8_t JUPITER_OLED_ADDRESS = 0x3C;   // some displays use 0x3D

Adafruit_SH1106G display(JUPITER_SCREEN_WIDTH,
                         JUPITER_SCREEN_HEIGHT,
                         &Wire,
                         JUPITER_OLED_RESET);

bool initializeDisplay() {
  // XIAO ESP32-S3 default I2C pins: SDA = D4, SCL = D5
  Wire.begin();
  Wire.setClock(400000);

  if (!display.begin(JUPITER_OLED_ADDRESS, true)) {
    return false;
  }

  display.clearDisplay();
  display.display();
  return true;
}

void clearDisplay() {
  display.clearDisplay();
}

void updateDisplay() {
  display.display();
}

// Dim / restore the OLED brightness (used by sleep mode and night mode)
void setDisplayDim(bool dim) {
  display.setContrast(dim ? 0x02 : 0xFF);   // 0xFF = the full brightness set at start-up
}

// ============================================================================
// FISH TANK
// ============================================================================

#define TOUCH_PIN   D1
#define SCREEN_W    JUPITER_SCREEN_WIDTH
#define SCREEN_H    JUPITER_SCREEN_HEIGHT
#define FLOOR_Y     60
#define FRAME_MS    40      // ~25 FPS

// Water surface waves: true = show, false = hide
// const bool SHOW_WATER_WAVE = true;
const bool SHOW_WATER_WAVE = false;

// Stones on the ground: true = show, false = hide
const bool SHOW_STONES = true;

// ---- MPU6050 features: each one can be switched off with false ----
const bool USE_TILT_SLOSH  = true;   // tilting makes the water slosh (fish, food, bubbles, seaweed drift)
const bool USE_TILT_AIM    = true;   // tilting moves an aim marker; touch drops the food where it points
const bool USE_KNOCK_SCARE = true;   // a knock or bump startles the fish; small octopuses jump
const bool USE_SHAKE_INK   = true;   // a hard shake: fish panic, the big octopus squirts ink
const bool USE_SLEEP_MODE  = true;   // device sits still for a while -> screen dims, movement wakes it
const bool USE_FLIP_MODE   = true;   // device turned upside down (see FLIP_STYLE)
const int  FLIP_STYLE      = 0;      // 0 = night mode (moon, sleeping fish), 1 = everything floats up
const bool FLIP_ROTATE_DISPLAY = true;   // flipped device: rotate the picture 180 degrees so it reads upright
const bool FLIP_MIRROR_TILT    = true;   // flipped device: swap left/right tilt (if backwards when flipped, set false)


#define TILT_AXIS   1
#define TILT_SIGN  -1
const float TAP_G        = 0.35f;
const float STILL_G      = 0.06f;
const float SHAKE_G      = 0.80f;
const int   SHAKE_SWINGS = 5;       // add this back
const float SLOSH_GAIN   = 2.4f;    // new
const float AIM_GAIN     = 150.0f;  // new
const int SCARE_MIN_MS   = 2000;   // knock: shortest scare (ms)
const int SCARE_MAX_MS   = 4000;   // knock: longest scare (ms)
const int SHAKE_EXTRA_MS = 1000;   // a shake lasts this much longer (ms)
const uint32_t SLEEP_AFTER_MS = 120000UL;   // sit still this long (ms) before the screen dims
const bool IMU_DEBUG = false;   // true = print readings in the Serial Monitor (115200) to check the axes


#define WHITE SH110X_WHITE
#define BLACK SH110X_BLACK

struct Fish {
  float x, y, vx, vy;
  int dir;            // +1 = facing right, -1 = facing left
  float speed;
  float phase;
  uint8_t size;       // half body length in pixels
  uint8_t style;      // 0 plain, 1 stripe, 2 dorsal fin
  uint8_t flash;      // frames left of "just ate" effect
  float targetY;      // depth this fish is currently heading for
  unsigned long nextTurn;   // when this fish next changes its mind (ms)
  unsigned long panicUntil; // startled by a knock or shake until this time (ms)
};

struct Food {
  float x, y, seed;
  bool active;
};

struct Bubble {
  float x, y, speed;
};

const int NUM_FISH    = 3;      // any number works now (sizes repeat every 3 fish)
const int MAX_FOOD    = 12;
const int NUM_BUBBLES = 5;
const int NUM_WEED    = 5;

Fish   fish[NUM_FISH];
Food   food[MAX_FOOD];
Bubble bubbles[NUM_BUBBLES];

const int weedX[NUM_WEED] = {10, 34, 70, 98, 120};
const int weedH[NUM_WEED] = {20, 14, 24, 16, 22};

// Small side branches on every seaweed strand (random heights, lengths and sides, set at startup)
const int BRANCHES = 3;
int branchH[NUM_WEED][BRANCHES];     // height along the strand (px above the floor)
int branchLen[NUM_WEED][BRANCHES];   // length in px; negative = grows to the left

// Stones on the ground (sizes and positions are random, set at startup)
const int NUM_STONES = 2;
int stoneX[NUM_STONES];   // centre x
int stoneW[NUM_STONES];   // half width in px
int stoneH[NUM_STONES];   // height in px

// Octopuses: octo[0] is the big old one: it visits at random times, wanders around like a
// fish, then leaves through a random side.
// The small ones live in the tank all the time and swim along with a fish buddy.
// state (big one): 0 = away, 1 = entering, 2 = wandering, 3 = leaving
const int NUM_SMALL_OCTO = 1;                 // how many small octopuses
const int NUM_OCTO = 1 + NUM_SMALL_OCTO;      // + 1 big one (index 0)
struct Octo {
  bool active;
  uint8_t state;
  uint8_t w;                       // head half width in px (7 = big, 3-4 = small)
  float x, y, vx, vy;
  float hx, hy;                    // heading it wants to swim in
  float tx, ty;                    // target point (entering / leaving)
  float dir, phase;
  unsigned long next;              // when the next visit starts (ms)
  unsigned long turnAt;            // when it next picks a new heading (ms)
  unsigned long stayUntil;         // big: when it decides to leave / small: when it picks a new buddy (ms)
  int buddy;                       // small ones only: index of the fish it swims with
};
Octo octo[NUM_OCTO];

// Ink cloud (big octopus squirts it when the device is shaken hard)
struct Ink {
  float x, y, vx, vy, r, life;
  bool active;
};
const int MAX_INK = 8;
Ink ink[MAX_INK];

// ---------- MPU6050 state ----------
constexpr uint8_t MPU_ADDR = 0x68;   // 0x69 if the AD0 pin is pulled high
bool  imuOK = false;
float gSlow[3] = {0, 0, 1};          // slow filter = gravity direction (in g)
float gFast[3] = {0, 0, 1};          // fast filter = tilt
float imuHp = 0;                     // size of any movement (g), used to tell "sitting still"
float imuJump = 0;                   // how suddenly the reading changed (g), used for knocks
float aPrev[3] = {0, 0, 1};          // previous reading
int   swings = 0;                    // hard back-and-forth swings counted for shake detection
int   swingSign[3] = {0, 0, 0};
unsigned long lastSwingMs = 0;
float tiltZero = 0;                  // "level" position, measured at start-up
float tilt = 0;                      // -1..1, positive = right side down
float sloshPos = 0, sloshVel = 0;    // water slosh (spring), -1..1
float aimX = SCREEN_W / 2;           // where touch drops the food
float motionTilt = 0, lastTiltFrame = 0;
bool  eventTap = false, eventShake = false;
bool  isFlipped = false, flipCond = false;
bool  sleeping = false, dimmed = false;  bool displayRotated = false;
bool  nightMode = false, floatUp = false;   // set while the device is upside down
unsigned long flipSince = 0, lastImuMs = 0, lastMotionMs = 0, lastTapMs = 0, lastShakeMs = 0;
unsigned long markerUntil = 0;       // aim marker is shown until this time (ms)

float t = 0;
unsigned long lastFrame = 0;
unsigned long lastFeed  = 0;
bool lastTouch = false;

// ---------- setup helpers ----------

void initFish() {
  const uint8_t sizes[3]  = {6, 5, 4};      // repeated for every extra fish
  const uint8_t styles[3] = {1, 2, 0};
  for (int i = 0; i < NUM_FISH; i++) {
    fish[i].x = random(20, SCREEN_W - 20);
    fish[i].y = random(12, FLOOR_Y - 14);
    // first two fish start in opposite directions, the rest are random
    fish[i].dir = (i == 0) ? 1 : (i == 1) ? -1 : ((random(0, 2) == 0) ? -1 : 1);
    fish[i].vx = fish[i].dir * 0.3f;
    fish[i].vy = 0;
    fish[i].speed = 0.25f + random(0, 25) / 100.0f;
    fish[i].phase = random(0, 628) / 100.0f;
    fish[i].size = sizes[i % 3];
    fish[i].style = styles[i % 3];
    fish[i].flash = 0;
    fish[i].targetY = fish[i].y;
    fish[i].nextTurn = millis() + random(500, 4000);
    fish[i].panicUntil = 0;
  }
  for (int i = 0; i < MAX_FOOD; i++) food[i].active = false;
  for (int i = 0; i < NUM_BUBBLES; i++) {
    bubbles[i].x = random(4, SCREEN_W - 4);
    bubbles[i].y = random(0, FLOOR_Y);
    bubbles[i].speed = 0.3f + random(0, 30) / 100.0f;
  }
}

void initWeed() {
  for (int i = 0; i < NUM_WEED; i++) {
    int span = weedH[i] - 6;                         // usable height of the strand
    for (int b = 0; b < BRANCHES; b++) {
      branchH[i][b] = 4 + (span * b) / BRANCHES + random(0, span / BRANCHES + 1);
      int len = random(3, 9);                        // random length, 3-8 px
      branchLen[i][b] = (((b + i) % 2 == 0) ? 1 : -1) * len;   // alternate sides
      if (random(0, 100) < 25) branchLen[i][b] = -branchLen[i][b];   // sometimes flip
    }
  }
}

void initStones() {
  // spread the stones across the floor with some random jitter, mix of big and small
  for (int i = 0; i < NUM_STONES; i++) {
    int slot = SCREEN_W / NUM_STONES;
    stoneX[i] = i * slot + slot / 2 + random(-slot / 3, slot / 3 + 1);
    stoneW[i] = random(3, 8);                          // half width 3-7 px
    stoneH[i] = (stoneW[i] * 2) / 3 + random(0, 2);    // rounder: height follows width
    if (stoneH[i] < 3) stoneH[i] = 3;
    if (stoneH[i] > 6) stoneH[i] = 6;
  }
}

// ---------- feeding ----------

// Drop a small cluster of food from the top at a random x.
// Pieces start above the screen at staggered heights so they enter one by one.
void feed() {
  // drop where the aim marker points (tilt), or at a random spot without the sensor
  float baseX = (USE_TILT_AIM && imuOK) ? aimX : (float)random(14, SCREEN_W - 14);
  int spawned = 0;
  for (int i = 0; i < MAX_FOOD && spawned < 5; i++) {
    if (!food[i].active) {
      food[i].x = baseX + random(-8, 9);
      food[i].y = floatUp ? (FLOOR_Y + (float)spawned * 5.0f) : -(float)spawned * 5.0f;   // upside down: food enters from the bottom
      food[i].seed = random(0, 628) / 100.0f;
      food[i].active = true;
      spawned++;
    }
  }
}

int nearestFood(int fi) {
  const Fish &f = fish[fi];
  int best = -1;
  float bestD = 1e9;
  for (int i = 0; i < MAX_FOOD; i++) {
    if (!food[i].active || food[i].y < 0 || food[i].y > FLOOR_Y) continue;   // only food on screen
    float dx = food[i].x - f.x;
    float dy = food[i].y - f.y;
    float d = dx * dx + dy * dy;
    if (d < bestD) { bestD = d; best = i; }
  }
  return best;
}

// ---------- update ----------

void updateFood() {
  for (int i = 0; i < MAX_FOOD; i++) {
    if (!food[i].active) continue;
    food[i].y += floatUp ? -0.35f : 0.35f;                          // sinks (rises when upside down)
    food[i].x += sinf(food[i].y * 0.35f + food[i].seed) * 0.18f;   // gentle zigzag
    if (USE_TILT_SLOSH) food[i].x += sloshPos * 0.5f;               // pushed by the slosh
    if (food[i].x < 1) food[i].x = 1;
    if (food[i].x > SCREEN_W - 3) food[i].x = SCREEN_W - 3;
    if (!floatUp && food[i].y >= FLOOR_Y - 2) food[i].active = false;   // sank to the bottom
    if (floatUp && food[i].y < -6) food[i].active = false;              // floated away at the top
  }
}

void updateFish(int fi) {
  Fish &f = fish[fi];
  // each fish picks its own direction, speed and depth every few seconds
  if (millis() > f.nextTurn) {
    if (random(0, 100) < 55) f.dir = -f.dir;
    f.speed = 0.2f + random(0, 40) / 100.0f;
    f.targetY = random(10, FLOOR_Y - 10);
    f.nextTurn = millis() + random(1500, 6000);
  }

  // default: relaxed swimming, drifting toward its own depth with a little bobbing
  float tx = f.dir * f.speed;
  float ty = sinf(t * 2.0f + f.phase) * 0.15f + (f.targetY - f.y) * 0.02f;

  int idx = nearestFood(fi);
  if (idx >= 0) {
    float dx = food[idx].x - f.x;
    float dy = food[idx].y - f.y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < f.size + 2) {
      food[idx].active = false;   // eaten
      f.flash = 10;
      f.dir = (random(0, 2) == 0) ? -1 : 1;            // swim off in a random direction
      f.nextTurn = millis() + random(800, 3000);
    } else {
      float s = f.speed * 2.4f;   // swim faster when chasing
      tx = dx / d * s;
      ty = dy / d * s;
      if (fabsf(dx) > 1.0f) f.dir = (dx > 0) ? 1 : -1;   // face the food
    }
  }

  // swim away from the big octopus when it gets close
  for (int k = 0; k < NUM_OCTO; k++) {
    if (!octo[k].active || octo[k].w < 6) continue;   // small octopuses are friends, not scary
    float ox = f.x - octo[k].x;
    float oy = f.y - octo[k].y;
    float od = sqrtf(ox * ox + oy * oy);
    float scareRadius = (octo[k].w >= 6) ? 30.0f : 16.0f;
    if (od < scareRadius && od > 0.5f) {
      float s = f.speed * 2.8f;
      tx = ox / od * s;
      ty = oy / od * s;
      f.dir = (ox > 0) ? 1 : -1;
    }
  }

  // startled by a knock or shake: dart away for a moment
  if (millis() < f.panicUntil) {
    tx = f.dir * f.speed * 6.1f;
    ty = (f.targetY - f.y) * 0.03f;
  }

  // water slosh pushes everything toward the low side
  if (USE_TILT_SLOSH) tx += sloshPos * 0.45f;

  // upside down: night mode (slow and sleepy, sinking) or everything floats up
  if (nightMode) { tx *= 0.35f; ty = (FLOOR_Y - 14 - f.y) * 0.02f; }
  if (floatUp) ty -= 0.35f;

  // smooth velocity change
  f.vx += (tx - f.vx) * 0.15f;
  f.vy += (ty - f.vy) * 0.15f;
  f.x += f.vx;
  f.y += f.vy;

  // keep inside the tank, turn around at the walls
  float minX = f.size + 5, maxX = SCREEN_W - f.size - 5;
  if (f.x < minX) { f.x = minX; f.dir = 1; }
  if (f.x > maxX) { f.x = maxX; f.dir = -1; }
  if (f.y < 11) f.y = 11;                      // stay under the water surface
  if (f.y > FLOOR_Y - 6) f.y = FLOOR_Y - 6;

  if (f.flash > 0) f.flash--;
}

void updateBubbles() {
  for (int i = 0; i < NUM_BUBBLES; i++) {
    bubbles[i].y += floatUp ? bubbles[i].speed : -bubbles[i].speed;   // rise (sink when upside down)
    bubbles[i].x += sinf(bubbles[i].y * 0.2f + i) * 0.15f;
    if (USE_TILT_SLOSH) bubbles[i].x += sloshPos * 0.4f;
    if (bubbles[i].x < 4) bubbles[i].x = 4;
    if (bubbles[i].x > SCREEN_W - 4) bubbles[i].x = SCREEN_W - 4;
    if (!floatUp && bubbles[i].y < 7) {                      // pop at the water surface
      bubbles[i].y = FLOOR_Y - 4;
      bubbles[i].x = random(4, SCREEN_W - 4);
    }
    if (floatUp && bubbles[i].y > FLOOR_Y - 3) {             // upside down: they sink and restart at the top
      bubbles[i].y = 8;
      bubbles[i].x = random(4, SCREEN_W - 4);
    }
  }
}

// ---------- draw ----------

// Adafruit_GFX has no ellipse, so fill one row at a time.
void fillEllipse(int cx, int cy, int rx, int ry, uint16_t color) {
  for (int dy = -ry; dy <= ry; dy++) {
    float k = 1.0f - (float)(dy * dy) / (float)(ry * ry);
    int w = (int)(rx * sqrtf(k) + 0.5f);
    display.drawFastHLine(cx - w, cy + dy, 2 * w + 1, color);
  }
}

void drawFish(int fi) {
  const Fish &f = fish[fi];
  int x = (int)f.x, y = (int)f.y, s = f.size, d = f.dir;
  int rx = s + (f.flash > 0 ? 1 : 0);   // puff up slightly after eating
  int ry = (s * 3) / 5 + 1;

  fillEllipse(x, y, rx, ry, WHITE);

  // wagging tail
  int wag = (int)(sinf(t * 10.0f + f.phase) * 2.0f);
  display.fillTriangle(x - d * (s - 1), y,
                       x - d * (s + 4), y - 3 + wag,
                       x - d * (s + 4), y + 3 + wag, WHITE);

  if (f.style == 2) {   // dorsal fin
    display.fillTriangle(x - 2, y - ry, x + 2, y - ry, x, y - ry - 3, WHITE);
  }

  display.drawPixel(x + d * (s - 3), y - 1, BLACK);                      // eye
  if (f.style == 1) display.drawFastVLine(x, y - ry + 1, 2 * ry - 1, BLACK);   // stripe
}

// Sideways offset of a seaweed strand at height h: its natural sway plus a lean when the water sloshes
int weedSway(int i, int h) {
  float lean = USE_TILT_SLOSH ? sloshPos * h * 0.4f : 0.0f;
  return (int)(sinf(t * 1.6f + i * 1.3f + h * 0.28f) * (h * 0.14f) + lean);
}

void drawSeaweed() {
  for (int i = 0; i < NUM_WEED; i++) {
    int px = weedX[i], py = FLOOR_Y;
    for (int h = 2; h <= weedH[i]; h += 2) {
      int nx = weedX[i] + weedSway(i, h);
      int ny = FLOOR_Y - h;
      display.drawLine(px, py, nx, ny, WHITE);
      px = nx; py = ny;
    }

    // small side branches, attached to the swaying stalk
    for (int b = 0; b < BRANCHES; b++) {
      int h = branchH[i][b];
      int len = branchLen[i][b];
      int side = (len < 0) ? -1 : 1;
      int alen = (len < 0) ? -len : len;
      int sx = weedX[i] + weedSway(i, h);   // stalk x here
      int sy = FLOOR_Y - h;
      int mx = sx + side * (alen / 2);
      int my = sy - 1 - alen / 4;
      int tx = sx + side * alen + (int)(sinf(t * 2.2f + i + b) * 1.5f);                 // tip sways
      int ty = sy - 2 - alen / 2;
      display.drawLine(sx, sy, mx, my, WHITE);
      display.drawLine(mx, my, tx, ty, WHITE);
    }
  }
}

// Animated water surface: a rippling line on top plus a dashed ripple line under it.
void drawWater() {
  int prevY = 0;
  for (int x = 0; x < SCREEN_W; x++) {
    float tiltLine = USE_TILT_SLOSH ? -(x - SCREEN_W / 2) * sloshPos * 0.08f : 0.0f;   // water piles up on the low side
    int y = 4 + (int)roundf(sinf(x * 0.16f + t * 2.2f) * 1.6f + sinf(x * 0.07f - t * 1.3f) * 1.2f + tiltLine);
    if (x > 0) display.drawLine(x - 1, prevY, x, y, WHITE);
    prevY = y;
    if (((x + (int)(t * 8.0f)) / 3) % 2 == 0) {
      int y2 = 9 + (int)roundf(sinf(x * 0.2f - t * 1.8f));
      display.drawPixel(x, y2, WHITE);
    }
  }
}

// Rounded stones sitting on the floor line: outline dome with a little texture inside.
void drawStones() {
  for (int i = 0; i < NUM_STONES; i++) {
    int cx = stoneX[i], w = stoneW[i], h = stoneH[i];
    for (int dy = 1; dy <= h; dy++) {
      float u = (dy - 0.5f) / (float)h;                 // 0 at the floor, 1 at the top
      int hw = (int)(w * sqrtf(1.0f - u * u) + 0.5f);   // dome shape
      int y = FLOOR_Y - dy;
      if (dy == h) {
        display.drawFastHLine(cx - hw, y, 2 * hw + 1, WHITE);   // closed top
      } else {
        display.drawPixel(cx - hw, y, WHITE);                   // left edge
        display.drawPixel(cx + hw, y, WHITE);                   // right edge
      }
    }
    // texture dots inside bigger stones
    if (h >= 4) {
      display.drawPixel(cx - w / 3, FLOOR_Y - 2, WHITE);
      display.drawPixel(cx + w / 4, FLOOR_Y - h / 2 - 1, WHITE);
    }
  }
}

void drawFloor() {
  display.drawFastHLine(0, FLOOR_Y, SCREEN_W, WHITE);
  for (int x = 3; x < SCREEN_W; x += 9) {
    display.drawPixel(x, FLOOR_Y + 1 + (x % 3), WHITE);
    display.drawPixel(x + 1, FLOOR_Y + 1 + (x % 3), WHITE);
  }
}

void drawFood() {
  for (int i = 0; i < MAX_FOOD; i++) {
    if (food[i].active && food[i].y >= 0 && food[i].y < FLOOR_Y - 1) {
      display.fillRect((int)food[i].x, (int)food[i].y, 2, 2, WHITE);
    }
  }
}

// Bubbles are hollow rings (radius 2 or 3) so they never look like the solid 2x2 food.
void drawBubbles() {
  for (int i = 0; i < NUM_BUBBLES; i++) {
    int r = 2 + (i % 2);
    int bx = (int)bubbles[i].x, by = (int)bubbles[i].y;
    display.drawCircle(bx, by, r, WHITE);
    if (r == 3) display.drawPixel(bx - 1, by - 1, WHITE);   // little shine
  }
}

// ---------- small octopuses ----------

void initOctopuses() {
  for (int i = 0; i < NUM_OCTO; i++) {
    octo[i].dir = 1;
    octo[i].phase = random(0, 628) / 100.0f;
    octo[i].vx = 0;
    octo[i].vy = 0;
    if (i == 0) {                                          // the big old octopus (visits randomly)
      octo[i].active = false;
      octo[i].state = 0;
      octo[i].w = 7;
      octo[i].next = millis() + random(8000, 20000);
    } else {                                               // small friends: always in the tank
      octo[i].w = 3 + random(0, 2);                        // head half width 3-4 px
      octo[i].active = true;
      octo[i].state = 2;
      octo[i].x = random(20, SCREEN_W - 20);
      octo[i].y = random(16, 40);
      octo[i].buddy = random(0, NUM_FISH);
      octo[i].stayUntil = millis() + random(10000, 25000); // next buddy change
    }
  }
}

void octoHeadTo(int oi, float tx, float ty) {
  Octo &o = octo[oi];
  float dx = tx - o.x, dy = ty - o.y;
  float d = sqrtf(dx * dx + dy * dy);
  if (d < 0.01f) d = 0.01f;
  o.hx = dx / d;
  o.hy = dy / d;
}

// Small octopus: swims beside its buddy fish all the time, switching buddies now and then.
void updateSmallOcto(int oi) {
  Octo &o = octo[oi];
  unsigned long now = millis();
    // night mode: sink gently to the bottom and rest, breathing slowly
  if (nightMode) {
    float restY = FLOOR_Y - 12;                           // keeps the tentacles above the floor line
    float dvy = (o.y < restY) ? 0.3f : 0.0f;
    o.vx += (0.0f - o.vx) * 0.2f;
    o.vy += (dvy - o.vy) * 0.2f;
    o.x += o.vx;
    o.y += o.vy;
    if (o.y > restY) { o.y = restY; o.vy = 0; }
    o.phase += 0.02f;                                     // very slow squeeze = breathing
    return;
  }

  if (now >= o.stayUntil) {                               // pick a new friend
    o.buddy = random(0, NUM_FISH);
    o.stayUntil = now + random(10000, 25000);
  }
  const Fish &f = fish[o.buddy];

  // a spot just behind and a little below the buddy, drifting around slowly
  float bx = f.x - f.dir * 12 + sinf(t * 0.8f + oi) * 4.0f;
  float by = f.y + 6 + sinf(t * 0.6f + oi * 2) * 4.0f;
  float dx = bx - o.x, dy = by - o.y;
  float d = sqrtf(dx * dx + dy * dy);

  o.phase += 0.14f;
  float pulse = sinf(o.phase * 2.0f);
  float jet = (pulse > 0) ? pulse : 0;                    // 0..1 jet on each pulse

  float dvx, dvy;
  if (d > 5.0f) {                                         // catch up (faster when far away)
    float sp = 0.3f + 0.35f * jet + d * 0.03f;
    dvx = dx / d * sp;
    dvy = dy / d * sp;
  } else {                                                // close: swim along with the buddy
    dvx = f.vx + f.dir * 0.12f * jet;
    dvy = f.vy;
  }

  // keep away from the big octopus
  if (octo[0].active) {
    float ox = o.x - octo[0].x, oy = o.y - octo[0].y;
    float od = sqrtf(ox * ox + oy * oy);
    if (od < 22.0f && od > 0.5f) {
      dvx = ox / od * 0.9f;
      dvy = oy / od * 0.9f;
    }
  }

  o.vx += (dvx - o.vx) * 0.2f;
  o.vy += (dvy - o.vy) * 0.2f;
  o.x += o.vx;
  o.y += o.vy;
  if (fabsf(o.vx) > 0.08f) o.dir = (o.vx > 0) ? 1 : -1;

  // stay inside the tank
  if (o.x < 8) o.x = 8;
  if (o.x > SCREEN_W - 8) o.x = SCREEN_W - 8;
  if (o.y < 14) o.y = 14;
  if (o.y > FLOOR_Y - 14) o.y = FLOOR_Y - 14;
}

void updateOctopus(int oi) {
  Octo &o = octo[oi];
  unsigned long now = millis();
  bool big = (o.w >= 6);
  if (!big) { updateSmallOcto(oi); return; }
  int edge = big ? 14 : 8;                      // how far off screen it starts / ends

    // night mode: the big octopus stays away. If it is already in the tank, it swims off.
  if (nightMode) {
    if (o.state == 0) {                                   // away: postpone the next visit
      if (o.next < now + 8000) o.next = now + 8000;
      return;
    }
    if (o.state == 1 || o.state == 2) {                   // entering or wandering: leave now
      o.tx = (o.x < SCREEN_W / 2) ? -30 : SCREEN_W + 30;  // nearest side
      o.ty = o.y;
      octoHeadTo(oi, o.tx, o.ty);
      o.state = 3;
    }
  }

  // ---- waiting for the next visit ----
  if (o.state == 0) {
    if (now >= o.next) {
      int side = random(0, 3);                  // 0 = left, 1 = right, 2 = top
      if (side == 0)      { o.x = -edge;                       o.y = random(14, 40); }
      else if (side == 1) { o.x = SCREEN_W + edge;             o.y = random(14, 40); }
      else                { o.x = random(20, SCREEN_W - 20);   o.y = -edge; }
      o.vx = 0;
      o.vy = 0;
      o.tx = random(30, SCREEN_W - 30);         // a spot inside the tank to swim to first
      o.ty = random(16, 38);
      octoHeadTo(oi, o.tx, o.ty);
      o.stayUntil = now + random(12000, 30000);
      o.state = 1;
      o.active = true;
    }
    return;
  }

  // ---- entering: swim to a spot inside the tank ----
  if (o.state == 1) {
    octoHeadTo(oi, o.tx, o.ty);
    float dx = o.tx - o.x, dy = o.ty - o.y;
    if (dx * dx + dy * dy < 25.0f) {            // arrived: start wandering
      o.state = 2;
      o.turnAt = now;
    }
  }

  // ---- wandering like a fish ----
  if (o.state == 2) {
    if (now >= o.turnAt) {                      // pick a new random heading
      float ang = random(0, 628) / 100.0f;
      o.hx = cosf(ang);
      o.hy = sinf(ang) * 0.7f;
      o.turnAt = now + random(1500, 4000);
    }
    // turn back from the walls, the surface and the floor
    int mx = big ? 16 : 10;
    int mb = big ? 22 : 14;
    if (o.x < mx && o.hx < 0) o.hx = -o.hx;
    if (o.x > SCREEN_W - mx && o.hx > 0) o.hx = -o.hx;
    if (o.y < 14 && o.hy < 0) o.hy = -o.hy;
    if (o.y > FLOOR_Y - mb && o.hy > 0) o.hy = -o.hy;

    if (now >= o.stayUntil) {                   // time to go: pick a random exit
      int side = random(0, 3);                  // 0 = left, 1 = right, 2 = top
      if (side == 0)      { o.tx = -30;           o.ty = o.y; }
      else if (side == 1) { o.tx = SCREEN_W + 30; o.ty = o.y; }
      else                { o.tx = o.x;           o.ty = -30; }
      octoHeadTo(oi, o.tx, o.ty);
      o.state = 3;
    }
  }

  // ---- move: jet pulse, then glide ----
  o.phase += big ? 0.12f : 0.14f;
  float pulse = sinf(o.phase * 2.0f);
  float speed = big ? (0.3f + 0.8f * (pulse > 0 ? pulse : 0))
                    : (0.25f + 0.6f * (pulse > 0 ? pulse : 0));
  if (o.state == 3) speed *= 1.4f;              // leaving: swim off faster

  o.vx += (o.hx * speed - o.vx) * 0.2f;
  o.vy += (o.hy * speed - o.vy) * 0.2f;
  o.x += o.vx;
  o.y += o.vy;
  if (fabsf(o.vx) > 0.08f) o.dir = (o.vx > 0) ? 1 : -1;

  // ---- gone: wait a random time before the next visit ----
  if (o.state == 3 && (o.x < -edge - 4 || o.x > SCREEN_W + edge + 4 || o.y < -edge - 4)) {
    o.state = 0;
    o.active = false;
    o.next = now + (big ? random(12000, 35000) : random(10000, 30000));
  }
}

void drawOctopus(int oi) {
  const Octo &o = octo[oi];
  if (!o.active) return;
  bool big = (o.w >= 6);
  int x = (int)o.x, y = (int)o.y;
  float squeeze = sinf(o.phase * 2.0f);
  int rx = o.w - ((squeeze > 0.5f) ? 1 : 0);   // head squeezes on each jet
  int ry = o.w - (big ? 1 : 0) + ((squeeze > 0.5f) ? 1 : 0);

  fillEllipse(x, y, rx, ry, WHITE);

  // eyes
  if (big) {
    display.fillRect(x - 4, y, 2, 2, BLACK);
    display.fillRect(x + 2, y, 2, 2, BLACK);
  } else {
    display.drawPixel(x - 1, y, BLACK);
    display.drawPixel(x + 1, y, BLACK);
  }

  // wavy tentacles trailing behind: 6 long ones (big) or 4 short ones (small)
  int n = big ? 6 : 4;
  int len = big ? 10 : 6;
  for (int i = 0; i < n; i++) {
    int baseX = x - rx + 1 + (i * (2 * rx - 2)) / (n - 1);
    int px = baseX, py = y + ry - 1;
    for (int h = 2; h <= len; h += 2) {
      float amp = big ? 0.3f : 0.25f;
      int nx = baseX + (int)(sinf(o.phase * 2.0f + i * 0.9f + h * (big ? 0.45f : 0.5f)) * (h * amp)
                             - o.dir * h * (big ? 0.2f : 0.15f));
      int ny = y + ry - 1 + h;
      display.drawLine(px, py, nx, ny, WHITE);
      px = nx; py = ny;
    }
  }
    // night mode: sleeping small octopus gets a floating "z" like the fish
  if (nightMode && !big) {
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(x + 4, y - ry - 9 - (int)(sinf(t * 1.5f) * 2.0f));
    display.print('z');
  }
}

// ============================================================================
// MPU6050 (read directly over I2C, no extra library needed)
// ============================================================================

bool imuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool initImu() {
  if (!imuWrite(0x6B, 0x00)) return false;   // wake the sensor up (clear the sleep bit)
  delay(50);
  imuWrite(0x1C, 0x10);                      // accelerometer range +-8 g (4096 counts per g)
  imuWrite(0x1A, 0x02);                      // internal filter about 94 Hz
  return true;
}

bool readAccel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);                          // first accelerometer register
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, (uint8_t)6) != 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  ax = (int16_t)((b[0] << 8) | b[1]) / 4096.0f;
  ay = (int16_t)((b[2] << 8) | b[3]) / 4096.0f;
  az = (int16_t)((b[4] << 8) | b[5]) / 4096.0f;
  return true;
}

// Measure the "level" position. Keep the device sitting normally while it starts up.
void calibrateImu() {
  float sum[3] = {0, 0, 0};
  int n = 0;
  for (int k = 0; k < 40; k++) {
    float a[3];
    if (readAccel(a[0], a[1], a[2])) {
      for (int i = 0; i < 3; i++) sum[i] += a[i];
      n++;
    }
    delay(8);
  }
  if (n == 0) return;
  for (int i = 0; i < 3; i++) {
    gSlow[i] = sum[i] / n;
    gFast[i] = sum[i] / n;
    aPrev[i] = sum[i] / n;
  }
  tiltZero = gSlow[TILT_AXIS];
}

// Called as often as possible. Works out tilt, knocks, shakes and flips.
// Called as often as possible. Works out tilt, knocks, shakes and flips.
void updateImu(unsigned long now) {
  if (!imuOK || now - lastImuMs < 4) return;
  float dt = (now - lastImuMs) / 1000.0f;
  lastImuMs = now;
  if (dt > 0.2f) dt = 0.2f;

  float a[3];
  if (!readAccel(a[0], a[1], a[2])) return;

  float kSlow = dt / (0.40f + dt);      // slow filter: gravity
  float kFast = dt / (0.10f + dt);      // fast filter: tilt
  float hp2 = 0;
  for (int i = 0; i < 3; i++) {
    gSlow[i] += (a[i] - gSlow[i]) * kSlow;
    gFast[i] += (a[i] - gFast[i]) * kFast;
    float d = a[i] - gSlow[i];
    hp2 += d * d;
  }
  imuHp = sqrtf(hp2);                   // sudden movement, ignoring slow tilting

  // tilt: positive = right side down (of the picture you are looking at)
  float tv = TILT_SIGN * (gFast[TILT_AXIS] - tiltZero);
  if (FLIP_MIRROR_TILT && USE_FLIP_MODE && FLIP_ROTATE_DISPLAY && isFlipped) tv = -tv;
  if (tv > 1.0f) tv = 1.0f;
  if (tv < -1.0f) tv = -1.0f;
  tilt = tv;

  if (imuHp > STILL_G) lastMotionMs = now;

  // knock = the reading suddenly jumps between two samples
  float jump2 = 0;
  for (int i = 0; i < 3; i++) {
    float dj = a[i] - aPrev[i];
    jump2 += dj * dj;
    aPrev[i] = a[i];
  }
  imuJump = sqrtf(jump2);
  if (imuJump > TAP_G && now - lastTapMs > 700) {
    lastTapMs = now;
    eventTap = true;
  }

  // shake = several hard swings back and forth in a row
  for (int i = 0; i < 3; i++) {
    float d = a[i] - gSlow[i];
    int sg = (d > SHAKE_G) ? 1 : (d < -SHAKE_G) ? -1 : 0;   // changed: was 0.6f
    if (sg == 0) continue;
    if (swingSign[i] != 0 && sg != swingSign[i]) swings++;
    swingSign[i] = sg;
    lastSwingMs = now;
  }
  if (now - lastSwingMs > 600) {
    swings = 0;
    for (int i = 0; i < 3; i++) swingSign[i] = 0;
  }
  if (swings >= SHAKE_SWINGS && now - lastShakeMs > 3000) {
    lastShakeMs = now;
    eventShake = true;
    swings = 0;
  }

  // upside down: gravity points the other way for at least half a second
  if (gSlow[2] < -0.6f) {
    if (!flipCond) { flipCond = true; flipSince = now; }
    if (now - flipSince > 500) isFlipped = true;
  } else if (gSlow[2] > -0.3f) {
    flipCond = false;
    isFlipped = false;
  }

  if (IMU_DEBUG) {
    static unsigned long lastDbg = 0;
    if (now - lastDbg > 200) {
      lastDbg = now;
      Serial.printf("ax=%.2f ay=%.2f az=%.2f | tilt=%.2f | jump=%.2f | swings=%d | upside-down=%d\n",
                    a[0], a[1], a[2], tilt, imuJump, swings, isFlipped ? 1 : 0);
    }
  }
}

// ---------- ink cloud ----------

void squirtInk(float x, float y) {
  int made = 0;
  for (int i = 0; i < MAX_INK && made < 5; i++) {
    if (ink[i].active) continue;
    ink[i].x = x + random(-5, 6);
    ink[i].y = y + random(-4, 5);
    ink[i].vx = random(-15, 16) / 100.0f;
    ink[i].vy = random(-12, 4) / 100.0f;
    ink[i].r = 2;
    ink[i].life = 1.0f;
    ink[i].active = true;
    made++;
  }
}

void updateInk() {
  for (int i = 0; i < MAX_INK; i++) {
    if (!ink[i].active) continue;
    ink[i].x += ink[i].vx;
    ink[i].y += ink[i].vy;
    if (ink[i].r < 7) ink[i].r += 0.12f;      // the cloud grows...
    ink[i].life -= 0.012f;                    // ...and fades (about 3 seconds)
    if (ink[i].life <= 0) ink[i].active = false;
  }
}

void drawInk() {
  for (int i = 0; i < MAX_INK; i++) {
    if (!ink[i].active) continue;
    int cx = (int)ink[i].x, cy = (int)ink[i].y, r = (int)ink[i].r;
    float life = ink[i].life;
    for (int dy = -r; dy <= r; dy++) {
      for (int dx = -r; dx <= r; dx++) {
        if (dx * dx + dy * dy > r * r) continue;
        bool on;
        if (life > 0.6f)      on = ((dx + dy) & 1) == 0;               // dense dots
        else if (life > 0.3f) on = ((dx & 1) == 0) && ((dy & 1) == 0); // thinner
        else                  on = ((dx + 2 * dy) % 5) == 0;           // almost gone
        if (on) display.drawPixel(cx + dx, cy + dy, WHITE);
      }
    }
  }
}

// ---------- reactions to the sensor ----------

// A knock (or a hard shake) startles everyone.
void scareAll(bool shake) {
  unsigned long now = millis();
  for (int i = 0; i < NUM_FISH; i++) {
    fish[i].dir = (random(0, 2) == 0) ? -1 : 1;
    // fish[i].panicUntil = now + random(900, 1600) + (shake ? 600 : 0);
    fish[i].panicUntil = now + random(SCARE_MIN_MS, SCARE_MAX_MS) + (shake ? SHAKE_EXTRA_MS : 0);
    fish[i].nextTurn   = fish[i].panicUntil;   // don't let a random turn interrupt the scare
    fish[i].vx = fish[i].dir * 2.0f;
    fish[i].vy = random(-10, 11) / 10.0f;
    fish[i].targetY = random(12, FLOOR_Y - 12);
  }
  for (int k = 1; k < NUM_OCTO; k++) {         // small octopuses jump
    octo[k].vy = -1.5f;
    octo[k].vx = ((random(0, 2) == 0) ? -1 : 1) * 1.4f;
  }
  if (shake && USE_SHAKE_INK && octo[0].active) {   // big octopus squirts ink and jets away
    squirtInk(octo[0].x - octo[0].dir * 6, octo[0].y);
    octo[0].vx = octo[0].dir * 2.5f;
  }
}

// Once per animation frame: events, slosh spring, aim marker, sleep, upside-down modes.
// Once per animation frame: events, slosh spring, aim marker, sleep, upside-down modes.
void updateReactions(unsigned long now) {
  // flip modes
  
  nightMode = USE_FLIP_MODE && isFlipped && FLIP_STYLE == 0;
  floatUp   = USE_FLIP_MODE && isFlipped && FLIP_STYLE == 1;


     // flipped: rotate the whole picture 180 degrees so it is readable from the flipped side
  bool wantRot = USE_FLIP_MODE && FLIP_ROTATE_DISPLAY && isFlipped;
  if (wantRot != displayRotated) {
    displayRotated = wantRot;
    display.setRotation(displayRotated ? 2 : 0);   // 0 = normal, 2 = 180 degrees
  }
  // knock / shake
  if (eventTap)   { eventTap = false;   if (USE_KNOCK_SCARE) scareAll(false); }
  if (eventShake) { eventShake = false; if (USE_SHAKE_INK || USE_KNOCK_SCARE) scareAll(true); }

  // water slosh: a spring that follows the tilt, so the water swings back and settles
  float target = USE_TILT_SLOSH ? tilt * SLOSH_GAIN : 0.0f;   // changed: was tilt * 2.0f
  if (target > 1.0f) target = 1.0f;
  if (target < -1.0f) target = -1.0f;
  sloshVel += (target - sloshPos) * 0.10f;
  sloshVel *= 0.88f;
  sloshPos += sloshVel;
  if (sloshPos > 1.0f) sloshPos = 1.0f;
  if (sloshPos < -1.0f) sloshPos = -1.0f;

  // aim marker follows the tilt; it shows while you tilt, for a few seconds after
  float aimTarget = SCREEN_W / 2 + tilt * AIM_GAIN;           // changed: was tilt * 120.0f
  if (aimTarget < 6) aimTarget = 6;
  if (aimTarget > SCREEN_W - 6) aimTarget = SCREEN_W - 6;
  aimX += (aimTarget - aimX) * 0.3f;
  if (fabsf(tilt - lastTiltFrame) > 0.008f) markerUntil = now + 3000;
  lastTiltFrame = tilt;

  // sleep: sitting still for a while dims the screen, movement wakes it
  if (fabsf(tilt - motionTilt) > 0.04f) { lastMotionMs = now; motionTilt = tilt; }
  sleeping = USE_SLEEP_MODE && imuOK && (now - lastMotionMs > SLEEP_AFTER_MS);
  bool wantDim = sleeping || nightMode;
  if (wantDim != dimmed) {
    dimmed = wantDim;
    setDisplayDim(dimmed);
  }
}

void drawAimMarker(unsigned long now) {
  if (!USE_TILT_AIM || !imuOK || sleeping || nightMode || now > markerUntil) return;
  int x = (int)aimX;
  if (floatUp) display.fillTriangle(x - 3, FLOOR_Y - 1, x + 3, FLOOR_Y - 1, x, FLOOR_Y - 5, WHITE);
  else         display.fillTriangle(x - 3, 0, x + 3, 0, x, 4, WHITE);
}

// Night mode (device upside down): moon, twinkling stars and a sleepy "z" above every fish.
void drawNightExtras() {
  if (!nightMode) return;
  display.fillCircle(112, 14, 6, WHITE);        // crescent moon
  display.fillCircle(115, 12, 5, BLACK);
  const int sx[5] = {14, 38, 62, 88, 100};
  const int sy[5] = {20, 28, 16, 30, 22};
  for (int i = 0; i < 5; i++) {
    if (((int)(t * 2.0f) + i) % 3 != 0) display.drawPixel(sx[i], sy[i], WHITE);
  }
  display.setTextSize(1);
  display.setTextColor(WHITE);
  for (int i = 0; i < NUM_FISH; i++) {
    int zx = (int)fish[i].x;
    int zy = (int)fish[i].y - 12 - (int)(sinf(t * 1.5f + i) * 2.0f);
    display.setCursor(zx, zy);
    display.print('z');
  }
}

// ---------- main ----------

void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT);   // TTP223 outputs HIGH when touched

  if (!initializeDisplay()) {
    Serial.println("OLED not found. Check wiring (SDA=D4, SCL=D5) and address 0x3C.");
    while (true) delay(1000);
  }

  randomSeed(esp_random());
  initFish();
  initWeed();
  initStones();
  initOctopuses();

  imuOK = initImu();
  if (imuOK) {
    calibrateImu();                  // keep the device level and still for a moment at start-up
  } else {
    Serial.println("MPU6050 not found - the tank runs without motion features. Check wiring / address 0x68.");
  }
  lastMotionMs = millis();
}

void loop() {
  unsigned long now = millis();
  updateImu(now);                              // the sensor is read as often as possible, not once per frame
  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;
  t += FRAME_MS / 1000.0f;

  // touch: act on the rising edge only, with a cooldown
  bool touched = digitalRead(TOUCH_PIN) == HIGH;
  if (touched && !lastTouch && now - lastFeed > 400) {
    feed();
    lastFeed = now;
  }
  lastTouch = touched;
  if (touched) { lastMotionMs = now; markerUntil = now + 3000; }   // a touch wakes the screen too

  updateReactions(now);
  updateFood();
  for (int i = 0; i < NUM_FISH; i++) updateFish(i);
  updateBubbles();
  for (int i = 0; i < NUM_OCTO; i++) updateOctopus(i);
  updateInk();

  clearDisplay();
  drawSeaweed();
  if (SHOW_STONES) drawStones();
  if (SHOW_WATER_WAVE) drawWater();
  drawBubbles();
  drawNightExtras();
  drawInk();
  for (int i = 0; i < NUM_OCTO; i++) drawOctopus(i);
  for (int i = 0; i < NUM_FISH; i++) drawFish(i);
  drawFood();
  drawAimMarker(now);
  drawFloor();
  updateDisplay();
  updateImu(millis());                         // sample again right after the (slow) screen transfer
}
