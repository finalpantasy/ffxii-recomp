import argparse
import csv
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

FUNCTION = re.compile(r"^void f_([0-9A-Fa-f]+)\(")
UNIMPLEMENTED = re.compile(r"Unimplemented\(c, 0x([0-9A-Fa-f]+)u, \"([^\"]+)\"\)")


def main():
    parser = argparse.ArgumentParser(description="Classify generated FFXII unimplemented sites by function provenance")
    parser.add_argument("generated", type=Path)
    parser.add_argument("--functions", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with args.functions.open(newline="", encoding="utf-8") as handle:
        known = {int(row["entry"], 16) for row in csv.DictReader(handle, delimiter="\t")}
    counts = Counter()
    examples = defaultdict(list)
    current = None
    for path in sorted(args.generated.glob("gen_*.cpp")):
        with path.open(encoding="utf-8") as handle:
            for line in handle:
                function = FUNCTION.match(line)
                if function:
                    current = int(function.group(1), 16)
                site = UNIMPLEMENTED.search(line)
                if not site:
                    continue
                address = int(site.group(1), 16)
                mnemonic = site.group(2)
                origin = "ghidra" if current in known else "discovered-only"
                counts[(origin, mnemonic)] += 1
                key = f"{origin}:{mnemonic}"
                if len(examples[key]) < 12:
                    examples[key].append({"function": current, "address": address, "file": path.name})
    origins = defaultdict(Counter)
    for (origin, mnemonic), count in counts.items():
        origins[origin][mnemonic] = count
    result = {
        "total": sum(counts.values()),
        "origins": {
            origin: {
                "total": sum(by_mnemonic.values()),
                "mnemonics": dict(sorted(by_mnemonic.items())),
            }
            for origin, by_mnemonic in sorted(origins.items())
        },
        "examples": dict(sorted(examples.items())),
    }
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")


if __name__ == "__main__":
    main()
