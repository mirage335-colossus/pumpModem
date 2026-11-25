#!/usr/bin/env python3
"""Render the observer-screen CSV as a small, dependency-free SVG figure."""
import csv
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent
rows = list(csv.DictReader((ROOT / "results/detectability.csv").open()))
looks_values = [1, 4, 16]
series = [
    ("wideband_radiometer_E_over_N0_dB", "Whole-band radiometer", "#b23a48"),
    ("known_hop_E_over_N0_dB", "Known hop schedule", "#246a73"),
    ("local16_E_over_N0_dB", "16-schedule sparse search", "#d17a22"),
    ("full_search_E_over_N0_dB", "Search all K schedules", "#58427c"),
]
width, height = 1000, 780
left, top, plot_w, plot_h = 85, 85, 830, 585
all_values = [float(row[key]) for row in rows for key, _, _ in series]
y_min = math.floor(min(all_values) / 5) * 5
y_max = math.ceil(max(all_values) / 5) * 5
k_values = sorted({int(row["K"]) for row in rows})
x_positions = {k: left + math.log(k) / math.log(max(k_values)) * plot_w for k in k_values}

def y(value):
    return top + (y_max - value) / (y_max - y_min) * plot_h

parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-labelledby="energy-title">']
parts.append('<title id="energy-title">Ideal observer energy requirement versus number of tone locations</title>')
parts.append('<rect width="100%" height="100%" fill="white"/>')
parts.append('<text x="500" y="32" text-anchor="middle" font-family="sans-serif" font-size="21">Ideal observer energy requirement vs frequency uncertainty</text>')
parts.append('<text x="500" y="57" text-anchor="middle" font-family="sans-serif" font-size="13">Pfa = 10⁻³, Pd = 0.9, 160 ms per look; total energy summed across all looks</text>')
for tick in range(y_min, y_max + 1, 5):
    parts.append(f'<line x1="{left}" y1="{y(tick):.1f}" x2="{left + plot_w}" y2="{y(tick):.1f}" stroke="#ddd"/>')
    parts.append(f'<text x="{left - 12}" y="{y(tick) + 4:.1f}" text-anchor="end" font-family="sans-serif" font-size="12">{tick}</text>')
for k in k_values:
    x = x_positions[k]
    parts.append(f'<line x1="{x:.1f}" y1="{top}" x2="{x:.1f}" y2="{top + plot_h}" stroke="#eee"/>')
    parts.append(f'<text x="{x:.1f}" y="{top + plot_h + 22}" text-anchor="middle" font-family="sans-serif" font-size="12">{k}</text>')
parts.append(f'<line x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" y2="{top + plot_h}" stroke="#222"/>')
parts.append(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" stroke="#222"/>')
parts.append(f'<text x="500" y="730" text-anchor="middle" font-family="sans-serif" font-size="14">Orthogonal tone locations K (6.25 Hz grid, logarithmic axis)</text>')
parts.append(f'<text x="20" y="{top + plot_h / 2}" text-anchor="middle" font-family="sans-serif" font-size="14" transform="rotate(-90 20 {top + plot_h / 2})">Required received energy E/N₀ (dB)</text>')

dash_styles = ["none", "8 4", "3 3"]
for panel, looks in enumerate(looks_values):
    group = [row for row in rows if int(row["looks"]) == looks]
    for key, label, color in series:
        points = " ".join(f'{x_positions[int(row["K"] )]:.1f},{y(float(row[key])):.1f}' for row in group)
        parts.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2.5" stroke-dasharray="{dash_styles[panel]}"/>')
        for row in group:
            x = x_positions[int(row["K"])]
            yy = y(float(row[key]))
            parts.append(f'<circle cx="{x:.1f}" cy="{yy:.1f}" r="3.3" fill="{color}"/>')

legend_x, legend_y = left + 12, top + 12
for i, (key, label, color) in enumerate(series):
    yy = legend_y + i * 21
    parts.append(f'<line x1="{legend_x}" y1="{yy}" x2="{legend_x + 28}" y2="{yy}" stroke="{color}" stroke-width="2.5"/>')
    parts.append(f'<text x="{legend_x + 36}" y="{yy + 4}" font-family="sans-serif" font-size="12">{label}</text>')
for panel, looks in enumerate(looks_values):
    xx = 585 + panel * 120
    yy = 68
    label = "look" if looks == 1 else "looks"
    parts.append(f'<line x1="{xx}" y1="{yy}" x2="{xx + 20}" y2="{yy}" stroke="#555" stroke-width="3" stroke-dasharray="{dash_styles[panel]}"/>')
    parts.append(f'<text x="{xx + 25}" y="{yy + 4}" font-family="sans-serif" font-size="12">{looks} {label} ({0.16 * looks:g} s)</text>')
parts.append('<text x="500" y="762" text-anchor="middle" font-family="sans-serif" font-size="11" fill="#444">Normalizations: complex noise variance N₀ per orthogonal bin; deterministic unknown-phase tone coefficients; chi-square thresholds and noncentral-chi-square power.</text>')
parts.append('</svg>')
(ROOT / "results/detectability.svg").write_text("\n".join(parts))

# A fixed-K view converts total required energy to the corresponding mean
# received power-to-noise-density ratio using E/N0 = (C/N0) * duration.
chosen_k = 352
duration_rows = [row for row in rows if int(row["K"]) == chosen_k]
duration_rows.sort(key=lambda row: int(row["looks"]))
duration_series = [
    ("wideband_radiometer_E_over_N0_dB", "Whole-band radiometer", "#b23a48"),
    ("known_hop_E_over_N0_dB", "Known hop schedule", "#246a73"),
    ("local16_E_over_N0_dB", "16-schedule sparse search", "#d17a22"),
    ("full_search_E_over_N0_dB", "Search all 352 schedules", "#58427c"),
]
cn0_values = [
    float(row[key]) - 10 * math.log10(int(row["looks"]) * 0.16)
    for row in duration_rows for key, _, _ in duration_series
]
cn0_min = math.floor(min(cn0_values) / 5) * 5
cn0_max = math.ceil(max(cn0_values) / 5) * 5
cn0_width, cn0_height = 1000, 650
cn0_left, cn0_top, cn0_plot_w, cn0_plot_h = 100, 95, 820, 450
durations = [int(row["looks"]) * 0.16 for row in duration_rows]
duration_positions = {
    duration: cn0_left + math.log(duration / min(durations)) / math.log(max(durations) / min(durations)) * cn0_plot_w
    for duration in durations
}

def cn0_y(value):
    return cn0_top + (cn0_max - value) / (cn0_max - cn0_min) * cn0_plot_h

cn0_parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{cn0_width}" height="{cn0_height}" viewBox="0 0 {cn0_width} {cn0_height}" role="img" aria-labelledby="cn0-title">']
cn0_parts.append('<title id="cn0-title">Ideal observer received carrier power to noise density requirement versus observation duration</title>')
cn0_parts.append('<rect width="100%" height="100%" fill="white"/>')
cn0_parts.append('<text x="500" y="35" text-anchor="middle" font-family="sans-serif" font-size="21">Required received C/N₀ vs observation duration</text>')
cn0_parts.append('<text x="500" y="61" text-anchor="middle" font-family="sans-serif" font-size="13">K = 352 tones, 6.25 Hz grid, Pfa = 10⁻³, Pd = 0.9; energy threshold converted to mean C/N₀</text>')
for tick in range(cn0_min, cn0_max + 1, 5):
    cn0_parts.append(f'<line x1="{cn0_left}" y1="{cn0_y(tick):.1f}" x2="{cn0_left + cn0_plot_w}" y2="{cn0_y(tick):.1f}" stroke="#ddd"/>')
    cn0_parts.append(f'<text x="{cn0_left - 12}" y="{cn0_y(tick) + 4:.1f}" text-anchor="end" font-family="sans-serif" font-size="12">{tick}</text>')
for duration in durations:
    x = duration_positions[duration]
    cn0_parts.append(f'<line x1="{x:.1f}" y1="{cn0_top}" x2="{x:.1f}" y2="{cn0_top + cn0_plot_h}" stroke="#eee"/>')
    cn0_parts.append(f'<text x="{x:.1f}" y="{cn0_top + cn0_plot_h + 22}" text-anchor="middle" font-family="sans-serif" font-size="12">{duration:g}</text>')
cn0_parts.append(f'<line x1="{cn0_left}" y1="{cn0_top + cn0_plot_h}" x2="{cn0_left + cn0_plot_w}" y2="{cn0_top + cn0_plot_h}" stroke="#222"/>')
cn0_parts.append(f'<line x1="{cn0_left}" y1="{cn0_top}" x2="{cn0_left}" y2="{cn0_top + cn0_plot_h}" stroke="#222"/>')
cn0_parts.append(f'<text x="510" y="590" text-anchor="middle" font-family="sans-serif" font-size="14">Observation duration (s, logarithmic axis)</text>')
cn0_parts.append(f'<text x="22" y="{cn0_top + cn0_plot_h / 2}" text-anchor="middle" font-family="sans-serif" font-size="14" transform="rotate(-90 22 {cn0_top + cn0_plot_h / 2})">Required received C/N₀ (dB-Hz)</text>')
for key, label, color in duration_series:
    values = [float(row[key]) - 10 * math.log10(int(row["looks"]) * 0.16) for row in duration_rows]
    points = " ".join(f'{duration_positions[duration]:.1f},{cn0_y(value):.1f}' for duration, value in zip(durations, values))
    cn0_parts.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2.5"/>')
    for duration, value in zip(durations, values):
        cn0_parts.append(f'<circle cx="{duration_positions[duration]:.1f}" cy="{cn0_y(value):.1f}" r="3.3" fill="{color}"/>')
for i, (_, label, color) in enumerate(duration_series):
    xx, yy = 690, 110 + i * 20
    cn0_parts.append(f'<line x1="{xx}" y1="{yy}" x2="{xx + 28}" y2="{yy}" stroke="{color}" stroke-width="2.5"/>')
    cn0_parts.append(f'<text x="{xx + 36}" y="{yy + 4}" font-family="sans-serif" font-size="12">{label}</text>')
cn0_parts.append('<text x="510" y="625" text-anchor="middle" font-family="sans-serif" font-size="11" fill="#444">E/N₀ values assume the correct schedule is included in each searched bank; fixed-amplitude orthogonal Gaussian-noise model.</text>')
cn0_parts.append('</svg>')
(ROOT / "results/detectability-cn0.svg").write_text("\n".join(cn0_parts))
