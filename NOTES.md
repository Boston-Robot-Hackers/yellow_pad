# Yellow Pad: project notes

A touchscreen Bluetooth gamepad for driving a ROS 2 robot. An ESP32 "Cheap
Yellow Display" board shows four arrows and per-direction speed settings, and
pairs with the robot's Raspberry Pi as a standard BLE gamepad. On the Pi, the
standard `joy` and `teleop_twist_joy` packages turn it into `/cmd_vel`.

Last updated 2026-10-18.

## Where things stand

Done and working:

- Identified the board and backed up its factory firmware (`backup/`).
- Wrote, built and flashed the Yellow Pad firmware (`firmware/`).
- Paired the pad with the Pi (dome-R1); `ros2 run joy joy_enumerate_devices`
  lists it.
- Built the ROS package `yellow_pad` on the Pi. (It was copied there as
  `~/ros2_ws/src/yellow_joy` before the directory was renamed; see
  "ROS package on the Pi" for replacing it.)

Not done yet:

- **`/cmd_vel` dry test.** Launch the package with the robot's bringup off and
  check `ros2 topic echo /cmd_vel` against the table under "Testing" below.
- **Robot bringup.** `bl dome2 robot.launch.py` fails with
  `Profile 'DEFAULT_PROFILE': no env var 'DEFAULT_PROFILE' found`. That's from
  dome2's own `robot.launch.py` (line 76): the `profile` launch argument must
  name an environment variable that is set. Unresolved; check the launch file
  and `env | grep -i profile`.
- **Message type.** Not yet checked whether the robot wants `Twist` or
  `TwistStamped` (`ros2 topic info /cmd_vel`). See "Changing the ROS side".
- **Possible duplicate joystick nodes.** Check `ros2 node list` with bringup
  running: if bringup already starts `joy_node` or `teleop_twist_joy_node`,
  it will clash with this package.
- **License.** `package.xml` has a `TODO` license.

## Layout

```
yellow_pad/
├── NOTES.md          this file
├── backup/           factory firmware image + how to restore it (see backup/README.md)
├── firmware/         ESP32 firmware (PlatformIO project)
│   ├── platformio.ini   two envs: cyd (tabbed design), cyd_v1 (first design)
│   └── src/main.cpp     tabbed design; src/arrows_v1.cpp is the first design
└── yellow_pad/       ROS 2 package yellow_pad (copy to the Pi's ~/ros2_ws/src/)
    ├── package.xml, CMakeLists.txt, README.md
    ├── config/yellow_pad.yaml
    └── launch/yellow_pad.launch.py
```

## The hardware

- **Board:** ESP32-2432S028R, the "Cheap Yellow Display" (CYD). 2.8" 320x240
  ILI9341 screen with resistive touch (XPT2046). Sold by Shenzhen Jingcai
  Intelligent; manual at http://pan.jczn1688.com/1
- **Chip:** ESP32-D0WD-V3 rev 3.1, dual core 240 MHz, Wi-Fi, Bluetooth Classic
  and BLE. 4 MB flash, no PSRAM. MAC 68:09:47:85:c8:b0 (Bluetooth address
  should be ...:c8:b2).
- **USB:** micro-USB through a CH340 serial chip. macOS has the driver built
  in; it appears as `/dev/cu.usbserial-*` (was `-2130`; the number depends on
  the port).
- **Cables:** the manual says don't use a USB-C to USB-C cable (the board
  lacks the resistors that make a USB-C port switch on). A micro-USB data
  cable through a USB-C adapter works.
- **Serial reliability:** through the Mac mini's USB-C adapter, large reads
  corrupt at every speed tried. Uploads are set to 115200 baud. Short
  operations are fine.
- **Power on the go:** a USB power bank with a micro-USB cable. Watch for
  banks that auto-shut-off at low current (the board draws roughly
  100-150 mA); use a bank with a low-current/trickle mode. (A power bank's
  own lithium cells are fine: it outputs regulated 5 V and has its own
  charging and protection.) Don't wire a bare LiPo cell to the board: it
  gives 3.0-4.2 V rather than 5 V, and the board has no charging or
  low-voltage cutoff.
- **Built-in battery (planned, not yet tested):** NULLLAB PM11 module
  (docs: https://github.com/nulllaborg/pm11-module). 1200 mAh LiPo, boost to
  5 V/1.2 A, USB-C charging (~1 hour), 4-LED gauge, slide switch, 56×40 mm
  with two M4 holes, so it fits behind the board. Expect roughly 5-8 hours of
  driving per charge.
  - Connect with a short USB-A to micro-USB cable, or wire its 5V header to
    the CYD's 4-pin P1 connector (5V → VIN, GND → GND; check polarity against
    both pinouts first).
  - **It has no low-battery cutoff.** Switch it OFF after driving (OFF fully
    disconnects the battery), and recharge when only one LED is lit.
  - **Not a UPS:** plugging or unplugging USB-C while running will likely
    reset the pad. That's safe (Bluetooth drops, the robot stops), but don't
    do it mid-drive.
  - With the switch OFF and USB-C plugged in, USB powers the board and the
    battery stays out of the circuit. Handy at the bench.
  - To check once it arrives: the docs don't mention a low-load auto
    shut-off, but confirm by running the pad on it for 30+ minutes.
- **Other battery options** (suggestions from Alan Kilian, 2026-10-18):
  - All-in-one: the NULLLAB PM11 above
    (https://www.amazon.com/dp/B0GWZTQC35).
  - Separate charger plus cell: Diymore TP4056 USB-C charger boards, 5-pack
    (https://www.amazon.com/dp/B07MDPLQ18). 1 A charging, 4.2 V cutoff,
    discharge cutoff at 3.2 V, 3 A over-current protection. Check whether
    this board also boosts to 5 V: a plain TP4056 outputs only the battery's
    3.0-4.2 V, so it would need a separate 5 V boost board as well.
  - A matching cell: Qimoo 503035 3.7 V 500 mAh LiPo
    (https://www.amazon.com/dp/B0CNLNFPYM). About 2-3 hours of driving at
    the pad's draw, less than half the PM11's runtime.
  - Adafruit PowerBoost 500C (https://www.adafruit.com/product/1944):
    charger and 5 V boost in one board, runs the load while charging, pairs
    with any 1S LiPo cell. Well documented. It's the more reliable choice
    if the PM11 disappoints.
  - Cheapest: TP4057 USB-C charger boards with protection, 9-pack, from
    Temu
    (https://www.temu.com/9pcs-tp4057-1a-3-7v-lithium-battery-charging-and--board-with-protection-type-c-usb-c-li--charging-board-g-606737797099759.html).
    These charge only and don't boost, so they need a 5 V boost board too.

## How it works

```
Yellow Pad ──BLE gamepad──▶ Pi: /dev/input/js0
                              └▶ joy_node          → /joy      (sensor_msgs/Joy)
                                  └▶ teleop_twist_joy → /cmd_vel (geometry_msgs/Twist)
                                                          └▶ robot's motor driver
```

### The screen (landscape, 320x240)

There are two designs; both send the same gamepad reports for the arrows.

**Tabbed design** (`src/main.cpp`, env `cyd`, the default):

- **Top:** Bluetooth status, then two tabs, **Drive** and **Settings**.
- **Drive:** the cross of four arrows with a red STOP in the middle, and three
  buttons **A**, **B**, **C** on the right. Holding A/B/C holds gamepad
  buttons 2/3/4. Nothing on the Pi uses those yet.
- **Settings:** speeds per direction in real units: Forward and Back in m/s
  (step 0.05, default 0.15), Left and Right in rad/s (step 0.10, default 0.50).
  Tap a row, then **−** / **+**. Saved in flash (NVS, namespace `yellowpad2`).
  New settings go in the `settings[]` table in `main.cpp`. Four rows fill the
  page, so a fifth setting will need paging or scrolling.
- The pad converts a speed to a stick value by dividing by the robot's
  full-stick speed, `LINEAR_MAX` (0.3 m/s) and `ANGULAR_MAX` (1.0 rad/s) in
  `main.cpp`. **These must match `scale_linear.x` and `scale_angular.yaw` in
  `config/yellow_pad.yaml`**; if the YAML changes, change them too, or the
  displayed m/s will be wrong. The maximum speed is still capped by the YAML.

**First design** (`src/arrows_v1.cpp`, env `cyd_v1`):

- **Left:** a cross of four arrows with a red STOP in the middle.
- **Right:** a speed list (Up, Down, Left, Right). Tap a row to select it
  (yellow), then **−** / **+** change it by 10%. Speeds default to 50% and are
  saved in flash (NVS, namespace `yellowpad`), so they survive power cycles.
- **Top:** Bluetooth status, "Waiting for Bluetooth" (grey) or "Connected"
  (green).

### What the pad sends

| Pad | Gamepad | ROS `/joy` | `/cmd_vel` |
|---|---|---|---|
| Up / Forward held | left stick Y up by that speed | `axes[1]` = +speed | `linear.x` = +speed × 0.3 |
| Down / Back held | left stick Y down | `axes[1]` = −speed | `linear.x` = −speed × 0.3 |
| Left held | left stick X left | `axes[0]` = +speed | `angular.z` = +speed × 1.0 |
| Right held | left stick X right | `axes[0]` = −speed | `angular.z` = −speed × 1.0 |
| Any arrow held | button 1 pressed | `buttons[0]` = 1 | enables motion |
| Release / STOP / slide off | stick centered, button released | all 0 | robot stops |
| A / B / C held (tabbed design) | button 2 / 3 / 4 | `buttons[1..3]` = 1 | nothing yet |

Here "speed" is the stick fraction, 0 to 1: the percentage in the first
design, or the set m/s or rad/s divided by the full-stick speed in the tabbed
design. The full-stick speeds live on the Pi in `config/yellow_pad.yaml`
(`scale_linear.x` = 0.3 m/s, `scale_angular.yaw` = 1.0 rad/s), so the top
speed is set in the robot's config, not on the pad.

**Safety:** `teleop_twist_joy` only publishes motion while button 0 is held
(`require_enable_button: true`), and the pad only holds it while an arrow is
touched. Lifting your finger, Bluetooth dropping, or the pad losing power all
stop the robot. On release, teleop_twist_joy sends one zero Twist, then goes
quiet.

**Limits:** resistive touch reads one point at a time, so one direction at a
time, no diagonals, and you can't hold A/B/C while driving. Pairing works
with Linux, macOS, Windows and Android; iOS generally ignores generic BLE
gamepads.

## Running it

### Pad

Power it on (USB or power bank). It advertises as "Yellow Pad" and reconnects
to the Pi on its own once paired and trusted. A BLE gamepad connects to one
host at a time, so if it's connected to the Mac, the Pi can't see it.

### Pairing with a Pi (one-time)

```bash
sudo apt update && sudo apt install bluez      # if bluetoothctl is missing
sudo systemctl enable --now bluetooth
bluetoothctl list                               # must show a Controller
# On Ubuntu for Pi, if no controller: sudo apt install pi-bluetooth && sudo reboot

bluetoothctl
  power on
  agent on
  default-agent
  scan on            # wait for "Yellow Pad"
  scan off
  pair <addr>
  trust <addr>       # makes it auto-reconnect
  connect <addr>
  exit
```

Check: `ls /dev/input/js*` and `ros2 run joy joy_enumerate_devices`.
If pairing gets stuck: `bluetoothctl remove <addr>` and start again.

### ROS package on the Pi

Install or update (copy from the Mac, then build on the Pi):

```bash
# Mac
scp -r /Users/pitosalas/mydev/yellow_pad/yellow_pad pitosalas@dome-R1:~/ros2_ws/src/

# Pi
cd ~/ros2_ws
rm -rf src/yellow_joy      # old copy from before the rename; two copies of the same package break colcon
rosdep install --from-paths src/yellow_pad -y   # installs joy, teleop_twist_joy
colcon build --packages-select yellow_pad
source install/setup.bash
```

Run:

```bash
ros2 launch yellow_pad yellow_pad.launch.py
# options: cmd_vel:=/other/topic   config:=/path/to/other.yaml
```

To actually drive, the robot's bringup must also be running (it owns the
motor driver). Run only one teleop at a time: anything else publishing to
`/cmd_vel` (e.g. teleop_twist_keyboard) will fight the pad.

### Testing (do this with wheels off the ground or bringup off)

```bash
ros2 topic echo /cmd_vel
```

With the tabbed design's default speeds (Forward and Back 0.15 m/s, Left
and Right 0.50 rad/s): Forward → `linear.x: 0.15`, Back → `-0.15`,
Left → `angular.z: 0.5`, Right → `-0.5`, release → one all-zero message.
Forward set to 0.25 → `linear.x: 0.25`. (In the first design, 50% gives the
same values as the defaults above, and Up at 80% gives `0.24`.)

Troubleshooting:

- Nothing on `/cmd_vel`: `ros2 topic echo /joy` and press an arrow. If `/joy`
  moves but `/cmd_vel` doesn't, the enable button index is wrong (check which
  `buttons[]` entry goes to 1 and set `enable_button` to it).
- A pair of directions reversed: negate the scale in the YAML (e.g.
  `scale_angular.yaw: -1.0`).
- Robot ignores commands: `ros2 topic info /cmd_vel`. If it's `TwistStamped`,
  set `publish_stamped_twist: true`.

## Changing the firmware

The tabbed design is `firmware/src/main.cpp`, the first design
`firmware/src/arrows_v1.cpp`; build settings in `firmware/platformio.ini`. Each
PlatformIO env builds one of the two files (`build_src_filter`). Add
`-e cyd_v1` to the commands below to build or flash the first design.

### Build, flash, watch

No install needed: PlatformIO runs via `uvx` (from `brew install uv`).

```bash
cd /Users/pitosalas/mydev/yellow_pad/firmware
uvx --from platformio pio run                  # build only
uvx --from platformio pio run -t upload        # build + flash
uvx --from platformio pio device monitor       # serial output (Ctrl-C to quit)
```

Close any serial monitor before uploading. If an upload fails with corrupt
data, run it again. The Arduino IDE's bundled esptool is Intel-only and fails
on this Mac ("bad CPU type"); use the commands above instead.

Library and platform versions are pinned in `platformio.ini` (espressif32
7.1.3, ESP32-BLE-Gamepad 0.8.0, NimBLE-Arduino 2.5.1, TFT_eSPI 2.5.43,
XPT2046_Touchscreen from GitHub). Change them deliberately: the BLE libraries
are sensitive to version mismatches.

### Common changes

The table is for `main.cpp` (tabbed design). In `arrows_v1.cpp`, the speed step
and default are `SPEED_STEP` (10) and `SPEED_DEFAULT` (50), and the panel is
`SPEED_ROWS`.

| Want | Change |
|---|---|
| Speed step, default, or a new setting | the `settings[]` table (values in thousandths: mm/s, mrad/s). Saved values override the defaults. |
| Robot's full-stick speed changed in the YAML | `LINEAR_MAX`, `ANGULAR_MAX` |
| Bluetooth name | `BleGamepad gamepad("Yellow Pad", ...)`. Then update `device_name` in the YAML and re-pair on the Pi (`remove`, then pair again). |
| Taps land off-target | `RAW_X_MIN/MAX`, `RAW_Y_MIN/MAX` (touch calibration) |
| Button positions/sizes | `TAB_RECTS`, `PAD_CX`, `PAD_CY`, `KEY`, `ACTION_RECTS`, `ROW_Y`, `ROW_H`, `MINUS_RECT`, `PLUS_RECT` |
| A/B/C labels or gamepad buttons | `ACTION_LABELS`, `ACTION_BUTTONS` |
| Colors | `COLOR_*` constants |
| Which way an arrow pushes the stick | the `switch` in `sendGamepad()` |
| Report rate / touch polling | `LOOP_MS` (20 ms) |

How the code is organized:

- `setup()` loads speeds from flash, starts the display, the touch controller
  (on its own SPI bus, pins 25/32/33/36/39) and the BLE gamepad (2 axes,
  4 buttons, range 0-32767, center 16384, manual reports).
- `loop()` redraws the status when the connection changes, then calls
  `handleTouch()` every 20 ms.
- `handleTouch()` decides what's under the finger. On the Drive page, arrows,
  STOP and A/B/C follow the finger continuously. Tabs, setting rows and −/+ act
  once per tap.
- `setActive()` / `setActionHeld()` change the held arrow or button, redraw,
  and call `sendGamepad()`, which sets the axes and buttons 1-4 and sends a
  report.
- `selectTab()` redraws the tab bar and the page below it.
- Drawing is in the `draw*()` functions; only changed parts are redrawn.

### If the screen looks wrong

There are several CYD variants. If colors are inverted, add
`-DTFT_INVERSION_ON=1` to `build_flags`. If the image is wrong or blank, the
board may use an ST7789 instead: replace `-DILI9341_2_DRIVER=1` with
`-DST7789_DRIVER=1` (and possibly add `-DTFT_RGB_ORDER=TFT_BGR`).

### Ideas not done yet

- Long-press −/+ to change speed quickly.
- Tap-to-toggle motion instead of hold-to-move (would need care: it removes
  the dead-man behavior).
- Battery operation: the PM11 module is the plan (see "The hardware").

## Changing the ROS side

- **Top speeds:** `scale_linear.x` and `scale_angular.yaw` in
  `config/yellow_pad.yaml`. Start low; raise once trusted.
- **TwistStamped robots:** add `publish_stamped_twist: true` under
  `teleop_twist_joy_node` → `ros__parameters`.
- **Keep `deadzone: 0.0`.** joy rescales values outside the deadzone, so the
  stock teleop launch file's 0.3 deadzone would ignore 20% and turn 50% into
  about 29%. That's why this package has its own launch file.
- After editing on the Mac, copy to the Pi again and rebuild. Or edit on the
  Pi, but then copy changes back to the Mac so they aren't overwritten next
  time.

## Restoring the factory firmware

See `backup/README.md`. Short version:

```bash
cd /Users/pitosalas/mydev/yellow_pad/backup
shasum -a 256 -c factory-firmware-4MB.bin.sha256
uvx esptool --port /dev/cu.usbserial-XXXX --baud 115200 write-flash 0 factory-firmware-4MB.bin
```

Replace `XXXX` with the board's port (`ls /dev/cu.usbserial-*`). The Yellow
Pad firmware can be flashed again anytime from `firmware/`.
