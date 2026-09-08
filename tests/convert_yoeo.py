#!/usr/bin/env python3
"""Export convolution heads for OpenCV 4.5, verify decoder against original ONNX.
No retraining or weight changes. Run with PYTHONPATH=.vision-deps.
"""
from pathlib import Path
import json
import hashlib
import time
import cv2
import numpy as np
import onnx
import onnxruntime as ort
from evaluate_yoeo import prepare, balls

root = Path(__file__).resolve().parents[1]
folder = root/'models/bitbots-2026'
heads = [f'/module_list.{i}/conv_{i}/Conv_output_0' for i in (29, 36)]
onnx.utils.extract_model(str(folder/'yoeo.onnx'), str(folder/'opencv.onnx'), ['InputLayer'], heads)
cv2.setNumThreads(2)
net = cv2.dnn.readNetFromONNX(str(folder/'opencv.onnx'))
opts = ort.SessionOptions(); opts.intra_op_num_threads = 2
reference = ort.InferenceSession(str(folder/'yoeo.onnx'), opts, providers=['CPUExecutionProvider'])
results = []
for path in sorted((root/'tests/fixtures').glob('*')):
    image = cv2.imread(str(path)); tensor = prepare(image)
    net.setInput(tensor); started = time.perf_counter()
    outputs = net.forward(net.getUnconnectedOutLayersNames())
    elapsed = (time.perf_counter()-started)*1000
    decoded = []
    for output in outputs:
        size = output.shape[2]
        raw = output[0].transpose(1, 2, 0)
        y, x = np.mgrid[:size, :size]
        xy = (1/(1+np.exp(-raw[..., :2])) + np.stack([x,y],axis=-1))*416/size
        wh = np.exp(raw[..., 2:4])*99.99983
        confidence = 1/(1+np.exp(-raw[..., 4:]))
        decoded.append(np.concatenate([xy,wh,confidence],axis=-1).reshape(-1,7))
    decoded = np.concatenate(decoded,axis=0)[None].astype(np.float32)
    expected = reference.run(['Detections'], {'InputLayer':tensor})[0]
    np.testing.assert_allclose(decoded, expected, rtol=2e-4, atol=.02)
    result = dict(image=path.name, max_abs_error=float(np.max(np.abs(decoded-expected))),
                  opencv_ms=elapsed, balls=balls(decoded,image.shape))
    results.append(result); print(result,flush=True)
(folder/'conversion_validation.json').write_text(json.dumps(results,indent=2)+'\n')
provenance = json.loads((folder/'provenance.json').read_text())
provenance.update(status='OpenCV convolution heads validated against original ONNX on four fixtures',
                  opencv_sha256=hashlib.sha256((folder/'opencv.onnx').read_bytes()).hexdigest(),
                  decoder_anchor=99.99983)
(folder/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
