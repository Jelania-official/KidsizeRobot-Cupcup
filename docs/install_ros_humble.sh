#!/usr/bin/env bash
set -euo pipefail

# Run as your normal user: bash docs/install_ros_humble.sh
source /etc/os-release
if [[ "$ID" != ubuntu || "$VERSION_ID" != 22.04 ]]; then
  echo '此脚本仅适用于 Ubuntu 22.04。' >&2
  exit 1
fi
if [[ $EUID == 0 ]]; then
  echo '请用普通用户运行此脚本，脚本会自行调用 sudo。' >&2
  exit 1
fi

# Use direct connections for this script only; leave the user's proxy settings intact.
unset http_proxy https_proxy all_proxy HTTP_PROXY HTTPS_PROXY ALL_PROXY
for required in curl gpg sudo add-apt-repository; do
  command -v "$required" >/dev/null || { echo "缺少命令：$required" >&2; exit 1; }
done
sudo -v
key_file=$(mktemp)
trap 'rm -f "$key_file"' EXIT
curl --noproxy '*' --fail --location --retry 3 --connect-timeout 15 --max-time 120 https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -o "$key_file"
gpg --show-keys "$key_file"
sudo install -m 644 "$key_file" /usr/share/keyrings/ros-archive-keyring.gpg
if [[ -f /etc/apt/sources.list.d/ros2.list && ! -f /etc/apt/sources.list.d/ros2.list.cupcup-backup ]]; then
  sudo cp -p /etc/apt/sources.list.d/ros2.list /etc/apt/sources.list.d/ros2.list.cupcup-backup
fi
printf 'deb [arch=%s signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] https://mirrors.tuna.tsinghua.edu.cn/ros2/ubuntu jammy main\n' "$(dpkg --print-architecture)" | sudo tee /etc/apt/sources.list.d/ros2.list >/dev/null
sudo add-apt-repository -y --no-update universe
apt_options=(-o Acquire::http::Proxy=DIRECT -o Acquire::https::Proxy=DIRECT)
sudo apt-get "${apt_options[@]}" -o APT::Update::Error-Mode=any update
sudo apt-get "${apt_options[@]}" install -y ros-humble-desktop ros-humble-webots-ros2 \
  build-essential cmake git python3-colcon-common-extensions \
  python3-pip python3-rosdep python3-vcstool python3-argcomplete \
  libopencv-dev libeigen3-dev qtbase5-dev

if ! grep -Fq '# cupcup ROS Humble environment' "$HOME/.bashrc"; then
  cat >> "$HOME/.bashrc" <<'ENV'

# cupcup ROS Humble environment
source /opt/ros/humble/setup.bash
export WEBOTS_HOME=/usr/local/webots
export LD_LIBRARY_PATH="$WEBOTS_HOME/lib/controller${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
ENV
fi

set +u
source /opt/ros/humble/setup.bash
set -u
ros2 --help >/dev/null
ros2 pkg prefix webots_ros2_driver
if [[ ! -f /etc/ros/rosdep/sources.list.d/20-default.list ]]; then
  sudo rosdep init || echo 'rosdep 初始化失败，可稍后重试；ROS 软件包已安装。' >&2
fi
rosdep update --rosdistro humble || echo 'rosdep 索引更新失败，可稍后重试；ROS 软件包已安装。' >&2
echo 'ROS 2 Humble 安装完成。请新开终端，进入项目目录运行 colcon build。'
echo 'Webots 联调尚需验证；如有控制器版本不匹配，请保留报错信息。'
