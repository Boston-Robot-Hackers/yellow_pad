# yellow_pad

ROS 2 (Kilted) teleop for the Yellow Pad, the ESP32-2432S028R touchscreen
running `../firmware` as a BLE gamepad. Starts `joy_node` and
`teleop_twist_joy`, publishing `geometry_msgs/Twist` on `/cmd_vel`.

## Install

Copy this directory into a workspace as `src/yellow_pad`, then:

```bash
cd ~/ros2_ws
rosdep install --from-paths src/yellow_pad -y
colcon build --packages-select yellow_pad
source install/setup.bash
```

## Run

Pair the pad first (`bluetoothctl`: pair, trust, connect), then:

```bash
ros2 launch yellow_pad yellow_pad.launch.py
```

Arguments:

- `cmd_vel:=/other/topic` publishes somewhere else.
- `config:=/path/to/file.yaml` uses a different parameter file.

## Mapping

| Pad | Joy | Twist |
|---|---|---|
| Up / Down | axis 1 = +/- speed | `linear.x` = axis * `scale_linear.x` |
| Left / Right | axis 0 = +/- speed | `angular.z` = axis * `scale_angular.yaw` |
| Any arrow held | button 0 | enable (dead-man) button |

Speed is set per direction on the pad's screen as a fraction of the scales in
`config/yellow_pad.yaml` (0.3 m/s and 1.0 rad/s at 100%). If the robot expects
`TwistStamped`, set `publish_stamped_twist: true` there.
