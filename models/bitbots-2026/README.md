# Bit-Bots YOEO-X 2026

来源：https://data.bit-bots.de/models/2026_07_01_yoeo_x_wm/

原作者代码及许可证：https://github.com/bit-bots/YOEO （GPL-3.0）。
模型服务器没有单独的权重许可证文件；保留来源，发布前核对权重授权。

- `yoeo.onnx`：官方原始模型，未训练、未修改权重。
- `model_config.yaml`：官方类别配置，检测 ball/robot，分割 background/lines。
- `opencv.onnx`：从原模型提取两个卷积检测输出，移除分割和 OpenCV 4.5 不兼容的五维解码运算。解码在 player.cpp 内执行。
- `provenance.json`：下载来源及原始、转换文件 SHA-256。
- `conversion_validation.json`：四张真实仿真画面的 ONNX Runtime 与 OpenCV 解码一致性检查。

复现（开发评估依赖只安装到项目目录）：

```bash
python3 -m pip install --target .vision-deps 'numpy<2' onnx onnxruntime
python3 tests/download_yoeo.py
PYTHONPATH=.vision-deps python3 tests/convert_yoeo.py
PYTHONPATH=.vision-deps python3 tests/evaluate_yoeo.py
bash docs/build_project.sh --packages-select unirobot
source install/setup.bash
python3 tests/check_strategy.py
python3 tests/run_match.py --smoke 180 --domain 97
```

运行比赛程序只使用现有 C++ OpenCV，不依赖 Python、PyTorch 或 GPU。
从项目根目录启动；其他目录启动时通过 ROS 参数 `ball_model` 指定
`opencv.onnx` 的绝对路径。缺少文件会报错，不静默切回旧视觉。
显式设置 `ball_model` 为空字符串可对照传统视觉。

初测：四张关键画面均检测到球，包括近距离白线误检样例。转换后
解码与原模型最大绝对误差小于 0.0002，双线程 CPU 推理约 60 ms。
这不是完整数据集准确率，也不是进球验证。

模型二进制和完整比赛录像保留在本地、不纳入 Git；新检出使用以上下载与转换命令复现。
