#!/usr/bin/env python3
"""
Test script for tarball creation in remote.py.

Creates temporary git repos with various states and tests the
_create_changed_files_tarball and _create_full_tarball methods.
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


class TestTarballCreation(unittest.TestCase):
    """Test the tarball creation logic used by BuildServerClient."""

    def setUp(self):
        self.test_dir = tempfile.mkdtemp(prefix="ai-traps-test-")
        self.orig_dir = os.getcwd()

    def tearDown(self):
        os.chdir(self.orig_dir)
        shutil.rmtree(self.test_dir, ignore_errors=True)

    def _git(self, *args):
        """Run a git command in the test directory."""
        result = subprocess.run(
            ["git"] + list(args),
            capture_output=True, text=True, timeout=30,
            cwd=self.test_dir,
        )
        return result

    def _create_file(self, path: str, content: str = "hello"):
        """Create a file in the test directory."""
        full_path = os.path.join(self.test_dir, path)
        os.makedirs(os.path.dirname(full_path), exist_ok=True)
        with open(full_path, "w") as f:
            f.write(content)
        return full_path

    def _init_repo(self):
        """Initialize a git repo in the test directory."""
        self._git("init")
        self._git("config", "user.email", "test@test.com")
        self._git("config", "user.name", "Test")
        # Create an initial commit
        self._create_file("README.md", "# Test Repo")
        self._git("add", ".")
        self._git("commit", "-m", "Initial commit")

    def _get_tarball_contents(self, tarball_path: str) -> set:
        """Get the set of file paths in a tarball."""
        contents = set()
        with tarfile.open(tarball_path, "r:gz") as tar:
            for member in tar.getmembers():
                contents.add(member.name)
        return contents

    def _call_create_changed_files_tarball(self, source_dir: str, output_path: str) -> None:
        """Call the private _create_changed_files_tarball method."""
        client = BuildServerClient(host="localhost")
        client._create_changed_files_tarball(source_dir, output_path)

    def _call_create_full_tarball(self, source_dir: str, output_path: str) -> None:
        """Call the private _create_full_tarball method."""
        client = BuildServerClient(host="localhost")
        client._create_full_tarball(source_dir, output_path)

    # ── Tests ─────────────────────────────────────────────────────────────

    def test_full_tarball_all_tracked_files(self):
        """Test that _create_full_tarball includes all tracked files."""
        self._init_repo()
        self._create_file("src/main.cpp", "int main() {}")
        self._create_file("src/utils.h", "#pragma once")
        self._git("add", ".")
        self._git("commit", "-m", "Add source files")

        tarball_path = os.path.join(self.test_dir, "output.tar.gz")
        self._call_create_full_tarball(self.test_dir, tarball_path)

        contents = self._get_tarball_contents(tarball_path)
        expected = {"README.md", "src/main.cpp", "src/utils.h"}
        self.assertEqual(contents, expected,
                         f"Expected {expected}, got {contents}")

    def test_full_tarball_includes_untracked(self):
        """Test that _create_full_tarball includes untracked files."""
        self._init_repo()
        self._create_file("untracked.txt", "new file")

        tarball_path = os.path.join(self.test_dir, "output.tar.gz")
        self._call_create_full_tarball(self.test_dir, tarball_path)

        contents = self._get_tarball_contents(tarball_path)
        self.assertIn("untracked.txt", contents,
                      "Untracked file should be in full tarball")
        self.assertIn("README.md", contents,
                      "Tracked file should be in full tarball")

    def test_changed_files_with_unstaged_changes(self):
        """Test that changed-files tarball includes unstaged modifications."""
        self._init_repo()
        # Modify a tracked file without staging
        with open(os.path.join(self.test_dir, "README.md"), "w") as f:
            f.write("# Modified Repo\n")

        tarball_path = os.path.join(self.test_dir, "output.tar.gz")
        self._call_create_changed_files_tarball(self.test_dir, tarball_path)

        contents = self._get_tarball_contents(tarball_path)
        self.assertIn("README.md", contents,
                      "Modified file should be in changed-files tarball")

    def test_changed_files_with_staged_changes(self):
        """Test that changed-files tarball includes staged modifications."""
        self._init_repo()
        # Modify a tracked file and stage it
        with open(os.path.join(self.test_dir, "README.md"), "w") as f:
            f.write("# Staged Modified Repo\n")
        self._git("add", "README.md")

        tarball_path = os.path.join(self.test_dir, "output.tar.gz")
        self._call_create_changed_files_tarball(self.test_dir, tarball_path)

        contents = self._get_tarball_contents(tarball_path)
        self.assertIn("README.md", contents,
                      "Staged modified file should be in changed-files tarball")

    def test_changed_files_with_untracked(self):
        """Test that changed-files tarball includes untracked files."""
        self._init_repo()
        self._create_file("new_file.txt", "brand new")

        tarball_path = os.path.join(self.test_dir, "output.tar.gz")
        self._call_create_changed_files_tarball(self.test_dir, tarball_path)

        contents = self._get_tarball_contents(tarball_path)
        self.assertIn("new_file.txt", contents,
                      "Untracked file should be in changed-files tarball")

    def test_changed_files_clean_working_tree(self):
        """Test that changed-files tarball falls back to all files when tree is clean."""
        self._init_repo()
        self._create_file("src/main.cpp", "int main() {}")
        self._git("add", ".")
        self._git("commit", "-m", "Add source")

        # Working tree is clean - no changes
        tarball_path = os.path.join(self.test_dir, "output.tar.gz")
        self._call_create_changed_files_tarball(self.test_dir, tarball_path)

        contents = self._get_tarball_contents(tarball_path)
        # Should fall back to all tracked + untracked files
        expected = {"README.md", "src/main.cpp"}
        self.assertEqual(contents, expected,
                         f"Clean tree should fall back to all files. Got {contents}")

    def test_changed_files_no_commits_yet(self):
        """Test that changed-files tarball works with a fresh repo (no commits)."""
        self._git("init")
        self._git("config", "user.email", "test@test.com")
        self._git("config", "user.name", "Test")
        self._create_file("README.md", "# Fresh Repo")
        self._create_file("src/main.cpp", "int main() {}")

        tarball_path = os.path.join(self.test_dir, "output.tar.gz")
        self._call_create_changed_files_tarball(self.test_dir, tarball_path)

        contents = self._get_tarball_contents(tarball_path)
        # Should fall back to all tracked + untracked files
        # (no commits yet, so git ls-files shows nothing tracked, but untracked works)
        self.assertIn("README.md", contents,
                      f"Fresh repo should include files. Got {contents}")
        self.assertIn("src/main.cpp", contents,
                      f"Fresh repo should include files. Got {contents}")

    def test_changed_files_with_staged_and_unstaged(self):
        """
        Test that changed-files tarball captures BOTH staged and unstaged changes.

        This is the critical test: if a file is staged AND then further modified,
        both versions should be captured.
        """
        self._init_repo()
        # Create a file, stage it, then modify it again
        self._create_file("src/feature.cpp", "// v1\n")
        self._git("add", "src/feature.cpp")
        # Now modify it without staging
        with open(os.path.join(self.test_dir, "src/feature.cpp"), "w") as f:
            f.write("// v2\n")

        tarball_path = os.path.join(self.test_dir, "output.tar.gz")
        self._call_create_changed_files_tarball(self.test_dir, tarball_path)

        contents = self._get_tarball_contents(tarball_path)
        self.assertIn("src/feature.cpp", contents,
                      "Staged+unstaged file should be in changed-files tarball")

    def test_changed_files_includes_all_tracked_files(self):
        """
        CRITICAL TEST: The changed-files tarball must include ALL tracked files,
        not just the changed ones. The build server needs the complete source
        tree to build. 'changed_files_only' refers to which components to
        rebuild, not which files to send.
        """
        self._init_repo()
        self._create_file("src/main.cpp", "int main() {}")
        self._create_file("src/utils.h", "#pragma once")
        self._git("add", ".")
        self._git("commit", "-m", "Add source files")

        # Now modify only one file
        with open(os.path.join(self.test_dir, "src/main.cpp"), "w") as f:
            f.write("int main() { return 0; }\n")

        tarball_path = os.path.join(self.test_dir, "output.tar.gz")
        self._call_create_changed_files_tarball(self.test_dir, tarball_path)

        contents = self._get_tarball_contents(tarball_path)
        # Should include ALL tracked files (the build server needs the full tree)
        self.assertIn("src/main.cpp", contents,
                      "Changed file should be included")
        self.assertIn("src/utils.h", contents,
                      "Unchanged file should ALSO be included (build server needs full tree)")
        self.assertIn("README.md", contents,
                      "Unchanged README should ALSO be included (build server needs full tree")

    def test_changed_files_with_new_file_and_modified(self):
        """Test that changed-files tarball includes both new and modified files."""
        self._init_repo()
        self._create_file("src/main.cpp", "int main() {}")
        self._git("add", ".")
        self._git("commit", "-m", "Add main")

        # Modify existing + add new
        with open(os.path.join(self.test_dir, "src/main.cpp"), "w") as f:
            f.write("int main() { return 0; }\n")
        self._create_file("src/new_feature.cpp", "// new\n")

        tarball_path = os.path.join(self.test_dir, "output.tar.gz")
        self._call_create_changed_files_tarball(self.test_dir, tarball_path)

        contents = self._get_tarball_contents(tarball_path)
        self.assertIn("src/main.cpp", contents)
        self.assertIn("src/new_feature.cpp", contents)

    def test_tarball_not_empty(self):
        """Test that the tarball is never empty (has at least some files)."""
        self._init_repo()

        tarball_path = os.path.join(self.test_dir, "output.tar.gz")
        self._call_create_changed_files_tarball(self.test_dir, tarball_path)

        # Verify tarball exists and is non-empty
        self.assertTrue(os.path.exists(tarball_path),
                        "Tarball should exist")
        self.assertGreater(os.path.getsize(tarball_path), 0,
                           "Tarball should not be empty")

        # Verify it has contents
        contents = self._get_tarball_contents(tarball_path)
        self.assertGreater(len(contents), 0,
                           "Tarball should contain at least one file")

    def test_git_diff_head_fails_on_no_commits(self):
        """
        Verify that 'git diff --name-only HEAD' fails on a repo with no commits.
        This confirms the fallback path is necessary.
        """
        self._git("init")
        self._git("config", "user.email", "test@test.com")
        self._git("config", "user.name", "Test")
        self._create_file("test.txt", "content")

        result = subprocess.run(
            ["git", "diff", "--name-only", "HEAD"],
            capture_output=True, text=True, timeout=30,
            cwd=self.test_dir,
        )
        self.assertNotEqual(result.returncode, 0,
                            "git diff HEAD should fail on repo with no commits")
        self.assertIn("fatal", result.stderr.lower(),
                      "Should get a fatal error about ambiguous argument 'HEAD'")

    def test_git_diff_head_with_single_commit(self):
        """
        Verify that 'git diff --name-only HEAD' works with a single commit.
        """
        self._init_repo()

        # Clean tree - should return empty
        result = subprocess.run(
            ["git", "diff", "--name-only", "HEAD"],
            capture_output=True, text=True, timeout=30,
            cwd=self.test_dir,
        )
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout.strip(), "",
                         "Clean tree should have no diff")

        # Now make a change
        with open(os.path.join(self.test_dir, "README.md"), "w") as f:
            f.write("# Changed\n")
        result = subprocess.run(
            ["git", "diff", "--name-only", "HEAD"],
            capture_output=True, text=True, timeout=30,
            cwd=self.test_dir,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("README.md", result.stdout.strip().splitlines())

    def test_git_diff_head_with_staged_changes(self):
        """
        Verify that 'git diff --name-only HEAD' includes staged changes.
        """
        self._init_repo()

        # Stage a change
        with open(os.path.join(self.test_dir, "README.md"), "w") as f:
            f.write("# Staged\n")
        self._git("add", "README.md")

        result = subprocess.run(
            ["git", "diff", "--name-only", "HEAD"],
            capture_output=True, text=True, timeout=30,
            cwd=self.test_dir,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("README.md", result.stdout.strip().splitlines(),
                      "git diff HEAD should include staged changes")

    def test_git_ls_files_untracked_in_fresh_repo(self):
        """
        Verify that 'git ls-files --others --exclude-standard' works in a fresh repo.
        """
        self._git("init")
        self._create_file("untracked.txt", "content")

        result = subprocess.run(
            ["git", "ls-files", "--others", "--exclude-standard"],
            capture_output=True, text=True, timeout=30,
            cwd=self.test_dir,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("untracked.txt", result.stdout.strip().splitlines())

    def test_git_ls_files_tracked_in_fresh_repo(self):
        """
        Verify that 'git ls-files' returns nothing in a fresh repo with no commits.
        """
        self._git("init")
        self._create_file("tracked.txt", "content")
        self._git("add", "tracked.txt")

        result = subprocess.run(
            ["git", "ls-files"],
            capture_output=True, text=True, timeout=30,
            cwd=self.test_dir,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("tracked.txt", result.stdout.strip().splitlines(),
                      "git ls-files should show staged files even without commits")


if __name__ == "__main__":
    unittest.main(verbosity=2)
