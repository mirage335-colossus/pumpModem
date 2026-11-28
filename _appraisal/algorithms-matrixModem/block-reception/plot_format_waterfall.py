"""Render the native --format-example CSV from stdin to the given SVG path."""

import csv
import sys
from collections import Counter
from html import escape
from pathlib import Path


SLOTS = 64
CELL_SECONDS = 0.160
FIRST_HZ = 450
BIN_HZ = 6.25
BINS = 352
POWER = "#fff3a8"
REPEAT = "#69f3ce"
REPEAT_INK = "#075b50"
INK = "#193242"


def main():
    fields = ("tone", "symbol", "position", "phase_deg")
    reader = csv.DictReader(sys.stdin)
    if reader.fieldnames != list(fields):
        raise ValueError(f"Expected native CSV columns {', '.join(fields)}")
    rows = [tuple(int(row[field]) for field in fields) for row in reader]
    if len(rows) != SLOTS or any(
        slot != i or not 0 <= position < BINS or phase not in (0, 90, 180, 270)
        for i, (slot, _, position, phase) in enumerate(rows)
    ):
        raise ValueError("Expected 64 ordered HF tone slots with valid positions and phases")
    if Counter(symbol for _, symbol, _, _ in rows) != dict.fromkeys(range(32), 2):
        raise ValueError("Expected two copies of each of the 32 coded symbols")

    svg = [
        '<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="655" '
        'viewBox="0 0 1280 655" role="img" aria-labelledby="title description">',
        '<title id="title">HF transmit waterfall</title>',
        '<desc id="description">Frequency increases left to right and time increases down. '
        'The first panel shows all 64 tones. The second expands the first, four middle and '
        'last tone slots, labelled by coded byte index and scheduled phase. '
        'Brackets identify the full block, one cell and a repeated keyed phase. '
        'The two copies of the first transmitted symbol are linked. A magnified '
        'frequency ruler shows adjacent positions 6.25 Hz apart.</desc>',
    ]

    def rect(x, y, width, height, fill):
        svg.append(
            f'<rect x="{x:.3f}" y="{y:.3f}" width="{width:.3f}" height="{height:.3f}" '
            f'fill="{fill}"/>'
        )

    def line(x1, y1, x2, y2, color, width=1):
        svg.append(
            f'<line x1="{x1:.3f}" y1="{y1:.3f}" x2="{x2:.3f}" y2="{y2:.3f}" '
            f'stroke="{color}" stroke-width="{width}"/>'
        )

    def label(x, y, value, *, size=21, anchor="start", fill=INK, bold=False):
        weight = "700" if bold else "400"
        svg.append(
            f'<text x="{x}" y="{y:.3f}" text-anchor="{anchor}" font-size="{size}" '
            f'font-weight="{weight}" fill="{fill}">{escape(str(value))}</text>'
        )

    def vertical_bracket(x, start, end, tick, color):
        rect(x - 0.8, start, 1.6, end - start, color)
        left = min(x, x + tick)
        rect(left, start - 0.8, abs(tick), 1.6, color)
        rect(left, end - 0.8, abs(tick), 1.6, color)

    rect(0, 0, 1280, 655, "#ffffff")
    svg.append('<g font-family="DejaVu Sans, sans-serif">')
    label(27, 39, "HF transmit waterfall", size=31, bold=True)
    label(27, 70, "Word 173 (10 bits) → 32 coded bytes → 64 interleaved tones")

    overview_x, detail_x = 87, 596
    top, height, width = 153, 302, 360
    bottom = top + height
    symbol_x, phase_x = 1033, 1188
    repeat_symbol = rows[0][1]
    middle = 23
    if rows[middle][1] != repeat_symbol or rows[middle][2] == rows[0][2]:
        raise ValueError("The annotated fixture needs the same symbol at different frequencies in slots 0 and 23")

    label(overview_x, 109, "One block · 64 tones · 10.24 s", size=24, bold=True)
    label(detail_x, 109, "Selected tone slots", size=24, bold=True)
    label(overview_x - 11, 138, "Time (s)", size=18, anchor="end")
    label(detail_x - 74, 138, "Start (s)", size=18, anchor="end")
    label(detail_x - 12, 138, "Slot", size=18, anchor="end")
    label(symbol_x, 138, "Symbol index", size=19, anchor="middle")
    label(phase_x, 138, "Keyed phase", size=19, anchor="middle")

    def frequency_x(left, hz):
        return left + (hz - FIRST_HZ) * width / (BINS * BIN_HZ)

    for left in (overview_x, detail_x):
        rect(left, top, width, height, "#122635")
        for hz in (FIRST_HZ, 1000, 1500, 2000, FIRST_HZ + BINS * BIN_HZ):
            x = frequency_x(left, hz)
            line(x, top, x, bottom, "#49606b")
            line(x, bottom, x, bottom + 5, "#49606b")
            label(x, bottom + 27, int(hz), size=18, anchor="middle")
        label(left + width / 2, bottom + 54, "Audio frequency (Hz)", anchor="middle")

    for seconds in (0, 2, 4, 6, 8, 10.24):
        y = top + seconds / (SLOTS * CELL_SECONDS) * height
        line(overview_x, y, overview_x + width, y, "#3e5562")
        label(overview_x - 11, y + 6, f"{seconds:g}", size=18, anchor="end")

    vertical_bracket(overview_x + width + 11, top, bottom, -7, INK)

    def tone_bin(left, position, y, length, highlight):
        x = frequency_x(left, FIRST_HZ + position * BIN_HZ)
        bin_width = width / BINS
        start = max(left, x - bin_width / 2)
        end = min(left + width, x + bin_width / 2)
        rect(start, y, end - start, length, REPEAT if highlight else POWER)
        return x

    row_height = height / SLOTS
    for slot, symbol, position, _ in rows:
        y = top + slot * row_height
        highlight = symbol == repeat_symbol
        x = tone_bin(overview_x, position, y, row_height, highlight)
        if highlight:
            svg.append(
                f'<circle cx="{x:.3f}" cy="{y + row_height / 2:.3f}" r="5" '
                f'fill="none" stroke="{REPEAT}" stroke-width="1.5"/>'
            )

    slot_height, gap_height = 40, 31
    y = top
    shown = {}
    for slot in (0, None, *range(middle, middle + 4), None, SLOTS - 1):
        if slot is None:
            rect(detail_x, y, width, gap_height, "#eaf0f1")
            for offset in (8, 15, 22):
                svg.append(f'<circle cx="{detail_x + width / 2:.3f}" cy="{y + offset:.3f}" '
                           'r="1.6" fill="#193242"/>')
            y += gap_height
            continue
        _, symbol, position, phase = rows[slot]
        shown[slot] = y
        highlight = symbol == repeat_symbol
        ink = REPEAT_INK if highlight else INK
        line(detail_x, y, detail_x + width, y, "#49606b")
        tone_bin(detail_x, position, y, slot_height, highlight)
        if highlight:
            rect(symbol_x - 20, y + 5, 40, 30, "#d7f7ee")
        label(detail_x - 74, y + 27, f"{slot * CELL_SECONDS:.2f}",
              anchor="end", fill=ink, bold=highlight)
        label(detail_x - 12, y + 27, slot, anchor="end", fill=ink, bold=highlight)
        label(symbol_x, y + 27, symbol, anchor="middle", fill=ink, bold=highlight)
        label(phase_x, y + 27, f"{phase}°", anchor="middle", fill=ink, bold=highlight)
        y += slot_height

    first_tone_x = frequency_x(detail_x, FIRST_HZ + rows[0][2] * BIN_HZ)
    first_center = top + slot_height / 2
    rect(first_tone_x + 9, first_center - 0.8, 13, 1.6, REPEAT)
    svg.append(f'<polygon points="{first_tone_x + 3:.3f},{first_center:.3f} '
               f'{first_tone_x + 10:.3f},{first_center - 4:.3f} '
               f'{first_tone_x + 10:.3f},{first_center + 4:.3f}" fill="{REPEAT}"/>')
    label(first_tone_x + 28, top + 16, "Tone", size=17, fill=REPEAT, bold=True)
    label(first_tone_x + 28, top + 34, "C = 1 cell", size=16, fill=REPEAT)
    cell_x = detail_x + width - 90
    vertical_bracket(cell_x, top, top + slot_height, -7, "#e7f3f4")
    label(cell_x + 8, top + 16, "1 cell", size=17, fill="#e7f3f4", bold=True)
    label(cell_x + 8, top + 34, "160 ms", size=16, fill="#e7f3f4")

    repeat_center = shown[middle] + slot_height / 2
    copy_x = symbol_x + 39
    vertical_bracket(copy_x, first_center, repeat_center, -18, REPEAT_INK)
    label(copy_x + 8, (first_center + repeat_center) / 2 + 4, "Copies", size=17, fill=REPEAT_INK)

    if rows[25][3] == rows[26][3]:
        phase_bracket = phase_x + 33
        vertical_bracket(phase_bracket, shown[25] + slot_height / 2,
                         shown[26] + slot_height / 2, -8, INK)
        label(phase_bracket + 6, shown[26] + 5, "same", size=17)

    ruler_x = (113, 226, 339)
    ruler_y = 585
    label(overview_x, 545, "Adjacent positions · enlarged", size=21, bold=True)
    spacing_y = ruler_y - 12
    line(ruler_x[0], spacing_y, ruler_x[1], spacing_y, INK)
    line(ruler_x[0], spacing_y - 4, ruler_x[0], spacing_y + 4, INK)
    line(ruler_x[1], spacing_y - 4, ruler_x[1], spacing_y + 4, INK)
    label((ruler_x[0] + ruler_x[1]) / 2, spacing_y - 4,
          f"Δf = {BIN_HZ:g} Hz", size=17, anchor="middle", bold=True)
    line(ruler_x[0] - 12, ruler_y, ruler_x[-1] + 12, ruler_y, INK)
    for position, x in enumerate(ruler_x):
        line(x, ruler_y - 5, x, ruler_y + 5, INK)
        label(x, ruler_y + 23, f"{FIRST_HZ + position * BIN_HZ:g} Hz", size=17, anchor="middle")

    label(detail_x, 545, "Frequency from the slot’s coded-byte placement", size=20, bold=True)
    label(detail_x, 572, "One keyed phase per tone, assigned independently", size=20)
    label(detail_x, 606, "Bright marks show ideal analysis bins.", size=19)
    label(detail_x, 631, "Measure occupied bandwidth separately.", size=19)
    svg.extend(("</g>", "</svg>"))
    Path(sys.argv[1]).write_text("\n".join(svg) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
