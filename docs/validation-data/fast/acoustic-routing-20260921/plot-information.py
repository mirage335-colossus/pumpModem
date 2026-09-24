#!/usr/bin/env python3
"""Render measured per-cycle decoder information with ReportLab's chart tools."""
import csv
from pathlib import Path
from reportlab.graphics.shapes import Drawing, String, Line
from reportlab.graphics.charts.lineplots import LinePlot
from reportlab.graphics import renderSVG
from reportlab.lib.colors import HexColor

root = Path(__file__).resolve().parent
series = [
    ("q16-mono", "16-QAM / right only / passed", "#187850"),
    ("q16-stereo", "16-QAM / both speakers / passed", "#6b9c37"),
    ("q64-stereo", "64-QAM / both speakers / passed", "#2169a8"),
    ("q64-mono", "64-QAM / right only / failed", "#bf4038"),
]
chart = Drawing(700, 425)
plot = LinePlot()
plot.x, plot.y, plot.width, plot.height = 62, 155, 607, 210
plot.data = []
for suffix, label, color in series:
    with (root / f"acoustic-routing-95-{suffix}-known-cycles.csv").open() as source:
        rows = list(csv.DictReader(source))
    plot.data.append([(int(row["index"]), float(row["gmi_per_bit"])) for row in rows])
plot.data.append([(0, .75), (3, .75)])
plot.xValueAxis.valueMin, plot.xValueAxis.valueMax = 0, 3
plot.xValueAxis.valueSteps = [0, 1, 2, 3]
plot.yValueAxis.valueMin, plot.yValueAxis.valueMax = .4, 1.
plot.yValueAxis.valueSteps = [.4, .5, .6, .7, .8, .9, 1.]
plot.yValueAxis.visibleGrid = True
plot.yValueAxis.gridStrokeColor = HexColor("#e5e7eb")
for i, (_, _, color) in enumerate(series):
    plot.lines[i].strokeColor = HexColor(color)
    plot.lines[i].strokeWidth = 2.5
plot.lines[4].strokeColor = HexColor("#444444")
plot.lines[4].strokeDashArray = [5, 4]
chart.add(plot)
chart.add(String(62, 400, "Stationary speaker/microphone: 100 KB live trials", fontName="Helvetica-Bold", fontSize=17))
chart.add(String(62, 378, "Empirical information per coded bit (receiver LLR metric)", fontSize=11))
chart.add(String(245, 125, "Fixed coding cycle (0 = bootstrap)", fontSize=11))
for i, (_, label, color) in enumerate(series):
    x, y = 65 + (i % 2) * 310, 93 - (i // 2) * 24
    chart.add(Line(x, y, x + 23, y, strokeColor=HexColor(color), strokeWidth=2.5))
    chart.add(String(x + 31, y - 4, label, fontSize=11))
chart.add(Line(65, 43, 88, 43, strokeColor=HexColor("#444444"), strokeDashArray=[5, 4]))
chart.add(String(96, 39, "LDPC rate 3/4; finite codes need additional margin", fontSize=11))
chart.add(String(65, 15, "Sequential trials at unchanged mixer settings; these are not reliability probabilities.", fontSize=10))
renderSVG.drawToFile(chart, str(root / "coded-bit-information.svg"))
