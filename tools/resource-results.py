#!/usr/bin/env python3
"""Validate and atomically update the compact latest resource-results file."""

import argparse
import json
import math
import os
import re
import sys
import tempfile
from pathlib import Path


SCHEMA_VERSION = 1
MAX_FILE_BYTES = 32 * 1024
MAX_PROFILES = 16
MAX_METRICS = 64
CONFIGURATION_KEYS = ("compiler", "build_type", "sanitizers", "runner", "fixture")
UNITS = {"wall_seconds", "process_cpu_seconds", "workspace_bytes", "ratio"}
STATUSES = {"passed", "warning", "failed", "incomplete", "omitted", "unavailable"}
SOURCE_RE = re.compile(r"(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})\Z")


class ValidationError(ValueError):
    pass


def _reject_constant(value):
    raise ValidationError("non-finite JSON number: " + value)


def _object_without_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValidationError("duplicate JSON key: " + key)
        result[key] = value
    return result


def _load_json(data, label):
    try:
        return json.loads(data, object_pairs_hook=_object_without_duplicates,
                          parse_constant=_reject_constant)
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise ValidationError("invalid " + label + " JSON: " + str(exc)) from exc


def _object(value, label, required=None, optional=()):
    if not isinstance(value, dict):
        raise ValidationError(label + " must be an object")
    required = set(required or ())
    allowed = required | set(optional)
    missing = required - set(value)
    extra = set(value) - allowed
    if missing:
        raise ValidationError(label + " is missing: " + ", ".join(sorted(missing)))
    if extra:
        raise ValidationError(label + " has unknown keys: " + ", ".join(sorted(extra)))
    return value


def _nonempty_string(value, label):
    if not isinstance(value, str) or not value.strip():
        raise ValidationError(label + " must be a nonempty string")


def validate_record(record):
    _object(record, "record", required=("source", "configuration", "provenance", "metrics"))

    source = record["source"]
    if not isinstance(source, str) or not SOURCE_RE.fullmatch(source):
        raise ValidationError("source must be a full 40- or 64-character hexadecimal commit")

    configuration = _object(record["configuration"], "configuration", required=CONFIGURATION_KEYS)
    for key in CONFIGURATION_KEYS:
        _nonempty_string(configuration[key], "configuration." + key)

    provenance = _object(record["provenance"], "provenance", optional=("run_id", "local_command", "report_reference"))
    if not provenance:
        raise ValidationError("provenance must identify a run, local command, or report")
    for key, value in provenance.items():
        _nonempty_string(value, "provenance." + key)

    metrics = record["metrics"]
    if not isinstance(metrics, dict) or not metrics:
        raise ValidationError("metrics must be a nonempty object")
    if len(metrics) > MAX_METRICS:
        raise ValidationError("metrics exceeds the 64-metric profile limit")
    for metric_id, metric in metrics.items():
        _nonempty_string(metric_id, "metric ID")
        _object(metric, "metric " + metric_id, required=("value", "unit", "status"))
        if not isinstance(metric["unit"], str) or metric["unit"] not in UNITS:
            raise ValidationError("metric " + metric_id + " has an unsupported unit")
        status = metric["status"]
        if not isinstance(status, str) or status not in STATUSES:
            raise ValidationError("metric " + metric_id + " has an unsupported status")
        value = metric["value"]
        if value is not None:
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise ValidationError("metric " + metric_id + " value must be a number or null")
            try:
                finite = math.isfinite(value)
            except OverflowError:
                finite = False
            if not finite or value < 0:
                raise ValidationError("metric " + metric_id + " value must be finite and nonnegative")
        if status in {"passed", "warning", "failed"} and value is None:
            raise ValidationError("metric " + metric_id + " requires a measured value for status " + status)
        if status in {"omitted", "unavailable"} and value is not None:
            raise ValidationError("metric " + metric_id + " must have a null value for status " + status)


def validate_document(document):
    _object(document, "document", required=("schema_version", "profiles"))
    if type(document["schema_version"]) is not int or document["schema_version"] != SCHEMA_VERSION:
        raise ValidationError("schema_version must be 1")
    profiles = document["profiles"]
    if not isinstance(profiles, dict):
        raise ValidationError("profiles must be an object")
    if len(profiles) > MAX_PROFILES:
        raise ValidationError("profiles exceeds the 16-profile limit")
    for profile, record in profiles.items():
        _nonempty_string(profile, "profile name")
        validate_record(record)


def _serialize(document):
    return (json.dumps(document, sort_keys=True, indent=2, ensure_ascii=True,
                       allow_nan=False) + "\n").encode("utf-8")


def _read_document(path):
    try:
        with path.open("rb") as stream:
            data = stream.read(MAX_FILE_BYTES + 1)
    except OSError as exc:
        raise ValidationError("cannot read " + str(path) + ": " + str(exc)) from exc
    if len(data) > MAX_FILE_BYTES:
        raise ValidationError("file exceeds the 32 KiB limit")
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ValidationError("file must be UTF-8 JSON") from exc
    document = _load_json(text, "file")
    validate_document(document)
    return document


def check_file(path):
    _read_document(path)


def update_file(path, profile, input_text):
    _nonempty_string(profile, "profile name")
    record = _load_json(input_text, "input")
    validate_record(record)

    existing_mode = None
    if path.exists():
        existing_mode = path.stat().st_mode & 0o777
        document = _read_document(path)
    else:
        document = {"schema_version": SCHEMA_VERSION, "profiles": {}}
    profiles = document["profiles"]
    if profile not in profiles and len(profiles) >= MAX_PROFILES:
        raise ValidationError("profiles exceeds the 16-profile limit")
    profiles[profile] = record
    validate_document(document)
    output = _serialize(document)
    if len(output) > MAX_FILE_BYTES:
        raise ValidationError("updated file exceeds the 32 KiB limit")

    parent = path.parent
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="wb", dir=parent,
                                         prefix="." + path.name + ".",
                                         delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(output)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, existing_mode if existing_mode is not None else 0o644)
        os.replace(temporary, path)
        temporary = None
    except OSError as exc:
        raise ValidationError("cannot atomically update " + str(path) + ": " + str(exc)) from exc
    finally:
        if temporary is not None:
            try:
                temporary.unlink()
            except OSError:
                pass


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check_parser = commands.add_parser("check", help="validate a resource-results file")
    check_parser.add_argument("file", type=Path)
    update_parser = commands.add_parser("update", help="replace the latest record for one profile")
    update_parser.add_argument("file", type=Path)
    update_parser.add_argument("--profile", required=True)
    update_parser.add_argument("--input", required=True, metavar="RECORD_JSON",
                               help="JSON object for one profile's latest record")
    args = parser.parse_args(argv)
    try:
        if args.command == "check":
            check_file(args.file)
        else:
            update_file(args.file, args.profile, args.input)
    except (ValidationError, OSError) as exc:
        print("resource-results: " + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
