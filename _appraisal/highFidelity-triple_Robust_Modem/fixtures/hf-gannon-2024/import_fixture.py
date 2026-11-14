#!/usr/bin/env python3
"""Copy two verified DigitalRF columns losslessly; no signal processing.

Requires h5py and NumPy for HDF5 decoding only. The receiver and all replay
interpolation/normalization remain native C++.
"""

import argparse
import hashlib
import json
from pathlib import Path

import h5py
import numpy as np


UUID = "0e51e045ca1d42868f3c9cd483112bd7"
FREQUENCIES_MHZ = [2.5, 3.33, 5.0, 7.85, 10.0, 14.67, 15.0, 20.0, 25.0]
KNOTS = 36000
SOURCE_FILES = {
    17: (1715360400, "8397dc68dcae74222252ddc93162e331a96931d12db9a9e656af031c361dffc5"),
    20: (1715371200, "5b9e38b1319cb172d1c6c161b28960e10607d69b951593cd1700df91517173c7"),
}
METADATA_HASHES = {
    "drf_properties.h5": "bd1b62288f4e3e920bf862a9c100590266baa6fe3d25b738e59b81f982bb8ab7",
    "metadata@1715299200.h5": "8f2466c794c042c4ab591697666d8dabf267fa848fe955efbe054e5acd4047c3",
}
METADATA_MEMBERS = {
    "drf_properties.h5": "data/psws_grapeDRF/ch0_W2NAF_2024-05-10/drf_properties.h5",
    "metadata@1715299200.h5": "data/psws_grapeDRF/ch0_W2NAF_2024-05-10/metadata/2024-05-10T00-00-00/metadata@1715299200.h5",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def text(value):
    return value.decode("ascii") if isinstance(value, bytes) else str(value)


def import_fixture(source, output):
    for name, expected in METADATA_HASHES.items():
        require(digest(source / name) == expected, f"Unexpected source metadata: {name}")
    with h5py.File(source / "metadata@1715299200.h5") as metadata:
        group = metadata["17152992000"]
        require(text(group["uuid_str"][()]) == UUID, "Wrong metadata stream UUID")
        require(text(group["callsign"][()]) == "W2NAF", "Wrong station")
        require(np.array_equal(group["center_frequencies"][...], FREQUENCIES_MHZ),
                "Wrong nine-channel frequency map")
    with h5py.File(source / "drf_properties.h5") as properties:
        for field, expected in {"sample_rate_numerator": 10, "sample_rate_denominator": 1,
                                "num_subchannels": 9, "is_complex": 1,
                                "is_continuous": 1, "file_cadence_millisecs": 3600000}.items():
            require(properties.attrs[field] == expected, f"Wrong DRF property {field}")
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for hour, (epoch, source_sha) in SOURCE_FILES.items():
        filename = f"rf@{epoch}.000.h5"
        source_path = source / "rf" / filename
        require(digest(source_path) == source_sha, f"Source hash mismatch: {filename}")
        with h5py.File(source_path) as data_file:
            data = data_file["rf_data"]
            require(data.shape == (KNOTS, 9) and data.dtype.descr == [("r", "<i2"), ("i", "<i2")],
                    "Wrong raw shape or I/Q type")
            require(text(data.attrs["uuid_str"]) == UUID, "Wrong raw stream UUID")
            require(text(data.attrs["epoch"]) == "1970-01-01T00:00:00Z", "Wrong epoch")
            for field, expected in {"sample_rate_numerator": 10, "sample_rate_denominator": 1,
                                    "num_subchannels": 9, "is_complex": 1,
                                    "is_continuous": 1, "file_cadence_millisecs": 3600000}.items():
                require(data.attrs[field] == expected, f"Wrong raw attribute {field}")
            require(np.array_equal(data_file["rf_data_index"][...], [[epoch * 10, 0]]),
                    "Gapped or unexpected DigitalRF sample index")
            raw = data[:, 4]
        # Every int16 integer is represented exactly in IEEE-754 binary64.
        # Preserve signed I/Q order and all zeros. Do not normalize, repair,
        # interpolate, remove a carrier, or unwrap phase here.
        pairs = np.empty((KNOTS, 2), dtype="<f8")
        pairs[:, 0], pairs[:, 1] = raw["r"], raw["i"]
        content = pairs.tobytes(order="C")
        output_name = f"w2naf-20240510-{hour:02d}00-col4.f64iq"
        (output / output_name).write_bytes(content)
        ri, rq = raw["r"].astype(np.int64), raw["i"].astype(np.int64)
        records.append({
            "utc_hour": hour, "utc_first_sample_epoch_seconds": epoch,
            "source_member": f"data/psws_grapeDRF/ch0_W2NAF_2024-05-10/2024-05-10T00-00-00/{filename}",
            "source_hdf5_sha256": source_sha,
            "fixture": output_name, "fixture_sha256": hashlib.sha256(content).hexdigest(),
            "fixture_bytes": len(content), "knots": KNOTS,
            "integer_iq_energy_sum": int(np.sum(ri * ri + rq * rq)),
            "exact_zero_pairs": int(np.count_nonzero((ri == 0) & (rq == 0))),
            "int16_rail_components": int(np.count_nonzero((ri == -32768) | (ri == 32767)) +
                                         np.count_nonzero((rq == -32768) | (rq == 32767))),
        })
    manifest = {
        "schema_version": 1, "dataset_doi": "10.5281/zenodo.20466479",
        "dataset_url": "https://zenodo.org/records/20466479",
        "license": "CC-BY-4.0", "station": "W2NAF", "stream_uuid": UUID,
        "receiver_name": "KA9Q_0_WWV", "nominal_center_frequency_hz": 10000000,
        "zero_based_column": 4, "sample_rate_hz": 10,
        "format": "headerless little-endian IEEE-754 binary64 real,imag pairs",
        "normalization_in_file": "none; raw int16 values converted exactly",
        "native_normalization": "one joint RMS of both complete selected hours; no per-hour or per-window renormalization",
        "last_knot_seconds_from_start": 3599.9,
        "metadata_sha256": METADATA_HASHES, "metadata_members": METADATA_MEMBERS,
        "records": records,
        "limits": [
            "Observed narrowband receiver voltage, not a noise-free or broadband channel impulse response.",
            "WWV and WWVH share 10 MHz; transmitter/path contributions are not separated.",
            "No specific receiver clock, gain, noise floor or absolute propagation calibration is established.",
            "The interleaved one-channel grape19 minute records and their metadata are excluded.",
            "Recorded receiver noise/interference becomes signal-proportional under multiplication replay.",
        ],
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_directory", type=Path, help="Verified w2naf-may10 directory")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    print(json.dumps(import_fixture(args.source_directory, args.output), indent=2))
