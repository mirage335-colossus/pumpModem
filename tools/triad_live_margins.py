#!/usr/bin/env python3
"""Physical-cable empirical admission experiments; never opens an audio device.

Prepare finite keyed stimuli, record with triad_audio_capture, then analyze the
untouched PCM with the maintained FFT receiver and separately calibrated gates.
Public chirps condition transport timing/clock; this is not blind acquisition.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import platform
from pathlib import Path
import subprocess
import time

import numpy as np
import scipy
from scipy import stats
import triad_live_model as model
import triad_live_waveforms as wave

FS = 48000
TEMPLATE_GAIN = .001
CASES = {
    "baseline": dict(rate=3600, added=1, carrier=1725., seconds=8.),
    "edge": dict(rate=24000, added=1, carrier=15500., seconds=8.),
    # Native minimum complexity rounds the eight-second request up to one
    # complete 10.8-second tile. Eleven seconds would unnecessarily add a tile.
    "spreading": dict(rate=36, added=100, carrier=1725., seconds=8.),
    "stress": dict(rate=3600, added=1, carrier=1725., seconds=8., stress=True),
}


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def read_json(path):
    return json.loads(Path(path).read_text())


def preprocessing_sources():
    return {Path(module.__file__).name: digest(module.__file__) for module in (model, wave)}


def verify_fixture(directory, fixture, *, require_complete=True):
    if digest(directory/"basis.npy") != fixture["basis_sha256"]:
        raise ValueError("frozen public basis changed")
    if "signal_sha256" not in fixture:
        if require_complete:
            raise ValueError("legacy pilot fixture lacks complete provenance; regenerate for calibration")
        return
    for bit, expected in enumerate(fixture["signal_sha256"]):
        if digest(directory/f"signal-{bit}.f32") != expected:
            raise ValueError("frozen source waveform changed")
    if fixture["preprocessing_sources"] != preprocessing_sources():
        raise ValueError("audio preprocessing source changed; prepare a new fixture")


def fresh(path):
    path = Path(path)
    path.mkdir(parents=True, exist_ok=False)
    return path


def fixed_noise(profile, count, seed):
    """Independent finite-record surrogate residual, not a whiteness assertion."""
    return model.simulate_audio_profile(np.zeros(count), profile, seed=seed,
        include_noise=True, production_s16=False)


def convert(pcm, fixture):
    return wave.from_audio(pcm, FS, fixture["receiver_sample_rate"], fixture["carrier"],
        occupied_bandwidth_hz=fixture["bandwidth"], expected_samples=fixture["receiver_samples"])


def make_fixture(directory, name, binary, profile, profile_hash, epoch, key_seed, noise_rms):
    directory.mkdir(parents=True, exist_ok=False)
    case = dict(CASES[name])
    exports = []
    for bit in (0, 1):
        dest = directory / f"native-{bit}"
        argv = [str(binary), "--mode", "export", "--seconds", str(case["seconds"]),
            "--channel", "none", "--phase-diffusion", "0", "--rf-hz", "0", "--nominal-if-hz", "0",
            "--basis", "two-ray" if case.get("stress") else "flat", "--threads", "1",
            "--seed", str(key_seed + 100), "--key-seed", str(key_seed), "--epoch", str(epoch),
            "--signal-bit", str(bit), "--rate", str(case["rate"]), "--added", str(case["added"]),
            "--margin", "3" if case.get("stress") else "1", "--output", str(dest)]
        if case.get("stress"):
            argv += ["--sample-rate", "4800", "--spread-preset", "hf-worst", "--selective-perturbation", "each-65",
                "--tap-delay-ms", "21", "--tap-doppler-hz", "90"]
        start = time.perf_counter()
        run = subprocess.run(argv, capture_output=True, text=True, timeout=120)
        write_json(directory / f"export-{bit}.json", dict(argv=argv, returncode=run.returncode,
            stdout=run.stdout, stderr=run.stderr, wall_seconds=time.perf_counter()-start))
        if run.returncode:
            raise RuntimeError(run.stderr)
        exports.append(wave.load_export(dest))
    native = exports[0]
    m, nfs = native["manifest"], native["sample_rate"]
    fs = min(FS, 2 * nfs)
    width = m["occupied_bandwidth_hz"]
    basis = native["basis"]
    candidates = m["candidateparams"]
    chosen = [max((i for i, c in enumerate(candidates) if c["bit"] == b),
                  key=lambda i: candidates[i]["tap_count"]) for b in (0, 1)]
    public = []
    tail_details = []
    source_audio = []
    for bit in (0, 1):
        audio, bridge = wave.to_audio(exports[bit]["signal"], nfs, FS, case["carrier"],
            occupied_bandwidth_hz=width, fixed_gain=1., max_peak=1e6)
        audio.astype("<f4").tofile(directory / f"signal-{bit}.f32")
        source_audio.append(len(audio))
        cols = []
        for col in range(candidates[chosen[bit]]["tap_count"]):
            a, _ = wave.to_audio(basis[chosen[bit], :, col], nfs, FS, case["carrier"],
                occupied_bandwidth_hz=width, fixed_gain=TEMPLATE_GAIN, max_peak=.9)
            filtered = model.apply_audio_profile(a, profile, full_tail=True)
            z, _, _ = wave.from_audio(filtered, FS, fs, case["carrier"], occupied_bandwidth_hz=width)
            energy = np.abs(z.astype(np.complex128))**2
            remaining = np.cumsum(energy[::-1], dtype=np.float64)[::-1]
            minimum = math.ceil(len(a) / FS * fs)
            allowable = np.flatnonzero(remaining <= 1e-10 * remaining[0])
            end = max(minimum, int(allowable[0]) if len(allowable) else len(z))
            tail_details.append(dict(bit=bit, column=col, full_samples=len(z), kept_samples=end,
                discarded_energy_fraction=float(remaining[end] / remaining[0]) if end < len(z) else 0.))
            cols.append(z[:end])
        if len(cols) == 1:
            cols.append(cols[0].copy())  # explicitly dependent flat-channel columns
        public.append(cols)
    # Earliest bank is 5ms before the nominal source. Its initial discarded
    # prefix must contain negligible energy, and the complete retained tail is
    # available at every searched lag. No observed payload influences this bank.
    half = max(1, round(.005 * fs))
    prefix = half
    end = max(len(col) for cols in public for col in cols)
    n = end + 2 * half + math.ceil(.1 * fs)
    bank = np.zeros((2, n, 2), np.complex128)
    prefix_losses = []
    for bit, cols in enumerate(public):
        for j, col in enumerate(cols):
            loss = float(np.sum(abs(col[:prefix])**2) / np.sum(abs(col)**2))
            if loss > 1e-10:
                raise ValueError("earliest public timing shift would remove non-negligible template energy")
            prefix_losses.append(loss)
            bank[bit, :len(col)-prefix, j] = col[prefix:]
    np.save(directory / "basis.npy", bank)
    audio_count = max(max(source_audio), math.ceil(n / fs * FS))
    fixture = dict(schema="triad-live-margin-fixture-v1", id=name, **case,
        epoch=epoch, key_seed=key_seed, native_sample_rate=nfs,
        receiver_sample_rate=fs, receiver_samples=n, payload_audio_samples=audio_count,
        bandwidth=width, lags=list(range(2 * half + 1)),
        public_key=dict(arrival_seconds=candidates[chosen[0]]["arrival_seconds"]-half/fs,
            frequency_hz=0., clock_ppm=0., epoch_offset_seconds=0, frequency_drift_hz_per_second=0.),
        frequency_offsets_hz=[-.06,-.03,0.,.03,.06], template_gain=TEMPLATE_GAIN,
        flat_columns_duplicated=not case.get("stress", False), tail_approximation=tail_details,
        discarded_prefix_energy_fractions=prefix_losses, profile_sha256=profile_hash,
        noise_rms=noise_rms, basis_sha256=digest(directory/"basis.npy"),
        signal_sha256=[digest(directory/f"signal-{b}.f32") for b in (0,1)],
        preprocessing_sources=preprocessing_sources(),
        source_waveform_samples=source_audio,
        useful_duration_seconds=m["actual_elapsed_seconds"],
        nominal_source_energy_seconds=[e["manifest"]["channel_signal_energy_samples"]/nfs for e in exports],
        source_generation="native keyed complete waveform; all selected sampled propagation remains enabled",
        scope="marker-conditioned epoch/clock; residual timing and CFO are searched, no payload alignment")
    # Independent fixed model draw: sets a scale, not an assertion of white
    # Gaussian noise, and is frozen before physical calibration or signal data.
    rng = np.random.default_rng(912018)
    noise = rng.normal(0., noise_rms, audio_count).astype(np.float32)
    noise_out = model.simulate_audio_profile(noise, profile, seed=912019, production_s16=True)
    y, valid, _ = convert(noise_out, fixture)
    fixture["reference_variance"] = max(1e-30, float(np.mean(abs(y[valid])**2)))
    fixture["reference_variance_method"] = "fixed independently seeded frozen-profile null, not actual trial samples"
    fixture["preprocessing_identity"] = hashlib.sha256(json.dumps(fixture, sort_keys=True).encode()).hexdigest()
    write_json(directory / "fixture.json", fixture)
    return fixture


def prepare(args):
    start = time.perf_counter()
    if args.noise_rms < 0 or not math.isfinite(args.noise_rms) or (args.phase != "floor" and args.noise_rms == 0):
        raise ValueError("non-floor experiments require finite positive pre-DAC noise RMS")
    if min(args.trials,args.calibration_trials) < 1 or args.null_trials < 0:
        raise ValueError("trial counts must be positive, with nonnegative null count")
    out = fresh(args.output)
    profile = read_json(args.profile)
    profile_hash = digest(args.profile)
    write_json(out / "profile.json", profile)
    names = args.cases.split(",")
    if not names or any(n not in CASES for n in names) or len(set(names)) != len(names):
        raise ValueError("cases must be a unique comma-list of baseline,edge,spreading,stress")
    noise_rms = 0. if args.phase == "floor" else args.noise_rms
    epoch = args.epoch if args.epoch is not None else int(time.time())
    fixtures_dir = Path(args.fixtures).resolve() if args.fixtures else out / "fixtures"
    if args.fixtures:
        fixtures = {name: read_json(fixtures_dir/name/"fixture.json") for name in names}
        for f in fixtures.values():
            if f["profile_sha256"] != profile_hash or f["noise_rms"] != noise_rms:
                raise ValueError("cached fixture profile/noise condition differs")
            verify_fixture(fixtures_dir/f["id"],f,require_complete=args.phase!="pilot")
        epoch = fixtures[names[0]]["epoch"]
    else:
        fixtures_dir.mkdir()
        fixtures = {name: make_fixture(fixtures_dir/name, name, Path(args.binary).resolve(),
            profile, profile_hash, epoch, args.key_seed, noise_rms) for name in names}
    records = []
    def nulls(count, calibration):
        for _ in range(count):
            records.append(dict(case=None, truth=None, gain=0., cn0_db_hz=None, calibration=calibration))
    if args.phase == "calibration":
        nulls(args.calibration_trials, True)
    else:
        if args.phase == "pilot":
            levels = [-4.,2.,8.,14.]
            trials = 2
            nulls(2, False)
        elif args.phase == "floor":
            levels = list(map(float,args.gains.split(",")))
            trials = args.trials
            nulls(args.calibration_trials, True)
            nulls(args.null_trials, False)
        else:
            levels = list(map(float,args.levels.split(",")))
            trials = args.trials
            nulls(args.null_trials, False)
        for name in names:
            for level in levels:
                for trial in range(trials):
                    gain = level if args.phase == "floor" else math.sqrt(2*noise_rms**2/FS*10**(level/10))
                    records.append(dict(case=name, truth=trial%2, gain=gain,
                        cn0_db_hz=None if args.phase == "floor" else level, calibration=False))
    rng = np.random.default_rng(args.seed)
    rng.shuffle(records)
    max_count = max(f["payload_audio_samples"] for f in fixtures.values())
    position, peak = 0, 0.
    with (out/"stimulus.f32").open("wb") as stream:
        for i, record in enumerate(records):
            record["noise_seed"] = int(args.seed * 10000 + i)
            count = fixtures[record["case"]]["payload_audio_samples"] if record["case"] else max_count
            payload = np.random.default_rng(record["noise_seed"]).normal(0.,noise_rms,count).astype(np.float32)
            if record["case"]:
                s = np.fromfile(fixtures_dir/record["case"]/f"signal-{record['truth']}.f32", dtype="<f4")
                payload[:len(s)] += record["gain"] * s
                q = model.quantize_production_playback(record["gain"]*s)
                signal_energy = float(np.sum((record["gain"]*s.astype(np.float64))**2))
                record["signal_only_quantization_diagnostic"] = dict(
                    nonzero_samples=int(np.count_nonzero(q)),samples=len(q),
                    distinct_codes=int(len(np.unique(q))),
                    retained_energy_fraction=float(np.sum(q*q))/signal_energy if signal_energy else 0.,
                    scope="evaluation only; receiver never receives this clean signal, especially when noise dithers the actual composite")
            tx, markers = model.wrap_with_markers(payload, FS, seed=args.seed+100+i*2)
            if np.max(abs(tx)) >= .8:
                raise ValueError("fixed composite exceeds conservative playback headroom")
            peak = max(peak,float(np.max(abs(tx))))
            record.update(index=i,offset=position,samples=len(tx),markers=markers,
                stimulus_sha256=hashlib.sha256(tx.astype("<f4").tobytes()).hexdigest())
            tx.astype("<f4").tofile(stream)
            position += len(tx)
    plan = dict(schema="triad-live-margin-plan-v1", phase=args.phase, seed=args.seed,
        epoch=epoch, fixtures_directory=str(fixtures_dir.resolve()), cases=names,
        profile_sha256=profile_hash, noise_rms=noise_rms, records=records, samples=position,
        profile_copy_sha256=digest(out/"profile.json"),
        fixture_json_sha256={name:digest(fixtures_dir/name/"fixture.json") for name in names},
        seconds=position/FS, peak=peak, stimulus_sha256=digest(out/"stimulus.f32"),
        binary_sha256=digest(args.binary), source_sha256=digest(__file__),
        environment=dict(numpy=np.__version__,scipy=scipy.__version__),
        preparation_wall_seconds=time.perf_counter()-start,
        noise_convention="nominal unit total signal power / one-sided pre-DAC real noise PSD 2*rms^2/Fs; not receiver-input AWGN",
        physical_action="not performed; capture the complete playlist with the production native adapter")
    write_json(out/"plan.json", plan)
    print(json.dumps({k:plan[k] for k in ("phase","cases","seconds","peak","preparation_wall_seconds")}))


def validate_capture(metadata, count):
    if metadata.get("status") != "complete" or metadata.get("failure"):
        raise ValueError("capture incomplete")
    if any(metadata.get("discontinuity", {}).get(k, 1) for k in ("capture","playback")):
        raise ValueError("discontinuous capture")
    backend = metadata.get("backend")
    native_format = metadata.get("audio_format", {}).get("capture", {})
    if backend is None and {"logical_rate", "hardware_rate", "logical_channels",
                            "hardware_channels", "selected_capture_channel"}.issubset(native_format):
        # Existing production-native receipts predate the backend discriminator.
        fmt = native_format
        expected = dict(logical_rate=FS,hardware_rate=FS,hardware_channels=2,
                        logical_channels=1,selected_capture_channel=0)
        if any(fmt.get(k) != v for k,v in expected.items()):
            raise ValueError("unexpected physical capture format")
        samples = metadata["capture"]["logical_samples"]
    elif backend == "alsa-utils-raw-stereo-v1" or backend is None:
        # The opt-in ALSA-utils route has an independent raw stereo receipt.
        # Current receipts declare this backend; older ones are identifiable
        # by the two raw endpoint reports. Do not rewrite them to resemble the
        # native adapter. Both
        # endpoint negotiation records and the fixed channel selection are
        # required because this path has no logical mono conversion metadata.
        formats = metadata.get("audio_format")
        if not isinstance(formats, dict):
            raise ValueError("unexpected physical capture format")
        required = {"access": "RW_INTERLEAVED", "format": "S16_LE",
                    "channels": "2", "rate": str(FS)}
        for endpoint in ("capture", "playback"):
            observed = formats.get(endpoint)
            direction = endpoint.upper()
            exact_rate = observed.get("exact rate") if isinstance(observed, dict) else None
            if not isinstance(observed, dict) or observed.get("stream") != direction \
                    or any(observed.get(key) != value for key,value in required.items()) \
                    or not isinstance(exact_rate, str) or not exact_rate.split() \
                    or exact_rate.split()[0] != str(FS):
                raise ValueError("unexpected physical capture format")
        requested = metadata.get("requested", {})
        capture = metadata.get("capture", {})
        capture_device = requested.get("capture_device", "")
        playback_device = requested.get("playback_device", "")
        if requested.get("exclusive") is not True \
                or not capture_device.startswith("hw:") \
                or playback_device != capture_device \
                or requested.get("capture_channel") != 0 \
                or capture.get("selected_channel") != 0 \
                or capture.get("logical_rate") != FS:
            raise ValueError("unexpected physical capture format")
        samples = capture.get("logical_samples")
    else:
        raise ValueError("unsupported physical capture backend")
    if samples != count:
        raise ValueError("capture metadata/sample count mismatch")


def interval(k,n):
    return [float(stats.beta.ppf(.025,k,n-k+1)) if k else 0.,
            float(stats.beta.ppf(.975,k+1,n-k)) if k<n else 1.]


def analyze(args):
    from triad_live_receiver import EmpiricalReceiver, empirical_decision, evaluate
    start = time.perf_counter()
    src, out = Path(args.prepared), fresh(args.output)
    plan, profile = read_json(src/"plan.json"), read_json(src/"profile.json")
    if digest(src/"stimulus.f32") != plan["stimulus_sha256"]:
        raise ValueError("prepared stimulus changed")
    if "profile_copy_sha256" in plan and digest(src/"profile.json") != plan["profile_copy_sha256"]:
        raise ValueError("frozen copied profile changed")
    if "profile_copy_sha256" not in plan and plan["phase"] != "pilot":
        raise ValueError("unbound legacy profile allowed only for exploratory pilot")
    # Profile was copied through JSON; compare its original frozen identity via
    # fixtures, and bind the exact copied model to this analysis as well.
    tx = np.memmap(src/"stimulus.f32",dtype="<f4",mode="r")
    rx = np.memmap(args.capture,dtype="<f4",mode="r")
    capture_metadata = read_json(args.capture_metadata)
    validate_capture(capture_metadata,len(rx))
    if float(np.max(abs(rx))) >= .999:
        raise ValueError("near-fullscale physical capture invalidates unclipped campaign")
    fixtures_dir = Path(plan["fixtures_directory"])
    fixtures = {n:read_json(fixtures_dir/n/"fixture.json") for n in plan["cases"]}
    receivers = {}
    for name,f in fixtures.items():
        if digest(fixtures_dir/name/"basis.npy") != f["basis_sha256"] or f["profile_sha256"] != plan["profile_sha256"]:
            raise ValueError("frozen public fixture changed")
        if "fixture_json_sha256" in plan and digest(fixtures_dir/name/"fixture.json") != plan["fixture_json_sha256"][name]:
            raise ValueError("frozen fixture metadata changed")
        verify_fixture(fixtures_dir/name,f,require_complete=plan["phase"]!="pilot")
        basis = np.load(fixtures_dir/name/"basis.npy")
        valid = np.ones(f["receiver_samples"],bool)
        # Same declared edge validity on every record; no observed-data mask.
        _,valid,_ = convert(np.zeros(f["payload_audio_samples"]),f)
        receivers[name] = EmpiricalReceiver(basis,f["receiver_sample_rate"],np.asarray(f["lags"]),
            key=f["public_key"],valid=valid,reference_variance=f["reference_variance"],
            frequency_offsets=tuple(f["frequency_offsets_hz"]),precision="fp32",
            preprocessing_identity=json.dumps(dict(fixture=f["preprocessing_identity"],
                sources=preprocessing_sources(),campaign_source=digest(__file__),
                numpy=np.__version__,scipy=scipy.__version__),sort_keys=True))
    # Determine one transport offset from the first independent public marker.
    # Every later record receives its own two-marker rate/offset fit on a local
    # capture interval; no payload correlation participates in synchronization.
    first = plan["records"][0]
    marker = first["markers"]["markers"][0]
    initial = model._marker_delay(np.asarray(tx[:first["samples"]]),np.asarray(rx[:first["samples"]+2*FS]),FS,marker)
    delay = initial["delay_samples"]
    state = model.AudioModelState(profile)
    rows, alignments = [], []
    for record in plan["records"]:
        offset,n = record["offset"],record["samples"]
        chunk = np.asarray(tx[offset:offset+n])
        if hashlib.sha256(chunk.tobytes()).hexdigest() != record["stimulus_sha256"]:
            raise ValueError("record input changed")
        # Keep enough room for local affine drift and a full marker search.
        a = max(0,int(offset+delay-FS))
        b = min(len(rx),int(offset+delay+n+FS))
        aligned,mask,alignment = model.align_capture(chunk,np.asarray(rx[a:b]),FS,record["markers"]["markers"])
        delay = a+alignment["offset_samples"]-offset
        alignments.append(dict(index=record["index"],**alignment))
        # Continuous deterministic simulation across playlist records. Only its
        # stationary finite-record residual uses independent chunks.
        predicted = state.process(model.quantize_production_playback(chunk))
        predicted += fixed_noise(profile,n,record["noise_seed"]+991000)
        p = record["markers"]["payload"]
        payload_a = p["start"]
        names = [record["case"]] if record["case"] else plan["cases"]
        for name in names:
            f = fixtures[name]
            payload_b = payload_a+f["payload_audio_samples"]
            if not mask[payload_a:payload_b].all():
                raise ValueError("incomplete aligned payload")
            actual_pcm = aligned[payload_a:payload_b]
            predicted_pcm = predicted[payload_a:payload_b]
            actual,_,ddc = convert(actual_pcm,f)
            simulated,_,_ = convert(predicted_pcm,f)
            pair = {"physical":receivers[name].score(actual),"simulated":receivers[name].score(simulated)}
            for kind,score in pair.items():
                rows.append(dict(index=record["index"],case=name,kind=kind,truth=record["truth"],
                    gain=record["gain"],cn0_db_hz=record["cn0_db_hz"],calibration_record=record["calibration"],
                    **score))
            e = float(np.sum(abs(simulated)**2))
            write_json(out/f"record-{record['index']:04d}-{name}.json",dict(scores=pair,ddc=ddc,
                total_energy_ratio_db=10*math.log10(float(np.sum(abs(actual)**2))/e),
                composite_prediction_residual_ratio_db=10*math.log10(float(np.sum(abs(actual-simulated)**2))/e),
                scope="energy/residual includes transmitted noise and independent hardware/model residuals; not isolated signal margin"))
        print(f"scored {record['index']+1}/{len(plan['records'])}",flush=True)
    gates = read_json(Path(args.calibration)/"gates.json") if args.calibration else {}
    for name,receiver in receivers.items():
        nulls = [r["score"] for r in rows if r["case"]==name and r["kind"]=="physical" and r["calibration_record"]]
        if nulls:
            gates[name] = receiver.freeze_calibration(nulls,alpha=args.alpha)
    write_json(out/"gates.json",gates)
    for row in rows:
        if row["case"] in gates and not row["calibration_record"]:
            row["decision"] = empirical_decision(row,gates[row["case"]])
            row["classification"] = evaluate(row["decision"],row["truth"])
    summaries = []
    grouping = sorted(set((r["case"],r["kind"],r["gain"],r["cn0_db_hz"]) for r in rows if not r["calibration_record"]))
    for name,kind,gain,cn0 in grouping:
        selected = [r for r in rows if (r["case"],r["kind"],r["gain"],r["cn0_db_hz"])==(name,kind,gain,cn0) and not r["calibration_record"]]
        counts = {k:0 for k in ("correct_admission","wrong_admission","miss","false_admission","correct_rejection")}
        for r in selected:
            key = r.get("classification","uncalibrated")
            counts[key] = counts.get(key,0)+1
        summaries.append(dict(case=name,kind=kind,gain=gain,cn0_db_hz=cn0,trials=len(selected),counts=counts,
            intervals_95={k:interval(v,len(selected)) for k,v in counts.items()}))
    write_json(out/"results.json",dict(schema="triad-live-margin-results-v1",rows=rows,summaries=summaries,
        alignments=alignments,wall_seconds=time.perf_counter()-start,plan_sha256=digest(src/"plan.json"),
        capture_sha256=digest(args.capture),profile_copy_sha256=digest(src/"profile.json"),
        source_sha256=digest(__file__),
        capture_metadata_sha256=digest(args.capture_metadata),
        synthetic_only=bool(capture_metadata.get("synthetic_only",False)),
        environment=dict(python=platform.python_version(),numpy=np.__version__,scipy=scipy.__version__,
            torch=__import__('torch').__version__,platform=platform.platform(),machine=platform.machine()),
        scope="offline maintained receiver on supplied PCM with empirical per-record gate; physical evidence only when synthetic_only=false and device receipt verified; marker-conditioned timing/clock and epoch; not real-time or operational rare-alarm qualification",
        gate_limit="rank calibration is marginal under exchangeable null records; fixed-threshold FAP requires independent null validation; no suite-wide multiplicity claim"))
    print(json.dumps(summaries,indent=2))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command",required=True)
    a = sub.add_parser("prepare")
    a.add_argument("--output",required=True); a.add_argument("--binary",default="build/dev-cli/triad_long")
    a.add_argument("--profile",required=True); a.add_argument("--phase",choices=("pilot","calibration","validation","floor"),required=True)
    a.add_argument("--seed",type=int,default=94101); a.add_argument("--key-seed",type=int,default=913711)
    a.add_argument("--epoch",type=int); a.add_argument("--fixtures"); a.add_argument("--cases",default="baseline")
    a.add_argument("--noise-rms",type=float,default=.03); a.add_argument("--levels",default="-2,2,6,10")
    a.add_argument("--gains",default=".000003,.000006,.000009,.000012,.000016,.000024")
    a.add_argument("--trials",type=int,default=8); a.add_argument("--null-trials",type=int,default=32)
    a.add_argument("--calibration-trials",type=int,default=39)
    b = sub.add_parser("analyze")
    b.add_argument("--prepared",required=True); b.add_argument("--capture",required=True)
    b.add_argument("--capture-metadata",required=True); b.add_argument("--output",required=True)
    b.add_argument("--calibration"); b.add_argument("--alpha",type=float,default=.05)
    args = p.parse_args()
    (prepare if args.command=="prepare" else analyze)(args)


if __name__ == "__main__":
    main()
