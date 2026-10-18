// Yellow Pad, tabbed design: a touchscreen BLE gamepad for driving a ROS robot via teleop_twist_joy.
//
// Drive tab: holding an arrow moves the left stick in that direction and holds
// button 1 (the teleop enable / dead-man button). A, B and C hold buttons 2-4.
// Releasing stops. Settings tab: per-direction speeds in m/s and rad/s, saved in flash.
//
// The first design (arrows + speed panel in %) is in arrows_v1.cpp (env cyd_v1).

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

// The robot's speed at full stick. Must match scale_linear.x and
// scale_angular.yaw in yellow_pad/config/yellow_pad.yaml.
constexpr int LINEAR_MAX = 300;    // mm/s (0.3 m/s)
constexpr int ANGULAR_MAX = 1000;  // mrad/s (1.0 rad/s)

constexpr uint32_t LOOP_MS = 20;

enum Dir { UP, DOWN, LEFT, RIGHT, DIR_COUNT, NONE = -1 };
enum Tab { DRIVE, SETTINGS, TAB_COUNT };
const char* const TAB_NAMES[TAB_COUNT] = {"Drive", "Settings"};

constexpr int ACTION_COUNT = 3;
const char* const ACTION_LABELS[ACTION_COUNT] = {"A", "B", "C"};
const uint8_t ACTION_BUTTONS[ACTION_COUNT] = {BUTTON_2, BUTTON_3, BUTTON_4};

// A numeric setting, stored in thousandths of its unit (mm/s, mrad/s).
struct Setting {
  const char* label;
  const char* unit;
  const char* key;  // NVS key
  int step, max, fallback;
  int value;
};

// The first four are the speeds, indexed by Dir; their max is the full-stick speed.
// New settings go after them.
Setting settings[] = {
    {"Forward", "m/s", "up", 50, LINEAR_MAX, 150, 0},
    {"Back", "m/s", "down", 50, LINEAR_MAX, 150, 0},
    {"Left", "rad/s", "left", 100, ANGULAR_MAX, 500, 0},
    {"Right", "rad/s", "right", 100, ANGULAR_MAX, 500, 0},
};
constexpr int SETTING_COUNT = sizeof(settings) / sizeof(settings[0]);

struct Rect {
  int16_t x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Layout for a 320x240 landscape screen: status line, tab bar, page.
constexpr int STATUS_H = 20;
const Rect TAB_RECTS[TAB_COUNT] = {{0, 20, 160, 36}, {160, 20, 160, 36}};
constexpr int PAGE_Y = 58;

// Drive page.
constexpr int PAD_CX = 92, PAD_CY = 149, KEY = 56, HALF = KEY / 2;
const Rect ARROW_RECTS[DIR_COUNT] = {
    {PAD_CX - HALF, PAD_CY - HALF - KEY, KEY, KEY},  // up
    {PAD_CX - HALF, PAD_CY + HALF, KEY, KEY},        // down
    {PAD_CX - HALF - KEY, PAD_CY - HALF, KEY, KEY},  // left
    {PAD_CX + HALF, PAD_CY - HALF, KEY, KEY},        // right
};
const Rect STOP_RECT = {PAD_CX - HALF, PAD_CY - HALF, KEY, KEY};
const Rect ACTION_RECTS[ACTION_COUNT] = {{196, 66, 116, 52}, {196, 124, 116, 52}, {196, 182, 116, 52}};

// Settings page. Rows fit four settings above the -/+ buttons; more will need paging.
// The -/+ buttons run to the screen edges; the drawn area is the touch area.
constexpr int ROW_Y = 60, ROW_H = 30;
const Rect MINUS_RECT = {0, 182, 158, 58};
const Rect PLUS_RECT = {162, 182, 158, 58};

constexpr uint16_t COLOR_BG = TFT_BLACK;
constexpr uint16_t COLOR_KEY = 0x2945;  // dark grey
constexpr uint16_t COLOR_ACTIVE = TFT_GREEN;
constexpr uint16_t COLOR_STOP = TFT_RED;
constexpr uint16_t COLOR_SELECT = TFT_YELLOW;
constexpr uint16_t COLOR_ADJUST = 0x02DF;  // medium blue

TFT_eSPI tft;
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
BleGamepad gamepad("Yellow Pad", "pitosalas", 100);
Preferences prefs;

Tab tab = DRIVE;
int selected = 0;
Dir active = NONE;
int actionHeld = -1;
int adjustHeld = 0;  // -1 while the finger is on -, +1 on +
bool stopHeld = false;
bool wasTouched = false;
bool wasConnected = false;

Rect settingRect(int i) { return {8, static_cast<int16_t>(ROW_Y + i * ROW_H), 304, ROW_H - 2}; }

String formatSetting(const Setting& s) { return String(s.value / 1000.0f, 2) + " " + s.unit; }

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

void drawAction(int i) {
  const Rect& r = ACTION_RECTS[i];
  bool on = actionHeld == i;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, on ? COLOR_ACTIVE : COLOR_KEY);
  tft.setTextColor(on ? TFT_BLACK : TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(ACTION_LABELS[i], r.x + r.w / 2, r.y + r.h / 2, 4);
}

void drawSettingRow(int i) {
  Rect r = settingRect(i);
  bool sel = selected == i;
  tft.fillRect(r.x, r.y, r.w, r.h, sel ? COLOR_SELECT : COLOR_BG);
  tft.setTextColor(sel ? TFT_BLACK : TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(settings[i].label, r.x + 6, r.y + r.h / 2, 4);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(formatSetting(settings[i]), r.x + r.w - 6, r.y + r.h / 2, 4);
}

// Draws - (sign -1) or + (sign +1) as thick bars; the font's minus is too small.
void drawAdjust(int sign) {
  const Rect& r = sign < 0 ? MINUS_RECT : PLUS_RECT;
  bool on = adjustHeld == sign;
  uint16_t glyph = on ? TFT_BLACK : TFT_WHITE;
  int cx = r.x + r.w / 2, cy = r.y + r.h / 2, len = 30, thick = 8;
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, on ? COLOR_ACTIVE : COLOR_ADJUST);
  tft.fillRect(cx - len / 2, cy - thick / 2, len, thick, glyph);
  if (sign > 0) tft.fillRect(cx - thick / 2, cy - len / 2, thick, len, glyph);
}

void drawStatus(bool connected) {
  tft.fillRect(0, 0, 320, STATUS_H, COLOR_BG);
  tft.fillCircle(10, STATUS_H / 2, 5, connected ? TFT_GREEN : TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(connected ? "Connected" : "Waiting for Bluetooth", 22, STATUS_H / 2, 2);
}

void drawTabs() {
  for (int t = 0; t < TAB_COUNT; t++) {
    const Rect& r = TAB_RECTS[t];
    bool on = tab == t;
    tft.fillRect(r.x, r.y, r.w, r.h, on ? COLOR_KEY : COLOR_BG);
    if (on) tft.fillRect(r.x, r.y + r.h - 3, r.w, 3, COLOR_SELECT);
    tft.setTextColor(on ? TFT_WHITE : TFT_DARKGREY);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(TAB_NAMES[t], r.x + r.w / 2, r.y + r.h / 2 - 1, 4);
  }
}

void drawPage() {
  tft.fillRect(0, PAGE_Y, 320, 240 - PAGE_Y, COLOR_BG);
  if (tab == DRIVE) {
    for (int d = 0; d < DIR_COUNT; d++) drawArrow(static_cast<Dir>(d));
    drawStop();
    for (int i = 0; i < ACTION_COUNT; i++) drawAction(i);
  } else {
    for (int i = 0; i < SETTING_COUNT; i++) drawSettingRow(i);
    drawAdjust(-1);
    drawAdjust(1);
  }
}

void drawAll() {
  tft.fillScreen(COLOR_BG);
  drawStatus(wasConnected);
  drawTabs();
  drawPage();
}

void setButton(uint8_t button, bool down) {
  if (down) {
    gamepad.press(button);
  } else {
    gamepad.release(button);
  }
}

void sendGamepad() {
  int16_t x = AXIS_CENTER, y = AXIS_CENTER;
  if (active != NONE) {
    const Setting& s = settings[active];
    int16_t offset = static_cast<int32_t>(AXIS_MAX - AXIS_CENTER) * s.value / s.max;
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
  setButton(BUTTON_1, active != NONE);
  for (int i = 0; i < ACTION_COUNT; i++) setButton(ACTION_BUTTONS[i], actionHeld == i);
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

void setActionHeld(int i) {
  if (i == actionHeld) return;
  int prev = actionHeld;
  actionHeld = i;
  if (prev >= 0) drawAction(prev);
  if (actionHeld >= 0) drawAction(actionHeld);
  sendGamepad();
}

void setAdjustHeld(int sign) {
  if (sign == adjustHeld) return;
  int prev = adjustHeld;
  adjustHeld = sign;
  if (prev != 0) drawAdjust(prev);
  if (adjustHeld != 0) drawAdjust(adjustHeld);
}

void setStopHeld(bool held) {
  if (held == stopHeld) return;
  stopHeld = held;
  drawStop();
}

void selectTab(Tab t) {
  if (t == tab) return;
  tab = t;
  drawTabs();
  drawPage();
}

void selectSetting(int i) {
  if (i == selected) return;
  int prev = selected;
  selected = i;
  drawSettingRow(prev);
  drawSettingRow(selected);
}

void changeSetting(int steps) {
  Setting& s = settings[selected];
  int v = constrain(s.value + steps * s.step, 0, s.max);
  if (v == s.value) return;
  s.value = v;
  prefs.putInt(s.key, v);
  drawSettingRow(selected);
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

int actionAt(int x, int y) {
  for (int i = 0; i < ACTION_COUNT; i++) {
    if (ACTION_RECTS[i].contains(x, y)) return i;
  }
  return -1;
}

void handleTouch() {
  int x, y;
  bool touched = readTouch(x, y);
  bool newPress = touched && !wasTouched;
  wasTouched = touched;

  if (!touched) {
    setActive(NONE);
    setActionHeld(-1);
    setAdjustHeld(0);
    setStopHeld(false);
    return;
  }

  // On the Settings page, light up - or + while the finger is on it.
  if (tab == SETTINGS) {
    setAdjustHeld(MINUS_RECT.contains(x, y) ? -1 : PLUS_RECT.contains(x, y) ? 1 : 0);
  }

  // On the Drive page, motion and A/B/C follow the finger; sliding off releases.
  if (tab == DRIVE) {
    setActive(arrowAt(x, y));
    setActionHeld(actionAt(x, y));
    setStopHeld(STOP_RECT.contains(x, y));
  }

  if (!newPress) return;
  for (int t = 0; t < TAB_COUNT; t++) {
    if (TAB_RECTS[t].contains(x, y)) selectTab(static_cast<Tab>(t));
  }
  if (tab != SETTINGS) return;
  for (int i = 0; i < SETTING_COUNT; i++) {
    if (settingRect(i).contains(x, y)) selectSetting(i);
  }
  if (MINUS_RECT.contains(x, y)) changeSetting(-1);
  if (PLUS_RECT.contains(x, y)) changeSetting(1);
}

void setup() {
  Serial.begin(115200);

  // Own namespace: the first design stores percentages under the same keys.
  prefs.begin("yellowpad2", false);
  for (Setting& s : settings) s.value = constrain(prefs.getInt(s.key, s.fallback), 0, s.max);

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
