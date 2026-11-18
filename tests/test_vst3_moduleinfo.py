from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from normalize_vst3_moduleinfo import (
    normalize_file,
    read_project_version,
    strip_trailing_commas,
    validate_moduleinfo,
)


def valid_moduleinfo() -> dict:
    return {
        "Name": "SkellyTune Realtime",
        "Version": "3.3.1",
        "Factory Info": {"Vendor": "Skelly Audio"},
        "Classes": [
            {
                "CID": "ABCDEF019182FAEB536B6C79536B5254",
                "Category": "Audio Module Class",
                "Name": "SkellyTune Realtime",
                "Vendor": "Skelly Audio",
                "Version": "3.3.1",
            },
            {
                "CID": "ABCDEF011234ABCD536B6C79536B5254",
                "Category": "Component Controller Class",
                "Name": "SkellyTune Realtime",
                "Vendor": "Skelly Audio",
                "Version": "3.3.1",
            },
        ],
    }


class Vst3ModuleInfoTests(unittest.TestCase):
    def test_removes_object_and_array_trailing_commas(self) -> None:
        source = '{"items":[1,2,],"nested":{"ok":true,},}'
        normalized = strip_trailing_commas(source)
        self.assertEqual(json.loads(normalized), {"items": [1, 2], "nested": {"ok": True}})

    def test_preserves_commas_and_delimiters_inside_strings(self) -> None:
        source = r'{"text":"comma, }], and escaped quote: \"","items":[1,],}'
        normalized = strip_trailing_commas(source)
        self.assertEqual(json.loads(normalized)["text"], 'comma, }], and escaped quote: "')
        self.assertEqual(json.loads(normalized)["items"], [1])

    def test_validates_expected_name_version_vendor_and_classes(self) -> None:
        summary = validate_moduleinfo(valid_moduleinfo(), "SkellyTune Realtime", "3.3.1")
        self.assertEqual(summary["class_count"], 2)
        self.assertEqual(len(summary["class_ids"]), 2)

    def test_rejects_duplicate_class_ids(self) -> None:
        document = valid_moduleinfo()
        document["Classes"][1]["CID"] = document["Classes"][0]["CID"]
        with self.assertRaisesRegex(ValueError, "duplicate VST3 class IDs"):
            validate_moduleinfo(document, "SkellyTune Realtime", "3.3.1")

    def test_rejects_version_mismatch(self) -> None:
        with self.assertRaisesRegex(ValueError, "Version must match"):
            validate_moduleinfo(valid_moduleinfo(), "SkellyTune Realtime", "3.3.2")

    def test_reads_project_version_from_cmake(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            cmake = Path(temp) / "CMakeLists.txt"
            cmake.write_text(
                "cmake_minimum_required(VERSION 3.22)\n"
                "project(SkellyTune VERSION 3.3.1 LANGUAGES C CXX)\n",
                encoding="utf-8",
            )
            self.assertEqual(read_project_version(cmake), "3.3.1")

    def test_normalizes_file_and_writes_strict_json(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            cmake = root / "CMakeLists.txt"
            moduleinfo = root / "moduleinfo.json"
            cmake.write_text("project(SkellyTune VERSION 3.3.1 LANGUAGES C CXX)\n", encoding="utf-8")
            document = valid_moduleinfo()
            source = json.dumps(document, indent=2).replace(
                '"Vendor": "Skelly Audio"\n', '"Vendor": "Skelly Audio",\n'
            )
            moduleinfo.write_text(source, encoding="utf-8")
            summary = normalize_file(moduleinfo, cmake, "SkellyTune Realtime")
            self.assertEqual(summary["version"], "3.3.1")
            parsed = json.loads(moduleinfo.read_text(encoding="utf-8"))
            self.assertEqual(parsed, document)
            self.assertTrue(moduleinfo.read_text(encoding="utf-8").endswith("\n"))


if __name__ == "__main__":
    unittest.main()
