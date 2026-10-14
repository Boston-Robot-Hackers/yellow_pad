"""Launch joy + teleop_twist_joy for the Yellow Pad.

Run with: ros2 launch yellow_pad yellow_pad.launch.py
"""

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    default_config = os.path.join(
        get_package_share_directory("yellow_pad"), "config", "yellow_pad.yaml")
    config = LaunchConfiguration("config")
    cmd_vel = LaunchConfiguration("cmd_vel")
    return LaunchDescription([
        DeclareLaunchArgument("config", default_value=default_config,
                              description="Parameter file for joy and teleop_twist_joy"),
        DeclareLaunchArgument("cmd_vel", default_value="/cmd_vel",
                              description="Topic to publish velocity commands on"),
        Node(
            package="joy",
            executable="joy_node",
            name="joy_node",
            parameters=[config],
        ),
        Node(
            package="teleop_twist_joy",
            executable="teleop_node",
            name="teleop_twist_joy_node",
            parameters=[config],
            remappings=[("/cmd_vel", cmd_vel)],
        ),
    ])
