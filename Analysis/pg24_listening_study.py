#!/usr/bin/env python3
# ============================================================================
# pg24_listening_study.py
#
# Course:      CS 4463 / CS 5173 - Team 21
# Project:     Echo Hiding Audio
# Authors:     John N. Weaver and Alex W. Bryant
# GitHub:      https://github.com/John-N-Weaver/Echo-Hiding-Audio
# Created:     July 29, 2026
# Last updated: July 29, 2026
#
# Purpose:
#   Prepare and summarize the controlled auditory-perception evaluation needed
#   for PG-13 and PG-24.
# ============================================================================

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import math
import os
import random
import shutil
import struct
import sys
import wave
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Sequence, Tuple


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT_DEFAULT = SCRIPT_DIR.parent
DEFAULT_PG23_COMBINED = (
    PROJECT_ROOT_DEFAULT
    / "Analysis"
    / "pg23_results"
    / "Latest PG23 Combined Results.csv"
)
DEFAULT_OUTPUT_ROOT = PROJECT_ROOT_DEFAULT / "Analysis" / "pg24_results"
DEFAULT_FRACTIONS = ("0", "0.25", "0.5", "0.75", "1", "1.25")
ALLOWED_RATINGS = (
    "obvious",
    "apparent_close_listening",
    "undetectable_without_original",
)
PERCEPTIBLE_RATINGS = {"obvious", "apparent_close_listening"}

KEY_FIELDS = [
    "study_id",
    "pair_id",
    "presentation_order",
    "file_a",
    "file_b",
    "a_is",
    "b_is",
    "cover_category",
    "cover_name",
    "cover_path",
    "stego_path",
    "test_id",
    "target_payload_fraction",
    "requested_payload_bytes",
    "payload_capacity_complete_bytes",
    "requested_to_capacity_ratio",
    "sample_rate_hz",
    "channels",
    "bits_per_sample",
    "clip_start_sec",
    "clip_duration_sec",
    "clip_frames",
    "pg23_case_pass",
    "pg14_analysis_status",
    "ber_compared",
    "normalized_rmse",
    "snr_db",
    "histogram_total_variation",
    "echo_detector_delta_l1",
]

RATING_FIELDS = [
    "study_id",
    "pair_id",
    "listener_id",
    "rating",
    "playback_device",
    "listening_environment",
    "original_available",
    "replays",
    "notes",
    "rated_at_utc",
]

COMBINED_FIELDS = KEY_FIELDS + [
    "listener_id",
    "rating",
    "playback_device",
    "listening_environment",
    "original_available",
    "replays",
    "rating_notes",
    "rated_at_utc",
    "rating_valid",
]

SUMMARY_FIELDS = [
    "cover_name",
    "cover_category",
    "ratings_count",
    "ratings_complete",
    "first_apparent_or_obvious_fraction",
    "first_obvious_fraction",
    "highest_undetectable_fraction",
    "perceptual_threshold_category",
    "notes",
]


class StudyError(Exception):
    pass


@dataclass
class WaveInfo:
    channels: int
    sample_width: int
    sample_rate: int
    frame_count: int
    frames: bytes

    @property
    def block_align(self) -> int:
        return self.channels * self.sample_width

    @property
    def bits_per_sample(self) -> int:
        return self.sample_width * 8

    @property
    def duration_sec(self) -> float:
        return self.frame_count / self.sample_rate if self.sample_rate else 0.0


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def run_stamp() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def read_csv(path: Path) -> List[Dict[str, str]]:
    try:
        with path.open("r", newline="", encoding="utf-8-sig") as stream:
            return list(csv.DictReader(stream))
    except OSError as exc:
        raise StudyError(f"could not read CSV '{path}': {exc}") from exc


def write_csv(
    path: Path,
    fields: Sequence[str],
    rows: Sequence[Dict[str, object]],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(fields))
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fields})


def normalize_fraction(value: object) -> str:
    text = str(value).strip()
    try:
        number = float(text)
    except ValueError:
        return text
    return f"{number:.6f}".rstrip("0").rstrip(".") or "0"


def parse_fractions(text: str) -> Tuple[str, ...]:
    result: List[str] = []
    for token in text.split(","):
        normalized = normalize_fraction(token)
        if normalized and normalized not in result:
            result.append(normalized)
    if not result:
        raise argparse.ArgumentTypeError("at least one fraction is required")
    return tuple(result)


def read_wave(path: Path) -> WaveInfo:
    try:
        with wave.open(str(path), "rb") as stream:
            channels = stream.getnchannels()
            width = stream.getsampwidth()
            rate = stream.getframerate()
            frames = stream.getnframes()
            compression = stream.getcomptype()
            if compression != "NONE":
                raise StudyError(
                    f"compressed WAV is unsupported for listening excerpts: '{path}'"
                )
            if width not in {1, 2}:
                raise StudyError(
                    f"only 8-bit and 16-bit PCM are supported: '{path}'"
                )
            data = stream.readframes(frames)
    except (wave.Error, OSError) as exc:
        raise StudyError(f"could not read WAV '{path}': {exc}") from exc

    return WaveInfo(
        channels=channels,
        sample_width=width,
        sample_rate=rate,
        frame_count=frames,
        frames=data,
    )


def write_pcm_wave(path: Path, source: WaveInfo, frames: bytes) -> None:
    # Write valid classic PCM, including a RIFF pad byte for odd data lengths.
    block_align = source.block_align
    if len(frames) % block_align != 0:
        raise StudyError("excerpt data is not aligned to complete audio frames")

    data_size = len(frames)
    pad = b"\x00" if data_size % 2 else b""
    byte_rate = source.sample_rate * block_align
    fmt_data = struct.pack(
        "<HHIIHH",
        1,
        source.channels,
        source.sample_rate,
        byte_rate,
        block_align,
        source.bits_per_sample,
    )
    riff_size = 4 + (8 + len(fmt_data)) + (8 + data_size + len(pad))

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as output:
        output.write(b"RIFF")
        output.write(struct.pack("<I", riff_size))
        output.write(b"WAVE")
        output.write(b"fmt ")
        output.write(struct.pack("<I", len(fmt_data)))
        output.write(fmt_data)
        output.write(b"data")
        output.write(struct.pack("<I", data_size))
        output.write(frames)
        output.write(pad)


def excerpt_frames(
    wave_info: WaveInfo,
    start_sec: float,
    clip_seconds: float,
) -> Tuple[bytes, float, float, int]:
    if wave_info.frame_count <= 0:
        raise StudyError("cannot excerpt an empty WAV")

    requested_frames = max(1, int(round(clip_seconds * wave_info.sample_rate)))
    actual_frames = min(requested_frames, wave_info.frame_count)

    maximum_start = max(wave_info.frame_count - actual_frames, 0)
    requested_start = int(round(start_sec * wave_info.sample_rate))
    start_frame = max(0, min(requested_start, maximum_start))
    end_frame = start_frame + actual_frames

    start_byte = start_frame * wave_info.block_align
    end_byte = end_frame * wave_info.block_align
    data = wave_info.frames[start_byte:end_byte]

    return (
        data,
        start_frame / wave_info.sample_rate,
        actual_frames / wave_info.sample_rate,
        actual_frames,
    )


def center_start(wave_info: WaveInfo, clip_seconds: float) -> float:
    available = wave_info.duration_sec
    return max((available - min(available, clip_seconds)) / 2.0, 0.0)


def resolve_existing_path(value: str, project_root: Path) -> Path:
    path = Path(value).expanduser()
    if not path.is_absolute():
        path = project_root / path
    path = path.resolve()
    if not path.exists():
        raise StudyError(f"referenced file does not exist: '{path}'")
    return path


def select_pg23_rows(
    rows: Sequence[Dict[str, str]],
    fractions: Sequence[str],
) -> List[Dict[str, str]]:
    wanted = set(fractions)
    selected: List[Dict[str, str]] = []

    for row in rows:
        if row.get("test_kind", "") != "payload_fraction":
            continue
        if row.get("case_pass", "").strip().lower() != "true":
            continue
        if row.get("analysis_status", "") != "PASS":
            continue
        fraction = normalize_fraction(row.get("target_payload_fraction", ""))
        if fraction not in wanted:
            continue
        if not row.get("cover_path") or not row.get("stego_path"):
            continue

        copied = dict(row)
        copied["target_payload_fraction"] = fraction
        selected.append(copied)

    selected.sort(
        key=lambda row: (
            row.get("cover_category", ""),
            Path(row.get("cover_path", "")).name.lower(),
            float(row.get("target_payload_fraction", "0")),
        )
    )
    return selected


def unique_study_dir(output_root: Path, study_id: str) -> Tuple[str, Path]:
    history = output_root / "history"
    history.mkdir(parents=True, exist_ok=True)

    candidate = study_id
    counter = 1
    while (history / candidate).exists():
        candidate = f"{study_id}_rerun{counter:02d}"
        counter += 1

    path = history / candidate
    path.mkdir(parents=True, exist_ok=False)
    return candidate, path


def relative_audio_path(path: Path, html_path: Path) -> str:
    return os.path.relpath(path, html_path.parent).replace("\\", "/")


def study_html(
    study_id: str,
    pairs: Sequence[Dict[str, object]],
    html_path: Path,
) -> str:
    public_pairs = [
        {
            "pair_id": row["pair_id"],
            "file_a": relative_audio_path(Path(str(row["file_a"])), html_path),
            "file_b": relative_audio_path(Path(str(row["file_b"])), html_path),
        }
        for row in pairs
    ]
    pair_json = json.dumps(public_pairs, ensure_ascii=False)

    return f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>PG-24 Auditory Perception Study</title>
<style>
body {{
  font-family: Arial, sans-serif;
  max-width: 900px;
  margin: 2rem auto;
  padding: 0 1rem;
  line-height: 1.45;
}}
fieldset {{ margin: 1rem 0; padding: 1rem; }}
label {{ display: block; margin: 0.55rem 0; }}
input[type="text"] {{ width: 100%; max-width: 650px; padding: 0.35rem; }}
textarea {{ width: 100%; min-height: 5rem; }}
audio {{ width: 100%; margin: 0.35rem 0 1rem; }}
button {{ padding: 0.7rem 1rem; margin-right: 0.5rem; }}
#progress {{ font-weight: bold; }}
.warning {{ color: #8b0000; font-weight: bold; }}
</style>
</head>
<body>
<h1>PG-24 Auditory Perception Study</h1>
<p>Study ID: <code>{html.escape(study_id)}</code></p>
<p>
Listen to both excerpts using the same playback device and volume. File A and
File B are randomized; one is the original cover and the other is its stego
version. Rate the audible difference using exactly one required category.
</p>
<ul>
<li><strong>obvious</strong>: immediately noticeable during ordinary listening.</li>
<li><strong>apparent_close_listening</strong>: apparent only after focused comparison or replay.</li>
<li><strong>undetectable_without_original</strong>: the modified version would not be identifiable without direct access to the original.</li>
</ul>

<fieldset>
<legend>Listener information</legend>
<label>Listener ID
<input id="listener_id" type="text" required placeholder="Example: JW01"></label>
<label>Playback device
<input id="playback_device" type="text" required placeholder="Example: wired over-ear headphones"></label>
<label>Listening environment
<input id="listening_environment" type="text" required placeholder="Example: quiet home office"></label>
</fieldset>

<p id="progress"></p>
<h2 id="pair_title"></h2>
<label>File A</label>
<audio id="audio_a" controls preload="metadata"></audio>
<label>File B</label>
<audio id="audio_b" controls preload="metadata"></audio>

<fieldset>
<legend>Rating</legend>
<label><input type="radio" name="rating" value="obvious"> obvious</label>
<label><input type="radio" name="rating" value="apparent_close_listening"> apparent_close_listening</label>
<label><input type="radio" name="rating" value="undetectable_without_original"> undetectable_without_original</label>
<label>Number of replays
<input id="replays" type="number" min="0" value="0"></label>
<label>Optional notes
<textarea id="notes"></textarea></label>
</fieldset>

<button id="previous" type="button">Previous</button>
<button id="save_next" type="button">Save and Next</button>
<button id="download" type="button">Download Completed Ratings CSV</button>
<p id="message" class="warning"></p>

<script>
const studyId = {json.dumps(study_id)};
const pairs = {pair_json};
const ratings = {{}};
let index = 0;

function value(id) {{
  return document.getElementById(id).value.trim();
}}

function selectedRating() {{
  const selected = document.querySelector('input[name="rating"]:checked');
  return selected ? selected.value : "";
}}

function loadPair() {{
  const pair = pairs[index];
  document.getElementById("progress").textContent =
    `Pair ${{index + 1}} of ${{pairs.length}}`;
  document.getElementById("pair_title").textContent = pair.pair_id;
  document.getElementById("audio_a").src = pair.file_a;
  document.getElementById("audio_b").src = pair.file_b;

  document.querySelectorAll('input[name="rating"]').forEach(x => x.checked = false);
  document.getElementById("replays").value = 0;
  document.getElementById("notes").value = "";
  document.getElementById("message").textContent = "";

  if (ratings[pair.pair_id]) {{
    const saved = ratings[pair.pair_id];
    const radio = document.querySelector(
      `input[name="rating"][value="${{saved.rating}}"]`
    );
    if (radio) radio.checked = true;
    document.getElementById("replays").value = saved.replays;
    document.getElementById("notes").value = saved.notes;
  }}
}}

function saveCurrent() {{
  const listener = value("listener_id");
  const device = value("playback_device");
  const environment = value("listening_environment");
  const rating = selectedRating();

  if (!listener || !device || !environment) {{
    document.getElementById("message").textContent =
      "Complete all listener information fields.";
    return false;
  }}
  if (!rating) {{
    document.getElementById("message").textContent =
      "Select one required rating category.";
    return false;
  }}

  const pair = pairs[index];
  ratings[pair.pair_id] = {{
    study_id: studyId,
    pair_id: pair.pair_id,
    listener_id: listener,
    rating: rating,
    playback_device: device,
    listening_environment: environment,
    original_available: "true",
    replays: document.getElementById("replays").value || "0",
    notes: document.getElementById("notes").value.replace(/\\r?\\n/g, " "),
    rated_at_utc: new Date().toISOString()
  }};
  return true;
}}

function csvEscape(value) {{
  const text = String(value ?? "");
  return '"' + text.replaceAll('"', '""') + '"';
}}

document.getElementById("save_next").addEventListener("click", () => {{
  if (!saveCurrent()) return;
  if (index < pairs.length - 1) {{
    index += 1;
    loadPair();
  }} else {{
    document.getElementById("message").textContent =
      "All pairs reached. Download the completed ratings CSV.";
  }}
}});

document.getElementById("previous").addEventListener("click", () => {{
  if (index > 0) {{
    index -= 1;
    loadPair();
  }}
}});

document.getElementById("download").addEventListener("click", () => {{
  if (!saveCurrent()) return;
  const missing = pairs.filter(pair => !ratings[pair.pair_id]);
  if (missing.length) {{
    document.getElementById("message").textContent =
      `${{missing.length}} pair(s) are still unrated.`;
    return;
  }}

  const fields = {json.dumps(RATING_FIELDS)};
  const lines = [fields.map(csvEscape).join(",")];
  pairs.forEach(pair => {{
    const row = ratings[pair.pair_id];
    lines.push(fields.map(field => csvEscape(row[field])).join(","));
  }});

  const blob = new Blob(["\\ufeff" + lines.join("\\r\\n") + "\\r\\n"], {{
    type: "text/csv;charset=utf-8"
  }});
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = `PG24_Ratings_${{studyId}}.csv`;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  URL.revokeObjectURL(url);
  document.getElementById("message").textContent =
    "Ratings CSV downloaded. Run the PG-24 summary command with that file.";
}});

loadPair();
</script>
</body>
</html>
'''


def prepare(args: argparse.Namespace) -> int:
    project_root = Path(args.project_root).expanduser().resolve()
    input_csv = Path(args.pg23_results).expanduser()
    if not input_csv.is_absolute():
        input_csv = project_root / input_csv
    input_csv = input_csv.resolve()

    output_root = Path(args.output_root).expanduser()
    if not output_root.is_absolute():
        output_root = project_root / output_root
    output_root = output_root.resolve()

    rows = read_csv(input_csv)
    selected = select_pg23_rows(rows, args.fractions)
    if not selected:
        raise StudyError(
            "no successful PG-23 payload-fraction cases matched the requested fractions"
        )

    cover_names = {
        Path(row["cover_path"]).name.lower()
        for row in selected
    }
    cover_fraction_pairs = {
        (
            Path(row["cover_path"]).name.lower(),
            row["target_payload_fraction"],
        )
        for row in selected
    }
    expected_pair_count = len(cover_names) * len(args.fractions)
    if len(cover_fraction_pairs) != expected_pair_count:
        raise StudyError(
            "the selected PG-23 data are incomplete; expected one row per "
            "cover and requested fraction"
        )

    requested_study_id = args.study_id or run_stamp()
    study_id, study_dir = unique_study_dir(output_root, requested_study_id)
    audio_dir = study_dir / "audio"
    html_path = study_dir / "PG24 Listening Study.html"
    key_path = study_dir / "PG24 Study Key.csv"
    manifest_path = study_dir / "PG24 Study Manifest.txt"

    seed = args.seed or int(
        hashlib.sha256(study_id.encode("utf-8")).hexdigest()[:16], 16
    )
    randomizer = random.Random(seed)

    prepared: List[Dict[str, object]] = []
    for row_number, row in enumerate(selected, start=1):
        cover_path = resolve_existing_path(row["cover_path"], project_root)
        stego_path = resolve_existing_path(row["stego_path"], project_root)

        cover_wave = read_wave(cover_path)
        stego_wave = read_wave(stego_path)
        if (
            cover_wave.channels != stego_wave.channels
            or cover_wave.sample_width != stego_wave.sample_width
            or cover_wave.sample_rate != stego_wave.sample_rate
            or cover_wave.frame_count != stego_wave.frame_count
        ):
            raise StudyError(
                f"cover/stego format mismatch for test {row.get('test_id', '')}"
            )

        start_sec = center_start(cover_wave, args.clip_seconds)
        cover_excerpt, actual_start, actual_duration, clip_frames = excerpt_frames(
            cover_wave, start_sec, args.clip_seconds
        )
        stego_excerpt, _, _, stego_clip_frames = excerpt_frames(
            stego_wave, start_sec, args.clip_seconds
        )
        if clip_frames != stego_clip_frames:
            raise StudyError("cover/stego excerpt frame counts differ")

        pair_id = f"P{row_number:03d}"
        original_first = bool(randomizer.getrandbits(1))
        file_a = audio_dir / f"{pair_id}_A.wav"
        file_b = audio_dir / f"{pair_id}_B.wav"

        if original_first:
            write_pcm_wave(file_a, cover_wave, cover_excerpt)
            write_pcm_wave(file_b, stego_wave, stego_excerpt)
            a_is, b_is = "cover", "stego"
        else:
            write_pcm_wave(file_a, stego_wave, stego_excerpt)
            write_pcm_wave(file_b, cover_wave, cover_excerpt)
            a_is, b_is = "stego", "cover"

        prepared.append(
            {
                "study_id": study_id,
                "pair_id": pair_id,
                "presentation_order": row_number,
                "file_a": str(file_a),
                "file_b": str(file_b),
                "a_is": a_is,
                "b_is": b_is,
                "cover_category": row.get("cover_category", ""),
                "cover_name": cover_path.name,
                "cover_path": str(cover_path),
                "stego_path": str(stego_path),
                "test_id": row.get("test_id", ""),
                "target_payload_fraction": row.get(
                    "target_payload_fraction", ""
                ),
                "requested_payload_bytes": row.get(
                    "requested_payload_bytes", ""
                ),
                "payload_capacity_complete_bytes": row.get(
                    "payload_capacity_complete_bytes", ""
                ),
                "requested_to_capacity_ratio": row.get(
                    "requested_to_capacity_ratio", ""
                ),
                "sample_rate_hz": row.get("sample_rate_hz", ""),
                "channels": row.get("channels", ""),
                "bits_per_sample": row.get("bits_per_sample", ""),
                "clip_start_sec": actual_start,
                "clip_duration_sec": actual_duration,
                "clip_frames": clip_frames,
                "pg23_case_pass": row.get("case_pass", ""),
                "pg14_analysis_status": row.get("analysis_status", ""),
                "ber_compared": row.get("ber_compared", ""),
                "normalized_rmse": row.get("normalized_rmse", ""),
                "snr_db": row.get("snr_db", ""),
                "histogram_total_variation": row.get(
                    "histogram_total_variation", ""
                ),
                "echo_detector_delta_l1": row.get(
                    "echo_detector_delta_l1", ""
                ),
            }
        )

    randomizer.shuffle(prepared)
    for order, row in enumerate(prepared, start=1):
        row["presentation_order"] = order

    write_csv(key_path, KEY_FIELDS, prepared)
    html_path.write_text(
        study_html(study_id, prepared, html_path),
        encoding="utf-8",
        newline="\r\n",
    )

    manifest_lines = [
        "Echo Hiding Audio - PG-13 / PG-24 Listening Study Manifest",
        "==========================================================",
        f"Study ID                  : {study_id}",
        f"Created UTC               : {utc_now()}",
        f"PG-23 combined results    : {input_csv}",
        f"Pairs prepared            : {len(prepared)}",
        f"Fractions                 : {', '.join(args.fractions)}",
        f"Clip duration requested   : {args.clip_seconds} seconds",
        f"Randomization seed        : {seed}",
        f"Listening form            : {html_path}",
        f"Study key                 : {key_path}",
        "",
        "Required ratings:",
        "- obvious",
        "- apparent_close_listening",
        "- undetectable_without_original",
        "",
        "Keep the study key closed until ratings are completed.",
    ]
    manifest_path.write_text(
        "\n".join(manifest_lines) + "\n",
        encoding="utf-8",
        newline="\r\n",
    )

    for source, destination in [
        (html_path, output_root / "Latest PG24 Listening Study.html"),
        (key_path, output_root / "Latest PG24 Study Key.csv"),
        (manifest_path, output_root / "Latest PG24 Study Manifest.txt"),
    ]:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    print("PG-13 / PG-24 listening study prepared")
    print("--------------------------------------")
    print(f"Study ID       : {study_id}")
    print(f"Pairs          : {len(prepared)}")
    print(f"Study directory: {study_dir}")
    print(f"Open in browser: {html_path}")
    print(f"Study key      : {key_path}")
    return 0


def validate_rating_rows(
    ratings: Sequence[Dict[str, str]],
    key_rows: Sequence[Dict[str, str]],
) -> Tuple[List[Dict[str, object]], List[str]]:
    key_by_pair = {row["pair_id"]: row for row in key_rows}
    seen: set[Tuple[str, str]] = set()
    errors: List[str] = []
    combined: List[Dict[str, object]] = []

    for row_number, rating in enumerate(ratings, start=2):
        pair_id = rating.get("pair_id", "").strip()
        listener_id = rating.get("listener_id", "").strip()
        category = rating.get("rating", "").strip()

        if pair_id not in key_by_pair:
            errors.append(f"ratings line {row_number}: unknown pair_id '{pair_id}'")
            continue
        if not listener_id:
            errors.append(f"ratings line {row_number}: listener_id is blank")
            continue
        if category not in ALLOWED_RATINGS:
            errors.append(
                f"ratings line {row_number}: invalid rating '{category}'"
            )
            continue

        duplicate_key = (listener_id, pair_id)
        if duplicate_key in seen:
            errors.append(
                f"ratings line {row_number}: duplicate listener/pair {duplicate_key}"
            )
            continue
        seen.add(duplicate_key)

        key = key_by_pair[pair_id]
        combined_row: Dict[str, object] = dict(key)
        combined_row.update(
            {
                "listener_id": listener_id,
                "rating": category,
                "playback_device": rating.get("playback_device", "").strip(),
                "listening_environment": rating.get(
                    "listening_environment", ""
                ).strip(),
                "original_available": rating.get(
                    "original_available", ""
                ).strip(),
                "replays": rating.get("replays", "").strip(),
                "rating_notes": rating.get("notes", "").strip(),
                "rated_at_utc": rating.get("rated_at_utc", "").strip(),
                "rating_valid": True,
            }
        )
        combined.append(combined_row)

    listeners = sorted({str(row["listener_id"]) for row in combined})
    for listener in listeners:
        listener_pairs = {
            str(row["pair_id"])
            for row in combined
            if row["listener_id"] == listener
        }
        missing = sorted(set(key_by_pair) - listener_pairs)
        if missing:
            errors.append(
                f"listener '{listener}' is missing {len(missing)} pair(s): "
                + ", ".join(missing)
            )

    return combined, errors


def first_fraction(
    rows: Sequence[Dict[str, object]],
    accepted: set[str],
) -> str:
    values = sorted(
        {
            float(str(row["target_payload_fraction"]))
            for row in rows
            if str(row["rating"]) in accepted
        }
    )
    return normalize_fraction(values[0]) if values else ""


def highest_fraction(
    rows: Sequence[Dict[str, object]],
    accepted: set[str],
) -> str:
    values = sorted(
        {
            float(str(row["target_payload_fraction"]))
            for row in rows
            if str(row["rating"]) in accepted
        }
    )
    return normalize_fraction(values[-1]) if values else ""


def cover_summaries(
    combined: Sequence[Dict[str, object]],
    expected_ratings_per_cover: int,
) -> List[Dict[str, object]]:
    grouped: Dict[Tuple[str, str], List[Dict[str, object]]] = {}
    for row in combined:
        key = (str(row["cover_name"]), str(row["cover_category"]))
        grouped.setdefault(key, []).append(row)

    summaries: List[Dict[str, object]] = []
    for (cover_name, category), rows in sorted(grouped.items()):
        first_perceptible = first_fraction(rows, PERCEPTIBLE_RATINGS)
        first_obvious = first_fraction(rows, {"obvious"})
        highest_undetectable = highest_fraction(
            rows, {"undetectable_without_original"}
        )

        if first_obvious:
            threshold_category = "obvious"
        elif first_perceptible:
            threshold_category = "apparent_close_listening"
        else:
            threshold_category = "not_observed_in_tested_range"

        summaries.append(
            {
                "cover_name": cover_name,
                "cover_category": category,
                "ratings_count": len(rows),
                "ratings_complete": len(rows) >= expected_ratings_per_cover,
                "first_apparent_or_obvious_fraction": first_perceptible,
                "first_obvious_fraction": first_obvious,
                "highest_undetectable_fraction": highest_undetectable,
                "perceptual_threshold_category": threshold_category,
                "notes": (
                    "Threshold is reported per cover; do not generalize one "
                    "universal threshold from heterogeneous audio."
                ),
            }
        )
    return summaries


def summary_text(
    study_id: str,
    combined: Sequence[Dict[str, object]],
    errors: Sequence[str],
    cover_summary: Sequence[Dict[str, object]],
    fractions: Sequence[str],
) -> str:
    listeners = sorted({str(row["listener_id"]) for row in combined})
    complete = not errors and bool(combined)

    counts = {
        rating: sum(str(row["rating"]) == rating for row in combined)
        for rating in ALLOWED_RATINGS
    }

    fraction_lines: List[str] = []
    for fraction in sorted(set(fractions), key=float):
        rows = [
            row
            for row in combined
            if normalize_fraction(row["target_payload_fraction"]) == fraction
        ]
        if not rows:
            continue
        local_counts = {
            rating: sum(str(row["rating"]) == rating for row in rows)
            for rating in ALLOWED_RATINGS
        }
        fraction_lines.append(
            f"- Fraction {fraction}: n={len(rows)}; "
            f"obvious={local_counts['obvious']}; "
            f"apparent_close_listening="
            f"{local_counts['apparent_close_listening']}; "
            f"undetectable_without_original="
            f"{local_counts['undetectable_without_original']}"
        )

    lines = [
        "Echo Hiding Audio - PG-13 / PG-24 Auditory Summary",
        "===================================================",
        f"Study ID                         : {study_id}",
        f"Summarized UTC                   : {utc_now()}",
        f"Listeners                        : {', '.join(listeners)}",
        f"Validated rating rows            : {len(combined)}",
        f"Validation errors                : {len(errors)}",
        f"PG-24 three-category completion  : {'GO' if complete else 'NO-GO'}",
        f"PG-13 threshold documented       : {'GO' if complete else 'NO-GO'}",
        "",
        "Overall rating counts",
        "---------------------",
        f"obvious                          : {counts['obvious']}",
        f"apparent_close_listening         : {counts['apparent_close_listening']}",
        f"undetectable_without_original    : {counts['undetectable_without_original']}",
        "",
        "Ratings by requested capacity fraction",
        "--------------------------------------",
    ]
    lines.extend(fraction_lines)
    lines += [
        "",
        "Per-cover perceptual thresholds",
        "-------------------------------",
    ]
    for row in cover_summary:
        lines.append(
            f"- {row['cover_name']} ({row['cover_category']}): "
            f"first apparent/obvious="
            f"{row['first_apparent_or_obvious_fraction'] or 'not observed'}; "
            f"first obvious={row['first_obvious_fraction'] or 'not observed'}; "
            f"highest undetectable="
            f"{row['highest_undetectable_fraction'] or 'none'}"
        )

    lines += [
        "",
        "Interpretation",
        "--------------",
        "- The perceptual threshold is the first tested fraction rated either",
        "  apparent_close_listening or obvious for each cover.",
        "- An obvious threshold is reported separately.",
        "- Ratings are cover-specific.",
        "- The study uses paired original/stego excerpts.",
    ]

    if errors:
        lines += ["", "Validation errors", "-----------------"]
        lines.extend(f"- {error}" for error in errors)

    return "\n".join(lines) + "\n"


def summarize(args: argparse.Namespace) -> int:
    project_root = Path(args.project_root).expanduser().resolve()

    key_path = Path(args.key).expanduser()
    if not key_path.is_absolute():
        key_path = project_root / key_path
    key_path = key_path.resolve()

    rating_paths: List[Path] = []
    for value in args.ratings:
        path = Path(value).expanduser()
        if not path.is_absolute():
            path = project_root / path
        rating_paths.append(path.resolve())

    output_root = Path(args.output_root).expanduser()
    if not output_root.is_absolute():
        output_root = project_root / output_root
    output_root = output_root.resolve()

    key_rows = read_csv(key_path)
    if not key_rows:
        raise StudyError("study key contains no pairs")

    all_ratings: List[Dict[str, str]] = []
    for path in rating_paths:
        all_ratings.extend(read_csv(path))

    combined, errors = validate_rating_rows(all_ratings, key_rows)
    study_ids = {row["study_id"] for row in key_rows}
    if len(study_ids) != 1:
        raise StudyError("study key contains multiple study IDs")
    study_id = next(iter(study_ids))

    fractions = sorted(
        {normalize_fraction(row["target_payload_fraction"]) for row in key_rows},
        key=float,
    )
    listeners = {str(row["listener_id"]) for row in combined}
    expected_per_cover = len(fractions) * max(len(listeners), 1)
    summaries = cover_summaries(combined, expected_per_cover)

    study_dir = key_path.parent
    combined_path = study_dir / "PG24 Combined Ratings.csv"
    cover_summary_path = study_dir / "PG24 Per-Cover Thresholds.csv"
    summary_path = study_dir / "PG13 PG24 Auditory Summary.txt"

    write_csv(combined_path, COMBINED_FIELDS, combined)
    write_csv(cover_summary_path, SUMMARY_FIELDS, summaries)
    summary_path.write_text(
        summary_text(study_id, combined, errors, summaries, fractions),
        encoding="utf-8",
        newline="\r\n",
    )

    for source, name in [
        (combined_path, "Latest PG24 Combined Ratings.csv"),
        (cover_summary_path, "Latest PG24 Per-Cover Thresholds.csv"),
        (summary_path, "Latest PG13 PG24 Auditory Summary.txt"),
    ]:
        destination = output_root / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    print("PG-13 / PG-24 auditory results summarized")
    print("------------------------------------------")
    print(f"Study ID          : {study_id}")
    print(f"Listeners         : {len(listeners)}")
    print(f"Rating rows       : {len(combined)}")
    print(f"Validation errors : {len(errors)}")
    print(f"Combined ratings  : {combined_path}")
    print(f"Thresholds        : {cover_summary_path}")
    print(f"Summary           : {summary_path}")
    print(f"Overall result    : {'PASS' if not errors and combined else 'FAIL'}")

    return 0 if not errors and combined else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Prepare or summarize the PG-13/PG-24 listening study."
    )
    subparsers = parser.add_subparsers(dest="mode", required=True)

    prepare_parser = subparsers.add_parser(
        "prepare",
        help="Create blinded audio pairs and the browser rating form.",
    )
    prepare_parser.add_argument(
        "--project-root",
        default=str(PROJECT_ROOT_DEFAULT),
    )
    prepare_parser.add_argument(
        "--pg23-results",
        default=str(DEFAULT_PG23_COMBINED),
    )
    prepare_parser.add_argument(
        "--output-root",
        default=str(DEFAULT_OUTPUT_ROOT),
    )
    prepare_parser.add_argument(
        "--fractions",
        type=parse_fractions,
        default=DEFAULT_FRACTIONS,
    )
    prepare_parser.add_argument(
        "--clip-seconds",
        type=float,
        default=12.0,
    )
    prepare_parser.add_argument("--study-id", default="")
    prepare_parser.add_argument("--seed", type=int, default=0)
    prepare_parser.set_defaults(function=prepare)

    summarize_parser = subparsers.add_parser(
        "summarize",
        help="Validate completed ratings and calculate thresholds.",
    )
    summarize_parser.add_argument(
        "--project-root",
        default=str(PROJECT_ROOT_DEFAULT),
    )
    summarize_parser.add_argument(
        "--key",
        default=str(DEFAULT_OUTPUT_ROOT / "Latest PG24 Study Key.csv"),
    )
    summarize_parser.add_argument(
        "--ratings",
        action="append",
        required=True,
    )
    summarize_parser.add_argument(
        "--output-root",
        default=str(DEFAULT_OUTPUT_ROOT),
    )
    summarize_parser.set_defaults(function=summarize)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return int(args.function(args))
    except (StudyError, OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
