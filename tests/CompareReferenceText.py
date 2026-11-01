"""Compare Qt diagnostic glyphs with the user's original Magpie About screenshot.

Usage: python CompareReferenceText.py reference.png build/reference-probe
The reference image is read only and is not copied into the repository.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def ink(image, background, foreground):
    pixels = np.asarray(image.convert("RGB"), dtype=np.float32)
    alpha = np.clip(((pixels - background) / (foreground - background)).mean(2), 0, 1)
    y, x = np.where(alpha > 0.08)
    return alpha[y.min():y.max() + 1, x.min():x.max() + 1]


def difference(reference, candidate):
    height = max(reference.shape[0], candidate.shape[0]) + 10
    width = max(reference.shape[1], candidate.shape[1]) + 10
    a = np.zeros((height, width), dtype=np.float32)
    a[5:5 + reference.shape[0], 5:5 + reference.shape[1]] = reference
    denominator = reference.sum() + candidate.sum()
    best = float("inf")
    for dy in range(-3, 4):
        for dx in range(-3, 4):
            b = np.zeros_like(a)
            b[5 + dy:5 + dy + candidate.shape[0], 5 + dx:5 + dx + candidate.shape[1]] = candidate
            best = min(best, float(np.abs(a - b).sum() / denominator))
    return best


reference_path, candidates_path = Path(sys.argv[1]), Path(sys.argv[2])
reference = Image.open(reference_path)
regions = [(78, 408, 152, 430), (78, 435, 300, 456), (558, 113, 601, 142)]
backgrounds = [np.array(value, dtype=np.float32) for value in ([35, 35, 39], [35, 35, 39], [45, 45, 51])]
foregrounds = [np.array(value, dtype=np.float32) for value in ([237, 237, 241], [185, 185, 193], [237, 237, 241])]
targets = [ink(reference.crop(region), backgrounds[i], foregrounds[i]) for i, region in enumerate(regions)]
results = [[], [], []]
for candidate in json.loads((candidates_path / "candidates.json").read_text(encoding="utf-8")):
    role = candidate["role"]
    image = Image.open(candidates_path / candidate["file"])
    rendered = ink(image, backgrounds[role], foregrounds[role])
    if abs(rendered.shape[1] - targets[role].shape[1]) > 10:
        continue
    candidate["difference"] = difference(targets[role], rendered)
    candidate["ink_area"] = round(float(rendered.sum()), 2)
    results[role].append(candidate)
for role, matches in enumerate(results):
    matches.sort(key=lambda item: item["difference"])
    print(f"Role {role}: reference ink {targets[role].sum():.2f}, bounds {targets[role].shape}")
    for match in matches[:6]:
        print(json.dumps(match, ensure_ascii=False))
(candidates_path / "comparison.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
