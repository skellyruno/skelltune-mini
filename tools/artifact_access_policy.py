#!/usr/bin/env python3
"""Fail-closed policy for publishing SkellyTune Mini Windows VST3 artifacts."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


KNOWN_VISIBILITIES = {"public", "private", "internal"}


def resolve_policy(visibility: str | None, private_flag: str | bool | None) -> dict[str, Any]:
    raw_visibility = (visibility or "").strip().lower()
    normalized_visibility = raw_visibility if raw_visibility in KNOWN_VISIBILITIES else "unknown"
    is_private = private_flag is True or (
        isinstance(private_flag, str) and private_flag.strip().lower() == "true"
    )
    allowed = normalized_visibility == "private" and is_private

    if allowed:
        reason = "repository-is-private"
    elif normalized_visibility == "private":
        reason = "private-flag-mismatch"
    elif normalized_visibility == "unknown":
        reason = "repository-visibility-unknown"
    else:
        reason = "repository-is-not-private"

    return {
        "schema": "skelly-artifact-access/1",
        "repository_visibility": normalized_visibility,
        "repository_private_flag": is_private,
        "artifact_access": "repository-scoped" if allowed else "withheld",
        "artifact_upload_allowed": allowed,
        "reason": reason,
        "product_artifact": "SkellyTune-Realtime-Windows-VST3-Preview",
        "retention_days": 7 if allowed else None,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--visibility", default="unknown")
    parser.add_argument("--private-flag", default="false")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    result = resolve_policy(args.visibility, args.private_flag)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(result, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
