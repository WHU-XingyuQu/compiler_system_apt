"""Run the compiler against the unchanged course archive, without extracting it."""

import argparse
import difflib
import hashlib
from pathlib import Path
import re
import subprocess
import zipfile


def error_lines(records, *, require_reason=False):
    if not records:
        raise ValueError("reject without a diagnostic")
    locations = []
    for record in records:
        pattern = r"([0-9]+) .+" if require_reason else r"([0-9]+)(?:\s+.*)?"
        match = re.fullmatch(pattern, record)
        if not match:
            raise ValueError(f"malformed diagnostic: {record!r}")
        locations.append(int(match.group(1)))
    return locations


def graded_output(mode, output):
    # Ignore diagnostic wording, keeping verdict, line order and multiplicity.
    lines = output.decode("utf-8").replace("\r\n", "\n").split("\n")
    if lines[-1] == "":
        lines.pop()
    if mode == "-ast":
        if lines == ["accept"]:
            return lines
        if lines and lines[0] == "reject":
            return ["reject", *map(str, error_lines(lines[1:]))]
        raise ValueError("expected accept or reject followed by diagnostics")
    if lines and lines[0] == "reject":
        divider = lines.index("", 1)
        numbers = error_lines(lines[1:divider], require_reason=True)
        return ["reject", *map(str, numbers), "", *lines[divider + 1:]]
    return lines


def run_suite(archive, compiler, folder, mode, suffix):
    cases = sorted(name for name in archive.namelist()
                   if name.startswith(folder + "/") and name.endswith(".c"))
    if not cases:
        raise ValueError(f"no cases found in {folder}/")
    passed = 0
    for name in cases:
        expected = graded_output(mode, archive.read(name[:-2] + suffix))
        try:
            run = subprocess.run([compiler, mode], input=archive.read(name),
                                 capture_output=True, timeout=5, check=False)
            actual = graded_output(mode, run.stdout)
        except (subprocess.TimeoutExpired, ValueError) as error:
            print(f"FAIL {name}: {error}")
            continue
        if expected == actual and run.returncode == 0 and not run.stderr:
            passed += 1
            continue
        print(f"FAIL {name}: exit={run.returncode}, stderr={run.stderr!r}")
        diff = list(difflib.unified_diff(expected, actual, "expected", "actual", lineterm=""))
        for line in diff[:30]:
            print(line)
    print(f"{mode}: {passed}/{len(cases)} public cases passed")
    return passed == len(cases)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    print(f"Archive SHA256: {hashlib.sha256(args.archive.read_bytes()).hexdigest()}")
    with zipfile.ZipFile(args.archive) as archive:
        token_ok = run_suite(archive, str(args.compiler.resolve()), "lexical", "-token", ".token")
        ast_ok = run_suite(archive, str(args.compiler.resolve()), "syntax", "-ast", ".check-ast")
    return 0 if token_ok and ast_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
