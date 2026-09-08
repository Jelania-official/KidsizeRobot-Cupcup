#!/usr/bin/env python3
"""Replay camera frames through official YOEO weights, without ROS or simulator truth.
Run: PYTHONPATH=.vision-deps python3 tests/evaluate_yoeo.py
Preprocessing follows bit-bots/YOEO yoeo/utils/transforms.py:
RGB, centered zero square padding, nearest resize, float32 /255.
"""
import argparse
import json
import time
from pathlib import Path
import cv2
import numpy as np
import onnxruntime as ort

ROOT = Path(__file__).resolve().parents[1]


def prepare(image):
    h, w = image.shape[:2]
    side = max(h, w)
    top, left = (side-h)//2, (side-w)//2
    square = cv2.copyMakeBorder(image, top, side-h-top, left, side-w-left,
                               cv2.BORDER_CONSTANT, value=0)
    rgb = cv2.cvtColor(cv2.resize(square, (416, 416), interpolation=cv2.INTER_NEAREST), cv2.COLOR_BGR2RGB)
    return np.ascontiguousarray(rgb.transpose(2, 0, 1)[None], dtype=np.float32)/255


def balls(output, shape, threshold=.3):
    h, w = shape[:2]
    side = max(h, w)
    rows = output[0]
    scores = rows[:, 4] * rows[:, 5]
    rows, scores = rows[(scores >= threshold) & (rows[:, 5] >= rows[:, 6])], scores[(scores >= threshold) & (rows[:, 5] >= rows[:, 6])]
    boxes = []
    for x, y, bw, bh, *_ in rows:
        boxes.append([(float(x)-float(bw)/2)*side/416-(side-w)//2,
                      (float(y)-float(bh)/2)*side/416-(side-h)//2,
                      float(bw)*side/416, float(bh)*side/416])
    keep = np.asarray(cv2.dnn.NMSBoxes(boxes, scores.tolist(), threshold, .4)).reshape(-1)
    return [dict(box=boxes[int(i)], score=float(scores[int(i)])) for i in keep]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--images', type=Path, default=ROOT/'tests/fixtures')
    parser.add_argument('--model', type=Path, default=ROOT/'models/bitbots-2026/yoeo.onnx')
    parser.add_argument('--output', type=Path, default=ROOT/'reports/yoeo-fixtures')
    parser.add_argument('--pattern', default='*')
    args = parser.parse_args()
    opts = ort.SessionOptions()
    opts.intra_op_num_threads = 2
    session = ort.InferenceSession(str(args.model), opts, providers=['CPUExecutionProvider'])
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    for path in sorted(args.images.glob(args.pattern)):
        if path.suffix.lower() not in ('.png', '.jpg', '.jpeg'): continue
        image = cv2.imread(str(path))
        if image is None: raise RuntimeError(f'Cannot read {path}')
        tensor = prepare(image)
        started = time.perf_counter()
        detections = session.run(['Detections'], {session.get_inputs()[0].name: tensor})[0]
        elapsed = (time.perf_counter()-started)*1000
        found = balls(detections, image.shape)
        results.append(dict(image=str(path), inference_ms=elapsed, balls=found))
        for item in found:
            x, y, w, h = map(round, item['box'])
            cv2.rectangle(image, (x, y), (x+w, y+h), (0, 255, 0), 2)
            cv2.putText(image, f"ball {item['score']:.2f}", (max(0,x), max(15,y-5)), cv2.FONT_HERSHEY_SIMPLEX, .5, (0,255,0), 1)
        cv2.imwrite(str(args.output/path.name), image)
        print(path.name, round(elapsed,1), found, flush=True)
    (args.output/'detections.json').write_text(json.dumps(results, indent=2)+'\n')


if __name__ == '__main__': main()
