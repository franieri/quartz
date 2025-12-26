#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys
from pathlib import Path


def run(cmd, *, stdin_text=None, timeout=10):
    try:
        p = subprocess.run(
            cmd,
            input=(stdin_text.encode("utf-8") if stdin_text is not None else None),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
        return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        return 124, "", f"TIMEOUT after {timeout}s"


def norm(s: str) -> str:
    # normalize line endings and trailing whitespace
    return "\n".join([line.rstrip() for line in s.replace("\r\n", "\n").replace("\r", "\n").split("\n")]).strip() + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="./build/quartz")
    ap.add_argument("--samples", default="./samples")
    ap.add_argument("--timeout", type=int, default=10)
    ap.add_argument("--stdin", default="test input\n")
    ap.add_argument("--keep", action="store_true", help="Keep generated .qzb files")
    args = ap.parse_args()

    exe = Path(args.exe)
    if not exe.exists():
        print(f"Missing executable: {exe}", file=sys.stderr)
        return 2

    samples_dir = Path(args.samples)
    qz_files = sorted([p for p in samples_dir.glob("*.qz") if p.is_file()])
    if not qz_files:
        print(f"No .qz files found in {samples_dir}", file=sys.stderr)
        return 2

    build_dir = Path("./build")
    build_dir.mkdir(parents=True, exist_ok=True)

    failures = []

    for qz in qz_files:
        out_bc = build_dir / f"verify_{qz.stem}.qzb"

        interp_cmd = [str(exe), "--interp", str(qz)]
        bc_cmd = [str(exe), "--compile-run", str(qz), "-o", str(out_bc)]

        rc_i, out_i, err_i = run(interp_cmd, stdin_text=args.stdin, timeout=args.timeout)
        rc_b, out_b, err_b = run(bc_cmd, stdin_text=args.stdin, timeout=args.timeout)

        ok = (rc_i == 0 and rc_b == 0 and norm(out_i) == norm(out_b) and norm(err_i) == norm(err_b))

        status = "OK" if ok else "FAIL"
        print(f"[{status}] {qz.name}")

        if not ok:
            failures.append((qz, rc_i, rc_b, out_i, out_b, err_i, err_b))

        if (not args.keep) and out_bc.exists():
            try:
                out_bc.unlink()
            except OSError:
                pass

    if failures:
        print("\nMismatches:")
        for qz, rc_i, rc_b, out_i, out_b, err_i, err_b in failures:
            print(f"- {qz.name}: interp rc={rc_i}, bc rc={rc_b}")
            if norm(out_i) != norm(out_b):
                print("  stdout differs")
            if norm(err_i) != norm(err_b):
                print("  stderr differs")
        return 1

    print("\nAll samples matched (interp vs bytecode).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
