#!/usr/bin/env python3
"""Convert ``run_experiments.sh`` logs into a plotting-compatible CSV.

The runner names each log with the SHA-256 digest of the exact instance-list
entry.  This tool repeats that mapping, parses the solver status and GNU-time
wall clock, writes ``Instance,Result,Time,Best,Mono``, and reports aggregate
solved counts and PAR-2.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


STATUS_PATTERN = re.compile(
    r"^s\s+(SATISFIABLE|UNSATISFIABLE|UNKNOWN|OPTIMUM FOUND)\s*$",
    re.MULTILINE,
)
REAL_PATTERN = re.compile(
    r"^real\s+([0-9]+(?:\.[0-9]*)?(?:[eE][+-]?[0-9]+)?)\s*$",
    re.MULTILINE,
)
EXIT_PATTERN = re.compile(r"^runner_exit_status\s+(-?[0-9]+)\s*$", re.MULTILINE)

CSV_HEADER = ["Instance", "Result", "Time", "Best", "Mono"]
MIN_RECORDED_TIME = 0.01


@dataclass(frozen=True)
class InstanceRecord:
    instance: str
    result: str
    elapsed_seconds: float
    diagnostic: str


def positive_float(value: str) -> float:
    parsed = float(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Convert logs from script/run_experiments.sh to the CSV schema "
            "used by the CardSAT-LS analysis scripts."
        )
    )
    parser.add_argument(
        "--result-dir",
        required=True,
        type=Path,
        help="directory containing <sha256-of-list-entry>.log files",
    )
    parser.add_argument(
        "--instance-list",
        required=True,
        type=Path,
        help="the exact instance list passed to run_experiments.sh",
    )
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="destination *_stats.csv file",
    )
    parser.add_argument(
        "--cutoff",
        type=positive_float,
        default=3600.0,
        help="wall-clock cutoff in seconds (default: 3600)",
    )
    parser.add_argument(
        "--par-factor",
        type=positive_float,
        default=2.0,
        help="timeout multiplier for PAR-k (default: 2, i.e. PAR-2)",
    )
    parser.add_argument(
        "--summary-json",
        type=Path,
        help="optionally write the aggregate statistics and diagnostics as JSON",
    )
    return parser.parse_args(argv)


def load_instance_list(path: Path) -> list[str]:
    if not path.is_file():
        raise ValueError(f"instance list not found: {path}")

    entries = [
        line
        for line in path.read_text(encoding="utf-8").splitlines()
        if line and not line.startswith("#")
    ]
    if not entries:
        raise ValueError(f"instance list is empty: {path}")

    duplicates = sorted(name for name, count in Counter(entries).items() if count > 1)
    if duplicates:
        preview = ", ".join(repr(name) for name in duplicates[:3])
        suffix = " ..." if len(duplicates) > 3 else ""
        raise ValueError(f"instance list contains duplicate entries: {preview}{suffix}")
    return entries


def log_path_for(result_dir: Path, instance: str) -> Path:
    digest = hashlib.sha256(instance.encode("utf-8")).hexdigest()
    return result_dir / f"{digest}.log"


def parsed_status(text: str) -> tuple[str, str]:
    statuses = STATUS_PATTERN.findall(text)
    if not statuses:
        return "unknown", "missing_status"

    normalized = {
        "SATISFIABLE": "sat",
        "UNSATISFIABLE": "unsat",
        "OPTIMUM FOUND": "sat",
        "UNKNOWN": "unknown",
    }
    results = {normalized[status] for status in statuses}
    if len(results) != 1:
        return "unknown", "conflicting_status"
    result = results.pop()
    return result, "solver_unknown" if result == "unknown" else "candidate_solved"


def accepted_exit_code(result: str, exit_code: int) -> bool:
    if result == "sat":
        return exit_code in {0, 10, 30}
    if result == "unsat":
        return exit_code in {0, 20}
    return False


def parse_log(instance: str, log_path: Path, cutoff: float) -> InstanceRecord:
    if not log_path.is_file():
        return InstanceRecord(instance, "unknown", cutoff, "missing_log")

    try:
        text = log_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return InstanceRecord(instance, "unknown", cutoff, "unreadable_log")

    result, diagnostic = parsed_status(text)
    real_matches = REAL_PATTERN.findall(text)
    elapsed = float(real_matches[-1]) if real_matches else cutoff

    if not real_matches:
        return InstanceRecord(instance, "unknown", elapsed, "missing_time")
    if elapsed < 0:
        return InstanceRecord(instance, "unknown", cutoff, "invalid_time")
    if diagnostic != "candidate_solved":
        return InstanceRecord(instance, "unknown", elapsed, diagnostic)

    exit_matches = EXIT_PATTERN.findall(text)
    if not exit_matches:
        return InstanceRecord(instance, "unknown", elapsed, "missing_exit_status")
    exit_code = int(exit_matches[-1])
    if not accepted_exit_code(result, exit_code):
        return InstanceRecord(instance, "unknown", elapsed, "incompatible_exit_status")

    # The plotting scripts treat a runtime at or above the cutoff as unsolved.
    if elapsed >= cutoff:
        return InstanceRecord(instance, "unknown", elapsed, "cutoff")

    return InstanceRecord(
        instance,
        result,
        max(elapsed, MIN_RECORDED_TIME),
        "solved",
    )


def format_time(value: float) -> str:
    return f"{value:.9f}"


def write_csv(path: Path, records: list[InstanceRecord]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as output_file:
        writer = csv.writer(output_file)
        writer.writerow(CSV_HEADER)
        for record in records:
            writer.writerow(
                [
                    record.instance,
                    record.result,
                    format_time(record.elapsed_seconds),
                    "No",
                    "No",
                ]
            )


def summarize(
    records: list[InstanceRecord], cutoff: float, par_factor: float
) -> dict[str, object]:
    sat = sum(record.result == "sat" for record in records)
    unsat = sum(record.result == "unsat" for record in records)
    solved = sat + unsat
    penalty = cutoff * par_factor
    par_score = sum(
        record.elapsed_seconds if record.result in {"sat", "unsat"} else penalty
        for record in records
    ) / len(records)
    return {
        "all": len(records),
        "sat": sat,
        "unsat": unsat,
        "solved": solved,
        "unsolved": len(records) - solved,
        "cutoff_seconds": cutoff,
        "par_factor": par_factor,
        "par_score_seconds": par_score,
        "diagnostics": dict(sorted(Counter(r.diagnostic for r in records).items())),
    }


def print_summary(summary: dict[str, object], output: Path) -> None:
    print(f"CSV: {output}")
    print(f"#All: {summary['all']}")
    print(f"#SAT: {summary['sat']}")
    print(f"#UNSAT: {summary['unsat']}")
    print(f"#Solved: {summary['solved']}")
    print(f"#Unsolved: {summary['unsolved']}")
    print(f"PAR-{summary['par_factor']:g}: {summary['par_score_seconds']:.6f}")
    diagnostics = summary["diagnostics"]
    assert isinstance(diagnostics, dict)
    print(
        "Diagnostics: "
        + ", ".join(f"{name}={count}" for name, count in diagnostics.items())
    )


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        if not args.result_dir.is_dir():
            raise ValueError(f"result directory not found: {args.result_dir}")
        instances = load_instance_list(args.instance_list)
        records = [
            parse_log(instance, log_path_for(args.result_dir, instance), args.cutoff)
            for instance in instances
        ]
        write_csv(args.output, records)
        summary = summarize(records, args.cutoff, args.par_factor)
        if args.summary_json is not None:
            args.summary_json.parent.mkdir(parents=True, exist_ok=True)
            args.summary_json.write_text(
                json.dumps(summary, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
        print_summary(summary, args.output)
        return 0
    except (OSError, UnicodeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
