"""Local development launcher using the controller libraries shipped with Webots R2023b.
Run: ros2 launch ./docs/start_2023b.launch.py
Competition controller, world and motion code remain unchanged.
"""
import os
from pathlib import Path
from launch import LaunchDescription
from launch.actions import ExecuteProcess, RegisterEventHandler, EmitEvent
from launch.event_handlers import OnProcessExit
from launch.events import Shutdown
from launch_ros.actions import Node
from ament_index_python.packages import get_package_prefix, get_package_share_directory
from webots_ros2_driver.webots_launcher import WebotsLauncher


def generate_launch_description():
    webots_home = '/usr/local/webots'
    if not Path(webots_home, 'lib/controller/libCppController.so').is_file():
        raise RuntimeError('Webots R2023b is required at /usr/local/webots')
    os.environ['WEBOTS_HOME'] = webots_home
    native_lib = webots_home + '/lib/controller'
    library_path = native_lib + ':' + os.environ.get('LD_LIBRARY_PATH', '')
    webots = WebotsLauncher(world=os.path.join(
        get_package_share_directory('webots'), 'models/worlds/sim-robot.wbt'))
    actions = [webots, Node(package='params', executable='params', output='screen')]
    for robot, executable in [('red_1', 'controller'), ('blue_1', 'controller'), ('judge', 'supervisor')]:
        actions.append(ExecuteProcess(
            cmd=[os.path.join(get_package_prefix('controller'), 'lib/controller', executable)],
            name=robot + '_native_controller', output='screen',
            additional_env={
                'WEBOTS_HOME': webots_home,
                'WEBOTS_CONTROLLER_URL': 'ipc://1234/' + robot,
                'LD_LIBRARY_PATH': library_path,
            }))
        if robot != 'judge':
            actions.append(Node(package='motion', executable='motion', arguments=[robot], output='screen'))
    actions.append(RegisterEventHandler(OnProcessExit(
        target_action=webots, on_exit=[EmitEvent(event=Shutdown())])))
    return LaunchDescription(actions)
