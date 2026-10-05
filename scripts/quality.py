#!/usr/bin/env python3
"""Check changed first-party files, preserving the existing formatting baseline."""

import argparse
from collections import Counter
import difflib
import json
import os
from pathlib import Path
import re
import subprocess


def run(*args):
    return subprocess.check_output(args, text=True)


def changed_lines(base, path):
    diff = run("git", "diff", "--unified=0", base, "--", path)
    return [
        [int(start), int(start) + int(count or 1) - 1]
        for start, count in re.findall(r"^@@ .*?\+(\d+)(?:,(\d+))? @@", diff, re.M)
        if count != "0"
    ]


def check_format(base, path, lines, fix):
    original = path.read_text()
    if path.suffix in {".cpp", ".hpp", ".h"}:
        args = ["clang-format", str(path)]
        args += [f"--lines={start}:{end}" for start, end in lines]
        formatted = run(*args)
        before = original
    else:
        args = ["npx", "--yes", "--package=prettier@3.6.2", "prettier", str(path)]
        formatted = run(*args)
        # Require touched files to add no formatting debt. Existing debt is
        # measured against the base instead of rewriting unrelated text.
        result = subprocess.run(
            ["git", "show", f"{base}:{path}"], capture_output=True, text=True
        )
        if result.returncode == 0:
            previous = result.stdout
            previous_format = subprocess.run(
                args[:-1] + ["--stdin-filepath", str(path)],
                input=previous, capture_output=True, text=True, check=True
            ).stdout
            old_debt = formatting_debt(previous, previous_format)
            new_debt = formatting_debt(original, formatted)
            if new_debt <= old_debt:
                return True
        before = original
    if formatted == before:
        return True
    if fix:
        path.write_text(formatted)
        return True
    print(f"Formatting failed: {path}")
    return False


def formatting_debt(original, formatted):
    return Counter(
        (line[0], line[1:])
        for line in difflib.ndiff(original.splitlines(), formatted.splitlines())
        if line.startswith(("- ", "+ "))
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", help="Git ref used as the comparison base")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--fix", action="store_true", help="Apply formatting only")
    args = parser.parse_args()
    root = Path(run("git", "rev-parse", "--show-toplevel").strip())
    os.chdir(root)
    paths = run("git", "diff", "--name-only", "--diff-filter=AM", args.base).splitlines()
    untracked = set(run("git", "ls-files", "--others", "--exclude-standard").splitlines())
    paths += sorted(untracked)
    supported = {".cpp", ".hpp", ".h", ".md", ".yml", ".yaml", ".json"}
    paths = [Path(path) for path in paths if Path(path).suffix in supported]
    ranges = {
        path: ([[1, max(1, len(path.read_text().splitlines()))]] if str(path) in untracked
               else changed_lines(args.base, str(path)))
        for path in paths
    }
    passed = True
    for path in paths:
        if ranges[path]:
            passed = check_format(args.base, path, ranges[path], args.fix) and passed
    if args.fix:
        return 0
    databases = {}
    for database in Path(args.build_dir).rglob("compile_commands.json"):
        for entry in json.loads(database.read_text()):
            source = Path(entry["file"]).resolve()
            if (source.is_relative_to(root) and
                    source.relative_to(root).parts[0] in {"src", "tests", "examples", "benchmarks"}):
                databases[source.relative_to(root)] = database.parent
    cpp_files = [path for path in paths if path.suffix == ".cpp"]
    headers = [path for path in paths if path.suffix in {".hpp", ".h"}]
    if headers and not cpp_files:
        cpp_files = sorted(databases)
    line_filter = json.dumps([
        {"name": str(root / path), "lines": ranges[path]}
        for path in paths if path.suffix in {".cpp", ".hpp", ".h"} and ranges[path]
    ])
    for path in cpp_files:
        if path not in databases:
            raise RuntimeError(f"No compile command for {path}; build its consumer first")
        result = subprocess.run([
            "clang-tidy", str(path), "-p", str(databases[path]),
            f"--line-filter={line_filter}", "--quiet"
        ])
        passed = result.returncode == 0 and passed
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
