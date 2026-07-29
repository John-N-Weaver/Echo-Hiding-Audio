#!/usr/bin/env python3
# ============================================================================
# pg14_analyze_latest_tests.py
#
# Course:      CS 4463 / CS 5173 - Team 21
# Project:     Echo Hiding Audio
# Authors:     John N. Weaver and Alex W. Bryant
# GitHub:      https://github.com/John-N-Weaver/Echo-Hiding-Audio
# Created:     July 28, 2026
# Last updated: July 28, 2026
#
# Purpose:
#   Parse Tests\Latest Test Run.log, build a PG-23-compatible matrix for the
#   actual T1-T10 cover/stego/payload/extracted pairs, invoke pg14_analysis.py
#   once in matrix mode, and retain timestamped and "Latest" result artifacts.
#
# Why:
#   The regression harness already creates valid project pairs. Reusing those
#   exact files eliminates manual path transcription and provides repeatable
#   PG-14 evidence immediately after each test run.
# ============================================================================

from __future__ import annotations

import argparse
import csv
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple


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


class AutomationError(Exception):
    """Raised when actual test-pair evidence cannot be assembled safely."""


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8-sig", errors="replace")
    except OSError as exc:
        raise AutomationError(f"could not read '{path}': {exc}") from exc


def summary_run_stamp(summary_path: Path) -> str:
    if summary_path.exists():
        text = read_text(summary_path)
        match = re.search(
            r"^Run stamp\s*:\s*(\d{8}_\d{6})\s*$",
            text,
            flags=re.MULTILINE,
        )
        if match:
            return match.group(1)
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def infer_cover_category(path_text: str) -> str:
    name = Path(path_text).name.lower()
    if "near_silence" in name or "silence" in name:
        return "near_silence"
    if "sparse" in name or "quiet" in name:
        return "sparse_quiet"
    if "speech" in name or "voice" in name:
        return "speech"
    if "music" in name or "blues" in name or "rock" in name:
        return "music"
    if "tone" in name or "sine" in name or "sweep" in name:
        return "synthetic_tone"
    return "other_audio"


def infer_payload_category(path_text: str) -> str:
    suffix = Path(path_text).suffix.lower()
    if suffix in {".txt", ".md", ".csv", ".json", ".xml"}:
        return "text"
    if suffix in {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tif", ".tiff"}:
        return "image"
    if suffix in {".wav", ".mp3", ".flac", ".aac", ".ogg"}:
        return "audio"
    if suffix in {".zip", ".gz", ".7z", ".rar", ".bz2", ".xz"}:
        return "compressed"
    if suffix in {".enc", ".gpg", ".pgp", ".aes"}:
        return "encrypted"
    return "binary"


def parse_actual_round_trip_pairs(log_path: Path, run_id: str) -> List[Dict[str, str]]:
    text = read_text(log_path)

    start_pattern = re.compile(
        r'^=== T(?P<number>\d+): cover="(?P<cover>[^"]+)"\r?\n'
        r'\s+payload="(?P<payload>[^"]+)"\s*$',
        flags=re.MULTILINE,
    )
    starts = list(start_pattern.finditer(text))
    if not starts:
        raise AutomationError(
            f"no T1-Tn round-trip sections were found in '{log_path}'"
        )

    rows: List[Dict[str, str]] = []

    for index, match in enumerate(starts):
        number = int(match.group("number"))
        section_end = (
            starts[index + 1].start()
            if index + 1 < len(starts)
            else text.find("\n=== E1:", match.end())
        )
        if section_end < 0:
            section_end = len(text)
        block = text[match.start():section_end]

        stego_match = re.search(
            r"^Stego file:\s+(.+?)\s*$",
            block,
            flags=re.MULTILINE,
        )
        extracted_match = re.search(
            r"^Output file:\s+(.+?)\s*$",
            block,
            flags=re.MULTILINE,
        )
        result_match = re.search(
            rf"^T{number}\s+(PASS(?:-PARTIAL)?)\b.*$",
            block,
            flags=re.MULTILINE,
        )

        if stego_match is None:
            raise AutomationError(f"T{number} has no recorded stego file")
        if extracted_match is None:
            raise AutomationError(f"T{number} has no recorded extracted file")
        if result_match is None:
            raise AutomationError(f"T{number} has no PASS/PASS-PARTIAL result")

        cover = match.group("cover").strip()
        payload = match.group("payload").strip()
        stego = stego_match.group(1).strip()
        extracted = extracted_match.group(1).strip()
        test_result = result_match.group(1)

        rows.append(
            {
                "run_id": run_id,
                "test_id": f"T{number}",
                "matrix_group": "automated_regression_actual_pairs",
                "cover_category": infer_cover_category(cover),
                "payload_category": infer_payload_category(payload),
                "target_payload_fraction": "actual_regression_request",
                "parameter_set": "fixed_v1",
                "cover_path": cover,
                "stego_path": stego,
                "payload_path": payload,
                "extracted_path": extracted,
                "requested_payload_bytes_override": "",
                "segment_len_frames": "2048",
                "delay_zero_samples": "150",
                "delay_one_samples": "200",
                "echo_decay": "0.4",
                "repetition": "7",
                "header_bits": "64",
                "auditory_rating": "",
                "listener_id": "",
                "listening_original_available": "",
                "playback_device": "",
                "listening_environment": "",
                "listening_notes": "",
                "notes": (
                    f"Generated from {log_path.name}; harness result={test_result}"
                ),
            }
        )

    rows.sort(key=lambda row: int(row["test_id"][1:]))
    return rows


def unique_artifact_stamp(history_dir: Path, run_id: str) -> str:
    candidate = run_id
    counter = 1
    while (history_dir / f"pg14_results_{candidate}.csv").exists():
        candidate = f"{run_id}_rerun{counter:02d}"
        counter += 1
    return candidate


def write_matrix(path: Path, rows: Sequence[Dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=MATRIX_FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def float_values(rows: Sequence[Dict[str, str]], field: str) -> List[float]:
    values: List[float] = []
    for row in rows:
        text = row.get(field, "").strip()
        if text and text not in {"inf", "-inf"}:
            try:
                values.append(float(text))
            except ValueError:
                pass
    return values


def metric_line(label: str, values: Sequence[float]) -> str:
    if not values:
        return f"{label:<28}: unavailable"
    return (
        f"{label:<28}: min={min(values):.6g}, "
        f"mean={sum(values) / len(values):.6g}, max={max(values):.6g}"
    )


def summarize_results(
    results_path: Path,
    summary_path: Path,
    run_id: str,
    matrix_path: Path,
) -> Tuple[int, int]:
    with results_path.open("r", newline="", encoding="utf-8-sig") as stream:
        rows = list(csv.DictReader(stream))

    passed = sum(row.get("status") == "PASS" for row in rows)
    failed = len(rows) - passed
    prefix_exact = sum(
        row.get("prefix_exact", "").strip().lower() == "true"
        for row in rows
    )
    zero_prefix_ber = sum(
        row.get("ber_compared", "").strip() in {"0", "0.0"}
        for row in rows
    )

    lines = [
        "Echo Hiding Audio - Latest PG-14 Summary",
        "========================================",
        f"Run ID                      : {run_id}",
        f"Matrix file                 : {matrix_path}",
        f"Results file                : {results_path}",
        f"Rows analyzed               : {len(rows)}",
        f"Rows passed                 : {passed}",
        f"Rows failed                 : {failed}",
        f"Exact recovered prefixes    : {prefix_exact}",
        f"Zero compared-prefix BER    : {zero_prefix_ber}",
        "",
        metric_line("MSE", float_values(rows, "mse")),
        metric_line("SNR dB", float_values(rows, "snr_db")),
        metric_line(
            "Histogram TV",
            float_values(rows, "histogram_total_variation"),
        ),
        metric_line(
            "Echo detector delta L1",
            float_values(rows, "echo_detector_delta_l1"),
        ),
        "",
        "Interpretation:",
        "- Compared-prefix BER evaluates the bytes extraction actually recovered.",
        "- End-to-end BER also counts bytes omitted after cover exhaustion.",
        "- Statistical and detector metrics must be interpreted across cover types",
        "  and payload levels; one file does not establish a universal threshold.",
    ]

    summary_path.write_text(
        "\n".join(lines) + "\n",
        encoding="utf-8",
        newline="\r\n",
    )
    return passed, failed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Analyze the actual T1-T10 cover/stego/payload/extracted pairs "
            "recorded by the latest Echo Hiding Audio regression run."
        )
    )
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent

    parser.add_argument(
        "--project-root",
        default=str(project_root),
        help="Project root containing Analysis, Tests, and TestData.",
    )
    parser.add_argument(
        "--log",
        default="",
        help="Override Tests/Latest Test Run.log.",
    )
    parser.add_argument(
        "--test-summary",
        default="",
        help="Override Tests/Test Run Summary.txt.",
    )
    parser.add_argument(
        "--output-dir",
        default="",
        help="Override Analysis/results.",
    )
    parser.add_argument(
        "--analysis-script",
        default=str(script_dir / "pg14_analysis.py"),
        help="Path to pg14_analysis.py.",
    )
    parser.add_argument(
        "--python",
        default=sys.executable,
        help="Python interpreter used to launch pg14_analysis.py.",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()

    project_root = Path(args.project_root).expanduser().resolve()
    log_path = (
        Path(args.log).expanduser().resolve()
        if args.log
        else project_root / "Tests" / "Latest Test Run.log"
    )
    test_summary_path = (
        Path(args.test_summary).expanduser().resolve()
        if args.test_summary
        else project_root / "Tests" / "Test Run Summary.txt"
    )
    output_dir = (
        Path(args.output_dir).expanduser().resolve()
        if args.output_dir
        else project_root / "Analysis" / "results"
    )
    analysis_script = Path(args.analysis_script).expanduser().resolve()

    try:
        if not log_path.exists():
            raise AutomationError(
                f"latest test log does not exist: '{log_path}'"
            )
        if not analysis_script.exists():
            raise AutomationError(
                f"analysis script does not exist: '{analysis_script}'"
            )

        run_id = summary_run_stamp(test_summary_path)
        history_dir = output_dir / "history"
        history_dir.mkdir(parents=True, exist_ok=True)
        artifact_stamp = unique_artifact_stamp(history_dir, run_id)

        rows = parse_actual_round_trip_pairs(log_path, run_id)

        matrix_archive = history_dir / f"pg14_matrix_{artifact_stamp}.csv"
        results_archive = history_dir / f"pg14_results_{artifact_stamp}.csv"
        summary_archive = history_dir / f"pg14_summary_{artifact_stamp}.txt"

        write_matrix(matrix_archive, rows)

        command = [
            args.python,
            str(analysis_script),
            "matrix",
            "--matrix",
            str(matrix_archive),
            "--output",
            str(results_archive),
            "--run-id",
            run_id,
        ]
        completed = subprocess.run(command)

        if not results_archive.exists():
            raise AutomationError(
                "pg14_analysis.py did not create the expected results CSV"
            )

        passed, failed = summarize_results(
            results_archive,
            summary_archive,
            run_id,
            matrix_archive,
        )

        latest_matrix = output_dir / "Latest PG14 Matrix.csv"
        latest_results = output_dir / "Latest PG14 Results.csv"
        latest_summary = output_dir / "Latest PG14 Summary.txt"

        shutil.copy2(matrix_archive, latest_matrix)
        shutil.copy2(results_archive, latest_results)
        shutil.copy2(summary_archive, latest_summary)

        print()
        print("PG-14 actual-pair automation complete")
        print("-------------------------------------")
        print(f"Regression pairs found : {len(rows)}")
        print(f"Analysis rows passed   : {passed}")
        print(f"Analysis rows failed   : {failed}")
        print(f"Latest matrix          : {latest_matrix}")
        print(f"Latest results         : {latest_results}")
        print(f"Latest summary         : {latest_summary}")
        print(f"Archived results       : {results_archive}")

        if completed.returncode != 0 or failed != 0:
            return 1
        return 0

    except (AutomationError, OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
