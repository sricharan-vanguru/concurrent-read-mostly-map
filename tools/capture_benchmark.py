"""Run one pinned Linux workload and save CSV plus comparison metadata."""
import argparse
import json
import os
import platform
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--cpus", required=True, help="comma-separated logical CPU numbers")
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--build-flags", required=True, help="verified flags used for this binary")
    parser.add_argument("workload", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    cpus = sorted(set(int(cpu) for cpu in args.cpus.split(",")))
    if not cpus or not set(cpus).issubset(os.sched_getaffinity(0)):
        parser.error("CPU selection is empty or outside permitted affinity")
    governor = {}
    for cpu in cpus:
        path = Path(f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/scaling_governor")
        governor[str(cpu)] = path.read_text().strip() if path.exists() else "unavailable"
    if "unavailable" in governor.values():
        parser.error("cannot record frequency governor; use a controlled Linux host")
    model = next(line.split(":", 1)[1].strip()
                 for line in Path("/proc/cpuinfo").read_text().splitlines()
                 if line.startswith("model name"))
    compiler = subprocess.check_output([args.compiler, "--version"], text=True).strip()
    workload = args.workload[1:] if args.workload[:1] == ["--"] else args.workload
    command = ["taskset", "--cpu-list", ",".join(map(str, cpus)),
               str(Path(args.executable).resolve()), *workload]
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=False)  # Never overwrite previous evidence.
    environment = {"cpu": model, "os": platform.platform(), "compiler": compiler,
                   "build_flags": args.build_flags, "affinity": cpus, "governor": governor,
                   "command": command}
    (output / "environment.json").write_text(json.dumps(environment, indent=2) + "\n")
    with (output / "benchmark.csv").open("w") as stream:
        subprocess.run(command, stdout=stream, check=True)
    print(f"Saved {output}; inspect background load and thermal state before comparison")


if __name__ == "__main__":
    main()
