# Yellow Pad

A touchscreen Bluetooth gamepad for driving a ROS 2 robot.

An ESP32 "Cheap Yellow Display" (ESP32-2432S028R) shows four arrows and a
speed setting for each direction. It pairs with the robot's Raspberry Pi as a
standard BLE gamepad, and the standard ROS `joy` and `teleop_twist_joy`
packages turn it into `/cmd_vel`. Holding an arrow drives in that direction at
its speed; letting go stops the robot.

```
Yellow Pad ──BLE──▶ Pi: joy_node ─/joy─▶ teleop_twist_joy ─/cmd_vel─▶ robot
```

## What's here

| Directory | What it is |
|---|---|
| [`firmware/`](firmware/) | ESP32 firmware (PlatformIO, Arduino framework) |
| [`yellow_pad/`](yellow_pad/README.md) | ROS 2 Kilted package: config and launch file for joy + teleop_twist_joy |
| [`backup/`](backup/README.md) | The board's factory firmware image and how to restore it |
| [`NOTES.md`](NOTES.md) | Detailed notes: status, how it works, running, changing, troubleshooting |

## Quick start

Flash the pad (from the Mac, board on USB):

```bash
cd firmware
uvx --from platformio pio run -t upload
```

Pair it with the Pi once with `bluetoothctl` (`scan on`, then `pair`, `trust`
and `connect` the "Yellow Pad" address).

Build and run the ROS package on the Pi:

```bash
# copy yellow_pad/ to ~/ros2_ws/src/, then
cd ~/ros2_ws
rosdep install --from-paths src/yellow_pad -y
colcon build --packages-select yellow_pad
source install/setup.bash
ros2 launch yellow_pad yellow_pad.launch.py
```

Test with the wheels off the ground first (`ros2 topic echo /cmd_vel`). See
[`NOTES.md`](NOTES.md) for pairing details, expected values, the speed limits
and what's still untested.
