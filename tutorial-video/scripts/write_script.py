#!/usr/bin/env python3
"""Refresh the readable narration after editing storyboard.json."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
story = json.loads((root / "storyboard.json").read_text())
header = "DATA PUMP — A PRACTICAL INTRODUCTION\nNarration and visible scene notes. Edit storyboard.json first, then refresh this file.\n\n"
scenes = "".join(
    scene["title"] + "\n[" + scene["label"] + "]\n"
    + "\n\n".join(scene["sentences"]) + "\n\n"
    for scene in story["scenes"]
)
(root / "script.txt").write_text(header + scenes)
