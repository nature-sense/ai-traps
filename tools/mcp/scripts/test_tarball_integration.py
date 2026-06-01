#!/usr/bin/env python3
"""
Integration test for tarball creation.

Tests the actual _create_changed_files_tarball and _create_full_tarball
methods against the real project directory to see what files end up in the tarball.
"""

import os
import sys
import tempfile
import tarfile
import subprocess
import shutil
import unittest

# Add the mcp_build package to the path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "."))

from mcp_build.remote import BuildServerClient


PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


class TestRealProjectTarball(unittest.TestCase):
    """Test tarball creation against the actual project."""

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp(prefix="ai-traps-integration-")

    def tearDown(self):
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def _get_tarball_contents(self, tarball_path: str) -> set:
        """Get the set of file paths in a tarball."""
        contents = set()
        with tarfile.open(tarball_path, "r:gz") as tar:
            for member in tar.getmembers():
                contents.add(member.name)
        return contents

    def test_changed_files_tarball_contains_meson_build_files(self):
        """
        CRITICAL: When changed_files_only=True, the tarball must contain
        ALL meson.build files needed for the build, not just the changed ones.
        The build server needs the full project structure to build.
        """
        client = BuildServerClient(host="localhost")
        tarball_path = os.path.join(self.temp_dir, "test.tar.gz")
        client._create_changed_files_tarball(PROJECT_ROOT, tarball_path)

        contents = self._get_tarball_contents(tarball_path)
        
        # Check for essential build files
        essential_files = [
            "traps/toolkit/meson.build",
            "traps/targets/meson.build",
            "traps/toolkit/src/actors/meson.build",
        ]
        
        print(f"\nTarball contains {len(contents)} files")
        print(f"Essential meson.build files present:")
        for f in essential_files:
            present = f in contents
            print(f"  {f}: {'✅' if present else '❌'}")
        
        missing = [f for f in essential_files if f not in contents]
        self.assertEqual(
            len(missing), 0,
            f"Essential build files missing from tarball: {missing}"
        )

    def test_changed_files_tarball_contains_source_files(self):
        """
        CRITICAL: When changed_files_only=True, the tarball must contain
        ALL source files needed for compilation, not just the changed ones.
        """
        client = BuildServerClient(host="localhost")
        tarball_path = os.path.join(self.temp_dir, "test.tar.gz")
        client._create_changed_files_tarball(PROJECT_ROOT, tarball_path)

        contents = self._get_tarball_contents(tarball_path)
        
        # Check for essential source files
        essential_sources = [
            "traps/targets/src/detection/platforms/rock3c/main.cpp",
            "traps/targets/src/detection/pipeline/base_detection_pipeline.hpp",
            "traps/targets/src/detection/pipeline/base_detection_pipeline.cpp",
            "traps/targets/src/detection/actors/types.hpp",
            "traps/toolkit/src/actors/ramen.hpp",
            "traps/toolkit/src/hal/api/config_loader.hpp",
            "traps/toolkit/src/hal/api/config_loader.cpp",
        ]
        
        print(f"\nTarball contains {len(contents)} files")
        print(f"Essential source files present:")
        for f in essential_sources:
            present = f in contents
            print(f"  {f}: {'✅' if present else '❌'}")
        
        missing = [f for f in essential_sources if f not in contents]
        self.assertEqual(
            len(missing), 0,
            f"Essential source files missing from tarball: {missing}"
        )

    def test_full_tarball_contains_all_files(self):
        """Test that _create_full_tarball includes all tracked + untracked files."""
        client = BuildServerClient(host="localhost")
        tarball_path = os.path.join(self.temp_dir, "test.tar.gz")
        client._create_full_tarball(PROJECT_ROOT, tarball_path)

        contents = self._get_tarball_contents(tarball_path)
        
        essential_files = [
            "traps/toolkit/meson.build",
            "traps/targets/meson.build",
            "traps/targets/src/detection/platforms/rock3c/main.cpp",
            "traps/toolkit/src/actors/ramen.hpp",
        ]
        
        print(f"\nFull tarball contains {len(contents)} files")
        for f in essential_files:
            present = f in contents
            print(f"  {f}: {'✅' if present else '❌'}")
        
        missing = [f for f in essential_files if f not in contents]
        self.assertEqual(
            len(missing), 0,
            f"Essential files missing from full tarball: {missing}"
        )

    def test_changed_files_vs_full_tarball_size(self):
        """
        Compare the size of changed-files vs full tarball.
        If changed-files is much smaller, it might be missing essential files.
        """
        client = BuildServerClient(host="localhost")
        
        changed_path = os.path.join(self.temp_dir, "changed.tar.gz")
        client._create_changed_files_tarball(PROJECT_ROOT, changed_path)
        changed_size = os.path.getsize(changed_path)
        changed_count = len(self._get_tarball_contents(changed_path))
        
        full_path = os.path.join(self.temp_dir, "full.tar.gz")
        client._create_full_tarball(PROJECT_ROOT, full_path)
        full_size = os.path.getsize(full_path)
        full_count = len(self._get_tarball_contents(full_path))
        
        print(f"\nChanged-files tarball: {changed_count} files, {changed_size} bytes")
        print(f"Full tarball: {full_count} files, {full_size} bytes")
        print(f"Ratio: {changed_count/full_count*100:.1f}% of files")
        
        # The changed-files tarball should be reasonable in size
        # If it's tiny (e.g., < 10 files), something is wrong
        self.assertGreater(
            changed_count, 10,
            f"Changed-files tarball only has {changed_count} files - likely missing essential files!"
        )

    def test_changed_files_tarball_has_actual_changes(self):
        """
        Verify that the changed-files tarball actually contains the files
        that git reports as changed.
        """
        # Get the actual changed files from git
        result = subprocess.run(
            ["git", "diff", "--name-only", "HEAD"],
            capture_output=True, text=True, timeout=30,
            cwd=PROJECT_ROOT,
        )
        git_changed = set(result.stdout.strip().splitlines()) if result.stdout.strip() else set()
        
        result = subprocess.run(
            ["git", "ls-files", "--others", "--exclude-standard"],
            capture_output=True, text=True, timeout=30,
            cwd=PROJECT_ROOT,
        )
        git_untracked = set(result.stdout.strip().splitlines()) if result.stdout.strip() else set()
        
        all_git_changes = git_changed | git_untracked
        
        client = BuildServerClient(host="localhost")
        tarball_path = os.path.join(self.temp_dir, "test.tar.gz")
        client._create_changed_files_tarball(PROJECT_ROOT, tarball_path)
        tarball_contents = self._get_tarball_contents(tarball_path)
        
        print(f"\nGit reports {len(all_git_changes)} changed/untracked files")
        print(f"Tarball contains {len(tarball_contents)} files")
        
        # Check that git-changed files are in the tarball
        missing_from_tarball = all_git_changes - tarball_contents
        if missing_from_tarball:
            print(f"\n❌ Files git says changed but NOT in tarball ({len(missing_from_tarball)}):")
            for f in sorted(missing_from_tarball)[:20]:
                print(f"  - {f}")
        
        # Check that tarball has files git doesn't know about (fallback files)
        extra_in_tarball = tarball_contents - all_git_changes
        if extra_in_tarball:
            print(f"\n⚠️  Tarball has extra files beyond git changes ({len(extra_in_tarball)}):")
            for f in sorted(extra_in_tarball)[:10]:
                print(f"  - {f}")
        
        # The changed files should be in the tarball
        for f in all_git_changes:
            if f:  # skip empty strings
                self.assertIn(
                    f, tarball_contents,
                    f"Changed file '{f}' should be in changed-files tarball"
                )


if __name__ == "__main__":
    unittest.main(verbosity=2)
