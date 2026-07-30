#!/usr/bin/env python3
# ============================================================================
# pg14_analysis.py
#
# Course:      CS 4463 / CS 5173 - Team 21
# Project:     Echo Hiding Audio
# Authors:     John N. Weaver and Alex W. Bryant
# GitHub:      https://github.com/John-N-Weaver/Echo-Hiding-Audio
# Created:     July 28, 2026
# Last updated: July 28, 2026
#
# Purpose:
#   Perform the basic statistical and detector-oriented checks required by
#   PG-14. The output schema is deliberately designed to become the results
#   table for the later PG-23 controlled test matrix.
#
# Design constraints:
#   * Python standard library only; no NumPy, SciPy, or third-party packages.
#   * Supports the project's 8-bit and 16-bit PCM mono/stereo WAV formats.
#   * Analyzes one cover/stego pair or every row in a PG-23 matrix CSV.
#   * Writes reproducible, machine-readable CSV results.
# ============================================================================

from __future__ import annotations

import argparse
import array
import csv
import hashlib
import math
import os
import struct
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Iterable, Iterator, List, Optional, Sequence, Tuple

ANALYSIS_VERSION = "1.0"
PCM_SUBFORMAT_GUID = bytes.fromhex(
    "0100000000001000800000aa00389b71"
)
POPCOUNT = tuple(bin(i).count("1") for i in range(256))

DEFAULT_SEGMENT_LEN = 2048
DEFAULT_DELAY_ZERO = 150
DEFAULT_DELAY_ONE = 200
DEFAULT_DECAY = 0.4
DEFAULT_REPETITION = 7
DEFAULT_HEADER_BITS = 64

INPUT_FIELDS = [
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

OUTPUT_FIELDS = [
    # Reproducibility and matrix identity
    "analysis_version",
    "timestamp_utc",
    "analysis_runtime_ms",
    "run_id",
    "test_id",
    "matrix_group",
    "status",
    "error_message",
    "cover_category",
    "payload_category",
    "target_payload_fraction",
    "parameter_set",
    "notes",

    # Input/output paths and hashes
    "cover_path",
    "stego_path",
    "payload_path",
    "extracted_path",
    "cover_sha256",
    "stego_sha256",
    "payload_sha256",
    "extracted_sha256",

    # WAV metadata
    "wav_format_code",
    "sample_rate_hz",
    "channels",
    "bits_per_sample",
    "valid_bits_per_sample",
    "block_align_bytes",
    "duration_sec",
    "frame_count",
    "sample_count",
    "cover_data_bytes",
    "stego_data_bytes",
    "metadata_match",
    "length_match",

    # Echo-hiding parameters
    "segment_len_frames",
    "delay_zero_samples",
    "delay_one_samples",
    "echo_decay",
    "repetition",
    "header_bits",

    # Capacity and payload demand
    "logical_capacity_bits",
    "payload_capacity_bits",
    "payload_capacity_complete_bytes",
    "requested_payload_bytes",
    "requested_payload_bits",
    "expected_embedded_payload_bits",
    "expected_recoverable_payload_bytes",
    "requested_to_capacity_ratio",
    "embedded_capacity_utilization",
    "cover_bytes_per_payload_byte",

    # Payload recovery / BER
    "extracted_payload_bytes",
    "compared_prefix_bytes",
    "prefix_exact",
    "byte_errors_compared",
    "byte_error_rate_compared",
    "bit_errors_compared",
    "ber_compared",
    "missing_payload_bytes",
    "extra_payload_bytes",
    "recovery_fraction",
    "end_to_end_error_bits",
    "end_to_end_ber",

    # Cover/stego distortion statistics
    "modified_samples",
    "sample_modification_rate",
    "cover_mean_sample",
    "stego_mean_sample",
    "mean_error",
    "mean_absolute_error",
    "mse",
    "rmse",
    "normalized_rmse",
    "peak_absolute_error",
    "peak_error_fraction_full_scale",
    "snr_db",
    "psnr_db",
    "pearson_correlation",

    # Distribution-oriented detectability
    "histogram_bins",
    "histogram_total_variation",
    "histogram_js_divergence_bits",

    # Echo-oriented detector metrics
    "cover_autocorr_delay_zero",
    "stego_autocorr_delay_zero",
    "autocorr_delta_delay_zero",
    "cover_autocorr_delay_one",
    "stego_autocorr_delay_one",
    "autocorr_delta_delay_one",
    "cover_delay_bias",
    "stego_delay_bias",
    "delay_bias_delta",
    "echo_detector_delta_l1",

    # PG-24 listening fields reserved for the later PG-23 matrix
    "auditory_rating",
    "listener_id",
    "listening_original_available",
    "playback_device",
    "listening_environment",
    "listening_notes",
]


class AnalysisError(Exception):
    """Raised when a row cannot be analyzed safely."""


@dataclass
class WavePcm:
    path: Path
    format_code: int
    channels: int
    sample_rate: int
    bits_per_sample: int
    valid_bits_per_sample: int
    block_align: int
    data: bytes

    @property
    def bytes_per_sample(self) -> int:
        return self.bits_per_sample // 8

    @property
    def sample_count(self) -> int:
        return len(self.data) // self.bytes_per_sample

    @property
    def frame_count(self) -> int:
        return len(self.data) // self.block_align

    @property
    def duration_sec(self) -> float:
        if self.sample_rate <= 0:
            return 0.0
        return self.frame_count / float(self.sample_rate)

    @property
    def full_scale(self) -> float:
        return 128.0 if self.bits_per_sample == 8 else 32768.0

    def centered_samples(self) -> Sequence[int]:
        """Return integer samples centered around zero without changing scale."""
        if self.bits_per_sample == 8:
            return tuple(value - 128 for value in self.data)

        values = array.array("h")
        values.frombytes(self.data)
        if sys.byteorder != "little":
            values.byteswap()
        return values


def utc_now_text() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def sha256_file(path: Optional[Path]) -> str:
    if path is None or not path.exists():
        return ""

    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def parse_int(value: object, default: int) -> int:
    text = "" if value is None else str(value).strip()
    return default if text == "" else int(text)


def parse_float(value: object, default: float) -> float:
    text = "" if value is None else str(value).strip()
    return default if text == "" else float(text)


def optional_path(value: object, base_dir: Path) -> Optional[Path]:
    text = "" if value is None else str(value).strip()
    if not text:
        return None

    path = Path(text).expanduser()
    if not path.is_absolute():
        path = base_dir / path
    return path.resolve()


def read_wave_pcm(path: Path) -> WavePcm:
    """Read the first fmt and data chunks from a supported RIFF/WAVE file."""
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise AnalysisError(f"could not read WAV '{path}': {exc}") from exc

    if len(raw) < 12:
        raise AnalysisError(f"'{path}' is too short to be a RIFF/WAVE file")
    if raw[0:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise AnalysisError(f"'{path}' is not a RIFF/WAVE file")

    declared_size = struct.unpack_from("<I", raw, 4)[0] + 8
    if declared_size > len(raw):
        raise AnalysisError(
            f"'{path}' is truncated: RIFF declares {declared_size} bytes, "
            f"file contains {len(raw)}"
        )

    fmt: Optional[bytes] = None
    data: Optional[bytes] = None
    offset = 12
    limit = declared_size

    while offset + 8 <= limit:
        chunk_id = raw[offset:offset + 4]
        chunk_size = struct.unpack_from("<I", raw, offset + 4)[0]
        payload_start = offset + 8
        payload_end = payload_start + chunk_size

        if payload_end > limit:
            raise AnalysisError(
                f"'{path}' contains a chunk extending past the RIFF boundary"
            )

        payload = raw[payload_start:payload_end]
        if chunk_id == b"fmt " and fmt is None:
            fmt = payload
        elif chunk_id == b"data" and data is None:
            data = payload

        offset = payload_end + (chunk_size & 1)

    if fmt is None:
        raise AnalysisError(f"'{path}' has no fmt chunk")
    if data is None:
        raise AnalysisError(f"'{path}' has no data chunk")
    if len(fmt) < 16:
        raise AnalysisError(f"'{path}' has a truncated fmt chunk")

    (
        format_code,
        channels,
        sample_rate,
        _avg_bytes_per_sec,
        block_align,
        bits_per_sample,
    ) = struct.unpack_from("<HHIIHH", fmt, 0)

    valid_bits = bits_per_sample
    if format_code == 0xFFFE:
        if len(fmt) < 40:
            raise AnalysisError(
                f"'{path}' has an incomplete WAVE_FORMAT_EXTENSIBLE fmt chunk"
            )
        valid_bits = struct.unpack_from("<H", fmt, 18)[0]
        subtype = fmt[24:40]
        if subtype != PCM_SUBFORMAT_GUID:
            raise AnalysisError(
                f"'{path}' uses a non-PCM extensible subtype"
            )
    elif format_code != 1:
        raise AnalysisError(
            f"'{path}' uses unsupported WAV format code {format_code}"
        )

    if bits_per_sample not in (8, 16):
        raise AnalysisError(
            f"'{path}' uses unsupported {bits_per_sample}-bit PCM"
        )
    if channels not in (1, 2):
        raise AnalysisError(
            f"'{path}' uses unsupported {channels}-channel PCM"
        )
    if valid_bits != bits_per_sample:
        raise AnalysisError(
            f"'{path}' uses {valid_bits} valid bits in a "
            f"{bits_per_sample}-bit container"
        )

    expected_align = channels * (bits_per_sample // 8)
    if block_align != expected_align:
        raise AnalysisError(
            f"'{path}' blockAlign is {block_align}; expected {expected_align}"
        )
    if len(data) % block_align != 0:
        raise AnalysisError(
            f"'{path}' data chunk is not a whole number of PCM frames"
        )

    return WavePcm(
        path=path,
        format_code=format_code,
        channels=channels,
        sample_rate=sample_rate,
        bits_per_sample=bits_per_sample,
        valid_bits_per_sample=valid_bits,
        block_align=block_align,
        data=data,
    )


def safe_ratio(numerator: float, denominator: float) -> object:
    if denominator == 0:
        return ""
    return numerator / denominator


def db_ratio(numerator: float, denominator: float, multiplier: float = 10.0) -> object:
    if denominator == 0:
        return "inf" if numerator > 0 else ""
    if numerator <= 0:
        return "-inf"
    return multiplier * math.log10(numerator / denominator)


def pearson_from_sums(
    count: int,
    sum_x: float,
    sum_y: float,
    sum_x2: float,
    sum_y2: float,
    sum_xy: float,
) -> object:
    if count <= 1:
        return ""

    numerator = count * sum_xy - sum_x * sum_y
    denom_x = count * sum_x2 - sum_x * sum_x
    denom_y = count * sum_y2 - sum_y * sum_y
    if denom_x <= 0 or denom_y <= 0:
        return ""
    return numerator / math.sqrt(denom_x * denom_y)


def histogram_bin(sample: int, bits_per_sample: int) -> int:
    if bits_per_sample == 8:
        return sample + 128
    return (sample + 32768) >> 8


def distribution_metrics(
    cover_samples: Sequence[int],
    stego_samples: Sequence[int],
    bits_per_sample: int,
) -> Tuple[int, float, float]:
    bins = 256
    cover_hist = [0] * bins
    stego_hist = [0] * bins

    for cover, stego in zip(cover_samples, stego_samples):
        cover_hist[histogram_bin(cover, bits_per_sample)] += 1
        stego_hist[histogram_bin(stego, bits_per_sample)] += 1

    count = min(len(cover_samples), len(stego_samples))
    if count == 0:
        return bins, 0.0, 0.0

    total_variation = 0.0
    js_divergence = 0.0

    for c_count, s_count in zip(cover_hist, stego_hist):
        p = c_count / count
        q = s_count / count
        m = (p + q) / 2.0

        total_variation += abs(p - q)
        if p > 0:
            js_divergence += 0.5 * p * math.log2(p / m)
        if q > 0:
            js_divergence += 0.5 * q * math.log2(q / m)

    return bins, 0.5 * total_variation, js_divergence


def normalized_autocorrelation(
    samples: Sequence[int],
    channels: int,
    frame_count: int,
    delay_frames: int,
) -> object:
    if delay_frames <= 0 or frame_count <= delay_frames or channels <= 0:
        return ""

    channel_values: List[float] = []

    for channel in range(channels):
        sum_xy = 0.0
        sum_x2 = 0.0
        sum_y2 = 0.0

        for frame in range(delay_frames, frame_count):
            x = float(samples[frame * channels + channel])
            y = float(samples[(frame - delay_frames) * channels + channel])
            sum_xy += x * y
            sum_x2 += x * x
            sum_y2 += y * y

        denominator = math.sqrt(sum_x2 * sum_y2)
        if denominator > 0:
            channel_values.append(sum_xy / denominator)

    if not channel_values:
        return ""
    return sum(channel_values) / len(channel_values)


def compare_payloads(
    payload_path: Optional[Path],
    extracted_path: Optional[Path],
) -> Dict[str, object]:
    result: Dict[str, object] = {
        "extracted_payload_bytes": "",
        "compared_prefix_bytes": "",
        "prefix_exact": "",
        "byte_errors_compared": "",
        "byte_error_rate_compared": "",
        "bit_errors_compared": "",
        "ber_compared": "",
        "missing_payload_bytes": "",
        "extra_payload_bytes": "",
        "recovery_fraction": "",
        "end_to_end_error_bits": "",
        "end_to_end_ber": "",
    }

    if extracted_path is not None:
        try:
            extracted = extracted_path.read_bytes()
        except OSError as exc:
            raise AnalysisError(
                f"could not read extracted payload '{extracted_path}': {exc}"
            ) from exc
        result["extracted_payload_bytes"] = len(extracted)
    else:
        extracted = None

    if payload_path is None or extracted is None:
        return result

    try:
        payload = payload_path.read_bytes()
    except OSError as exc:
        raise AnalysisError(
            f"could not read original payload '{payload_path}': {exc}"
        ) from exc

    compared = min(len(payload), len(extracted))
    byte_errors = 0
    bit_errors = 0

    for original_byte, extracted_byte in zip(
        payload[:compared], extracted[:compared]
    ):
        if original_byte != extracted_byte:
            byte_errors += 1
            bit_errors += POPCOUNT[original_byte ^ extracted_byte]

    missing = max(len(payload) - len(extracted), 0)
    extra = max(len(extracted) - len(payload), 0)
    total_span_bits = max(len(payload), len(extracted)) * 8
    end_to_end_errors = bit_errors + 8 * missing + 8 * extra

    result.update(
        {
            "compared_prefix_bytes": compared,
            "prefix_exact": byte_errors == 0,
            "byte_errors_compared": byte_errors,
            "byte_error_rate_compared": safe_ratio(byte_errors, compared),
            "bit_errors_compared": bit_errors,
            "ber_compared": safe_ratio(bit_errors, compared * 8),
            "missing_payload_bytes": missing,
            "extra_payload_bytes": extra,
            "recovery_fraction": safe_ratio(compared, len(payload)),
            "end_to_end_error_bits": end_to_end_errors,
            "end_to_end_ber": safe_ratio(end_to_end_errors, total_span_bits),
        }
    )
    return result


def analyze_audio_pair(
    cover: WavePcm,
    stego: WavePcm,
    delay_zero: int,
    delay_one: int,
) -> Dict[str, object]:
    metadata_match = (
        cover.sample_rate == stego.sample_rate
        and cover.channels == stego.channels
        and cover.bits_per_sample == stego.bits_per_sample
        and cover.block_align == stego.block_align
        and cover.valid_bits_per_sample == stego.valid_bits_per_sample
    )
    length_match = len(cover.data) == len(stego.data)

    if not metadata_match:
        raise AnalysisError(
            "cover and stego WAV metadata do not match"
        )
    if not length_match:
        raise AnalysisError(
            "cover and stego data lengths do not match"
        )

    cover_samples = cover.centered_samples()
    stego_samples = stego.centered_samples()
    count = len(cover_samples)

    if count == 0:
        raise AnalysisError("cover/stego WAV data chunks are empty")

    sum_cover = 0.0
    sum_stego = 0.0
    sum_cover2 = 0.0
    sum_stego2 = 0.0
    sum_product = 0.0
    sum_error = 0.0
    sum_abs_error = 0.0
    sum_error2 = 0.0
    peak_error = 0
    modified = 0

    for cover_sample, stego_sample in zip(cover_samples, stego_samples):
        error = stego_sample - cover_sample
        abs_error = abs(error)

        sum_cover += cover_sample
        sum_stego += stego_sample
        sum_cover2 += cover_sample * cover_sample
        sum_stego2 += stego_sample * stego_sample
        sum_product += cover_sample * stego_sample
        sum_error += error
        sum_abs_error += abs_error
        sum_error2 += error * error

        if error != 0:
            modified += 1
        if abs_error > peak_error:
            peak_error = abs_error

    mse = sum_error2 / count
    rmse = math.sqrt(mse)
    full_scale = cover.full_scale

    bins, histogram_tv, histogram_js = distribution_metrics(
        cover_samples, stego_samples, cover.bits_per_sample
    )

    cover_corr_zero = normalized_autocorrelation(
        cover_samples, cover.channels, cover.frame_count, delay_zero
    )
    stego_corr_zero = normalized_autocorrelation(
        stego_samples, stego.channels, stego.frame_count, delay_zero
    )
    cover_corr_one = normalized_autocorrelation(
        cover_samples, cover.channels, cover.frame_count, delay_one
    )
    stego_corr_one = normalized_autocorrelation(
        stego_samples, stego.channels, stego.frame_count, delay_one
    )

    def difference(a: object, b: object) -> object:
        if a == "" or b == "":
            return ""
        return float(a) - float(b)

    cover_bias = difference(cover_corr_zero, cover_corr_one)
    stego_bias = difference(stego_corr_zero, stego_corr_one)
    bias_delta = difference(stego_bias, cover_bias)

    delta_zero = difference(stego_corr_zero, cover_corr_zero)
    delta_one = difference(stego_corr_one, cover_corr_one)
    detector_l1 = ""
    if delta_zero != "" and delta_one != "":
        detector_l1 = abs(float(delta_zero)) + abs(float(delta_one))

    return {
        "metadata_match": metadata_match,
        "length_match": length_match,
        "modified_samples": modified,
        "sample_modification_rate": modified / count,
        "cover_mean_sample": sum_cover / count,
        "stego_mean_sample": sum_stego / count,
        "mean_error": sum_error / count,
        "mean_absolute_error": sum_abs_error / count,
        "mse": mse,
        "rmse": rmse,
        "normalized_rmse": rmse / full_scale,
        "peak_absolute_error": peak_error,
        "peak_error_fraction_full_scale": peak_error / full_scale,
        "snr_db": db_ratio(sum_cover2, sum_error2, 10.0),
        "psnr_db": (
            "inf"
            if rmse == 0
            else 20.0 * math.log10(full_scale / rmse)
        ),
        "pearson_correlation": pearson_from_sums(
            count,
            sum_cover,
            sum_stego,
            sum_cover2,
            sum_stego2,
            sum_product,
        ),
        "histogram_bins": bins,
        "histogram_total_variation": histogram_tv,
        "histogram_js_divergence_bits": histogram_js,
        "cover_autocorr_delay_zero": cover_corr_zero,
        "stego_autocorr_delay_zero": stego_corr_zero,
        "autocorr_delta_delay_zero": delta_zero,
        "cover_autocorr_delay_one": cover_corr_one,
        "stego_autocorr_delay_one": stego_corr_one,
        "autocorr_delta_delay_one": delta_one,
        "cover_delay_bias": cover_bias,
        "stego_delay_bias": stego_bias,
        "delay_bias_delta": bias_delta,
        "echo_detector_delta_l1": detector_l1,
    }


def infer_payload_category(path: Optional[Path]) -> str:
    if path is None:
        return ""

    suffix = path.suffix.lower()
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


def blank_result() -> Dict[str, object]:
    return {field: "" for field in OUTPUT_FIELDS}


def analyze_row(
    source: Dict[str, object],
    base_dir: Path,
    default_run_id: str,
) -> Dict[str, object]:
    started = time.perf_counter()
    result = blank_result()

    result.update(
        {
            "analysis_version": ANALYSIS_VERSION,
            "timestamp_utc": utc_now_text(),
            "run_id": str(source.get("run_id", "")).strip() or default_run_id,
            "test_id": str(source.get("test_id", "")).strip(),
            "matrix_group": str(source.get("matrix_group", "")).strip(),
            "cover_category": str(source.get("cover_category", "")).strip(),
            "payload_category": str(source.get("payload_category", "")).strip(),
            "target_payload_fraction": str(
                source.get("target_payload_fraction", "")
            ).strip(),
            "parameter_set": str(source.get("parameter_set", "")).strip(),
            "auditory_rating": str(source.get("auditory_rating", "")).strip(),
            "listener_id": str(source.get("listener_id", "")).strip(),
            "listening_original_available": str(
                source.get("listening_original_available", "")
            ).strip(),
            "playback_device": str(source.get("playback_device", "")).strip(),
            "listening_environment": str(
                source.get("listening_environment", "")
            ).strip(),
            "listening_notes": str(source.get("listening_notes", "")).strip(),
            "notes": str(source.get("notes", "")).strip(),
        }
    )

    try:
        cover_path = optional_path(source.get("cover_path"), base_dir)
        stego_path = optional_path(source.get("stego_path"), base_dir)
        payload_path = optional_path(source.get("payload_path"), base_dir)
        extracted_path = optional_path(source.get("extracted_path"), base_dir)

        if cover_path is None:
            raise AnalysisError("cover_path is required")
        if stego_path is None:
            raise AnalysisError("stego_path is required")

        segment_len = parse_int(
            source.get("segment_len_frames"), DEFAULT_SEGMENT_LEN
        )
        delay_zero = parse_int(
            source.get("delay_zero_samples"), DEFAULT_DELAY_ZERO
        )
        delay_one = parse_int(
            source.get("delay_one_samples"), DEFAULT_DELAY_ONE
        )
        decay = parse_float(source.get("echo_decay"), DEFAULT_DECAY)
        repetition = parse_int(
            source.get("repetition"), DEFAULT_REPETITION
        )
        header_bits = parse_int(
            source.get("header_bits"), DEFAULT_HEADER_BITS
        )

        if segment_len <= 0:
            raise AnalysisError("segment_len_frames must be positive")
        if delay_zero <= 0 or delay_one <= 0:
            raise AnalysisError("echo delays must be positive")
        if delay_zero == delay_one:
            raise AnalysisError("echo delays must differ")
        if repetition <= 0:
            raise AnalysisError("repetition must be positive")
        if header_bits < 0:
            raise AnalysisError("header_bits cannot be negative")
        if not (0.0 < decay <= 1.0):
            raise AnalysisError("echo_decay must be in (0, 1]")

        result.update(
            {
                "cover_path": str(cover_path),
                "stego_path": str(stego_path),
                "payload_path": "" if payload_path is None else str(payload_path),
                "extracted_path": (
                    "" if extracted_path is None else str(extracted_path)
                ),
                "segment_len_frames": segment_len,
                "delay_zero_samples": delay_zero,
                "delay_one_samples": delay_one,
                "echo_decay": decay,
                "repetition": repetition,
                "header_bits": header_bits,
            }
        )

        if not result["payload_category"]:
            result["payload_category"] = infer_payload_category(payload_path)

        cover = read_wave_pcm(cover_path)
        stego = read_wave_pcm(stego_path)

        result.update(
            {
                "cover_sha256": sha256_file(cover_path),
                "stego_sha256": sha256_file(stego_path),
                "payload_sha256": sha256_file(payload_path),
                "extracted_sha256": sha256_file(extracted_path),
                "wav_format_code": cover.format_code,
                "sample_rate_hz": cover.sample_rate,
                "channels": cover.channels,
                "bits_per_sample": cover.bits_per_sample,
                "valid_bits_per_sample": cover.valid_bits_per_sample,
                "block_align_bytes": cover.block_align,
                "duration_sec": cover.duration_sec,
                "frame_count": cover.frame_count,
                "sample_count": cover.sample_count,
                "cover_data_bytes": len(cover.data),
                "stego_data_bytes": len(stego.data),
            }
        )

        audio_metrics = analyze_audio_pair(
            cover, stego, delay_zero, delay_one
        )
        result.update(audio_metrics)

        logical_capacity = (
            cover.frame_count // segment_len // repetition
        )
        payload_capacity_bits = max(
            logical_capacity - header_bits, 0
        )
        payload_capacity_bytes = payload_capacity_bits // 8

        override_text = str(
            source.get("requested_payload_bytes_override", "")
        ).strip()
        requested_bytes: Optional[int]
        if payload_path is not None:
            try:
                requested_bytes = payload_path.stat().st_size
            except OSError as exc:
                raise AnalysisError(
                    f"could not stat payload '{payload_path}': {exc}"
                ) from exc
        elif override_text:
            requested_bytes = int(override_text)
            if requested_bytes < 0:
                raise AnalysisError(
                    "requested_payload_bytes_override cannot be negative"
                )
        else:
            requested_bytes = None

        result.update(
            {
                "logical_capacity_bits": logical_capacity,
                "payload_capacity_bits": payload_capacity_bits,
                "payload_capacity_complete_bytes": payload_capacity_bytes,
                "cover_bytes_per_payload_byte": safe_ratio(
                    len(cover.data), payload_capacity_bytes
                ),
            }
        )

        if requested_bytes is not None:
            requested_bits = requested_bytes * 8
            expected_embedded_bits = min(
                requested_bits, payload_capacity_bits
            )
            result.update(
                {
                    "requested_payload_bytes": requested_bytes,
                    "requested_payload_bits": requested_bits,
                    "expected_embedded_payload_bits": expected_embedded_bits,
                    "expected_recoverable_payload_bytes": (
                        expected_embedded_bits // 8
                    ),
                    "requested_to_capacity_ratio": safe_ratio(
                        requested_bits, payload_capacity_bits
                    ),
                    "embedded_capacity_utilization": safe_ratio(
                        expected_embedded_bits, payload_capacity_bits
                    ),
                }
            )

        result.update(compare_payloads(payload_path, extracted_path))
        result["status"] = "PASS"

    except (AnalysisError, OSError, ValueError) as exc:
        result["status"] = "FAIL"
        result["error_message"] = str(exc)

    result["analysis_runtime_ms"] = (
        time.perf_counter() - started
    ) * 1000.0
    return result


def write_results(
    output_path: Path,
    rows: Sequence[Dict[str, object]],
    append: bool,
) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)

    file_exists = output_path.exists()
    mode = "a" if append else "w"
    write_header = not append or not file_exists or output_path.stat().st_size == 0

    if append and file_exists and output_path.stat().st_size > 0:
        with output_path.open("r", newline="", encoding="utf-8-sig") as check:
            reader = csv.reader(check)
            existing_header = next(reader, [])
        if existing_header != OUTPUT_FIELDS:
            raise AnalysisError(
                "existing output CSV header does not match analysis version "
                f"{ANALYSIS_VERSION}"
            )

    with output_path.open(mode, newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=OUTPUT_FIELDS)
        if write_header:
            writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in OUTPUT_FIELDS})


def run_single(args: argparse.Namespace) -> int:
    source: Dict[str, object] = {
        "run_id": args.run_id,
        "test_id": args.test_id,
        "matrix_group": args.matrix_group,
        "cover_category": args.cover_category,
        "payload_category": args.payload_category,
        "target_payload_fraction": args.target_payload_fraction,
        "parameter_set": args.parameter_set,
        "cover_path": args.cover,
        "stego_path": args.stego,
        "payload_path": args.payload,
        "extracted_path": args.extracted,
        "requested_payload_bytes_override": args.requested_payload_bytes,
        "segment_len_frames": args.segment_len,
        "delay_zero_samples": args.delay_zero,
        "delay_one_samples": args.delay_one,
        "echo_decay": args.decay,
        "repetition": args.repetition,
        "header_bits": args.header_bits,
        "auditory_rating": args.auditory_rating,
        "listener_id": args.listener_id,
        "listening_original_available": args.listening_original_available,
        "playback_device": args.playback_device,
        "listening_environment": args.listening_environment,
        "listening_notes": args.listening_notes,
        "notes": args.notes,
    }

    run_id = args.run_id or datetime.now().strftime("%Y%m%d_%H%M%S")
    result = analyze_row(source, Path.cwd(), run_id)

    output_path = Path(args.output)
    if output_path.exists() and not args.append:
        print(
            f"Warning: '{output_path}' already exists and will be overwritten. "
            "Use --append to preserve existing single-pair rows.",
            file=sys.stderr,
        )

    try:
        write_results(output_path, [result], args.append)
    except AnalysisError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    print_summary(result, Path(args.output))
    return 0 if result["status"] == "PASS" else 1


def run_matrix(args: argparse.Namespace) -> int:
    matrix_path = Path(args.matrix).expanduser().resolve()
    output_path = Path(args.output).expanduser().resolve()

    try:
        with matrix_path.open("r", newline="", encoding="utf-8-sig") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames is None:
                raise AnalysisError("matrix CSV has no header")
            missing = [
                required
                for required in ("cover_path", "stego_path")
                if required not in reader.fieldnames
            ]
            if missing:
                raise AnalysisError(
                    "matrix CSV is missing required field(s): "
                    + ", ".join(missing)
                )
            sources = list(reader)
    except OSError as exc:
        print(f"Error: could not read matrix '{matrix_path}': {exc}", file=sys.stderr)
        return 2
    except AnalysisError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    default_run_id = args.run_id or datetime.now().strftime(
        "%Y%m%d_%H%M%S"
    )
    rows = [
        analyze_row(source, matrix_path.parent, default_run_id)
        for source in sources
    ]

    if output_path.exists():
        print(
            f"Warning: '{output_path}' already exists and will be overwritten "
            "with the complete current matrix results.",
            file=sys.stderr,
        )

    try:
        write_results(output_path, rows, append=False)
    except AnalysisError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    passed = sum(row["status"] == "PASS" for row in rows)
    failed = len(rows) - passed

    print("PG-14 matrix analysis complete")
    print(f"Rows analyzed : {len(rows)}")
    print(f"Passed        : {passed}")
    print(f"Failed        : {failed}")
    print(f"Results CSV   : {output_path}")

    return 0 if failed == 0 else 1


def print_summary(result: Dict[str, object], output_path: Path) -> None:
    print("PG-14 statistical analysis")
    print("--------------------------")
    print(f"Status                     : {result['status']}")
    if result["error_message"]:
        print(f"Error                      : {result['error_message']}")
    print(f"Test ID                    : {result['test_id']}")
    print(f"Samples compared           : {result['sample_count']}")
    print(f"Modified-sample rate       : {result['sample_modification_rate']}")
    print(f"MSE                        : {result['mse']}")
    print(f"SNR (dB)                   : {result['snr_db']}")
    print(f"PSNR (dB)                  : {result['psnr_db']}")
    print(f"Compared-prefix BER        : {result['ber_compared']}")
    print(f"Histogram TV distance      : {result['histogram_total_variation']}")
    print(f"Echo detector delta L1     : {result['echo_detector_delta_l1']}")
    print(f"Results CSV                : {output_path.resolve()}")


def add_common_metadata_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--run-id", default="", help="Shared run identifier.")
    parser.add_argument("--test-id", default="", help="Unique matrix test ID.")
    parser.add_argument("--matrix-group", default="", help="Matrix subgroup or experiment.")
    parser.add_argument("--cover-category", default="", help="music, speech, silence, sparse, etc.")
    parser.add_argument("--payload-category", default="", help="text, image, audio, compressed, encrypted, or binary.")
    parser.add_argument("--target-payload-fraction", default="", help="Planned requested payload/capacity fraction.")
    parser.add_argument("--parameter-set", default="fixed_v1", help="Name of the parameter combination.")
    parser.add_argument("--notes", default="", help="General test notes.")

    parser.add_argument("--auditory-rating", default="", help="obvious, apparent_close_listening, or undetectable_without_original.")
    parser.add_argument("--listener-id", default="", help="Pseudonymous listener identifier.")
    parser.add_argument("--listening-original-available", default="", help="yes/no field for later PG-24 testing.")
    parser.add_argument("--playback-device", default="", help="Headphones/speakers used for listening.")
    parser.add_argument("--listening-environment", default="", help="Quiet room, lab, office, etc.")
    parser.add_argument("--listening-notes", default="", help="Subjective listening observations.")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Compute PG-14 statistical, BER, distribution, and echo-detector "
            "metrics for Echo Hiding Audio cover/stego pairs."
        )
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    single = subparsers.add_parser(
        "single", help="Analyze one cover/stego pair."
    )
    single.add_argument("--cover", required=True, help="Original cover WAV.")
    single.add_argument("--stego", required=True, help="Generated stego WAV.")
    single.add_argument("--payload", default="", help="Original hidden payload.")
    single.add_argument("--extracted", default="", help="Recovered payload.")
    single.add_argument(
        "--requested-payload-bytes",
        default="",
        help="Requested byte count when the original payload file is unavailable.",
    )
    single.add_argument(
        "--output",
        default="pg14_results.csv",
        help="Output results CSV. OVERWRITES an existing file unless --append is used.",
    )
    single.add_argument(
        "--append",
        action="store_true",
        help="Append one single-pair row instead of overwriting an existing compatible CSV.",
    )
    add_common_metadata_arguments(single)

    matrix = subparsers.add_parser(
        "matrix", help="Analyze every row in a PG-23 matrix CSV."
    )
    matrix.add_argument("--matrix", required=True, help="Input matrix CSV.")
    matrix.add_argument(
        "--output",
        default="pg14_pg23_results.csv",
        help="Complete matrix output CSV. An existing file is overwritten.",
    )
    matrix.add_argument("--run-id", default="", help="Default run ID for rows without one.")

    for subparser in (single,):
        subparser.add_argument("--segment-len", type=int, default=DEFAULT_SEGMENT_LEN)
        subparser.add_argument("--delay-zero", type=int, default=DEFAULT_DELAY_ZERO)
        subparser.add_argument("--delay-one", type=int, default=DEFAULT_DELAY_ONE)
        subparser.add_argument("--decay", type=float, default=DEFAULT_DECAY)
        subparser.add_argument("--repetition", type=int, default=DEFAULT_REPETITION)
        subparser.add_argument("--header-bits", type=int, default=DEFAULT_HEADER_BITS)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if args.command == "single":
        return run_single(args)
    if args.command == "matrix":
        return run_matrix(args)

    parser.error("unknown command")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
