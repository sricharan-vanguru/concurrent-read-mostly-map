"""Conservative throughput gate for repeated, controlled benchmark CSVs."""
import argparse
import csv
import json
import math
import statistics
import sys
from pathlib import Path

FIELDS = ("mode", "threads", "entries", "key_bytes", "value_bytes", "batch",
          "write_permille", "burst", "reads", "writes", "hot_permille")
ENV_FIELDS = ("cpu", "os", "compiler", "build_flags", "affinity", "governor")


def load(path):
    groups = {}
    with Path(path).open(encoding="utf-8") as stream:
        rows = csv.DictReader(line for line in stream if not line.startswith("#"))
        for row in rows:
            # Older uniform captures did not include hot-key configuration.
            key = tuple(row.get(field, "0") if field == "hot_permille" else row[field]
                        for field in FIELDS)
            value = float(row["ops_per_second"])
            if not math.isfinite(value) or value <= 0:
                raise ValueError("throughput must be finite and positive")
            repeat = int(row["repeat"])
            group = groups.setdefault(key, {})
            if repeat in group:
                raise ValueError("duplicate repeat in workload")
            group[repeat] = value
    if not groups:
        raise ValueError("empty benchmark")
    return groups


def compare(args):
    before_env = json.loads(Path(args.baseline_env).read_text(encoding="utf-8"))
    after_env = json.loads(Path(args.candidate_env).read_text(encoding="utf-8"))
    if not isinstance(before_env, dict) or not isinstance(after_env, dict):
        raise ValueError("environment must be a JSON object")
    for field in ENV_FIELDS:
        if not before_env.get(field) or before_env[field] != after_env.get(field):
            raise ValueError(f"missing or mismatched environment: {field}")
    before, after = load(args.baseline), load(args.candidate)
    if before.keys() != after.keys():
        raise ValueError("workloads differ")
    results = []
    # Validate every group before reporting any pass/fail conclusion.
    for key in sorted(before):
        left, right = list(before[key].values()), list(after[key].values())
        for samples in (left, right):
            if len(samples) < args.min_repeats:
                raise ValueError("insufficient repeats")
            if statistics.stdev(samples) / statistics.mean(samples) > args.max_cv:
                raise ValueError("unstable measurements; rerun on controlled hardware")
        ratio = statistics.median(right) / statistics.median(left)
        results.append((key, ratio))
    regressed = False
    for key, ratio in results:
        failed = ratio < 1 - args.tolerance
        regressed |= failed
        print(f"{'FAIL' if failed else 'PASS'} {key}: throughput ratio={ratio:.4f}")
    return 1 if regressed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("baseline", "candidate", "baseline-env", "candidate-env"):
        parser.add_argument(f"--{option}", required=True)
    parser.add_argument("--tolerance", type=float, default=0.05)
    parser.add_argument("--max-cv", type=float, default=0.10)
    parser.add_argument("--min-repeats", type=int, default=5)
    args = parser.parse_args()
    if (not 0 <= args.tolerance < 1 or not 0 < args.max_cv < 1 or args.min_repeats < 2):
        parser.error("invalid gate parameters")
    try:
        return compare(args)
    except (ValueError, KeyError, OSError, csv.Error, TypeError) as error:
        print(f"INCOMPARABLE: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
