#!/usr/bin/env python3
"""Stationary calibration using only the permitted head task and sensor topics."""
import os,time,signal,subprocess
from pathlib import Path
import rclpy,cv2,numpy as np
from common.msg import HeadTask,HeadAngles,BodyTask,ImuData
from sensor_msgs.msg import Image
root=Path(__file__).resolve().parents[1];out=root/'reports'/'head-calibration';out.mkdir(parents=True,exist_ok=True)
os.environ.update(ROS_DOMAIN_ID='95',ROS_LOCALHOST_ONLY='1',FASTRTPS_DEFAULT_PROFILES_FILE=str(root/'tests/fastdds_udp.xml'),CUPCUP_SMOKE_SECONDS='40')
processes=[];logs=[]
for args,name,cwd in [(['ros2','launch',str(root/'docs/start_2023b.launch.py')],'simulation',root),([str(Path.home()/'.cache/cupcup-build/match_operator/match_operator')],'operator',out)]:
 f=(out/(name+'.log')).open('w');logs.append(f);processes.append(subprocess.Popen(args,cwd=cwd,env={**os.environ,**({'QT_QPA_PLATFORM':'offscreen'} if name=='operator' else {})},stdout=f,stderr=subprocess.STDOUT,start_new_session=True))
rclpy.init();n=rclpy.create_node('head_calibration');hp=n.create_publisher(HeadTask,'/red_1/task/head',2);bp=n.create_publisher(BodyTask,'/red_1/task/body',2)
latest={}
def image(m):
 if m.width:latest['image']=np.frombuffer(m.data,dtype=np.uint8).reshape(m.height,m.step)[:,:m.width*3].reshape(m.height,m.width,3).copy()
subs=[n.create_subscription(Image,'/red_1/sensor/image',image,2),n.create_subscription(HeadAngles,'/red_1/sensor/joint/head',lambda m:latest.__setitem__('head',(m.yaw,m.pitch)),2),n.create_subscription(ImuData,'/red_1/sensor/imu',lambda m:latest.__setitem__('imu',(m.yaw,m.pitch,m.roll)),2)]
try:
 start=time.monotonic();saved=set();poses=[(0,20),(30,20),(-30,20),(0,45),(0,60)]
 while time.monotonic()-start<37:
  elapsed=time.monotonic()-start;idx=max(0,min(len(poses)-1,int((elapsed-10)//5)));yaw,pitch=poses[idx]
  hp.publish(HeadTask(yaw=float(yaw),pitch=float(pitch)));bp.publish(BodyTask(type=BodyTask.TASK_WALK,count=0))
  rclpy.spin_once(n,timeout_sec=.01);time.sleep(.08)
  if elapsed>14+5*idx and idx not in saved and 'image' in latest:
   saved.add(idx);cv2.imwrite(str(out/f'pose-{idx}.png'),cv2.cvtColor(latest['image'],cv2.COLOR_RGB2BGR));print(idx,poses[idx],latest.get('head'),latest.get('imu'),flush=True)
finally:
 for p in processes:
  if p.poll() is None:os.killpg(p.pid,signal.SIGINT)
 for p in processes:
  try:p.wait(timeout=8)
  except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait()
 for f in logs:f.close()
 n.destroy_node();rclpy.shutdown()
