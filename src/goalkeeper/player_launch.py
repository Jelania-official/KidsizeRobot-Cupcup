from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        Node(
            package='goalkeeper',
            executable='goalkeeper',
            arguments=['goalkeeper_1'],
            output='screen'
        )
    ])

