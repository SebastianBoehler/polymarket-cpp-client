import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("quality", Path(__file__).with_name("quality.py"))
quality = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quality)


class QualityTests(unittest.TestCase):
    def test_empty_diff_has_no_changed_files(self):
        with patch.object(quality, "run", return_value=""):
            self.assertEqual(quality.changed_files("base"), {})

    def test_rename_retains_original_path(self):
        diff = "R095\0include/types.hpp\0include/polymarket/types.hpp\0M\0src/main.cpp\0"
        with patch.object(quality, "run", return_value=diff):
            self.assertEqual(quality.changed_files("base"), {
                "include/polymarket/types.hpp": "include/types.hpp",
                "src/main.cpp": "src/main.cpp"
            })

    def test_changed_ranges_ignore_deleted_lines(self):
        diff = "@@ -1 +1 @@\n@@ -5,2 +5,0 @@\n@@ -10,0 +9,3 @@\n"
        with patch.object(quality, "run", return_value=diff):
            self.assertEqual(quality.changed_lines("base", "file.cpp"), [[1, 1], [9, 11]])

    def test_added_duplicate_formatting_debt_is_rejected(self):
        old = quality.formatting_debt("bad\n", "good\n")
        new = quality.formatting_debt("bad\nbad\n", "good\ngood\n")
        self.assertFalse(new <= old)

    def test_resolved_formatting_debt_is_allowed(self):
        old = quality.formatting_debt("bad\n", "good\n")
        new = quality.formatting_debt("good\n", "good\n")
        self.assertLessEqual(new, old)


if __name__ == "__main__":
    unittest.main()
