# unirobot 第一版开发与验证

策略仅修改 `src/unirobot/src/player.cpp` 标记允许修改的区域。没有修改守门员、裁判、消息定义、动作参数或运动引擎。使用 OpenCV 传统视觉，不需要 GPU 或额外模型。

## 编译

在项目根目录运行：

```bash
bash docs/build_project.sh
source install/setup.bash
```

后续只改策略时：

```bash
bash docs/build_project.sh --packages-select unirobot
```

本机 ROS 的消息生成器会截断包含“桌面”的中间文件路径。脚本将编译中间文件放在 `$HOME/.cache/cupcup-build`，安装产物仍在项目 `install/`。缓存目录清理后重新完整编译即可。不要同时运行多次编译。

## 本机启动

每个终端先进入项目根目录并执行 `source install/setup.bash`。

1. 仿真：`ros2 launch ./docs/start_2023b.launch.py`
2. 比赛控制器：`ros2 run gamectrl gamectrl`，选择 unirobot 为红方、goalkeeper 为蓝方。
3. 我方策略：`ros2 launch unirobot player_launch.py`
4. 守门员：`ros2 launch goalkeeper player_launch.py`

通过比赛控制器进行 Init / Ready / Play / Pause。观察策略日志；用 `rqt` 的 Image View 查看 `/red_1/result/image`。黄色圆圈表示球候选，红色十字表示固定踢球视角下的目标球心，文字显示当前状态及识别置信度。

新增本地启动入口使用 Webots 2023b 自带的控制器库。原 `start` 启动脚本将 `WEBOTS_HOME` 设为 ROS 驱动目录，在本机已复现 2025 库与 2023b 模拟器混用引起的 undefined symbol。新入口只是本地环境适配，不属于提交策略，不改变场景或比赛逻辑。

## 策略流程

- WAIT：未开始、暂停、结束、关键传感器超时或跌倒时停止下发行走，跌倒起身交给原运动层。
- SEARCH：不同俯仰角扫视，必要时原地转向。
- APPROACH：连续三帧确认球后接近，大偏角先转向。
- ORBIT：根据 IMU 与粗定位确定目标球门方向，转身配合横移调整到球后侧。
- ALIGN：固定头部角度，微调球到左右脚对应区域。
- SETTLE：位置连续稳定后停步，等待步态队列消退。
- KICK / VERIFY：短时下发一次踢球指令，清除指令后等待动作和观察结果。
- RECOVER：接近或调整超时后短暂后退横移，再次搜索。

摄像头输入按 RGB 处理。先对白色候选做开闭运算，再结合形状、霍夫圆、周围草地和帧间连续性筛选。白线碎块、天空及球门仍需在更多实际画面中验证。小于约 8 像素半径的远球、遮挡球、贴图边缘球是当前检测的限制。

传感器的原始图像/IMU 时间戳为零，策略在允许区域内额外订阅同一组允许话题，记录到达时间并验证图像格式；不读取 Webots 数据接口。粗定位只用于目标方向，不用于脚边精确定位。

## 已标定的踢球参数

2026-09-09 用固定球、移动机器人的 Webots 网格完成左右脚标定。对准使用固定头部
俯仰 60 度下的 YOEO 球心归一化坐标：

```bash
ros2 run unirobot unirobot unirobot_1 --ros-args \
  -p attack_yaw:=180.0 -p left_kick_x:=0.399 \
  -p right_kick_x:=0.575 -p kick_y:=0.78 -p kick_pitch:=60.0
```

| 参数 | 默认 | 含义 |
|---|---:|---|
| attack_yaw | 红方 180，蓝方 0 | 球场纵向的进攻朝向，单位度；不是初始朝向 |
| imu_yaw_sign | 1 | IMU 朝向符号 |
| imu_yaw_offset | 0 | IMU 朝向偏移，单位度 |
| left_kick_x | 0.399 | `left_kick` 的目标球心横坐标 |
| right_kick_x | 0.575 | `right_kick` 的目标球心横坐标 |
| kick_y | 0.78 | 两个动作的保守目标球心纵坐标 |
| kick_pitch | 60 | 踢球对准时固定头部俯仰角，单位度 |
| ball_min_score | 0.58 | 候选球最低评分 |

标定显示左右脚并非严格镜像，因此两个横坐标分开配置。参数在策略启动时读取，现场
修改后需重启策略节点。完整方法和数据见[踢球标定记录](./踢球标定记录.md)。

## 验证方法与边界

```bash
source install/setup.bash
python3 tests/check_strategy.py
```

脚本直接提取当前策略类并使用现有编译依赖生成临时测试程序，使用本机独立 ROS 域 91。包含真实 Webots 初始视角截图与合成图像，以及暂停、断流、跌倒、超时、重开和踢球指令清除检查。测试脚本和截图是开发资料，不需要作为参赛策略提交。

通过这些测试并不代表稳定进球；还需要在预置守门员启动的情况下完成多轮完整 8 分钟
得分测试。第一版没有球门柱/守门员空当检测，当前目标为球门中心方向。
