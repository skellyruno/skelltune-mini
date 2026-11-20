#!/usr/bin/env python3
"""Normalize JUCE VST3 moduleinfo.json and enforce the Mini release contract.

JUCE 8.0.12 emits trailing commas in its VST3 module metadata. Those commas
are tolerated by some readers but are not valid RFC 8259 JSON. This script
removes only commas immediately before a closing object/array delimiter while
outside a quoted string, then parses and validates the result with Python's
strict standard-library JSON parser.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


def strip_trailing_commas(source: str) -> str:
    """Remove JSON5-style trailing commas without touching string contents."""
    output: list[str] = []
    in_string = False
    escaped = False
    index = 0

    while index < len(source):
        char = source[index]

        if in_string:
            output.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            index += 1
            continue

        if char == '"':
            in_string = True
            output.append(char)
            index += 1
            continue

        if char == ",":
            lookahead = index + 1
            while lookahead < len(source) and source[lookahead].isspace():
                lookahead += 1
            if lookahead < len(source) and source[lookahead] in "}]":
                index += 1
                continue

        output.append(char)
        index += 1

    return "".join(output)


def read_project_version(cmake_file: Path) -> str:
    text = cmake_file.read_text(encoding="utf-8")
    match = re.search(
        r"(?im)^\s*project\s*\(\s*SkellyTune\s+VERSION\s+"
        r"([0-9]+(?:\.[0-9]+){1,2}(?:[-+][A-Za-z0-9.-]+)?)",
        text,
    )
    if not match:
        raise ValueError(f"Could not read SkellyTune project version from {cmake_file}")
    return match.group(1)


def validate_moduleinfo(document: Any, expected_name: str, expected_version: str) -> dict[str, Any]:
    if not isinstance(document, dict):
        raise ValueError("moduleinfo root must be a JSON object")
    if document.get("Name") != expected_name:
        raise ValueError(f"moduleinfo Name must be {expected_name!r}; got {document.get('Name')!r}")
    if document.get("Version") != expected_version:
        raise ValueError(
            "moduleinfo Version must match CMake project version "
            f"{expected_version!r}; got {document.get('Version')!r}"
        )

    factory_info = document.get("Factory Info")
    if not isinstance(factory_info, dict) or factory_info.get("Vendor") != "Skelly Audio":
        raise ValueError("moduleinfo Factory Info Vendor must be 'Skelly Audio'")

    classes = document.get("Classes")
    if not isinstance(classes, list) or len(classes) < 2:
        raise ValueError("moduleinfo must contain the audio module and controller classes")

    required_categories = {"Audio Module Class", "Component Controller Class"}
    observed_categories: set[str] = set()
    class_ids: list[str] = []
    for index, plugin_class in enumerate(classes):
        if not isinstance(plugin_class, dict):
            raise ValueError(f"Classes[{index}] must be a JSON object")
        cid = plugin_class.get("CID")
        if not isinstance(cid, str) or not re.fullmatch(r"[0-9A-Fa-f]{32}", cid):
            raise ValueError(f"Classes[{index}].CID must be a 32-character hexadecimal ID")
        class_ids.append(cid.lower())
        if plugin_class.get("Name") != expected_name:
            raise ValueError(f"Classes[{index}].Name does not match the plugin name")
        if plugin_class.get("Vendor") != "Skelly Audio":
            raise ValueError(f"Classes[{index}].Vendor must be 'Skelly Audio'")
        if plugin_class.get("Version") != expected_version:
            raise ValueError(f"Classes[{index}].Version does not match the CMake version")
        category = plugin_class.get("Category")
        if isinstance(category, str):
            observed_categories.add(category)

    if len(class_ids) != len(set(class_ids)):
        raise ValueError("moduleinfo contains duplicate VST3 class IDs")
    missing = required_categories - observed_categories
    if missing:
        raise ValueError(
            "moduleinfo is missing required VST3 class categories: " + ", ".join(sorted(missing))
        )

    return {
        "name": expected_name,
        "version": expected_version,
        "class_count": len(classes),
        "class_ids": sorted(set(class_ids)),
    }


def normalize_file(path: Path, cmake_file: Path, expected_name: str) -> dict[str, Any]:
    original = path.read_text(encoding="utf-8-sig")
    normalized = strip_trailing_commas(original)
    try:
        document = json.loads(normalized)
    except json.JSONDecodeError as error:
        raise ValueError(
            f"moduleinfo remains invalid strict JSON after trailing-comma cleanup: {error}"
        ) from error

    expected_version = read_project_version(cmake_file)
    summary = validate_moduleinfo(document, expected_name, expected_version)
    canonical = json.dumps(document, ensure_ascii=False, indent=2) + "\n"
    json.loads(canonical)
    path.write_text(canonical, encoding="utf-8", newline="\n")
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--path", required=True, type=Path, help="VST3 Contents/Resources/moduleinfo.json")
    parser.add_argument("--cmake-file", default=Path("CMakeLists.txt"), type=Path)
    parser.add_argument("--expected-name", default="SkellyTune Realtime")
    args = parser.parse_args()
    try:
        summary = normalize_file(args.path, args.cmake_file, args.expected_name)
    except (OSError, ValueError) as error:
        print(f"VST3 metadata validation failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(summary, sort_keys=True))
    print(f"PASS: strict JSON metadata normalized at {args.path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
