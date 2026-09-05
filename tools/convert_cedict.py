#!/usr/bin/env python3

"""Convert CC-CEDICT into the compact TSV used by the candidate UI."""

from __future__ import annotations

import argparse
import gzip
import re
from collections import OrderedDict
from pathlib import Path


ENTRY_RE = re.compile(r"^(\S+) (\S+) \[[^]]*\] /(.*)/$")
PARENTHETICAL_RE = re.compile(r"\s*\([^()]*\)")
MAX_DEFINITIONS = 3


def clean_definition(value: str) -> str:
    value = PARENTHETICAL_RE.sub("", value)
    return " ".join(value.replace("\t", " ").replace("|", "/").split())


def convert(source: Path, output: Path) -> tuple[int, int]:
    entries: OrderedDict[str, list[str]] = OrderedDict()
    variant_entries: list[tuple[str, list[str]]] = []
    traditional_aliases: list[tuple[str, list[str]]] = []
    source_entries = 0

    with gzip.open(source, "rt", encoding="utf-8") as stream:
        for line_number, raw_line in enumerate(stream, 1):
            line = raw_line.rstrip("\r\n")
            if not line or line.startswith("#"):
                continue
            match = ENTRY_RE.match(line)
            if not match:
                raise ValueError(f"invalid CC-CEDICT entry at line {line_number}")

            traditional, simplified, definitions_text = match.groups()
            definitions = [
                cleaned
                for definition in definitions_text.split("/")
                if (cleaned := clean_definition(definition))
                and not cleaned.startswith("CL:")
            ]
            if not definitions:
                continue

            source_entries += 1
            if any(definition.startswith("variant of ") for definition in definitions):
                variant_entries.append((simplified, definitions))
            else:
                meanings = entries.setdefault(simplified, [])
                for definition in definitions:
                    if definition not in meanings:
                        meanings.append(definition)
                    if len(meanings) >= MAX_DEFINITIONS:
                        break
            if traditional != simplified:
                traditional_aliases.append((traditional, definitions))

    # Variant spellings often collapse onto the same simplified character as
    # the canonical spelling. Add their senses only after canonical entries.
    for simplified, definitions in variant_entries:
        meanings = entries.setdefault(simplified, [])
        for definition in definitions:
            if definition not in meanings:
                meanings.append(definition)
            if len(meanings) >= MAX_DEFINITIONS:
                break

    # A traditional spelling can collide with a real simplified entry. Only
    # add it as an alias when it does not overwrite direct simplified senses.
    for traditional, definitions in traditional_aliases:
        if traditional in entries:
            continue
        entries[traditional] = definitions[:MAX_DEFINITIONS]

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("# Generated from CC-CEDICT; see data/dictionary/CC-CEDICT-LICENSE.txt\n")
        for chinese, meanings in entries.items():
            stream.write(f"{chinese}\t{meanings[0]}\t{'|'.join(meanings[1:])}\n")
    temporary.replace(output)
    return source_entries, len(entries)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source_entries, output_entries = convert(args.source, args.output)
    print(f"Converted {source_entries} CC-CEDICT rows into {output_entries} keys")


if __name__ == "__main__":
    main()
