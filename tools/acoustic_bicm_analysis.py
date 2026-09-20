#!/usr/bin/env python3
"""Conditional Gray-QAM BICM information on measured acoustic sounder spectra.

No audio or modem runtime changes. NumPy Gauss-Hermite integration computes
exact AWGN bit likelihoods, not log2(1+SNR) or max-log approximations. The measured
residual is treated as stationary Gaussian noise at its tested source power;
this is a candidate-screening model, not proof that an LDPC frame/file decodes.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path

import numpy as np


ORDERS = (4, 16, 64, 256, 1024)
RATES = (("1/2", .5), ("3/4", .75), ("7/9", 7 / 9), ("8/9", 8 / 9))


def _logsumexp(values, axis=-1):
    maximum = np.max(values, axis=axis, keepdims=True)
    return np.squeeze(maximum, axis=axis) + np.log(np.sum(np.exp(values - maximum), axis=axis))


def qam_bit_information(order, esn0_db, quadrature=96):
    """Return information per label bit, first I then Q, MSB first.

    Square QAM has E|X|²=1. Axis noise variance=N0/2 and Eaxis²=1/2.
    Hermite nodes integrate real Gaussian observations; all equally probable
    input PAM levels and all competing levels are included in every bit LLR.
    """
    if order not in ORDERS or not np.isfinite(esn0_db) or not 16 <= quadrature <= 256:
        raise ValueError("unsupported QAM, SNR or bounded quadrature count")
    side = math.isqrt(order)
    axis_bits = side.bit_length() - 1
    levels = (2 * np.arange(side) + 1 - side) / np.sqrt(2 * (order - 1) / 3)
    labels = np.arange(side) ^ (np.arange(side) >> 1)
    bits = ((labels[:, None] >> np.arange(axis_bits - 1, -1, -1)) & 1)
    nodes, weights = np.polynomial.hermite.hermgauss(quadrature)
    weights = weights / np.sqrt(np.pi)
    snr = 10 ** (esn0_db / 10)
    observations = levels[:, None] + nodes[None, :] / np.sqrt(snr)
    metrics = -(observations[:, :, None] - levels[None, None, :]) ** 2 * snr
    information = []
    for bit in range(axis_bits):
        llr = _logsumexp(metrics[:, :, bits[:, bit] == 1]) - _logsumexp(metrics[:, :, bits[:, bit] == 0])
        signs = 2 * bits[:, bit] - 1
        log_loss = np.logaddexp(0, -llr * signs[:, None]) / np.log(2)
        information.append(float(1 - np.mean(log_loss @ weights)))
    return np.clip(np.tile(information, 2), 0, 1)


def information_grid(step=.25, quadrature=96):
    snr = np.arange(-20, 45 + step / 2, step)
    values = {order: np.array([qam_bit_information(order, db, quadrature) for db in snr]) for order in ORDERS}
    return snr, values


def ofdm_bins(fft_size, sample_rate=48000, low_hz=500, high_hz=18000):
    """Current production pilot allocation, frozen here as explicit geometry."""
    first = math.ceil(low_hz * fft_size / sample_rate)
    last = math.floor(high_hz * fft_size / sample_rate)
    active = np.arange(first, last + 1)
    stride = max(2, min(8, len(active) // (2 * 128 + 2)))
    data = active[(active - first) % stride != 0]
    return data * sample_rate / fft_size, dict(fft_size=fft_size, first_bin=first, last_bin=last,
                                             active_bins=len(active), data_bins=len(data), pilot_stride=stride,
                                             sample_rate=sample_rate, low_hz=low_hz, high_hz=high_hz)


def pooled_bit_loading(information, rate):
    """Lagrange screen sharing information margin across all coded carriers.

    Maximize assigned label bits subject to nonnegative aggregate BICM margin.
    Discrete choices can leave a little excess margin; the returned allocation
    is feasible in this ideal information model, not a modem implementation.
    """
    bits = np.array([0, 2, 4, 6, 8, 10])
    values = np.vstack([np.zeros_like(information[4])] + [information[q] for q in ORDERS])
    surplus = values - rate * bits[:, None]
    def choose(multiplier):
        choices = np.argmax(bits[:, None] + multiplier * surplus, axis=0)
        margin = float(surplus[choices, np.arange(choices.size)].sum())
        return choices, margin
    choices, margin = choose(0.)
    if margin < 0:
        low, high = 0., 1.
        while choose(high)[1] < 0:
            high *= 2
            if high > 1e9:
                raise ValueError("cannot find a feasible bounded loading allocation")
        for _ in range(64):
            middle = (low + high) / 2
            if choose(middle)[1] < 0:
                low = middle
            else:
                high = middle
        choices, margin = choose(high)
    return bits[choices], margin


def analyze_spectrum(path, snr_grid, curves, fft_size=8192, prefix=4096, margins=(0., 1., 2.)):
    arrays = dict(np.load(path))
    frequency = arrays["frequency_hz"]
    raw_snr = np.divide(arrays["source_psd"] * arrays["capacity_gain_power"], arrays["capacity_noise_psd"],
                        out=np.zeros_like(frequency), where=arrays["capacity_noise_psd"] > 0)
    data_frequency, geometry = ofdm_bins(fft_size)
    # Interpolate powers/SNR, not dB across deep zeros. OFDM bin spacing differs
    # from the sounder's 2.9296875 Hz spectral grid.
    per_bin_db = 10 * np.log10(np.maximum(np.interp(data_frequency, frequency, raw_snr), 1e-30))
    geometry["prefix_samples"] = prefix
    geometry["prefix_fraction"] = prefix / (fft_size + prefix)
    symbol_rate = geometry["sample_rate"] / (fft_size + prefix)
    rows, loading, pooled_loading = [], [], []
    for margin in margins:
        infos = {order: np.column_stack([np.interp(per_bin_db - margin, snr_grid, curves[order][:, bit],
                                                  left=0, right=1) for bit in range(curves[order].shape[1])])
                 for order in ORDERS}
        for order in ORDERS:
            label_bits = int(math.log2(order))
            mi = infos[order].sum(axis=1)
            for rate_name, rate in RATES:
                rows.append(dict(qam=order, code_rate=rate_name, rate=rate, snr_penalty_db=margin,
                                 mean_bicm_bits_per_payload_symbol=float(mi.mean()),
                                 mean_bicm_per_coded_bit=float(mi.mean() / label_bits),
                                 information_margin_bits_per_symbol=float(mi.mean() - rate * label_bits),
                                 ideal_information_feasible=bool(mi.mean() >= rate * label_bits),
                                 ideal_coded_source_bps=float(len(mi) * label_bits * rate * symbol_rate),
                                 matched_bicm_information_bps=float(mi.sum() * symbol_rate),
                                 tenth_percentile_carrier_bicm_bits=float(np.quantile(mi, .1)),
                                 mean_information_per_bit_plane=infos[order].mean(axis=0).tolist()))
        for rate_name, rate in RATES:
            chosen = np.zeros(len(data_frequency), dtype=int)
            # Conservative local screen: each carrier's entire QAM label must
            # individually clear the rate. Unused bins keep their power unused;
            # there is no water filling or redistribution to other bins.
            for order in ORDERS:
                bits = int(math.log2(order))
                chosen[infos[order].sum(axis=1) >= rate * bits] = bits
            counts = {str(2 ** bits) if bits else "off": int(np.count_nonzero(chosen == bits))
                      for bits in (0, 2, 4, 6, 8, 10)}
            loading.append(dict(code_rate=rate_name, snr_penalty_db=margin,
                                ideal_coded_source_bps=float(chosen.sum() * rate * symbol_rate),
                                carrier_order_counts=counts, unused_carriers=int(np.count_nonzero(chosen == 0))))
            chosen, information_margin = pooled_bit_loading({q:infos[q].sum(axis=1) for q in ORDERS}, rate)
            counts = {str(2 ** bits) if bits else "off": int(np.count_nonzero(chosen == bits)) for bits in (0,2,4,6,8,10)}
            pooled_loading.append(dict(code_rate=rate_name, snr_penalty_db=margin,
                                       ideal_coded_source_bps=float(chosen.sum() * rate * symbol_rate),
                                       aggregate_information_margin_bits=information_margin,
                                       carrier_order_counts=counts))
    return dict(source_spectrum=str(path), source_sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest(),
                geometry=geometry, per_bin_snr_db_quantiles={str(q):float(np.quantile(per_bin_db,q)) for q in (0,.1,.5,.9,1)},
                uniform_modes=rows, local_bit_loading=loading, pooled_bit_loading=pooled_loading)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spectrum", action="append", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fft", type=int, default=8192, choices=(4096,8192,16384,32768))
    parser.add_argument("--prefix", type=int, default=4096)
    parser.add_argument("--quadrature", type=int, default=96)
    args = parser.parse_args()
    if not 256 <= args.prefix <= args.fft or not 32 <= args.quadrature <= 192:
        parser.error("invalid prefix or quadrature count")
    snr, curves = information_grid(quadrature=args.quadrature)
    args.output.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(args.output / "ideal-gray-qam-information.npz", esn0_db=snr,
                        **{f"qam_{order}_bit_information":values for order,values in curves.items()})
    with (args.output / "ideal-gray-qam-information.csv").open("w") as stream:
        writer = csv.writer(stream)
        writer.writerow(["EsN0_dB"] + [f"QAM{q}_BICM_bits_per_symbol" for q in ORDERS])
        writer.writerows([[float(db)] + [float(curves[q][i].sum()) for q in ORDERS] for i,db in enumerate(snr)])
    report = dict(method="Exact Gray square-QAM AWGN bit LLR mutual information using Gauss-Hermite quadrature on each PAM axis.",
                  quadrature_points=args.quadrature, snr_grid_step_db=.25,
                  interpretation="For ideal interleaved BICM, mean bit information >= code-rate times label bits is a necessary information screen. It does not locate this finite LDPC decoder's waterfall or establish frame/file success.",
                  measured_snr_model="Source PSD times independently estimated response power divided by conservative held-out residual PSD. Treating this as complex-symbol Es/N0 assumes stationary Gaussian-equivalent noise, independent subcarriers and adequate cyclic prefix/equalization. Residual can depend on waveform, level and time.",
                  rate_overheads="Rates include the specified OFDM cyclic prefix and current per-carrier pilot allocation. They exclude finite coding-cycle packing, outer RS, digests, bootstrap/training, end silence and delivery failures.",
                  loading_model="Fixed per-carrier power, orders4/16/64/256/1024 or off. Local screen requires every individual carrier to clear the rate and can underperform coding across frequency. Pooled Lagrange screen maximizes assigned bits while sharing nonnegative aggregate BICM margin; discrete choices leave slight slack. Neither includes loading signaling, changed LDPC mapping/waterfalls, or changes in distortion.",
                  cases=[analyze_spectrum(path,snr,curves,args.fft,args.prefix) for path in args.spectrum])
    (args.output / "bicm-analysis.json").write_text(json.dumps(report,indent=2,allow_nan=False)+"\n")
    columns=["case","qam","code_rate","snr_penalty_db","mean_bicm_bits_per_payload_symbol","mean_bicm_per_coded_bit",
             "information_margin_bits_per_symbol","ideal_information_feasible","ideal_coded_source_bps","matched_bicm_information_bps"]
    with (args.output / "uniform-mode-screen.csv").open("w") as stream:
        writer=csv.DictWriter(stream,fieldnames=columns);writer.writeheader()
        for case in report["cases"]:
            for row in case["uniform_modes"]:
                writer.writerow({key:case["source_spectrum"] if key=="case" else row[key] for key in columns})
    for case in report["cases"]:
        print(case["source_spectrum"],case["geometry"])
        for margin in (0.,1.,2.):
            candidates=[r for r in case["uniform_modes"] if r["snr_penalty_db"]==margin and r["ideal_information_feasible"]]
            if not candidates:
                print(f"penalty {margin:g} dB: no tested uniform mode clears the ideal information requirement")
                continue
            best=max(candidates,key=lambda r:r["ideal_coded_source_bps"])
            print(f"penalty {margin:g} dB: best screened uniform {best['qam']}-QAM {best['code_rate']}, {best['ideal_coded_source_bps']:.1f} bps, margin {best['information_margin_bits_per_symbol']:.3f} bits/symbol")


if __name__ == "__main__":
    main()
