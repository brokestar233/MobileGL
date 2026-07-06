#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import math
import pathlib
import re
import statistics
import subprocess
import sys
from dataclasses import dataclass


PERF_LINE_RE = re.compile(
    r"(SFPEW_PERF|MOBILEGL_PATH_PERF) case=(\S+) frame_ns=([0-9.]+) draw_ns=([0-9.]+)"
)


@dataclass(frozen=True)
class PerfCase:
    suite: str
    executable: str
    gtest_filter: str


DEFAULT_CASES: tuple[PerfCase, ...] = (
    PerfCase(
        "sfpew",
        "MobileGL/MG_Test/Compat/SFPEW/SFPEWPerfTest",
        "SFPEWPerfFixture.VanillaImmediateTriangleStripFontSmokeAndPerf",
    ),
    PerfCase(
        "sfpew",
        "MobileGL/MG_Test/Compat/SFPEW/SFPEWPerfTest",
        "SFPEWPerfFixture.VanillaClientArrayGuiQuads12SmokeAndPerf",
    ),
    PerfCase(
        "sfpew",
        "MobileGL/MG_Test/Compat/SFPEW/SFPEWPerfTest",
        "SFPEWPerfFixture.CMMImmediateMenuQuadsSmokeAndPerf",
    ),
    PerfCase(
        "sfpew",
        "MobileGL/MG_Test/Compat/SFPEW/SFPEWPerfTest",
        "SFPEWPerfFixture.VanillaDisplayListCompileAndCallGuiQuadsSmokeAndPerf",
    ),
    PerfCase(
        "sfpew",
        "MobileGL/MG_Test/Compat/SFPEW/SFPEWPerfTest",
        "SFPEWPerfFixture.VanillaWorldSizedClientArrayQuadsSmokeAndPerf",
    ),
    PerfCase(
        "path",
        "MobileGL/MG_Test/RenderPath/MobileGL/MobileGLPathPerfTest",
        "MobileGLPathPerfFixture.AngelicaWorldOneshotMultiDrawArraysSmokeAndPerf",
    ),
    PerfCase(
        "path",
        "MobileGL/MG_Test/RenderPath/MobileGL/MobileGLPathPerfTest",
        "MobileGLPathPerfFixture.AngelicaWorldRegionedOneshotMultiDrawArraysSmokeAndPerf",
    ),
    PerfCase(
        "path",
        "MobileGL/MG_Test/RenderPath/MobileGL/MobileGLPathPerfTest",
        "MobileGLPathPerfFixture.AngelicaWorldRegionedMultiDrawIndirectSmokeAndPerf",
    ),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run MobileGL perf smoke cases in isolated processes and summarize frame times. "
            "This intentionally avoids --gtest_repeat because some SFPEW cases become invalid "
            "after the first in-process iteration."
        )
    )
    parser.add_argument(
        "--build-dir",
        default="build-perf",
        help="Build directory that contains the perf test executables. Default: build-perf",
    )
    parser.add_argument(
        "--suite",
        choices=("sfpew", "path", "all"),
        default="all",
        help="Subset of the default case manifest to run.",
    )
    parser.add_argument(
        "--case",
        action="append",
        default=[],
        help=(
            "Case filter substring. Can be passed multiple times. "
            "When omitted, all cases from the chosen suite run."
        ),
    )
    parser.add_argument(
        "--repeats",
        type=int,
        default=5,
        help="Number of isolated process runs per case. Default: 5",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Print machine-readable JSON instead of the text table.",
    )
    parser.add_argument(
        "--gtest-repeat",
        type=int,
        default=0,
        help=(
            "Run each selected case once with --gtest_repeat=N and aggregate all PERF lines from that process. "
            "Use 0 to keep the default isolated-process mode."
        ),
    )
    return parser.parse_args()


def select_cases(args: argparse.Namespace) -> list[PerfCase]:
    cases = []
    for case in DEFAULT_CASES:
        if args.suite != "all" and case.suite != args.suite:
            continue
        if args.case and not any(pattern in case.gtest_filter for pattern in args.case):
            continue
        cases.append(case)
    return cases


def summarize_runs(frame_times: list[float], draw_times: list[float]) -> dict[str, object]:
    frame_mean = statistics.mean(frame_times)
    draw_mean = statistics.mean(draw_times)
    return {
        "frame_ns": frame_times,
        "draw_ns": draw_times,
        "frame_avg_ns": frame_mean,
        "frame_median_ns": statistics.median(frame_times),
        "frame_stddev_ns": statistics.pstdev(frame_times) if len(frame_times) > 1 else 0.0,
        "frame_min_ns": min(frame_times),
        "frame_max_ns": max(frame_times),
        "draw_avg_ns": draw_mean,
        "draw_median_ns": statistics.median(draw_times),
        "fps_avg": 1.0e9 / frame_mean if frame_mean > 0.0 else math.inf,
    }


def run_case_isolated(executable: pathlib.Path, gtest_filter: str, repeats: int) -> dict[str, object]:
    frame_times: list[float] = []
    draw_times: list[float] = []

    for _ in range(repeats):
        proc = subprocess.run(
            [str(executable), f"--gtest_filter={gtest_filter}"],
            capture_output=True,
            text=True,
            check=False,
        )
        if proc.returncode != 0:
            sys.stderr.write(proc.stdout)
            sys.stderr.write(proc.stderr)
            raise RuntimeError(f"Case failed: {gtest_filter}")

        match = PERF_LINE_RE.search(proc.stdout)
        if not match:
            sys.stderr.write(proc.stdout)
            sys.stderr.write(proc.stderr)
            raise RuntimeError(f"Could not find perf line for case: {gtest_filter}")

        frame_times.append(float(match.group(3)))
        draw_times.append(float(match.group(4)))

    return summarize_runs(frame_times, draw_times)


def run_case_gtest_repeat(executable: pathlib.Path, gtest_filter: str, gtest_repeat: int) -> dict[str, object]:
    if gtest_repeat <= 0:
        raise ValueError("gtest_repeat must be positive")

    proc = subprocess.run(
        [str(executable), f"--gtest_filter={gtest_filter}", f"--gtest_repeat={gtest_repeat}"],
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise RuntimeError(f"Case failed: {gtest_filter}")

    matches = list(PERF_LINE_RE.finditer(proc.stdout))
    if len(matches) != gtest_repeat:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise RuntimeError(
            f"Expected {gtest_repeat} PERF lines for case {gtest_filter}, got {len(matches)}"
        )

    frame_times = [float(match.group(3)) for match in matches]
    draw_times = [float(match.group(4)) for match in matches]
    result = summarize_runs(frame_times, draw_times)
    result["mode"] = "gtest_repeat"
    return result


def print_text(results: dict[str, dict[str, object]]) -> None:
    print(
        "case | avg_frame_ns | median_frame_ns | stddev_ns | min_ns | max_ns | avg_draw_ns | avg_fps"
    )
    print(
        "-----|--------------|-----------------|-----------|--------|--------|------------|--------"
    )
    for case_name, result in results.items():
        print(
            f"{case_name} | "
            f"{result['frame_avg_ns']:.2f} | "
            f"{result['frame_median_ns']:.2f} | "
            f"{result['frame_stddev_ns']:.2f} | "
            f"{result['frame_min_ns']:.2f} | "
            f"{result['frame_max_ns']:.2f} | "
            f"{result['draw_avg_ns']:.2f} | "
            f"{result['fps_avg']:.2f}"
        )


def main() -> int:
    args = parse_args()
    cases = select_cases(args)
    if not cases:
        print("No cases selected.", file=sys.stderr)
        return 1

    build_dir = pathlib.Path(args.build_dir)
    results: dict[str, dict[str, object]] = {}
    for case in cases:
        executable = build_dir / case.executable
        if not executable.exists():
            print(f"Missing executable: {executable}", file=sys.stderr)
            return 1
        if args.gtest_repeat > 0:
            results[case.gtest_filter] = run_case_gtest_repeat(
                executable, case.gtest_filter, args.gtest_repeat
            )
        else:
            results[case.gtest_filter] = run_case_isolated(
                executable, case.gtest_filter, args.repeats
            )

    if args.json:
        print(json.dumps(results, indent=2))
    else:
        print_text(results)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
