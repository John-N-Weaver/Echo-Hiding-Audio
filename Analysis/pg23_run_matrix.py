#!/usr/bin/env python3
# ============================================================================
# pg23_run_matrix.py
#
# Course:      CS 4463 / CS 5173 - Team 21
# Project:     Echo Hiding Audio
# Authors:     John N. Weaver and Alex W. Bryant
# GitHub:      https://github.com/John-N-Weaver/Echo-Hiding-Audio
# Created:     July 28, 2026
# Last updated: July 28, 2026
#
# Purpose:
#   Execute and document the controlled testing required by PG-23. The script
#   discovers supported project covers, generates deterministic payloads at
#   defined fractions of each cover's capacity, runs hide and extract, invokes
#   the PG-14 statistical analyzer, and identifies lower/upper operating limits.
#
# Design:
#   * Standard-library Python only.
#   * Uses the compiled project executable rather than reimplementing hiding.
#   * Default suite selects four representative cover categories.
#   * --all-covers expands the same matrix across every eligible cover.
#   * All run evidence is archived under a unique timestamped directory.
# ============================================================================

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import wave
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT_DEFAULT = SCRIPT_DIR.parent
PG14_PATH_DEFAULT = SCRIPT_DIR / "pg14_analysis.py"

DEFAULT_FRACTIONS = (0.0, 0.25, 0.50, 0.75, 1.00, 1.25)
DEFAULT_SEGMENT_LEN = 2048
DEFAULT_DELAY_ZERO = 150
DEFAULT_DELAY_ONE = 200
DEFAULT_DECAY = 0.4
DEFAULT_REPETITION = 7
DEFAULT_HEADER_BITS = 64

MATRIX_FIELDS = [
    "run_id",
    "test_id",
    "matrix_group",
    "cover_category",
    "payload_category",
    "target_payload_fraction",
    "parameter_set",
    "cover_path",
    "stego_path",
    "payload_path",
    "extracted_path",
    "requested_payload_bytes_override",
    "segment_len_frames",
    "delay_zero_samples",
    "delay_one_samples",
    "echo_decay",
    "repetition",
    "header_bits",
    "auditory_rating",
    "listener_id",
    "listening_original_available",
    "playback_device",
    "listening_environment",
    "listening_notes",
    "notes",
]

EXECUTION_FIELDS = [
    "run_id",
    "test_id",
    "test_kind",
    "matrix_group",
    "cover_category",
    "cover_path",
    "source_cover_path",
    "sample_rate_hz",
    "channels",
    "bits_per_sample",
    "frame_count",
    "duration_sec",
    "logical_capacity_bits",
    "payload_capacity_bits",
    "payload_capacity_complete_bytes",
    "target_payload_fraction",
    "requested_payload_bytes",
    "expected_recoverable_bytes",
    "expected_outcome",
    "hide_exit_code",
    "extract_exit_code",
    "stego_created",
    "extracted_created",
    "extracted_payload_bytes",
    "direct_prefix_exact",
    "direct_complete_exact",
    "observed_outcome",
    "execution_pass",
    "payload_path",
    "stego_path",
    "extracted_path",
    "command_log_path",
    "notes",
]

KEY_ANALYSIS_FIELDS = [
    "analysis_status",
    "analysis_error_message",
    "requested_to_capacity_ratio",
    "embedded_capacity_utilization",
    "compared_prefix_bytes",
    "prefix_exact",
    "byte_errors_compared",
    "ber_compared",
    "missing_payload_bytes",
    "extra_payload_bytes",
    "recovery_fraction",
    "end_to_end_ber",
    "modified_samples",
    "sample_modification_rate",
    "mse",
    "rmse",
    "normalized_rmse",
    "peak_error_fraction_full_scale",
    "snr_db",
    "psnr_db",
    "pearson_correlation",
    "histogram_total_variation",
    "histogram_js_divergence_bits",
    "echo_detector_delta_l1",
]

COMBINED_FIELDS = EXECUTION_FIELDS + KEY_ANALYSIS_FIELDS + [
    "case_pass",
    "limit_interpretation",
]


class Pg23Error(Exception):
    """Raised when the controlled matrix cannot be completed safely."""


@dataclass
class CoverInfo:
    path: Path
    category: str
    sample_rate: int
    channels: int
    bits_per_sample: int
    frame_count: int
    duration_sec: float
    logical_capacity_bits: int
    payload_capacity_bits: int
    payload_capacity_bytes: int
    wave: object


def utc_now_text() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def load_pg14_module(path: Path):
    if not path.exists():
        raise Pg23Error(f"PG-14 analyzer not found: '{path}'")

    spec = importlib.util.spec_from_file_location("pg14_analysis", path)
    if spec is None or spec.loader is None:
        raise Pg23Error(f"could not load PG-14 analyzer: '{path}'")

    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def parse_fractions(text: str) -> Tuple[float, ...]:
    values: List[float] = []
    for token in text.split(","):
        token = token.strip()
        if not token:
            continue
        value = float(token)
        if value < 0:
            raise argparse.ArgumentTypeError("payload fractions cannot be negative")
        values.append(value)

    if not values:
        raise argparse.ArgumentTypeError("at least one payload fraction is required")

    unique = sorted(set(values))
    return tuple(unique)


def fraction_label(value: float) -> str:
    return f"{value:.2f}".rstrip("0").rstrip(".") or "0"


def target_payload_bytes(capacity_bytes: int, fraction: float) -> int:
    if capacity_bytes <= 0 or fraction <= 0:
        return 0

    raw = capacity_bytes * fraction
    if fraction <= 1.0:
        value = int(math.floor(raw + 1.0e-12))
        return max(value, 1)

    return max(int(math.ceil(raw - 1.0e-12)), capacity_bytes + 1)


def classify_cover(path: Path) -> str:
    name = path.name.lower()
    if "near_silence" in name or "silence" in name:
        return "near_silence"
    if "sparse" in name or "quiet" in name:
        return "sparse_quiet"
    if "speech" in name or "voice" in name:
        return "speech"
    if "tone" in name or "sine" in name or "sweep" in name or "square" in name:
        return "synthetic_tone"
    if "music" in name or "blues" in name or "rock" in name:
        return "music"
    return "other_audio"


def deterministic_payload(size: int, seed_text: str) -> bytes:
    if size <= 0:
        return b""

    seed = hashlib.sha256(seed_text.encode("utf-8")).digest()
    output = bytearray()
    counter = 0

    while len(output) < size:
        block = hashlib.sha256(seed + counter.to_bytes(8, "little")).digest()
        output.extend(block)
        counter += 1

    return bytes(output[:size])


def candidate_executables(project_root: Path) -> List[Path]:
    return [
        project_root / "x64" / "Debug" / "Echo Hiding Audio.exe",
        project_root / "x64" / "Release" / "Echo Hiding Audio.exe",
        project_root / "Debug" / "Echo Hiding Audio.exe",
        project_root / "Release" / "Echo Hiding Audio.exe",
        project_root / "Echo Hiding Audio.exe",
        project_root / "stego.exe",
    ]


def latest_test_executable(project_root: Path) -> Optional[Path]:
    summary = project_root / "Tests" / "Test Run Summary.txt"
    if not summary.exists():
        return None

    try:
        text = summary.read_text(encoding="utf-8-sig", errors="replace")
    except OSError:
        return None

    match = re.search(
        r"^Executable\s*:\s*(.+?)\s*$",
        text,
        flags=re.MULTILINE,
    )
    if match is None:
        return None

    candidate = Path(match.group(1).strip().strip('"')).expanduser()
    if candidate.exists():
        return candidate.resolve()
    return None


def find_executable(project_root: Path, explicit: str) -> Path:
    if explicit:
        path = Path(explicit).expanduser()
        if not path.is_absolute():
            path = project_root / path
        path = path.resolve()
        if not path.exists():
            raise Pg23Error(f"executable does not exist: '{path}'")
        return path

    regression_executable = latest_test_executable(project_root)
    if regression_executable is not None:
        return regression_executable

    for path in candidate_executables(project_root):
        if path.exists():
            return path.resolve()

    attempted = "\n".join(f"  - {path}" for path in candidate_executables(project_root))
    raise Pg23Error(
        "could not locate the compiled executable. Tried:\n" + attempted
    )


def default_cover_roots(project_root: Path) -> List[Path]:
    return [
        project_root / "TestData" / "Corpus",
        project_root / "TestData" / "wavs_main_Massey",
    ]


def discover_covers(
    roots: Sequence[Path],
    pg14,
    min_capacity_bytes: int,
) -> Tuple[List[CoverInfo], List[str]]:
    covers: List[CoverInfo] = []
    skipped: List[str] = []
    seen: set[Path] = set()

    for root in roots:
        if not root.exists():
            skipped.append(f"cover root not found: {root}")
            continue

        for path in sorted(root.rglob("*.wav"), key=lambda item: str(item).lower()):
            resolved = path.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)

            lower_name = path.name.lower()
            if lower_name.endswith("_stego.wav"):
                skipped.append(f"generated stego excluded: {path}")
                continue

            try:
                wav = pg14.read_wave_pcm(resolved)
            except Exception as exc:
                skipped.append(f"unsupported WAV excluded: {path}: {exc}")
                continue

            logical_capacity = (
                wav.frame_count // DEFAULT_SEGMENT_LEN // DEFAULT_REPETITION
            )
            payload_bits = max(logical_capacity - DEFAULT_HEADER_BITS, 0)
            payload_bytes = payload_bits // 8

            if payload_bytes < min_capacity_bytes:
                skipped.append(
                    f"capacity below {min_capacity_bytes} byte(s): {path} "
                    f"({payload_bytes} byte capacity)"
                )
                continue

            covers.append(
                CoverInfo(
                    path=resolved,
                    category=classify_cover(path),
                    sample_rate=wav.sample_rate,
                    channels=wav.channels,
                    bits_per_sample=wav.bits_per_sample,
                    frame_count=wav.frame_count,
                    duration_sec=wav.duration_sec,
                    logical_capacity_bits=logical_capacity,
                    payload_capacity_bits=payload_bits,
                    payload_capacity_bytes=payload_bytes,
                    wave=wav,
                )
            )

    return covers, skipped


def representative_covers(covers: Sequence[CoverInfo], max_covers: int) -> List[CoverInfo]:
    if max_covers <= 0:
        return []

    category_order = [
        "music",
        "speech",
        "sparse_quiet",
        "near_silence",
        "synthetic_tone",
        "other_audio",
    ]

    selected: List[CoverInfo] = []
    used_paths: set[Path] = set()
    used_formats: set[Tuple[int, int]] = set()

    for category in category_order:
        candidates = [cover for cover in covers if cover.category == category]
        if not candidates:
            continue

        candidates.sort(
            key=lambda cover: (
                (cover.bits_per_sample, cover.channels) in used_formats,
                -cover.payload_capacity_bytes,
                cover.path.name.lower(),
            )
        )
        chosen = candidates[0]
        selected.append(chosen)
        used_paths.add(chosen.path)
        used_formats.add((chosen.bits_per_sample, chosen.channels))

        if len(selected) >= max_covers:
            return selected

    remaining = [cover for cover in covers if cover.path not in used_paths]
    remaining.sort(
        key=lambda cover: (
            (cover.bits_per_sample, cover.channels) in used_formats,
            cover.category,
            -cover.payload_capacity_bytes,
            cover.path.name.lower(),
        )
    )

    for cover in remaining:
        selected.append(cover)
        used_formats.add((cover.bits_per_sample, cover.channels))
        if len(selected) >= max_covers:
            break

    return selected


def unique_run_directory(history_root: Path, run_id: str) -> Tuple[str, Path]:
    candidate = run_id
    counter = 1
    while (history_root / candidate).exists():
        candidate = f"{run_id}_rerun{counter:02d}"
        counter += 1

    path = history_root / candidate
    path.mkdir(parents=True, exist_ok=False)
    return candidate, path


def write_csv(path: Path, fields: Sequence[str], rows: Sequence[Dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(fields))
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fields})


def read_csv(path: Path) -> List[Dict[str, str]]:
    with path.open("r", newline="", encoding="utf-8-sig") as stream:
        return list(csv.DictReader(stream))


def run_command(
    arguments: Sequence[str],
    log_path: Path,
    timeout_sec: int,
    cwd: Path,
) -> int:
    environment = os.environ.copy()
    environment["STEGO_DISABLE_COMMAND_LOG"] = "1"

    started = time.perf_counter()
    try:
        completed = subprocess.run(
            list(arguments),
            cwd=str(cwd),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            errors="replace",
            env=environment,
            timeout=timeout_sec,
        )
        exit_code = completed.returncode
        output = completed.stdout
    except subprocess.TimeoutExpired as exc:
        exit_code = 124
        output = (exc.stdout or "") + (exc.stderr or "")
        output += f"\nERROR: command exceeded {timeout_sec} seconds.\n"
    except OSError as exc:
        exit_code = 127
        output = f"ERROR: could not execute command: {exc}\n"

    elapsed = time.perf_counter() - started
    rendered = subprocess.list2cmdline(list(arguments))
    log_text = (
        f"Command: {rendered}\n"
        f"Working directory: {cwd}\n"
        f"Exit code: {exit_code}\n"
        f"Elapsed seconds: {elapsed:.3f}\n"
        "------------------------------------------------------------\n"
        f"{output}"
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(log_text, encoding="utf-8", newline="\r\n")
    return exit_code


def compare_payload_files(payload_path: Path, extracted_path: Optional[Path]) -> Dict[str, object]:
    payload = payload_path.read_bytes()

    if extracted_path is None or not extracted_path.exists():
        return {
            "extracted_payload_bytes": "",
            "direct_prefix_exact": False,
            "direct_complete_exact": False,
        }

    extracted = extracted_path.read_bytes()
    prefix_exact = (
        len(extracted) <= len(payload)
        and payload[:len(extracted)] == extracted
    )

    return {
        "extracted_payload_bytes": len(extracted),
        "direct_prefix_exact": prefix_exact,
        "direct_complete_exact": payload == extracted,
    }


def expected_fraction_outcome(requested_bytes: int, capacity_bytes: int) -> str:
    if requested_bytes <= capacity_bytes:
        return "complete_exact"
    return "partial_exact_prefix"


def observed_outcome(
    hide_exit: int,
    extract_exit: object,
    stego_created: bool,
    extracted_created: bool,
    direct_prefix_exact: bool,
    direct_complete_exact: bool,
) -> str:
    if hide_exit != 0 or not stego_created:
        return "hide_failed"
    if extract_exit == "" or int(extract_exit) != 0 or not extracted_created:
        return "extract_failed"
    if direct_complete_exact:
        return "complete_exact"
    if direct_prefix_exact:
        return "partial_exact_prefix"
    return "corrupted"


def write_minimal_pcm_wave(destination: Path, source_wave, frame_count: int) -> None:
    """Write a valid classic-PCM RIFF/WAVE file at an exact frame boundary.

    The standard-library wave writer does not consistently add the required
    RIFF pad byte after an odd-length data chunk. That matters for 8-bit mono
    lower-limit covers with an odd number of frames, including
    L03_ONE_BYTE_MINUS. This writer records the unpadded data chunk length,
    adds one physical pad byte when necessary, and includes that pad in the
    RIFF container size.
    """
    required_bytes = frame_count * source_wave.block_align
    if required_bytes <= len(source_wave.data):
        data = source_wave.data[:required_bytes]
    else:
        repeats = math.ceil(required_bytes / len(source_wave.data))
        data = (source_wave.data * repeats)[:required_bytes]

    channels = source_wave.channels
    sample_rate = source_wave.sample_rate
    bits_per_sample = source_wave.bits_per_sample
    block_align = source_wave.block_align
    byte_rate = sample_rate * block_align
    data_size = len(data)
    pad = b"\x00" if data_size % 2 else b""

    fmt_data = struct.pack(
        "<HHIIHH",
        1,
        channels,
        sample_rate,
        byte_rate,
        block_align,
        bits_per_sample,
    )
    riff_size = 4 + (8 + len(fmt_data)) + (8 + data_size + len(pad))

    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("wb") as output:
        output.write(b"RIFF")
        output.write(struct.pack("<I", riff_size))
        output.write(b"WAVE")
        output.write(b"fmt ")
        output.write(struct.pack("<I", len(fmt_data)))
        output.write(fmt_data)
        output.write(b"data")
        output.write(struct.pack("<I", data_size))
        output.write(data)
        output.write(pad)


def create_case_paths(run_dir: Path, test_id: str) -> Tuple[Path, Path, Path, Path]:
    payload = run_dir / "generated" / "payloads" / f"{test_id}_payload.bin"
    stego = run_dir / "generated" / "stego" / f"{test_id}_stego.wav"
    extracted = run_dir / "generated" / "extracted" / f"{test_id}_extracted.bin"
    log = run_dir / "logs" / f"{test_id}.log"
    return payload, stego, extracted, log


def execute_case(
    *,
    project_root: Path,
    executable: Path,
    run_dir: Path,
    run_id: str,
    test_id: str,
    test_kind: str,
    matrix_group: str,
    cover: CoverInfo,
    source_cover_path: Path,
    target_fraction: object,
    requested_bytes: int,
    expected_recoverable_bytes: int,
    expected: str,
    timeout_sec: int,
    notes: str,
) -> Tuple[Dict[str, object], Optional[Dict[str, object]]]:
    payload_path, stego_path, extracted_path, log_path = create_case_paths(
        run_dir, test_id
    )
    # Create every output parent before launching the C++ executable. The
    # application creates files but does not create missing directories.
    payload_path.parent.mkdir(parents=True, exist_ok=True)
    stego_path.parent.mkdir(parents=True, exist_ok=True)
    extracted_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    payload_path.write_bytes(deterministic_payload(requested_bytes, test_id))

    hide_arguments = [
        str(executable),
        "-hide",
        "-m",
        str(payload_path),
        "-c",
        str(cover.path),
        "-o",
        str(stego_path),
    ]
    hide_exit = run_command(
        hide_arguments,
        log_path,
        timeout_sec,
        project_root,
    )

    stego_created = stego_path.exists()
    extract_exit: object = ""
    extracted_created = False

    if hide_exit == 0 and stego_created:
        extract_arguments = [
            str(executable),
            "-extract",
            "-s",
            str(stego_path),
            "-o",
            str(extracted_path),
        ]
        extract_log = run_dir / "logs" / f"{test_id}_extract.log"
        extract_exit = run_command(
            extract_arguments,
            extract_log,
            timeout_sec,
            project_root,
        )
        extracted_created = extracted_path.exists()

    compare = compare_payload_files(
        payload_path,
        extracted_path if extracted_created else None,
    )

    outcome = observed_outcome(
        hide_exit,
        extract_exit,
        stego_created,
        extracted_created,
        bool(compare["direct_prefix_exact"]),
        bool(compare["direct_complete_exact"]),
    )
    execution_pass = outcome == expected

    execution_row: Dict[str, object] = {
        "run_id": run_id,
        "test_id": test_id,
        "test_kind": test_kind,
        "matrix_group": matrix_group,
        "cover_category": cover.category,
        "cover_path": str(cover.path),
        "source_cover_path": str(source_cover_path),
        "sample_rate_hz": cover.sample_rate,
        "channels": cover.channels,
        "bits_per_sample": cover.bits_per_sample,
        "frame_count": cover.frame_count,
        "duration_sec": cover.duration_sec,
        "logical_capacity_bits": cover.logical_capacity_bits,
        "payload_capacity_bits": cover.payload_capacity_bits,
        "payload_capacity_complete_bytes": cover.payload_capacity_bytes,
        "target_payload_fraction": target_fraction,
        "requested_payload_bytes": requested_bytes,
        "expected_recoverable_bytes": expected_recoverable_bytes,
        "expected_outcome": expected,
        "hide_exit_code": hide_exit,
        "extract_exit_code": extract_exit,
        "stego_created": stego_created,
        "extracted_created": extracted_created,
        "extracted_payload_bytes": compare["extracted_payload_bytes"],
        "direct_prefix_exact": compare["direct_prefix_exact"],
        "direct_complete_exact": compare["direct_complete_exact"],
        "observed_outcome": outcome,
        "execution_pass": execution_pass,
        "payload_path": str(payload_path),
        "stego_path": str(stego_path),
        "extracted_path": str(extracted_path) if extracted_created else "",
        "command_log_path": str(log_path),
        "notes": notes,
    }

    pg14_row: Optional[Dict[str, object]] = None
    if stego_created:
        pg14_row = {
            "run_id": run_id,
            "test_id": test_id,
            "matrix_group": matrix_group,
            "cover_category": cover.category,
            "payload_category": "deterministic_binary",
            "target_payload_fraction": target_fraction,
            "parameter_set": "fixed_v1",
            "cover_path": str(cover.path),
            "stego_path": str(stego_path),
            "payload_path": str(payload_path),
            "extracted_path": str(extracted_path) if extracted_created else "",
            "requested_payload_bytes_override": "",
            "segment_len_frames": DEFAULT_SEGMENT_LEN,
            "delay_zero_samples": DEFAULT_DELAY_ZERO,
            "delay_one_samples": DEFAULT_DELAY_ONE,
            "echo_decay": DEFAULT_DECAY,
            "repetition": DEFAULT_REPETITION,
            "header_bits": DEFAULT_HEADER_BITS,
            "auditory_rating": "",
            "listener_id": "",
            "listening_original_available": "",
            "playback_device": "",
            "listening_environment": "",
            "listening_notes": "",
            "notes": notes,
        }

    return execution_row, pg14_row


def make_cover_info(path: Path, category: str, pg14) -> CoverInfo:
    wav = pg14.read_wave_pcm(path)
    logical = wav.frame_count // DEFAULT_SEGMENT_LEN // DEFAULT_REPETITION
    payload_bits = max(logical - DEFAULT_HEADER_BITS, 0)
    return CoverInfo(
        path=path.resolve(),
        category=category,
        sample_rate=wav.sample_rate,
        channels=wav.channels,
        bits_per_sample=wav.bits_per_sample,
        frame_count=wav.frame_count,
        duration_sec=wav.duration_sec,
        logical_capacity_bits=logical,
        payload_capacity_bits=payload_bits,
        payload_capacity_bytes=payload_bits // 8,
        wave=wav,
    )


def lower_limit_cases(
    source_cover: CoverInfo,
    derived_dir: Path,
    pg14,
) -> List[Tuple[str, CoverInfo, int, int, str, str]]:
    header_frames = (
        DEFAULT_HEADER_BITS * DEFAULT_SEGMENT_LEN * DEFAULT_REPETITION
    )
    one_byte_frames = (
        (DEFAULT_HEADER_BITS + 8)
        * DEFAULT_SEGMENT_LEN
        * DEFAULT_REPETITION
    )

    definitions = [
        (
            "L01_BELOW_HEADER",
            header_frames - 1,
            0,
            0,
            "hide_failed",
            "One frame below the minimum needed for the 64-bit header.",
        ),
        (
            "L02_HEADER_EXACT",
            header_frames,
            0,
            0,
            "complete_exact",
            "Exactly enough frames for the header and an empty payload.",
        ),
        (
            "L03_ONE_BYTE_MINUS",
            one_byte_frames - 1,
            1,
            0,
            "partial_exact_prefix",
            "One frame below complete one-byte capacity; only seven payload bits fit.",
        ),
        (
            "L04_ONE_BYTE_EXACT",
            one_byte_frames,
            1,
            1,
            "complete_exact",
            "Exactly enough frames for the header and one complete payload byte.",
        ),
    ]

    cases: List[Tuple[str, CoverInfo, int, int, str, str]] = []
    for test_id, frames, requested, expected_bytes, expected, notes in definitions:
        path = derived_dir / f"{test_id.lower()}.wav"
        write_minimal_pcm_wave(path, source_cover.wave, frames)
        cover = make_cover_info(path, "lower_limit_derived", pg14)
        cases.append(
            (test_id, cover, requested, expected_bytes, expected, notes)
        )

    return cases


def read_pg14_results(path: Path) -> Dict[str, Dict[str, str]]:
    if not path.exists():
        return {}
    return {row["test_id"]: row for row in read_csv(path)}


def combined_result_rows(
    execution_rows: Sequence[Dict[str, object]],
    pg14_by_id: Dict[str, Dict[str, str]],
) -> List[Dict[str, object]]:
    combined: List[Dict[str, object]] = []

    for execution in execution_rows:
        test_id = str(execution["test_id"])
        analysis = pg14_by_id.get(test_id, {})
        row = dict(execution)

        row["analysis_status"] = analysis.get("status", "")
        row["analysis_error_message"] = analysis.get("error_message", "")
        for field in KEY_ANALYSIS_FIELDS[2:]:
            row[field] = analysis.get(field, "")

        analysis_required = bool(execution["stego_created"])
        analysis_pass = (
            not analysis_required
            or analysis.get("status", "") == "PASS"
        )
        row["case_pass"] = bool(execution["execution_pass"]) and analysis_pass

        observed = str(execution["observed_outcome"])
        if execution["test_kind"] == "lower_limit":
            if test_id == "L01_BELOW_HEADER":
                interpretation = "Header lower bound confirmed."
            elif test_id == "L02_HEADER_EXACT":
                interpretation = "Header-only minimum confirmed."
            elif test_id == "L03_ONE_BYTE_MINUS":
                interpretation = "Incomplete payload byte is not emitted."
            else:
                interpretation = "One-byte complete lower bound confirmed."
        elif observed == "complete_exact":
            interpretation = "At or below complete-recovery capacity."
        elif observed == "partial_exact_prefix":
            interpretation = "Capacity exhausted; exact recoverable prefix retained."
        elif observed in {"hide_failed", "extract_failed", "corrupted"}:
            interpretation = "Reliability failure or unsupported condition."
        else:
            interpretation = ""

        row["limit_interpretation"] = interpretation
        combined.append(row)

    return combined


def optional_float(value: object) -> Optional[float]:
    text = str(value).strip()
    if text == "" or text in {"inf", "-inf"}:
        return None
    try:
        return float(text)
    except ValueError:
        return None


def metric_summary(
    rows: Sequence[Dict[str, object]],
    field: str,
) -> str:
    values = [
        value
        for value in (optional_float(row.get(field, "")) for row in rows)
        if value is not None
    ]
    if not values:
        return "unavailable"
    return (
        f"min={min(values):.6g}, "
        f"mean={sum(values) / len(values):.6g}, "
        f"max={max(values):.6g}"
    )


def cover_limit_lines(rows: Sequence[Dict[str, object]]) -> List[str]:
    by_cover: Dict[str, List[Dict[str, object]]] = {}
    for row in rows:
        if row["test_kind"] != "payload_fraction":
            continue
        by_cover.setdefault(str(row["cover_path"]), []).append(row)

    lines: List[str] = []
    for cover_path, cover_rows in sorted(by_cover.items()):
        cover_rows.sort(
            key=lambda row: float(row["target_payload_fraction"])
        )
        complete = [
            float(row["target_payload_fraction"])
            for row in cover_rows
            if row["observed_outcome"] == "complete_exact"
        ]
        partial = [
            float(row["target_payload_fraction"])
            for row in cover_rows
            if row["observed_outcome"] == "partial_exact_prefix"
        ]
        failed = [
            float(row["target_payload_fraction"])
            for row in cover_rows
            if row["observed_outcome"]
            in {"hide_failed", "extract_failed", "corrupted"}
        ]

        highest_complete = max(complete) if complete else None
        first_partial = min(partial) if partial else None
        first_failure = min(failed) if failed else None
        capacity = cover_rows[0]["payload_capacity_complete_bytes"]

        lines.append(
            f"- {Path(cover_path).name}: capacity={capacity} byte(s); "
            f"highest complete fraction="
            f"{'none' if highest_complete is None else fraction_label(highest_complete)}; "
            f"first capacity-exhausted fraction="
            f"{'none' if first_partial is None else fraction_label(first_partial)}; "
            f"first reliability failure="
            f"{'none observed' if first_failure is None else fraction_label(first_failure)}"
        )
    return lines


def fraction_metric_lines(rows: Sequence[Dict[str, object]]) -> List[str]:
    by_fraction: Dict[str, List[Dict[str, object]]] = {}
    for row in rows:
        if row["test_kind"] != "payload_fraction":
            continue
        key = str(row["target_payload_fraction"])
        by_fraction.setdefault(key, []).append(row)

    lines: List[str] = []
    for key in sorted(by_fraction, key=float):
        group = by_fraction[key]
        complete = sum(row["observed_outcome"] == "complete_exact" for row in group)
        partial = sum(
            row["observed_outcome"] == "partial_exact_prefix" for row in group
        )
        failures = len(group) - complete - partial

        lines.append(
            f"- Fraction {key}: cases={len(group)}, complete={complete}, "
            f"partial={partial}, failures={failures}; "
            f"normalized RMSE ({metric_summary(group, 'normalized_rmse')}); "
            f"SNR dB ({metric_summary(group, 'snr_db')}); "
            f"histogram TV ({metric_summary(group, 'histogram_total_variation')}); "
            f"echo delta L1 ({metric_summary(group, 'echo_detector_delta_l1')})"
        )
    return lines


def write_summary(
    path: Path,
    *,
    run_id: str,
    executable: Path,
    selected_covers: Sequence[CoverInfo],
    fractions: Sequence[float],
    combined_rows: Sequence[Dict[str, object]],
    pg14_results_path: Path,
    run_dir: Path,
) -> None:
    total = len(combined_rows)
    passed = sum(bool(row["case_pass"]) for row in combined_rows)
    failed = total - passed
    fraction_rows = [
        row for row in combined_rows if row["test_kind"] == "payload_fraction"
    ]
    lower_rows = [
        row for row in combined_rows if row["test_kind"] == "lower_limit"
    ]
    exact_complete = sum(
        row["observed_outcome"] == "complete_exact" for row in combined_rows
    )
    exact_partial = sum(
        row["observed_outcome"] == "partial_exact_prefix"
        for row in combined_rows
    )
    reliability_failures = sum(
        row["observed_outcome"] in {"hide_failed", "extract_failed", "corrupted"}
        and row["expected_outcome"] != "hide_failed"
        for row in combined_rows
    )
    zero_prefix_ber = sum(
        str(row.get("ber_compared", "")).strip() in {"0", "0.0"}
        for row in combined_rows
        if str(row.get("analysis_status", "")) == "PASS"
        and str(row.get("compared_prefix_bytes", "")).strip() not in {"", "0"}
    )

    header_frames = DEFAULT_HEADER_BITS * DEFAULT_SEGMENT_LEN * DEFAULT_REPETITION
    one_byte_frames = (
        (DEFAULT_HEADER_BITS + 8)
        * DEFAULT_SEGMENT_LEN
        * DEFAULT_REPETITION
    )

    lines = [
        "Echo Hiding Audio - PG-23 Controlled Limit Testing Summary",
        "===========================================================",
        f"Run ID                        : {run_id}",
        f"Timestamp UTC                 : {utc_now_text()}",
        f"Executable                    : {executable}",
        f"Run directory                 : {run_dir}",
        f"PG-14 detailed results        : {pg14_results_path}",
        f"Selected covers               : {len(selected_covers)}",
        f"Payload fractions             : {', '.join(fraction_label(v) for v in fractions)}",
        f"Payload-fraction cases        : {len(fraction_rows)}",
        f"Lower-limit cases             : {len(lower_rows)}",
        f"Total controlled cases        : {total}",
        f"Cases passed                  : {passed}",
        f"Cases failed                  : {failed}",
        f"Complete exact recoveries     : {exact_complete}",
        f"Exact-prefix partial recovery : {exact_partial}",
        f"Unexpected reliability fails  : {reliability_failures}",
        f"Zero compared-prefix BER rows : {zero_prefix_ber}",
        "",
        "Lower operating limits",
        "----------------------",
        f"Minimum frames for 64-bit header : {header_frames}",
        f"Minimum frames for one byte      : {one_byte_frames}",
        (
            "Observed lower-limit suite        : "
            + ("PASS" if all(bool(row["case_pass"]) for row in lower_rows) else "FAIL")
        ),
        "",
        "Per-cover upper and reliability limits",
        "--------------------------------------",
    ]
    lines.extend(cover_limit_lines(combined_rows))
    lines += [
        "",
        "Metrics by requested capacity fraction",
        "--------------------------------------",
    ]
    lines.extend(fraction_metric_lines(combined_rows))
    lines += [
        "",
        "Interpretation",
        "--------------",
        "- Complete recovery is established when the full generated payload is",
        "  byte-exact and compared-prefix BER is zero.",
        "- Capacity exhaustion is established when extraction returns the exact",
        "  available prefix but omits bytes requested beyond complete capacity.",
        "- A reliability limit occurs at the first nonzero compared-prefix BER,",
        "  corrupted output, header failure, or unexpected hide/extract failure.",
        "- Raw MSE should not be averaged across 8-bit and 16-bit covers. Use",
        "  normalized RMSE, PSNR, SNR, distribution metrics, and echo-detector",
        "  metrics for cross-format comparisons.",
        "- PG-24 auditory ratings remain blank until controlled listening is done.",
    ]

    path.write_text(
        "\n".join(lines) + "\n",
        encoding="utf-8",
        newline="\r\n",
    )


def copy_latest(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Run the controlled PG-23 payload-fraction and lower-limit matrix "
            "using the compiled Echo Hiding Audio executable."
        )
    )
    parser.add_argument(
        "--project-root",
        default=str(PROJECT_ROOT_DEFAULT),
        help="Project root containing Analysis, TestData, and the executable.",
    )
    parser.add_argument(
        "--exe",
        default="",
        help="Compiled executable path. Defaults to Release, then Debug candidates.",
    )
    parser.add_argument(
        "--pg14-script",
        default=str(PG14_PATH_DEFAULT),
        help="Path to pg14_analysis.py.",
    )
    parser.add_argument(
        "--cover-root",
        action="append",
        default=[],
        help="Cover directory; may be supplied more than once.",
    )
    parser.add_argument(
        "--all-covers",
        action="store_true",
        help="Use every eligible cover instead of the representative subset.",
    )
    parser.add_argument(
        "--max-covers",
        type=int,
        default=4,
        help="Maximum representative covers when --all-covers is not used.",
    )
    parser.add_argument(
        "--min-capacity-bytes",
        type=int,
        default=1,
        help="Exclude ordinary matrix covers below this complete-byte capacity.",
    )
    parser.add_argument(
        "--fractions",
        type=parse_fractions,
        default=DEFAULT_FRACTIONS,
        help="Comma-separated requested payload/capacity fractions.",
    )
    parser.add_argument(
        "--skip-lower-limit",
        action="store_true",
        help="Skip the four exact frame-boundary lower-limit cases.",
    )
    parser.add_argument(
        "--timeout-sec",
        type=int,
        default=900,
        help="Maximum seconds allowed for each hide or extract command.",
    )
    parser.add_argument(
        "--output-root",
        default="",
        help="Defaults to Analysis/pg23_results.",
    )
    parser.add_argument(
        "--run-id",
        default="",
        help="Run identifier; defaults to current date/time.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Discover and report the selected matrix without creating files.",
    )
    parser.add_argument(
        "--cleanup-generated",
        action="store_true",
        help="Delete generated payload/stego/extracted files after metrics are written.",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()

    project_root = Path(args.project_root).expanduser().resolve()
    pg14_path = Path(args.pg14_script).expanduser()
    if not pg14_path.is_absolute():
        pg14_path = project_root / pg14_path
    pg14_path = pg14_path.resolve()

    output_root = (
        Path(args.output_root).expanduser().resolve()
        if args.output_root
        else project_root / "Analysis" / "pg23_results"
    )
    history_root = output_root / "history"

    try:
        executable = find_executable(project_root, args.exe)
        pg14 = load_pg14_module(pg14_path)

        roots = (
            [
                (
                    Path(value).expanduser()
                    if Path(value).expanduser().is_absolute()
                    else project_root / value
                ).resolve()
                for value in args.cover_root
            ]
            if args.cover_root
            else default_cover_roots(project_root)
        )

        all_covers, skipped = discover_covers(
            roots,
            pg14,
            args.min_capacity_bytes,
        )
        if not all_covers:
            raise Pg23Error("no eligible project cover WAVs were found")

        selected = (
            list(all_covers)
            if args.all_covers
            else representative_covers(all_covers, args.max_covers)
        )
        if not selected:
            raise Pg23Error("cover selection produced no test covers")

        fractions = tuple(args.fractions)
        estimated_cases = len(selected) * len(fractions)
        if not args.skip_lower_limit:
            estimated_cases += 4
        estimated_stego_bytes = sum(
            cover.path.stat().st_size * len(fractions)
            for cover in selected
        )

        print("PG-23 controlled matrix plan")
        print("----------------------------")
        print(f"Executable             : {executable}")
        print(f"Eligible covers        : {len(all_covers)}")
        print(f"Selected covers        : {len(selected)}")
        print(
            "Payload fractions      : "
            + ", ".join(fraction_label(value) for value in fractions)
        )
        print(f"Planned cases          : {estimated_cases}")
        print(
            f"Estimated stego storage: {estimated_stego_bytes / (1024 ** 2):.1f} MiB"
        )
        print("Selected files:")
        for index, cover in enumerate(selected, start=1):
            print(
                f"  {index}. {cover.path.name} | {cover.category} | "
                f"{cover.bits_per_sample}-bit | {cover.channels} channel(s) | "
                f"capacity={cover.payload_capacity_bytes} byte(s)"
            )
        if skipped:
            print(f"Excluded/discovery notes: {len(skipped)}")

        if args.dry_run:
            return 0

        run_id = args.run_id or datetime.now().strftime("%Y%m%d_%H%M%S")
        artifact_id, run_dir = unique_run_directory(history_root, run_id)
        run_id = artifact_id

        execution_rows: List[Dict[str, object]] = []
        pg14_rows: List[Dict[str, object]] = []

        for cover_index, cover in enumerate(selected, start=1):
            for fraction in fractions:
                label = fraction_label(fraction).replace(".", "p")
                test_id = f"C{cover_index:02d}_F{label}"
                requested = target_payload_bytes(
                    cover.payload_capacity_bytes, fraction
                )
                expected_bytes = min(
                    requested, cover.payload_capacity_bytes
                )
                expected = expected_fraction_outcome(
                    requested, cover.payload_capacity_bytes
                )
                notes = (
                    f"Controlled payload-fraction case; fraction={fraction_label(fraction)}; "
                    f"capacity={cover.payload_capacity_bytes} complete byte(s)."
                )

                execution, analysis = execute_case(
                    project_root=project_root,
                    executable=executable,
                    run_dir=run_dir,
                    run_id=run_id,
                    test_id=test_id,
                    test_kind="payload_fraction",
                    matrix_group="pg23_payload_fraction",
                    cover=cover,
                    source_cover_path=cover.path,
                    target_fraction=fraction_label(fraction),
                    requested_bytes=requested,
                    expected_recoverable_bytes=expected_bytes,
                    expected=expected,
                    timeout_sec=args.timeout_sec,
                    notes=notes,
                )
                execution_rows.append(execution)
                if analysis is not None:
                    pg14_rows.append(analysis)

                print(
                    f"{test_id}: expected={expected}; "
                    f"observed={execution['observed_outcome']}; "
                    f"{'PASS' if execution['execution_pass'] else 'FAIL'}"
                )

        if not args.skip_lower_limit:
            source_cover = max(selected, key=lambda item: item.frame_count)
            derived_dir = run_dir / "generated" / "lower_limit_covers"
            for (
                test_id,
                cover,
                requested,
                expected_bytes,
                expected,
                notes,
            ) in lower_limit_cases(source_cover, derived_dir, pg14):
                execution, analysis = execute_case(
                    project_root=project_root,
                    executable=executable,
                    run_dir=run_dir,
                    run_id=run_id,
                    test_id=test_id,
                    test_kind="lower_limit",
                    matrix_group="pg23_lower_limit",
                    cover=cover,
                    source_cover_path=source_cover.path,
                    target_fraction="frame_boundary",
                    requested_bytes=requested,
                    expected_recoverable_bytes=expected_bytes,
                    expected=expected,
                    timeout_sec=args.timeout_sec,
                    notes=notes,
                )
                execution_rows.append(execution)
                if analysis is not None:
                    pg14_rows.append(analysis)

                print(
                    f"{test_id}: expected={expected}; "
                    f"observed={execution['observed_outcome']}; "
                    f"{'PASS' if execution['execution_pass'] else 'FAIL'}"
                )

        execution_path = run_dir / "PG23 Execution Manifest.csv"
        matrix_path = run_dir / "PG23 PG14 Matrix.csv"
        pg14_results_path = run_dir / "PG23 PG14 Detailed Results.csv"
        combined_path = run_dir / "PG23 Combined Results.csv"
        summary_path = run_dir / "PG23 Summary.txt"
        discovery_path = run_dir / "PG23 Cover Discovery.txt"

        write_csv(execution_path, EXECUTION_FIELDS, execution_rows)
        write_csv(matrix_path, MATRIX_FIELDS, pg14_rows)

        discovery_lines = [
            "PG-23 Cover Discovery",
            "=====================",
            f"Eligible covers: {len(all_covers)}",
            f"Selected covers: {len(selected)}",
            "",
            "Selected:",
        ]
        discovery_lines.extend(
            f"- {cover.path} | category={cover.category} | "
            f"{cover.bits_per_sample}-bit | channels={cover.channels} | "
            f"frames={cover.frame_count} | capacity={cover.payload_capacity_bytes} byte(s)"
            for cover in selected
        )
        discovery_lines += ["", "Excluded or noted:"]
        discovery_lines.extend(f"- {item}" for item in skipped)
        discovery_path.write_text(
            "\n".join(discovery_lines) + "\n",
            encoding="utf-8",
            newline="\r\n",
        )

        if pg14_rows:
            completed = subprocess.run(
                [
                    sys.executable,
                    str(pg14_path),
                    "matrix",
                    "--matrix",
                    str(matrix_path),
                    "--output",
                    str(pg14_results_path),
                    "--run-id",
                    run_id,
                ],
                cwd=str(project_root),
            )
            if not pg14_results_path.exists():
                raise Pg23Error(
                    "PG-14 matrix analysis did not create its results CSV"
                )
            pg14_return_code = completed.returncode
        else:
            write_csv(pg14_results_path, [], [])
            pg14_return_code = 0

        pg14_by_id = read_pg14_results(pg14_results_path)
        combined = combined_result_rows(execution_rows, pg14_by_id)
        write_csv(combined_path, COMBINED_FIELDS, combined)

        write_summary(
            summary_path,
            run_id=run_id,
            executable=executable,
            selected_covers=selected,
            fractions=fractions,
            combined_rows=combined,
            pg14_results_path=pg14_results_path,
            run_dir=run_dir,
        )

        copy_latest(
            execution_path,
            output_root / "Latest PG23 Execution Manifest.csv",
        )
        copy_latest(
            matrix_path,
            output_root / "Latest PG23 PG14 Matrix.csv",
        )
        copy_latest(
            pg14_results_path,
            output_root / "Latest PG23 PG14 Detailed Results.csv",
        )
        copy_latest(
            combined_path,
            output_root / "Latest PG23 Combined Results.csv",
        )
        copy_latest(
            summary_path,
            output_root / "Latest PG23 Summary.txt",
        )
        copy_latest(
            discovery_path,
            output_root / "Latest PG23 Cover Discovery.txt",
        )

        total_pass = all(bool(row["case_pass"]) for row in combined)
        if pg14_return_code != 0:
            total_pass = False

        if args.cleanup_generated:
            generated_dir = run_dir / "generated"
            if generated_dir.exists():
                shutil.rmtree(generated_dir)

        print()
        print("PG-23 controlled matrix complete")
        print("--------------------------------")
        print(f"Run directory       : {run_dir}")
        print(f"Execution manifest  : {execution_path}")
        print(f"Combined results    : {combined_path}")
        print(f"Summary             : {summary_path}")
        print(
            f"Cases passed        : "
            f"{sum(bool(row['case_pass']) for row in combined)}/{len(combined)}"
        )
        print(f"Overall result      : {'PASS' if total_pass else 'FAIL'}")

        return 0 if total_pass else 1

    except (Pg23Error, OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
