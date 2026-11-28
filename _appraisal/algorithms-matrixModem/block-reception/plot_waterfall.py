"""Render the native --orientation-waterfall CSV on stdin. Requires Pillow."""

import csv
import sys
from PIL import Image, ImageDraw, ImageFont


def main():
    reader = csv.DictReader(line for line in sys.stdin if not line.startswith("#"))
    rows, bins, edge = 64, 357, 178
    panels = [Image.new("RGB", (bins, rows)) for _ in range(2)]
    # Each scale stop is (dB relative to row median, sRGB). A common fixed
    # scale on both panels retains the exact left/right reflection.
    stops = [(-6, (10, 18, 34)), (3, (24, 44, 71)), (9, (40, 84, 112)),
             (13, (52, 158, 160)), (16, (244, 201, 97)), (19, (255, 250, 220))]

    def color(value):
        for (low, a), (high, b) in zip(stops, stops[1:]):
            if value <= high:
                w = max(0, (value - low) / (high - low))
                return tuple(round(x + (y - x) * w) for x, y in zip(a, b))
        return stops[-1][1]

    count = 0
    for item in reader:
        row, k = int(item["row"]), int(item["bin"])
        if (row, k) != (count // bins, count % bins - edge):
            raise ValueError("Expected each time-frequency cell in native row order")
        for panel, field in zip(panels, ("normal_db", "mirrored_db")):
            panel.putpixel((k + edge, row), color(float(item[field])))
        count += 1
    if count != rows * bins:
        raise ValueError("Incomplete waterfall")

    canvas = Image.new("RGB", (1192, 470), "white")
    draw = ImageDraw.Draw(canvas)
    regular = ImageFont.truetype("DejaVuSans.ttf", 17)
    small = ImageFont.truetype("DejaVuSans.ttf", 14)
    bold = ImageFont.truetype("DejaVuSans-Bold.ttf", 19)
    ink = "#192d40"
    top, width, height = 54, 536, 272
    for i, (panel, title) in enumerate(zip(panels, ("Original spectrum", "Inverted spectrum"))):
        left = 68 + 572 * i
        draw.text((left, 18), title, font=bold, fill=ink)
        canvas.paste(panel.resize((width, height), Image.Resampling.NEAREST), (left, top))
        for hz in (-1000, -500, 0, 500, 1000):
            x = left + width * (hz / 6.25 + edge + 0.5) / bins
            draw.line((x, top + height, x, top + height + 5), fill=ink)
            draw.text((x, top + height + 8), str(hz), anchor="mt", font=small, fill=ink)
        draw.text((left + width / 2, 367), "Baseband frequency (Hz)", anchor="mm", font=regular, fill=ink)
    for seconds in (0, 2, 4, 6, 8, 10):
        y = top + height * seconds / 10.24
        draw.text((55, y), str(seconds), anchor="rm", font=small, fill=ink)
    draw.text((26, 30), "t (s)", anchor="mt", font=small, fill=ink)
    draw.text((68, 418), "Power relative to row median (dB)", anchor="lm", font=regular, fill=ink)
    for j in range(361):
        draw.line((400 + j, 404, 400 + j, 425), fill=color(-6 + 25 * j / 360))
    for tick in (-6, 0, 6, 12, 19):
        x = 400 + 360 * (tick + 6) / 25
        draw.text((x, 433), str(tick), anchor="mt", font=small, fill=ink)
    canvas.save(sys.argv[1])


if __name__ == "__main__":
    main()
