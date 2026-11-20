import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from artifact_access_policy import resolve_policy


class ArtifactAccessPolicyTests(unittest.TestCase):
    def test_private_repository_allows_repository_scoped_artifact(self):
        policy = resolve_policy("private", "true")
        self.assertTrue(policy["artifact_upload_allowed"])
        self.assertEqual(policy["artifact_access"], "repository-scoped")
        self.assertEqual(policy["reason"], "repository-is-private")

    def test_public_repository_withholds_product_artifact(self):
        policy = resolve_policy("public", "false")
        self.assertFalse(policy["artifact_upload_allowed"])
        self.assertEqual(policy["artifact_access"], "withheld")
        self.assertEqual(policy["reason"], "repository-is-not-private")

    def test_internal_repository_is_not_treated_as_private(self):
        policy = resolve_policy("internal", "true")
        self.assertFalse(policy["artifact_upload_allowed"])

    def test_unknown_visibility_fails_closed(self):
        policy = resolve_policy(None, "true")
        self.assertFalse(policy["artifact_upload_allowed"])
        self.assertEqual(policy["repository_visibility"], "unknown")

    def test_private_visibility_requires_matching_private_flag(self):
        policy = resolve_policy("private", "false")
        self.assertFalse(policy["artifact_upload_allowed"])
        self.assertEqual(policy["reason"], "private-flag-mismatch")

    def test_visibility_is_normalized(self):
        policy = resolve_policy(" PRIVATE ", True)
        self.assertTrue(policy["artifact_upload_allowed"])

    def test_cli_writes_machine_readable_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "nested" / "ARTIFACT-ACCESS.json"
            script = Path(__file__).resolve().parents[1] / "tools" / "artifact_access_policy.py"
            completed = subprocess.run(
                [sys.executable, str(script), "--visibility", "public",
                 "--private-flag", "false", "--output", str(output)],
                check=True, capture_output=True, text=True,
            )
            self.assertFalse(json.loads(completed.stdout)["artifact_upload_allowed"])
            written = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(written["schema"], "skelly-artifact-access/1")
            self.assertFalse(written["artifact_upload_allowed"])


if __name__ == "__main__":
    unittest.main()
