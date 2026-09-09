#!/usr/bin/env python3
"""Run an isolated match using unchanged referee, motion and goalkeeper code.
Requires built project and match_operator. Saves logs/images/result in reports/.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time
import shutil
import rclpy
from sensor_msgs.msg import Image
import cv2
import numpy as np

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--smoke',type=int,default=0,help='Short diagnostic run; not a competition result')
parser.add_argument('--domain',type=int,default=94)
args=parser.parse_args()
report=root/'reports'/time.strftime('%Y%m%d-%H%M%S')
report.mkdir(parents=True)
env={**os.environ,'ROS_DOMAIN_ID':str(args.domain),'ROS_LOCALHOST_ONLY':'1',
     'RMW_FASTRTPS_PUBLICATION_MODE':'ASYNCHRONOUS',
     'CUPCUP_WEBOTS_PORT':str(12000 + args.domain),
     'FASTRTPS_DEFAULT_PROFILES_FILE':str(root/'tests/fastdds_udp.xml'),
     'CUPCUP_SMOKE_SECONDS':str(args.smoke)}
cache=Path(os.environ.get('CUPCUP_BUILD_DIR',str(Path.home()/'.cache/cupcup-build')))
operator=cache/'match_operator/match_operator'
if not operator.is_file(): raise SystemExit('Build tests/match_operator first (see development notes).')
metadata={'strategy_sha256':hashlib.sha256((root/'src/unirobot/src/player.cpp').read_bytes()).hexdigest(),
          'executable_sha256':hashlib.sha256((root/'install/unirobot/lib/unirobot/unirobot').read_bytes()).hexdigest(),
          'smoke_seconds':args.smoke,'ros_domain':args.domain,'goalkeeper':'unchanged src/goalkeeper',
          'referee':'unchanged src/simulation/controller/src/supervisor.cpp',
          'game_control':'unchanged CtrlWindow, button slots automated by test operator'}
model=root/'models/bitbots-2026/opencv.onnx'
metadata['model_sha256']=hashlib.sha256(model.read_bytes()).hexdigest() if model.exists() else None
metadata['publication_mode']=env.get('RMW_FASTRTPS_PUBLICATION_MODE','default')
(report/'metadata.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2))
shutil.copyfile(root/'src/unirobot/src/player.cpp',report/'player.cpp')
processes=[];files=[]
os.environ.update(env)
rclpy.init()
recorder=rclpy.create_node('match_image_recorder')
last_capture={}
def capture(m, kind):
    t=time.monotonic()
    if not m.width or t-last_capture.get(kind,0)<2: return
    last_capture[kind]=t
    image=np.frombuffer(m.data,dtype=np.uint8).reshape(m.height,m.step)[:,:m.width*3].reshape(m.height,m.width,3)
    folder=report/'images';folder.mkdir(exist_ok=True)
    cv2.imwrite(str(folder/(kind+'-'+str(time.time_ns())+'.jpg')),cv2.cvtColor(image,cv2.COLOR_RGB2BGR))
raw_sub=recorder.create_subscription(Image,'/red_1/sensor/image',lambda m:capture(m,'raw'),1)
debug_sub=recorder.create_subscription(Image,'/red_1/result/image',lambda m:capture(m,'debug'),1)
def start(command,name,workdir=root,extra=None):
    f=(report/(name+'.log')).open('w');files.append(f)
    p=subprocess.Popen(command,cwd=workdir,env={**env,**(extra or {})},stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
    processes.append((name,p));return p
try:
    start(['ros2','launch',str(root/'docs/start_2023b.launch.py')],'simulation')
    match=start([str(operator)],'operator',report,{'QT_QPA_PLATFORM':'offscreen'})
    start(['ros2','launch','unirobot','player_launch.py'],'player')
    start(['ros2','launch','goalkeeper','player_launch.py'],'goalkeeper')
    print('REPORT',report,flush=True)
    deadline=time.monotonic()+1100
    while match.poll() is None and time.monotonic()<deadline:
        for name,p in processes:
            if p is not match and p.poll() is not None:
                raise RuntimeError(name+' exited before the match completed; inspect logs')
        rclpy.spin_once(recorder,timeout_sec=0.1)
    if match.poll() is None: raise TimeoutError('Match exceeded 1100 wall seconds')
    print((report/'result.json').read_text() if (report/'result.json').exists() else 'No result generated',flush=True)
finally:
    for _,p in processes:
        if p.poll() is None: os.killpg(p.pid,signal.SIGINT)
    for _,p in processes:
        try:p.wait(timeout=8)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid,signal.SIGKILL);p.wait()
    for f in files:f.close()
    recorder.destroy_node();rclpy.shutdown()
