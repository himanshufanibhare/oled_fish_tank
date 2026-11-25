# Digital Fish Tank

A tiny animated aquarium for a **XIAO ESP32-S3** and a **1.3" SH1106 OLED (128x64)**.
Touch the sensor to feed the fish, tilt the device to slosh the water, knock it to startle the fish,
shake it to make the octopus squirt ink, and turn it upside down for **night mode**.

Everything lives in one Arduino sketch. The MPU6050 is read directly over I2C, so no extra IMU library is needed.

---

## Table of contents

1. [Features](#features)
2. [Hardware](#hardware)
3. [Wiring](#wiring)
4. [Software setup](#software-setup)
5. [How to play (controls)](#how-to-play-controls)
6. [Configuration reference](#configuration-reference)
7. [MPU6050 calibration](#mpu6050-calibration)
8. [How it works](#how-it-works)
9. [Code map](#code-map)
10. [Customizing](#customizing)
11. [Troubleshooting](#troubleshooting)
12. [Known limitations](#known-limitations)
13. [License](#license)

---

## Features

### The tank
- **3 fish** with different sizes and styles (plain, striped, dorsal fin). Each picks its own direction, speed and depth every few seconds, bobs gently, and has a wagging tail.
- **Feeding:** a touch drops a cluster of 5 food pieces. Fish chase the nearest piece, puff up briefly when they eat, then swim off.
- **Big octopus:** visits at random times, enters from the left, right or top, wanders like a fish, then leaves through a random side. Fish swim away from it when it gets close.
- **Small octopus:** always in the tank. It swims beside a "buddy" fish and switches buddy every 10 to 25 seconds. The fish are not scared of it.
- **Seaweed:** 5 strands with random side branches that sway and lean with the water.
- **Stones:** 2 rounded stones on the floor, random size and position at every start-up.
- **Bubbles:** 5 hollow rings that rise and pop at the surface.
- **Optional water surface waves** (off by default).

### Motion features (MPU6050)
| Motion | What happens |
|---|---|
| **Tilt** | Water sloshes. Fish, food, bubbles and seaweed drift toward the low side. |
| **Tilt + touch** | An aim marker (triangle at the top) follows the tilt. Touch drops the food where it points. |
| **Knock / bump** | All fish dart away. The small octopus jumps. |
| **Hard shake** | Fish panic. The big octopus (if present) squirts an ink cloud and jets away. |
| **Sitting still for 2 minutes** | The screen dims. Any movement or touch wakes it. |
| **Turned upside down** | **Night mode:** the picture rotates 180 degrees so it reads upright, the moon and stars appear, every fish gets a "z", fish slow down and sink, the small octopus rests on the floor with a "z", and the big octopus stays away. |

---

## Hardware

| Part | Notes |
|---|---|
| Seeed Studio **XIAO ESP32-S3** | Default I2C pins: SDA = D4, SCL = D5 |
| **1.3" OLED, SH1106**, 128x64, I2C | Address `0x3C` (some modules use `0x3D`) |
| **Touch sensor TTP223** (or similar) | Output goes HIGH when touched |
| **MPU6050** breakout (GY-521 or similar) | Address `0x68` (`0x69` if AD0 is pulled high) |
| Jumper wires, USB-C cable | |

The OLED and MPU6050 share the same two I2C wires.

---

## Wiring

```
                 XIAO ESP32-S3
              +-----------------+
   OLED  VCC -|3V3          D4  |- SDA  (OLED + MPU6050)
   OLED  GND -|GND          D5  |- SCL  (OLED + MPU6050)
              |             D1  |- OUT  (touch sensor)
              +-----------------+
```

| Module | VCC | GND | SDA | SCL | Other |
|---|---|---|---|---|---|
| OLED SH1106 | 3V3 | GND | D4 | D5 | |
| MPU6050 | 3V3 | GND | D4 | D5 | AD0 left low for `0x68` |
| Touch (TTP223) | 3V3 | GND | | | OUT -> D1 |

**Mounting tip:** fix the MPU6050 **flat, chip facing up**, in line with the screen. The software assumes:
- one sensor axis (X or Y) runs left-right along the screen,
- the Z axis points up when the device sits normally.

---

## Software setup

1. Install the **Arduino IDE** (2.x recommended).
2. Add ESP32 support: *Boards Manager* -> install **esp32 by Espressif Systems**.
3. Select the board **XIAO_ESP32S3** (Tools -> Board -> esp32).
4. Install these libraries from the *Library Manager*:
   - **Adafruit SH110X**
   - **Adafruit GFX Library**
   - (`Wire` is built in.)
5. Open the sketch, select the correct port and upload.
6. Open the Serial Monitor at **115200 baud** to see messages.

> **Important:** power the device on while it is **flat and level**. At start-up it measures the "level" position (`tiltZero`). If it starts tilted, everything will feel off-centre until the next reset.

---

## How to play (controls)

| Action | Result |
|---|---|
| Tap the touch sensor | Drop food (400 ms cooldown). Also wakes the screen. |
| Tilt left / right | Water sloshes, the aim marker appears and moves (visible for about 3 s after the last tilt or touch). |
| Tilt, then touch | Food drops under the aim marker. |
| Knock or bump the device | Fish and small octopus get startled. |
| Shake hard | Fish panic, the big octopus squirts ink (if it is in the tank). |
| Leave it alone for 2 minutes | Screen dims. |
| Move or touch it | Screen wakes up. |
| Turn upside down and hold for half a second | Night mode. |
| Turn it back | Normal mode. |

---

## Configuration reference

All settings are constants near the top of the sketch.

### Visual and feature switches

| Constant | Default | Meaning |
|---|---|---|
| `SHOW_WATER_WAVE` | `false` | Show the animated water surface line |
| `SHOW_STONES` | `true` | Show stones on the floor |
| `USE_TILT_SLOSH` | `true` | Tilt makes the water slosh |
| `USE_TILT_AIM` | `true` | Tilt moves the aim marker for feeding |
| `USE_KNOCK_SCARE` | `true` | Knocks startle the fish |
| `USE_SHAKE_INK` | `true` | Hard shake makes the big octopus squirt ink |
| `USE_SLEEP_MODE` | `true` | Dim the screen after sitting still |
| `USE_FLIP_MODE` | `true` | React when the device is turned upside down |
| `FLIP_STYLE` | `0` | `0` = night mode (moon, sleeping fish), `1` = everything floats up |
| `FLIP_ROTATE_DISPLAY` | `true` | Rotate the picture 180 degrees when flipped |
| `FLIP_MIRROR_TILT` | `true` | Swap left/right tilt when flipped. Set `false` if tilt feels backwards when flipped. |
| `IMU_DEBUG` | `false` | Print sensor values to the Serial Monitor every 200 ms |

### Sensor tuning (from calibration)

| Constant | Current value | Meaning |
|---|---|---|
| `TILT_AXIS` | `1` | Sensor axis along the screen's left-right direction (`0` = X, `1` = Y) |
| `TILT_SIGN` | `-1` | Flip to `1` if tilting right moves things left |
| `TAP_G` | `0.50f` | A knock is a sudden jump bigger than this (g) |
| `STILL_G` | `0.06f` | Movement smaller than this counts as "sitting still" (g) |
| `SHAKE_G` | `0.80f` | A shake swing must exceed this (g) |
| `SHAKE_SWINGS` | `5` | Number of hard back-and-forth swings in a row that make a shake |
| `SLOSH_GAIN` | `2.4f` | How strongly tilt drives the water slosh |
| `AIM_GAIN` | `150.0f` | How far the aim marker moves per unit of tilt (pixels) |
| `SLEEP_AFTER_MS` | `120000UL` | Time sitting still before the screen dims (ms) |

> These values were measured on one specific build. Your own device will differ slightly, so see [MPU6050 calibration](#mpu6050-calibration).

### Tank contents

| Constant | Default | Meaning |
|---|---|---|
| `NUM_FISH` | `3` | Number of fish (sizes and styles repeat every 3) |
| `MAX_FOOD` | `12` | Maximum food pieces on screen |
| `NUM_BUBBLES` | `5` | Number of bubbles |
| `NUM_WEED` | `5` | Seaweed strands (positions in `weedX`, heights in `weedH`) |
| `BRANCHES` | `3` | Side branches per seaweed strand |
| `NUM_STONES` | `2` | Number of stones |
| `NUM_SMALL_OCTO` | `1` | Number of small octopuses (the big one is always added) |
| `FLOOR_Y` | `60` | Y position of the floor line |
| `FRAME_MS` | `40` | Frame time (about 25 FPS) |

---

## MPU6050 calibration

The default numbers will work for many builds, but mounting differs (which way the chip points, how hard you knock). A guided helper sketch, `mpu6050_calibration.ino`, measures your device and prints values to paste into the tank sketch.

### Running it
1. Upload `mpu6050_calibration.ino` (only the MPU6050 needs to be connected).
2. Open the Serial Monitor at 115200 baud and send `a`.
3. Follow the prompts. Each step has a 4-second countdown.

| Step | What to do | What it measures |
|---|---|---|
| 1. Level | Flat on the table, don't touch | Resting values, noise, `STILL_G` |
| 2. Left | Tilt the left side of the screen down, hold | Which axis tilts, and which direction |
| 3. Right | Tilt the right side down, hold | `TILT_AXIS`, `TILT_SIGN`, tilt range |
| 4. Knock | About 8 taps, 1 second apart | `TAP_G` |
| 5. Shake | Shake hard for 6 seconds | Shake swing counts |
| 6. Gentle | Slow tilts and gentle movement | Checks nothing false-triggers |
| 7. Flip | Fully upside down, hold | Confirms flip detection works |

Send `p` instead of `a` for live plotter mode (open *Tools -> Serial Plotter*).

### Reading the result
The sketch ends with a paste-ready block:

```cpp
#define TILT_AXIS   1
#define TILT_SIGN  -1
const float TAP_G      = 0.50f;
const float STILL_G    = 0.06f;
const float SHAKE_G    = 0.80f;
const float AIM_GAIN   = 150.0f;
const float SLOSH_GAIN = 2.4f;
```

### Tips
- **Knock threshold:** `TAP_G` has to sit between the biggest jump during normal handling and your weakest real knock. If knocks are missed, lower it. If tilting quickly scares the fish, raise it.
- **Shake threshold:** choose a value where a hard shake counts many swings but gentle handling counts none.
- **Flip test:** the device must be turned *fully* upside down. If Z stays positive, it was held on its side.
- After pasting the values, set `IMU_DEBUG = true` once and check `upside-down=1`, tilt direction and knock behavior in the Serial Monitor. Then set it back to `false`.

---

## How it works

### Sensor processing
The MPU6050 is set to +-8 g and a 94 Hz internal filter. The sketch reads the three acceleration axes as often as possible (at least every 4 ms) and also samples again right after each slow screen transfer. Three signals come from the same readings:

- **Gravity / tilt.** Two low-pass filters track the reading: a slow one (about 0.4 s) for gravity direction, and a faster one (about 0.1 s) for tilt.
  `tilt = TILT_SIGN * (fastReading - tiltZero)`, clamped to -1..+1. Positive means the right side is down.
- **Knock.** The size of the change between two consecutive readings (the "jump"). Tilting and turning are smooth, so they do not count. A knock is a jump above `TAP_G`, with a 700 ms cooldown.
- **Shake.** The reading minus the slow gravity filter. A swing is counted each time it passes `+-SHAKE_G` in the opposite direction on the same axis. `SHAKE_SWINGS` swings in a row (with no gap longer than 600 ms) trigger a shake, with a 3 second cooldown.
- **Flip.** The slow Z reading below -0.6 g for at least 0.5 s means upside down. It returns to normal when Z rises above -0.3 g.
- **Stillness.** Movement size below `STILL_G` and tilt changes below 0.04 count as sitting still.

### Water slosh
The tilt drives a small **spring**: it overshoots slightly and settles, so the water swings back like real water. `sloshPos` (-1..+1) pushes fish, food, bubbles and seaweed sideways.

### Night mode (upside down)
When flipped:
- `nightMode` turns on and the display is rotated 180 degrees (`setRotation(2)`).
- Tilt is mirrored so the picture still follows your hand.
- The screen dims.
- Fish slow down and settle in the lower water. Moon, twinkling stars and a "z" above each fish appear.
- The small octopus sinks to the floor and rests with slow breathing and its own "z".
- The big octopus doesn't start new visits. If it is already in the tank, it swims off through the nearest side.
- The aim marker is hidden.

### Sleep dim (2 minutes still)
The screen contrast drops to a very low level. Touch, tilt, or any movement wakes it. This is separate from night mode and does not change the octopuses.

### Frame loop
`loop()` runs at about 25 FPS:
1. Read the IMU (also continuously between frames).
2. Handle touch (rising edge only, 400 ms cooldown).
3. `updateReactions()`: knocks, shakes, slosh spring, aim marker, sleep, flip modes.
4. Update food, fish, bubbles, octopuses, ink.
5. Draw everything back-to-front: seaweed, stones, bubbles, night extras, ink, octopuses, fish, food, aim marker, floor.
6. Send the frame to the OLED and read the IMU once more.

---

## Code map

| Section | Functions |
|---|---|
| **Display** | `initializeDisplay`, `clearDisplay`, `updateDisplay`, `setDisplayDim` |
| **Setup helpers** | `initFish`, `initWeed`, `initStones`, `initOctopuses` |
| **Feeding** | `feed`, `nearestFood`, `updateFood` |
| **Fish** | `updateFish`, `drawFish` |
| **Scenery** | `updateBubbles`, `drawBubbles`, `drawSeaweed`, `weedSway`, `drawStones`, `drawFloor`, `drawWater` |
| **Octopuses** | `updateOctopus` (big, state machine), `updateSmallOcto`, `octoHeadTo`, `drawOctopus` |
| **Ink** | `squirtInk`, `updateInk`, `drawInk` |
| **MPU6050** | `initImu`, `readAccel`, `calibrateImu`, `updateImu` |
| **Reactions** | `scareAll`, `updateReactions`, `drawAimMarker`, `drawNightExtras` |
| **Main** | `setup`, `loop` |

The big octopus is a small state machine: `0` away, `1` entering, `2` wandering, `3` leaving.

---

## Customizing

- **More fish:** change `NUM_FISH`. Sizes and styles repeat every 3 fish.
- **More small octopuses:** change `NUM_SMALL_OCTO`.
- **Float-up flip instead of night mode:** set `FLIP_STYLE = 1`. Note that the picture still rotates 180 degrees when flipped. Set `FLIP_ROTATE_DISPLAY = false` for the original float-up look without rotation.
- **Longer or shorter sleep time:** change `SLEEP_AFTER_MS`. Use `10000UL` while testing.
- **Turn off a feature:** set its `USE_*` flag to `false`.
- **Different seaweed layout:** edit `weedX` and `weedH`.
- **Less jumpy knock detection:** raise `TAP_G`. **More sensitive:** lower it.

---

## Troubleshooting

| Problem | Likely cause and fix |
|---|---|
| Serial says "OLED not found" | Check SDA/SCL wiring, power, and the I2C address (`0x3C` or `0x3D`). |
| Serial says "MPU6050 not found" | Check wiring and address `0x68` / `0x69`. The tank still runs without motion features. |
| Compile error about `SHAKE_SWINGS` | Make sure `const int SHAKE_SWINGS = 5;` is defined with the other sensor constants. |
| Water and aim marker move the wrong way | Change `TILT_SIGN` to `1` (or `-1`). |
| Tilt barely does anything, or the wrong axis reacts | Re-run the calibration sketch and update `TILT_AXIS`. |
| Everything feels off-centre | Power on while the device is flat and level (`tiltZero` is measured at start-up). |
| Fish get scared when you just tilt quickly | Raise `TAP_G`. |
| Knocks are ignored | Lower `TAP_G`, or knock harder. |
| Flip not detected | Turn the device fully upside down. Check `upside-down=1` with `IMU_DEBUG = true`. |
| Tilt is backwards only when flipped | Set `FLIP_MIRROR_TILT = false`. |
| Screen dims too early or never | Adjust `STILL_G` and `SLEEP_AFTER_MS`. A noisy sensor can keep resetting the timer, so raise `STILL_G`. |
| Picture is upside down in normal use | The sensor or display is mounted rotated. Check `TILT_SIGN`, or set `FLIP_ROTATE_DISPLAY = false` while testing. |
| Display looks too bright or too dim after waking | `setDisplayDim` uses contrast `0xFF` for normal. Change the value if you prefer another level. |

---

## Known limitations

- Flip detection assumes the chip faces **up** when the device is sitting normally (Z is about +1 g).
- `TILT_AXIS` supports X or Y only, so the chip must be mounted flat and not vertical.
- Knock detection compares two consecutive readings. Their spacing varies because the OLED transfer takes time, so the best `TAP_G` is found by testing on the real device.
- The 2-minute sleep dim does not affect the octopuses. Only night mode does.
- The aim marker only shows after a tilt or touch, and is hidden in night mode and while the screen is dimmed.

---

## License

Add the license of your choice (for example MIT) before publishing.
