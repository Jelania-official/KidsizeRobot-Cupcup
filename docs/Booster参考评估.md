# Booster K1 代码参考评估

查阅仓库：https://github.com/Jelania-official/booster-k1-CLASSresearch

固定版本：`1802313b7a9c17bd99433fe7cece4e981c6954c8`，2026-09-04，提交说明为
“导入 Booster 官方 robocup_demo 完整基线”。本文是代码核查，不代表已在本机器人上复现。
许可证：Apache-2.0。当前尚未将其源码直接复制进比赛程序。

## 最优先适配的部分

- `src/brain/src/brain_tree.cpp:142`，CamTrackBall：球在画面中心附近时不转头，
  横纵容差各为画面尺寸的 30%；超出时按视场角换算偏差并除以 3.5 平滑。
  丢失时缓慢朝最后已知位置转头。值得参考的是死区和分阶段观察；
  其头部反馈、单位及参数须适配本平台，不能直接照搬。
- 同文件 `:193`，CamFindBall：高低两个俯角、左中右六个驻留位置，
  每秒切换一次。比当前持续正弦扫描更适合验证稳定观察窗口。
- 同文件 `:266`，Chase：沿踢球方向选择球后目标点，必要时绕安全圆；
  直达/绕行选择带滞回。其场地坐标和可靠里程计假设不适用于本项目
  约米级粗定位，精细接近需要相对球位置。
- 同文件 `:645`，Adjust：径向接近与切向绕球合成速度，近目标降低切向速度，
  有不转向死区和先转再走条件。可参考其几何结构，保留本项目步长/步频接口。
- `src/brain/behavior_trees/subtrees/subtree_striker_play.xml`：行为树组织
  Chase、Adjust、Kick、RLVisionKick，可作为职责划分参考。

## 不能直接等同的部分

- 普通 `Kick::onStart()`（同文件 `:980`）调用 `crabWalk` 朝球运动；
  不是本项目 `left_kick/right_kick` 关键帧动作。
- `RLVisionKick` 经 `robot_client.cpp` 调用 Booster K1 的固件视觉踢球接口。
  README 要求 K1 固件至少 1.5.2；不能声称仓库提供了可在 SEURobot 上直接运行
  的完整低层视觉踢球权重。
- `Chase` 虽计算 smoothVx/smoothVy/smoothVtheta，实际调用却传入原始 vx/vy/vtheta；
  不能仅凭变量名认为输出已经平滑。
- 仓库含 `src/vision/model/sim_data_det_0126.onnx` 等仿真模型，值得用本地录像
  与 YOEO 比较；目前尚未验证其类别映射、输入和识别效果。

## 后续顺序

先完成本机器人官方动作的脚位/距离标定，再适配带死区的头部控制和六点驻留搜索，
最后适配球后接近与绕球；每一步用固定画面和独立比赛报告验证。
不把“参考了架构”写成“已完成复现”，也不把源平台表现当成本平台效果。
