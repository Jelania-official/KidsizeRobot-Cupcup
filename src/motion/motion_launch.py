from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        Node(
            package='params',
            executable='params'
        ),
        Node(
            package='motion',
            executable='motion',
            arguments=['red_1']
        )
    ])