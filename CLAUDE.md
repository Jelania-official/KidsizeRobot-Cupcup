# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

东南大学 RoboCup Kidsize 2026 比赛代码：ROS2 Humble + Webots 2023b 仿真环境下的双足仿人机器人足球赛。机器人（red_1/red_2/blue_1/blue_2）在 Webots 中仿真，比赛控制器为 Qt 图形程序（gamectrl），各队策略代码在各自的 `player.cpp` 中。

官方示例队包是 `src/unirobot`（与 template 原型内容一致，策略在 `src/unirobot/src/player.cpp`），已注册在 `teams.cfg`。

## 常用命令

工作区根目录执行（ROS2 标准工作区，无测试套件，编译约 2 分钟）：

```shell
colcon build          # 编译全部包
colcon build --packages-select unirobot   # 只编译单包
```

运行三终端流程（README 原版）：

```shell
# 终端 1：启动仿真（点球模式用 penalty_start）
ros2 launch start start_launch.py
# 终端 2：比赛控制器（读取 teams.cfg 选择红蓝队）
ros2 run gamectrl gamectrl
# 终端 3：己方/敌方策略节点
ros2 launch unirobot player_launch.py
ros2 launch template player_launch.py   # 敌方
```

查看处理后的图像：rqt 的 Plugins → Visualization → Image View，话题 `<robot>/result/image`。

新建队伍包：`cd src && ./createPkg.sh <队名>`（仅小写字母、≤20 字符、不能是 test），会自动复制 template、替换包名并把队名追加到 `teams.cfg`。注意 `teams.cfg` 的修改需要重启 gamectrl 才生效。

## 架构与数据流

```
player（策略，每队一份）──BodyTask/HeadTask──▶ motion（动作执行）──关节角──▶ controller（Webots C API）──▶ Webots 仿真
player ◀──图像/IMU/头部角度/比分/定位── controller / supervisor
```

各包职责：

- **common**：自定义 msg/srv 的唯一来源（BodyTask、HeadTask、ImuData、GameData、Location、HeadAngles、BodyAngles；服务 GetColor、GetAngles、AddAngles 等），所有包依赖它
- **libraries**：`seumath`（Eigen 数学库：角度、矩阵、变换）、`basic_parser`（配置文件解析）
- **params**：启动时把 `conf/` 下的配置（`model/robot.conf`、`action/action.conf`、`action/walk.conf`、`action/offset.conf`）注册为 ROS2 参数，motion 节点通过 `SyncParametersClient` 拉取
- **motion**：每个机器人一个节点（参数为机器人名），订阅 BodyTask/HeadTask，经 `seurobot::ActionEngine`（预置动作：`reset`/`left_kick`/`right_kick`/`ready`）或 `WalkEngine`（IK 行走，`src/motion/src/walk/`）生成关节角发布；提供 `get_angles`/`add_angles` 服务用于动作编辑
- **simulation/controller**：Webots 机器人控制器（SimRobot）与裁判 supervisor（`judge`，发布 `/sensor/game` 和定位）
- **simulation/webots**：Webots 世界与 PROTO 模型（SEURobot、RobocupSoccerField、SoccerBall 等）
- **start**：`start_launch.py` 一次性拉起 Webots、两个机器人 controller、supervisor、params 和两个 motion 节点
- **gamectrl**：Qt 比赛控制器（`src/gamectrl/`，UI 在 ctrlwindow）

## 策略开发要点（player 节点约定）

- 机器人名解析：launch 传入 `<队名>_1`，player 调用 `gamectrl/get_color` 服务得到颜色，拼成 `red_1`/`blue_2` 等；`friendName` 为同队队友名（3 - myId）
- 订阅话题（封装类在 `src/<team>/src/topics.hpp`）：`<robot>/sensor/image`（OpenCV Mat RGB3）、`<robot>/sensor/imu`（看 yaw）、`<robot>/sensor/joint/head`（yaw/pitch 度）、`/sensor/game`（state 有 INIT/READY/PLAY/PAUSE/END，禁止改比分）、`/sensor/<robot>_location`（精度 1m）
- 发布话题：`<robot>/task/body`（BodyTask：TASK_WALK=1 时用 step/lateral/turn/count，count 一般取 2；TASK_ACT=2 时 actname 取 `left_kick`/`right_kick`/`ready`）、`<robot>/task/head`（yaw/pitch 度）、`<robot>/result/image`（调试图像）
- 主循环是 `rclcpp::spin_some` + `loop_rate.sleep()` 手动调度模式，不是回调驱动；`gameData.state == STATE_INIT` 每次开球触发
- 详细接口说明见 `src/template/src/README.md`（中文）

## 已知坑（README 常见问题）

- Webots 2023b 与 libController 版本不匹配会导致黑屏/初始位置错误，需替换 `/opt/ros/humble` 下旧版库文件（ros2023.zip）或补 libfastrtps（ros2025.zip），详见 README 第 3 节
- Webots 打开报 texture 错误时，把 `install/webots/share/webots/models/worlds` 里的 texture 文件夹移到 proto 文件夹
