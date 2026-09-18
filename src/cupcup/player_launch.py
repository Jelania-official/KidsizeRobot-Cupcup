from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
from os.path import join


def generate_launch_description():
    model = join(get_package_share_directory('cupcup'),
                 'models', 'bitbots-2026', 'opencv.onnx')
    return LaunchDescription([
        Node(
            package='cupcup',
            executable='cupcup',
            arguments=['cupcup_1'],
            parameters=[{'ball_model': model}],
            output='screen'
        )
    ])
