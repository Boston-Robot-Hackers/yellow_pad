// Yellow Pad, first design (arrows + speed panel in %): a touchscreen BLE gamepad for driving a ROS robot via teleop_twist_joy.
//
// Holding an arrow moves the left stick in that direction by that arrow's speed
// and holds button 1 (the teleop enable / dead-man button). Releasing stops.
// Speeds are set per direction on the right-hand panel and saved in flash.

#include <Arduino.h>
#include <BleGamepad.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// Touch controller is on its own SPI bus on the CYD.
constexpr int TOUCH_CS = 33;
constexpr int TOUCH_IRQ = 36;
constexpr int TOUCH_MOSI = 32;
constexpr int TOUCH_MISO = 39;
constexpr int TOUCH_CLK = 25;

// Raw touch range, typical for the CYD. Adjust if taps land off-target.
constexpr int RAW_X_MIN = 200, RAW_X_MAX = 3700;
constexpr int RAW_Y_MIN = 240, RAW_Y_MAX = 3800;

constexpr int16_t AXIS_MIN = 0;
constexpr int16_t AXIS_MAX = 32767;
constexpr int16_t AXIS_CENTER = 16384;

constexpr int SPEED_STEP = 10;
constexpr int SPEED_DEFAULT = 50;
constexpr uint32_t LOOP_MS = 20;

enum Dir { UP, DOWN, LEFT, RIGHT, DIR_COUNT, NONE = -1 };
const char* const DIR_NAMES[DIR_COUNT] = {"Up", "Down", "Left", "Right"};
const char* const PREF_KEYS[DIR_COUNT] = {"up", "down", "left", "right"};

struct Rect {
  int16_t x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Layout for a 320x240 landscape screen.
constexpr int PAD_CX = 85, PAD_CY = 132, KEY = 54, HALF = KEY / 2;
const Rect ARROW_RECTS[DIR_COUNT] = {
    {PAD_CX - HALF, PAD_CY - HALF - KEY, KEY, KEY},  // up
    {PAD_CX - HALF, PAD_CY + HALF, KEY, KEY},        // down
    {PAD_CX - HALF - KEY, PAD_CY - HALF, KEY, KEY},  // left
    {PAD_CX + HALF, PAD_CY - HALF, KEY, KEY},        // right
};
const Rect STOP_RECT = {PAD_CX - HALF, PAD_CY - HALF, KEY, KEY};
const Rect SPEED_ROWS[DIR_COUNT] = {
    {180, 50, 136, 28}, {180, 82, 136, 28}, {180, 114, 136, 28}, {180, 146, 136, 28}};
const Rect MINUS_RECT = {180, 186, 62, 44};
const Rect PLUS_RECT = {254, 186, 62, 44};

constexpr uint16_t COLOR_BG = TFT_BLACK;
constexpr uint16_t COLOR_KEY = 0x2945;      // dark grey
constexpr uint16_t COLOR_ACTIVE = TFT_GREEN;
constexpr uint16_t COLOR_STOP = TFT_RED;
constexpr uint16_t COLOR_SELECT = TFT_YELLOW;

TFT_eSPI tft;
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
BleGamepad gamepad("Yellow Pad", "pitosalas", 100);
Preferences prefs;

int speeds[DIR_COUNT];
int selected = UP;
Dir active = NONE;
bool stopHeld = false;
bool wasTouched = false;
bool wasConnected = false;

void drawArrowGlyph(const Rect& r, Dir d, uint16_t color) {
  int cx = r.x + r.w / 2, cy = r.y + r.h / 2, s = 14;
  switch (d) {
    case UP: tft.fillTriangle(cx, cy - s, cx - s, cy + s, cx + s, cy + s, color); break;
    case DOWN: tft.fillTriangle(cx, cy + s, cx - s, cy - s, cx + s, cy - s, color); break;
    case LEFT: tft.fillTriangle(cx - s, cy, cx + s, cy - s, cx + s, cy + s, color); break;
    case RIGHT: tft.fillTriangle(cx + s, cy, cx - s, cy - s, cx - s, cy + s, color); break;
    default: break;
  }
}

void drawArrow(Dir d) {
  const Rect& r = ARROW_RECTS[d];
  bool on = active == d;
  tft.fillRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 8, on ? COLOR_ACTIVE : COLOR_KEY);
  drawArrowGlyph(r, d, on ? TFT_BLACK : TFT_WHITE);
}

void drawStop() {
  const Rect& r = STOP_RECT;
  tft.fillRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 8, stopHeld ? TFT_WHITE : COLOR_STOP);
  tft.setTextColor(stopHeld ? COLOR_STOP : TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("STOP", r.x + r.w / 2, r.y + r.h / 2, 2);
}

void drawSpeedRow(int d) {
  const Rect& r = SPEED_ROWS[d];
  bool sel = selected == d;
  tft.fillRect(r.x, r.y, r.w, r.h, sel ? COLOR_SELECT : COLOR_BG);
  tft.setTextColor(sel ? TFT_BLACK : TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(DIR_NAMES[d], r.x + 6, r.y + r.h / 2, 4);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(String(speeds[d]) + "%", r.x + r.w - 6, r.y + r.h / 2, 4);
}

void drawButton(const Rect& r, const char* label) {
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, COLOR_KEY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, r.x + r.w / 2, r.y + r.h / 2, 4);
}

void drawStatus(bool connected) {
  tft.fillRect(0, 0, 320, 24, COLOR_BG);
  tft.fillCircle(10, 12, 6, connected ? TFT_GREEN : TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(connected ? "Connected" : "Waiting for Bluetooth", 22, 12, 2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SPEED", 248, 36, 2);
}

void drawAll() {
  tft.fillScreen(COLOR_BG);
  drawStatus(wasConnected);
  for (int d = 0; d < DIR_COUNT; d++) {
    drawArrow(static_cast<Dir>(d));
    drawSpeedRow(d);
  }
  drawStop();
  drawButton(MINUS_RECT, "-");
  drawButton(PLUS_RECT, "+");
}

void sendGamepad() {
  int16_t x = AXIS_CENTER, y = AXIS_CENTER;
  if (active != NONE) {
    int16_t offset = static_cast<int32_t>(AXIS_MAX - AXIS_CENTER) * speeds[active] / 100;
    // HID axis minimum is up/left; ROS joy reports that as +1 (forward / turn left).
    switch (active) {
      case UP: y = AXIS_CENTER - offset; break;
      case DOWN: y = AXIS_CENTER + offset; break;
      case LEFT: x = AXIS_CENTER - offset; break;
      case RIGHT: x = AXIS_CENTER + offset; break;
      default: break;
    }
  }
  gamepad.setX(x);
  gamepad.setY(y);
  if (active != NONE) {
    gamepad.press(BUTTON_1);
  } else {
    gamepad.release(BUTTON_1);
  }
  if (gamepad.isConnected()) gamepad.sendReport();
}

void setActive(Dir d) {
  if (d == active) return;
  Dir prev = active;
  active = d;
  if (prev != NONE) drawArrow(prev);
  if (active != NONE) drawArrow(active);
  sendGamepad();
}

void setStopHeld(bool held) {
  if (held == stopHeld) return;
  stopHeld = held;
  drawStop();
}

void changeSpeed(int delta) {
  int v = constrain(speeds[selected] + delta, 0, 100);
  if (v == speeds[selected]) return;
  speeds[selected] = v;
  prefs.putInt(PREF_KEYS[selected], v);
  drawSpeedRow(selected);
}

void selectRow(int d) {
  if (d == selected) return;
  int prev = selected;
  selected = d;
  drawSpeedRow(prev);
  drawSpeedRow(selected);
}

bool readTouch(int& x, int& y) {
  if (!touch.tirqTouched() || !touch.touched()) return false;
  TS_Point p = touch.getPoint();
  x = constrain(map(p.x, RAW_X_MIN, RAW_X_MAX, 0, 319), 0, 319);
  y = constrain(map(p.y, RAW_Y_MIN, RAW_Y_MAX, 0, 239), 0, 239);
  return true;
}

Dir arrowAt(int x, int y) {
  for (int d = 0; d < DIR_COUNT; d++) {
    if (ARROW_RECTS[d].contains(x, y)) return static_cast<Dir>(d);
  }
  return NONE;
}

void handleTouch() {
  int x, y;
  bool touched = readTouch(x, y);
  bool newPress = touched && !wasTouched;
  wasTouched = touched;

  if (!touched) {
    setActive(NONE);
    setStopHeld(false);
    return;
  }

  // While held, motion follows the finger; sliding off an arrow stops.
  setActive(arrowAt(x, y));
  setStopHeld(STOP_RECT.contains(x, y));

  if (!newPress) return;
  for (int d = 0; d < DIR_COUNT; d++) {
    if (SPEED_ROWS[d].contains(x, y)) selectRow(d);
  }
  if (MINUS_RECT.contains(x, y)) changeSpeed(-SPEED_STEP);
  if (PLUS_RECT.contains(x, y)) changeSpeed(SPEED_STEP);
}

void setup() {
  Serial.begin(115200);

  prefs.begin("yellowpad", false);
  for (int d = 0; d < DIR_COUNT; d++) speeds[d] = prefs.getInt(PREF_KEYS[d], SPEED_DEFAULT);

  tft.init();
  tft.setRotation(1);
  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSpi);
  touch.setRotation(1);

  BleGamepadConfiguration config;
  config.setAutoReport(false);
  config.setControllerType(CONTROLLER_TYPE_GAMEPAD);
  config.setButtonCount(4);
  config.setHatSwitchCount(0);
  config.setWhichAxes(true, true, false, false, false, false, false, false);
  config.setAxesMin(AXIS_MIN);
  config.setAxesMax(AXIS_MAX);
  gamepad.begin(&config);

  drawAll();
}

void loop() {
  bool connected = gamepad.isConnected();
  if (connected != wasConnected) {
    wasConnected = connected;
    drawStatus(connected);
    if (connected) sendGamepad();  // start the host from a centered, disabled state
  }
  handleTouch();
  delay(LOOP_MS);
}
