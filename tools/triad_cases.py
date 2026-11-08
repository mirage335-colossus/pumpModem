"""Strict parser for the manually maintained Robust abbreviated cases.

The markdown table is intentionally small and human editable. Parsing is kept
stdlib-only so malformed input is rejected before a numerical stack is loaded.
"""
from __future__ import annotations

import hashlib
import json
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CASES = ROOT / "_tests-RobustModem.md"

FIELDS = (
    "Encryption",
    "Helper Carrier Transmit Power",
    "Transmit Power",
    "Path loss",
    "Clock sync / Region / Offset / Audio error",
    "Base Oscillator",
    "Shift Oscillator",
    "Doppler/delay spread",
    "Additional frequency selective amplitude modulation",
    "Margin",
    "DSSS",
    "Spreading",
    "FHSS",
    "Symbol_burst",
    "Rate",
    "Shift",
)
_FIELD_LOOKUP = {name.casefold(): name for name in FIELDS}


class CaseParseError(ValueError):
    """Malformed manual case with its source line attached."""


def _error(line: int, message: str) -> CaseParseError:
    return CaseParseError(f"line {line}: {message}")


def _number(text: str, pattern: str, units: dict[str, float], label: str,
            line: int, *, minimum: float, maximum: float) -> float:
    match = re.fullmatch(pattern, text.strip(), flags=re.IGNORECASE)
    if not match:
        raise _error(line, f"invalid {label}: {text!r}")
    number = float(match.group(1))
    unit = match.group(2).casefold()
    value = number * units[unit]
    if not math.isfinite(value) or not minimum <= value <= maximum:
        raise _error(line, f"{label} must be finite and in [{minimum:g},{maximum:g}]")
    return value


def _power(text: str, label: str, line: int, *, zero_ok: bool) -> float:
    value = _number(text, r"([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?)\s*(W|mW|kW)",
                    {"w": 1, "mw": .001, "kw": 1000}, label, line,
                    minimum=0 if zero_ok else 1e-300, maximum=1e12)
    if not zero_ok and value == 0:
        raise _error(line, f"{label} must be positive")
    return value


def _rate(text: str, line: int) -> float:
    return _number(text, r"([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?)\s*(Hz)",
                   {"hz": 1}, "Rate", line, minimum=1e-12, maximum=30e6)


def _shift(text: str, line: int) -> float:
    return _number(text, r"([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?)\s*(Hz|kHz|MHz|GHz)",
                   {"hz": 1, "khz": 1e3, "mhz": 1e6, "ghz": 1e9},
                   "Shift", line, minimum=0, maximum=1e15)


def _parse_value(name: str, raw: str, line: int) -> tuple[object, list[str]]:
    value = raw.strip()
    if not value:
        raise _error(line, f"empty value for {name}")
    if name == "Encryption":
        if value.casefold() not in ("yes", "no"):
            raise _error(line, "Encryption must be Yes or No")
        enabled = value.casefold() == "yes"
        return enabled, ["--encrypt", "on" if enabled else "off"]
    if name in ("Helper Carrier Transmit Power", "Transmit Power"):
        watts = _power(value, name, line, zero_ok=name.startswith("Helper"))
        if name.startswith("Helper"):
            return watts, ["--helper", "off" if watts == 0 else "on",
                           "--helper-power-w", _fmt(watts)]
        return watts, ["--data-power-w", _fmt(watts)]
    if name == "Path loss":
        db = _number(value, r"([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?)\s*(dB)",
                     {"db": 1}, name, line, minimum=0, maximum=400)
        return db, ["--path-loss-db", _fmt(db)]
    if name == "Clock sync / Region / Offset / Audio error":
        if value != "Default several seconds receiver search space":
            raise _error(line, f"unknown {name} setting: {value!r}")
        return value, ["--timing-policy", "broad"]
    if name in ("Base Oscillator", "Shift Oscillator"):
        aliases = {
            "free-running crystal": "crystal",
            "cb ssb / ats mini v4 ssb: +/-20 ppm/radio": "cb-ssb",
            "gpsdo ocxo": "gpsdo-ocxo",
            "gpsdo: ocxo": "gpsdo-ocxo",
            "gpsdo hobbyist xo (no oven)": "gpsdo-xo",
            "gpsdo: hobbyist xo (no oven)": "gpsdo-xo",
            "gpsdo tcxo": "gpsdo-tcxo",
            "gpsdo: tcxo (no oven)": "gpsdo-tcxo",
            "ic-7100 +/-0.5 ppm/radio": "ic-7100",
            "baseband clock": "baseband-clock",
        }
        preset = aliases.get(value.casefold())
        allowed = {"crystal", "cb-ssb", "gpsdo-ocxo", "gpsdo-tcxo", "gpsdo-xo", "ic-7100", "baseband-clock"}
        if preset not in allowed or (name == "Base Oscillator" and preset == "baseband-clock"):
            raise _error(line, f"unknown {name} setting: {value!r}")
        option = "--base-oscillator" if name.startswith("Base") else "--shift-oscillator"
        return preset, [option, preset]
    if name == "Doppler/delay spread":
        spreads = {
            "none — 0 hz 0 ms": "none",
            "sub9 best 0.001hz 0.005ms": "sub9-best",
            "sub9 moderate 0.005hz 0.05ms": "sub9-moderate",
            "sub9 worst 0.05hz 5ms": "sub9-worst",
            "hf best 0.1hz 0.5ms": "hf-best",
            "hf moderate 10hz 6ms": "hf-moderate",
            "hf worst 30hz 7ms": "hf-worst",
            "aurora best 10hz 3ms": "aurora-best",
            "aurora worst 55hz 11ms": "aurora-worst",
            "eme focused 6hz 0.5ms": "eme-focused",
        }
        preset = spreads.get(value.casefold())
        if preset is None:
            raise _error(line, f"unknown {name} setting: {value!r}")
        return preset, ["--spread-preset", preset]
    if name == "Additional frequency selective amplitude modulation":
        if value.casefold() == "off":
            return "off", ["--selective-perturbation", "off"]
        match = re.fullmatch(r"Each\s*-\s*(1|5|10|25|65)%", value, re.IGNORECASE)
        if not match:
            raise _error(line, f"unknown {name} setting: {value!r}")
        modulation = "each-" + match.group(1)
        return modulation, ["--selective-perturbation", modulation]
    if name == "Margin":
        match = re.fullmatch(r"(\d+(?:\.\d+)?)x", value, re.IGNORECASE)
        if not match:
            raise _error(line, f"invalid Margin: {value!r}")
        margin = float(match.group(1))
        if not math.isfinite(margin) or not 0 < margin <= 1000:
            raise _error(line, "Margin must be in (0,1000]")
        return margin, ["--margin", _fmt(margin)]
    if name == "DSSS":
        if value.casefold() == "off":
            return False, []
        if value.casefold() == "interleave":
            return True, []
        raise _error(line, f"unknown DSSS setting: {value!r}")
    if name == "Spreading":
        match = re.fullmatch(r"(\d+)x", value, re.IGNORECASE)
        if not match:
            raise _error(line, f"invalid Spreading: {value!r}")
        count = int(match.group(1))
        if count > 1000:
            raise _error(line, "Spreading must be in [0,1000]")
        # DSSS coupling is checked after all fields have been collected.
        return count, []
    if name == "FHSS":
        if value.casefold() == "off":
            return {"enabled": False, "synthetic": False}, ["--fhss", "off"]
        if value.casefold() == "fake: fcc":
            return {"enabled": True, "synthetic": True,
                    "claim": "synthetic; no regulatory claim"}, ["--fhss", "on"]
        raise _error(line, f"unknown FHSS setting: {value!r}")
    if name == "Symbol_burst":
        if value.casefold() != "off":
            raise _error(line, f"unsupported Symbol_burst setting: {value!r}")
        return False, ["--burst-wait-tiles", "0"]
    if name == "Rate":
        rate = _rate(value, line)
        return rate, ["--rate", _fmt(rate)]
    if name == "Shift":
        hz = _shift(value, line)
        return hz, ["--rf-hz", _fmt(hz)]
    raise AssertionError(name)


def _fmt(value: float) -> str:
    return format(value, ".15g")


def _slug(value: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", value.casefold()).strip("-")
    return slug[:48] or "case"


def load_cases(path=DEFAULT_CASES) -> dict:
    """Read and validate the complete manual-case file on every invocation."""
    source = Path(path).resolve()
    data = source.read_bytes()
    text = data.decode("utf-8")
    digest = hashlib.sha256(data).hexdigest()
    parsed = []
    current = None

    def finish():
        nonlocal current
        if current is None:
            return
        missing = [field for field in FIELDS if field not in current["raw_fields"]]
        if missing:
            raise _error(current["heading_line"], "missing fields: " + ", ".join(missing))
        raw_fields = current["raw_fields"]
        fields = {}
        args = []
        for field in FIELDS:
            normalized, field_args = _parse_value(field, raw_fields[field][0], raw_fields[field][1])
            fields[field] = normalized
            args.extend(field_args)
        spreading = fields["Spreading"]
        dsss = fields["DSSS"]
        if (spreading == 0 and dsss) or (spreading >= 1 and not dsss):
            raise _error(raw_fields["Spreading"][1], "DSSS must be Off with Spreading 0x, or Interleave with Spreading Nx (N>=1)")
        if dsss:
            args.extend(["--added-symbols", str(spreading), "--added-interleave", "on"])
        else:
            args.extend(["--added-symbols", "1", "--added-interleave", "off"])
        scenario = "eme" if fields["Doppler/delay spread"] == "eme-focused" else "hf"
        # Keep the selected scenario explicit. Every physical/config field above
        # also has an explicit argument, overriding the scenario defaults.
        heading = current["heading"]
        configuration_hash = hashlib.sha256(json.dumps(fields, sort_keys=True,
            separators=(",", ":"), ensure_ascii=False).encode("utf-8")).hexdigest()
        index = len(parsed) + 1
        parsed.append({
            "id": f"{index:03d}-{_slug(heading)}-{configuration_hash[:10]}",
            "index": index,
            "heading": heading,
            "heading_line": current["heading_line"],
            "source_line": current["heading_line"],
            "raw_fields": {field: raw_fields[field][0] for field in FIELDS},
            "field_lines": {field: raw_fields[field][1] for field in FIELDS},
            "fields": fields,
            "configuration_hash": configuration_hash,
            "scenario": scenario,
            "native_args": ["--case", scenario, *args],
        })
        current = None

    for lineno, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or (stripped.startswith("<!--") and stripped.endswith("-->")):
            continue
        if line.startswith("- "):
            finish()
            heading = line[2:].strip()
            if not heading:
                raise _error(lineno, "case heading is empty")
            current = {"heading": heading, "heading_line": lineno, "raw_fields": {}}
            continue
        if current is None:
            raise _error(lineno, "unexpected content outside a case (expected heading, comment, or blank line)")
        if "\t" in line:
            pieces = line.split("\t")
            if len(pieces) != 2:
                raise _error(lineno, "field line must contain exactly one TAB delimiter")
            raw, label = pieces
        else:
            match = re.fullmatch(r"\s*(.*?)\s{2,}([^\t]+?)\s*", line)
            if not match:
                raise _error(lineno, "field line must be value<TAB>field or use a two-space delimiter")
            raw, label = match.groups()
        field = _FIELD_LOOKUP.get(label.strip().casefold())
        if field is None:
            raise _error(lineno, f"unknown field: {label.strip()!r}")
        if field in current["raw_fields"]:
            raise _error(lineno, f"duplicate field: {field}")
        current["raw_fields"][field] = (raw.strip(), lineno)
    finish()
    if not parsed:
        raise _error(1, "no cases found")
    return {"source_path": str(source), "source_sha256": digest, "cases": parsed}
