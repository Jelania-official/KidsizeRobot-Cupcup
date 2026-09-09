#!/usr/bin/env python3
"""Teleport-based kick calibration. Webots truth is used only by the test supervisor."""
import argparse, csv, json, math, os, signal, subprocess, time
from pathlib import Path
import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]

def detect_ball(net, image):
    rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    h, w = rgb.shape[:2]; side = max(h, w)
    top, left = (side-h)//2, (side-w)//2
    square = cv2.copyMakeBorder(rgb, top, side-h-top, left, side-w-left, cv2.BORDER_CONSTANT)
    resized = cv2.resize(square, (416,416), interpolation=cv2.INTER_NEAREST)
    net.setInput(cv2.dnn.blobFromImage(resized, 1/255.0))
    best = None
    for output in net.forward(net.getUnconnectedOutLayersNames()):
        raw = output[0]
        height, width = raw.shape[1:]
        for y in range(height):
            for x in range(width):
                v = raw[:,y,x]
                sig = 1/(1+np.exp(-v[[0,1,4,5,6]]))
                score = float(sig[2]*sig[3])
                if score < .30 or v[5] < v[6]: continue
                cx = (sig[0]+x)*side/width-left
                cy = (sig[1]+y)*side/height-top
                bw = math.exp(float(v[2]))*99.99983*side/416
                bh = math.exp(float(v[3]))*99.99983*side/416
                if 0 <= cx < w and 0 <= cy < h and (best is None or score > best['score']):
                    best = {'image_x':cx/w, 'image_y':cy/h,
                            'image_radius':(bw+bh)/(4*w), 'score':score}
    return best

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--forward', default='0.16,0.20,0.24,0.28')
    parser.add_argument('--lateral', default='-0.08,-0.04,0,0.04,0.08')
    parser.add_argument('--feet', default='left,right')
    parser.add_argument('--repeats', type=int, default=1)
    parser.add_argument('--domain', type=int, default=131)
    parser.add_argument('--success-distance', type=float, default=.25)
    parser.add_argument('--max-direction-error', type=float, default=20)
    parser.add_argument('--max-pre-drift', type=float, default=.03)
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('--startup-timeout', type=int, default=45)
    parser.add_argument('--retries', type=int, default=3)
    args = parser.parse_args()
    cache = Path(os.environ.get('CUPCUP_BUILD_DIR', str(Path.home()/'.cache/cupcup-build')))
    build = cache/'kick_calibration'; binary = build/'calibration_supervisor'
    if not (build/'CMakeCache.txt').is_file():
        subprocess.run(['cmake','-S',str(ROOT/'tests/kick_calibration'),'-B',str(build)],check=True)
    subprocess.run(['cmake','--build',str(build),'-j2'],check=True)
    report = ROOT/'reports'/('kick-calibration-'+time.strftime('%Y%m%d-%H%M%S'))
    report.mkdir(parents=True)
    base_env = {**os.environ, 'ROS_LOCALHOST_ONLY':'1',
           # Keep ROS launch logs with the report. Some competition/dev
           # environments mount the user's home directory read-only.
           'ROS_LOG_DIR':str(report/'ros-log'),
           'RMW_FASTRTPS_PUBLICATION_MODE':'ASYNCHRONOUS',
           'FASTRTPS_DEFAULT_PROFILES_FILE':str(ROOT/'tests/fastdds_udp.xml'),
           'CUPCUP_SUPERVISOR_BIN':str(binary), 'CUPCUP_CAL_OUTPUT':str(report),
           'CUPCUP_CAL_FORWARD':args.forward, 'CUPCUP_CAL_LATERAL':args.lateral,
           'CUPCUP_CAL_FEET':args.feet, 'CUPCUP_CAL_REPEATS':str(args.repeats)}
    truth = report/'trials_truth.csv'
    completed=False
    for attempt in range(args.retries):
        if truth.exists(): truth.unlink()
        for image in (report/'images').glob('*') if (report/'images').exists() else []: image.unlink()
        domain=args.domain+attempt
        env={**base_env,'ROS_DOMAIN_ID':str(domain),'CUPCUP_WEBOTS_PORT':str(13000+domain)}
        log_path=report/f'simulation-attempt-{attempt+1}.log'
        log=log_path.open('w')
        process=subprocess.Popen(['ros2','launch',str(ROOT/'docs/start_2023b.launch.py')],
            cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        started=time.monotonic(); produced=False; reason='launch exited'
        try:
            while process.poll() is None:
                produced=truth.exists() and sum(1 for _ in truth.open())>1
                elapsed=time.monotonic()-started
                if not produced and elapsed>args.startup_timeout:
                    reason='no completed trial before startup timeout';break
                if elapsed>args.timeout:
                    reason='calibration timeout';break
                time.sleep(1)
            code=process.poll()
            produced=truth.exists() and sum(1 for _ in truth.open())>1
            completed=code==0 and produced
        finally:
            if process.poll() is None: os.killpg(process.pid,signal.SIGINT)
            try: process.wait(timeout=8)
            except subprocess.TimeoutExpired: os.killpg(process.pid,signal.SIGKILL);process.wait()
            log.close()
        if completed: break
        print(f'calibration attempt {attempt+1} failed ({reason}); retrying',flush=True)
    if not completed:
        raise RuntimeError(f'calibration failed after {args.retries} attempts; inspect {report}')
    net = cv2.dnn.readNetFromONNX(str(ROOT/'models/bitbots-2026/opencv.onnx'))
    rows=[]
    with truth.open() as handle:
        for row in csv.DictReader(handle):
            image = cv2.imread(str(report/'images'/row['image']))
            found = detect_ball(net,image) if image is not None else None
            row.update(found or {'image_x':'','image_y':'','image_radius':'','score':''})
            dx=float(row['forward_displacement_m']); dz=float(row['lateral_displacement_m'])
            direction=abs(math.degrees(math.atan2(dz,dx))) if math.hypot(dx,dz)>.005 else 180
            row['success'] = int(float(row['max_displacement_m']) >= args.success_distance
                and direction <= args.max_direction_error and int(row['fall']) == 0
                and float(row['pre_ball_drift_m']) <= args.max_pre_drift)
            rows.append(row)
    fields=list(rows[0])+[k for k in ('image_x','image_y','image_radius','score','success') if k not in rows[0]]
    with (report/'trials.csv').open('w',newline='') as handle:
        writer=csv.DictWriter(handle,fieldnames=fields);writer.writeheader();writer.writerows(rows)
    groups={}
    for row in rows:
        key=(row['foot'],float(row['forward_m']),float(row['lateral_m']))
        groups.setdefault(key,[]).append(row)
    ranking=[]
    for (foot,forward,lateral),items in groups.items():
        distances=[float(i['max_displacement_m']) for i in items]
        successes=sum(int(i['success']) for i in items)
        angles=[math.degrees(math.atan2(float(i['lateral_displacement_m']),
                                        float(i['forward_displacement_m']))) for i in items
                if float(i['max_displacement_m'])>.005]
        images=[i for i in items if i['image_x']!='']
        ranking.append(dict(foot=foot,forward_m=forward,lateral_m=lateral,
            actual_forward_m=float(np.median([float(i['actual_forward_m']) for i in items])),
            actual_lateral_m=float(np.median([float(i['actual_lateral_m']) for i in items])),
            pre_ball_drift_m=float(np.median([float(i['pre_ball_drift_m']) for i in items])),
            trials=len(items),success_rate=successes/len(items),
            median_displacement_m=float(np.median(distances)),
            median_direction_deg=float(np.median(angles)) if angles else None,
            image_x=float(np.median([i['image_x'] for i in images])) if images else None,
            image_y=float(np.median([i['image_y'] for i in images])) if images else None,
            image_radius=float(np.median([i['image_radius'] for i in images])) if images else None))
    ranking.sort(key=lambda x:(x['success_rate'],x['median_displacement_m']),reverse=True)
    summary={'success_distance_m':args.success_distance,
             'max_direction_error_deg':args.max_direction_error,
             'max_pre_ball_drift_m':args.max_pre_drift,'trial_count':len(rows),'ranking':ranking,
             'recommended':{foot:next((r for r in ranking if r['foot']==foot),None)
                            for foot in ('left','right')}}
    (report/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
    print('REPORT',report)
    for foot,item in summary['recommended'].items(): print(foot,item)

if __name__ == '__main__': main()
