#!/usr/bin/env python3
"""Offline known-source EVM for Fast capacity single-carrier probe captures.

Reads a probe JSON report, its literal uint8 TX bits and float32 (I,Q) observer
records. It never opens audio or changes settings. Geometry, absence, erasure
and count checks prevent silently treating omitted bad groups as contiguous
symbols. Every 2,048-bit interval pads its final QAM label independently.

The input I/Q has already passed the production timing/carrier/equalizer loops.
Its gain is a residual in normalized modem coordinates, NOT hardware DAC/ADC
gain. Known-source residual includes noise, distortion, ISI and receiver error;
its ratio is not a calibrated AWGN SNR. Nearest-decision EVM is reported only
for comparison and can be small even when most labels are wrong.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path

import numpy as np

INTERVAL_BITS = 2048
MAX_BITS = 16 * 1024 * 1024
MAX_SYMBOLS = 2 * 1024 * 1024
BIT_COUNTS = np.array([value.bit_count() for value in range(256)], dtype=np.uint8)


def _db(power):
    return float(10 * math.log10(power)) if power > 0 else None


def _order(order):
    if isinstance(order, bool) or int(order) != order:
        raise ValueError("QAM order must be an integer power of four")
    order = int(order)
    bps = order.bit_length() - 1
    if not 2 <= bps <= 22 or bps % 2 or order != 1 << bps:
        raise ValueError("QAM order must be a power of four in 4..4194304")
    return order, bps


def labels_to_points(labels, order):
    order, bps = _order(order)
    labels = np.asarray(labels, dtype=np.uint32)
    if np.any(labels >= order):
        raise ValueError("QAM label exceeds constellation")
    side = 1 << (bps // 2)
    i, q = labels >> (bps // 2), labels & (side - 1)
    # Frozen Cartesian Gray-PAM mapping, not nearest decisions from RX data.
    shift = 1
    while shift < bps // 2:
        i = i ^ (i >> shift)
        q = q ^ (q >> shift)
        shift *= 2
    scale = math.sqrt(2 * (order - 1) / 3)
    return ((2 * i.astype(float) + 1 - side) +
            1j * (2 * q.astype(float) + 1 - side)) / scale


def expected_symbols(bits, order):
    order, bps = _order(order)
    bits = np.asarray(bits)
    if bits.ndim != 1 or not 0 < bits.size <= MAX_BITS or bits.size % INTERVAL_BITS:
        raise ValueError("TX bits must contain bounded, complete 2048-bit intervals")
    if np.any((bits != 0) & (bits != 1)):
        raise ValueError("TX bit file contains values other than literal 0/1")
    per_interval = (INTERVAL_BITS + bps - 1) // bps
    if bits.size // INTERVAL_BITS * per_interval > MAX_SYMBOLS:
        raise ValueError("Expected symbol count exceeds bounded calibration workspace")
    padded = np.pad(bits.reshape(-1, INTERVAL_BITS),
                    ((0, 0), (0, per_interval * bps - INTERVAL_BITS)))
    words = padded.reshape(-1, per_interval, bps).astype(np.uint32)
    weights = np.left_shift(np.uint32(1), np.arange(bps - 1, -1, -1, dtype=np.uint32))
    labels = np.sum(words * weights, axis=-1, dtype=np.uint32)
    return labels_to_points(labels, order)


def _nearest_points(values, order):
    order, bps = _order(order)
    side = 1 << (bps // 2)
    scale = math.sqrt(2 * (order - 1) / 3)
    i = np.clip(np.floor((values.real * scale + side - 1) / 2 + .5), 0, side - 1)
    q = np.clip(np.floor((values.imag * scale + side - 1) / 2 + .5), 0, side - 1)
    return ((2 * i + 1 - side) + 1j * (2 * q + 1 - side)) / scale


def _nearest_labels(values, order):
    order, bps = _order(order)
    side = 1 << (bps // 2)
    scale = math.sqrt(2 * (order - 1) / 3)
    i = np.clip(np.floor((values.real * scale + side - 1) / 2 + .5), 0, side - 1).astype(np.uint32)
    q = np.clip(np.floor((values.imag * scale + side - 1) / 2 + .5), 0, side - 1).astype(np.uint32)
    return ((i ^ (i >> 1)) << (bps // 2)) | (q ^ (q >> 1))


def _metrics(expected, actual, order):
    signal = float(np.vdot(expected, expected).real)
    error = float(np.vdot(actual - expected, actual - expected).real)
    nearest = _nearest_points(actual, order)
    decision_error = float(np.vdot(actual - nearest, actual - nearest).real)
    coefficient = np.vdot(expected, actual) / signal
    fitted_error = float(np.vdot(actual - coefficient * expected,
                                actual - coefficient * expected).real)
    fitted_signal = float(abs(coefficient) ** 2 * signal)
    return dict(symbols=int(expected.size), signal_energy=signal, error_energy=error,
                known_evm=math.sqrt(error / signal), known_evm_percent=100 * math.sqrt(error / signal),
                known_signal_to_residual_db=_db(signal / error) if error > 0 else None,
                nearest_decision_evm=math.sqrt(decision_error / signal),
                nearest_label_mismatches=int(np.count_nonzero(np.abs(nearest - expected) > 1e-10)),
                gain_real=float(coefficient.real), gain_imag=float(coefficient.imag),
                gain_magnitude=float(abs(coefficient)), gain_db=_db(float(abs(coefficient) ** 2)),
                phase_degrees=float(np.angle(coefficient) * 180 / np.pi),
                after_global_complex_gain_evm=math.sqrt(fitted_error / fitted_signal) if fitted_signal else None,
                after_global_complex_gain_signal_to_residual_db=_db(fitted_signal / fitted_error)
                if fitted_signal > 0 and fitted_error > 0 else None)


def analyze(report, bits, iq):
    if report.get("format") != "capacity" or report.get("waveform") != "single-carrier":
        raise ValueError("Only capacity single-carrier interval padding is supported; OFDM/classic differ")
    order, bps = _order(report.get("apsk", 0))
    expected = expected_symbols(bits, order)
    intervals, per_interval = expected.shape
    if report.get("tx_intervals") != intervals or report.get("rx_intervals") != intervals:
        raise ValueError("Probe interval counts differ from complete transmitted geometry")
    if not report.get("acquired") or not report.get("physical_end"):
        raise ValueError("Probe lacks acquisition or observed physical completion")
    if report.get("missing_intervals", 0) or report.get("raw_erased_bits", 0) or report.get("raw_missing_bits", 0):
        raise ValueError("Probe has missing/erased positions; observer I/Q is not a contiguous reference")
    if report.get("fifo_overflow") or report.get("capture_error") or report.get("playback_error"):
        raise ValueError("Probe reports an audio continuity/error condition")
    if any(int(offset) != 0 and count for offset, count in report.get("alignment_offsets", {}).items()):
        raise ValueError("Probe found a shifted interval alignment")
    if report.get("mode") == "raw" and report.get("unalignable_intervals", 0):
        raise ValueError("Raw probe could not independently align every interval")
    values = np.asarray(iq)
    if values.ndim != 1 or values.size != expected.size or not np.isfinite(values).all():
        raise ValueError(f"Expected exactly {expected.size} finite payload I/Q observations; bad groups may have been omitted")
    actual = values.astype(np.complex128).reshape(expected.shape)
    summary = _metrics(expected.ravel(), actual.ravel(), order)
    label_errors = _nearest_labels(expected, order) ^ _nearest_labels(actual, order)
    padding = per_interval * bps - INTERVAL_BITS
    if padding:
        label_errors[:, -1] &= np.uint32(((1 << bps) - 1) ^ ((1 << padding) - 1))
    byte_errors = label_errors.astype("<u4").view(np.uint8).reshape(intervals, per_interval, 4)
    bit_errors = BIT_COUNTS[byte_errors].sum(axis=(1, 2))
    summary.update(known_payload_bits=int(bits.size), known_payload_bit_errors=int(bit_errors.sum()),
                   known_payload_ber=float(bit_errors.sum() / bits.size))
    # One scalar fitted on alternating complete intervals, assessed on the
    # others. With one interval, use alternating symbols and identify this.
    if intervals > 1:
        train_x, train_y = expected[::2].ravel(), actual[::2].ravel()
        test_x, test_y = expected[1::2].ravel(), actual[1::2].ravel()
        split = "even intervals fit, odd intervals held out"
    else:
        train_x, train_y = expected.ravel()[::2], actual.ravel()[::2]
        test_x, test_y = expected.ravel()[1::2], actual.ravel()[1::2]
        split = "one interval: even symbols fit, odd symbols held out"
    fitted = np.vdot(train_x, train_y) / np.vdot(train_x, train_x).real
    prediction = fitted * test_x
    denominator = float(np.vdot(prediction, prediction).real)
    residual = float(np.vdot(test_y - prediction, test_y - prediction).real)
    heldout = dict(split=split, fitted_real=float(fitted.real), fitted_imag=float(fitted.imag),
                   symbols=int(test_x.size), evm=math.sqrt(residual / denominator) if denominator else None,
                   signal_to_residual_db=_db(denominator / residual) if denominator > 0 and residual > 0 else None)
    rows = [dict(interval=index, payload_bit_errors=int(bit_errors[index]), **_metrics(x, y, order))
            for index, (x, y) in enumerate(zip(expected, actual))]
    result = dict(analysis="known-source Gray QAM residual", qualified=True, order=order, bits_per_symbol=bps,
                  intervals=intervals, symbols_per_interval=per_interval,
                  zero_label_padding_bits_per_interval=per_interval * bps - INTERVAL_BITS,
                  summary=summary, heldout_global_gain=heldout,
                  probe=dict(exact=report.get("exact"), decoded_complete=report.get("decoded_complete"),
                             raw_wrong_bits=report.get("raw_wrong_bits") if report.get("mode") == "raw" else None,
                             raw_wrong_bits_counted=report.get("mode") == "raw", amplitude=report.get("amplitude"),
                             symbol_rate=report.get("symbol_rate"), rolloff=report.get("rolloff")),
                  gain_interpretation="Residual scalar after production RX normalization/equalization, not hardware gain.",
                  residual_interpretation="Known-source residual includes noise, distortion, timing, phase and equalizer errors. It is not calibrated AWGN SNR.",
                  global_fit_caveat="Complex gain fitting removes common amplitude/phase error and can hide a label rotation; raw known-source EVM remains the primary metric.")
    return result, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--tx-bits", required=True, type=Path)
    parser.add_argument("--rx-symbols", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--interval-csv", type=Path)
    args = parser.parse_args()
    if args.tx_bits.stat().st_size > MAX_BITS or args.rx_symbols.stat().st_size > MAX_SYMBOLS * 8:
        parser.error("calibration files exceed bounded diagnostic size")
    if args.rx_symbols.stat().st_size % 8:
        parser.error("RX I/Q file has an incomplete float32 complex sample")
    bits = np.fromfile(args.tx_bits, dtype=np.uint8)
    iq = np.fromfile(args.rx_symbols, dtype="<c8")
    try:
        result, rows = analyze(json.loads(args.probe.read_text()), bits, iq)
    except ValueError as error:
        parser.error(str(error))
    result["inputs"] = {name: dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                        for name, path in (("probe", args.probe), ("tx_bits", args.tx_bits), ("rx_symbols", args.rx_symbols))}
    text = json.dumps(result, indent=2, allow_nan=False) + "\n"
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end="")
    if args.interval_csv:
        with args.interval_csv.open("w", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)


if __name__ == "__main__":
    main()
