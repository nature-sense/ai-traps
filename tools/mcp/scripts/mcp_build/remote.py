"""
Remote Build Server Client

Provides an httpx-based client for the Build Server REST API running on target boards.
Replaces the old SSH-based approach with HTTP REST calls.
"""

from __future__ import annotations

import os
import subprocess
import tarfile
import tempfile
from typing import Optional

import httpx

from mcp_build.config import PLATFORM_CONFIGS


class BuildServerClient:
    """Client for the Build Server REST API on a target board."""

    def __init__(self, host: str, port: int = 8081, timeout: float = 30.0):
        self.base_url = f"http://{host}:{port}"
        self.timeout = timeout

    def _url(self, path: str) -> str:
        return f"{self.base_url}{path}"

    def _get(self, path: str, params: dict = None) -> dict:
        """Make a GET request."""
        with httpx.Client(base_url=self.base_url, timeout=self.timeout) as client:
            resp = client.get(path, params=params)
            resp.raise_for_status()
            return resp.json()

    def _post(self, path: str, json_data: dict = None, params: dict = None) -> dict:
        """Make a POST request with JSON body and optional query params."""
        with httpx.Client(base_url=self.base_url, timeout=self.timeout) as client:
            resp = client.post(path, json=json_data, params=params)
            resp.raise_for_status()
            return resp.json()

    def _post_file(self, path: str, file_path: str, data: dict) -> dict:
        """Make a POST request with file upload."""
        with open(file_path, "rb") as f:
            file_content = f.read()
        with httpx.Client(base_url=self.base_url, timeout=self.timeout * 10) as client:
            files = {"file": (os.path.basename(file_path), file_content, "application/gzip")}
            resp = client.post(path, files=files, data=data)
            resp.raise_for_status()
            return resp.json()

    # ── Status ───────────────────────────────────────────────────────────────

    def get_status(self, platform: str = "rock3c") -> dict:
        """Get server health and platform info."""
        return self._get("/api/v1/status", params={"platform": platform})

    # ── Environment ──────────────────────────────────────────────────────────

    def check_environment(self, platform: str = "rock3c") -> dict:
        """Check all dependencies on the board."""
        return self._get("/api/v1/environment", params={"platform": platform})

    def setup_environment(self, components: list[str] = None, platform: str = "rock3c") -> dict:
        """Install missing dependencies."""
        if components is None:
            components = ["build", "runtime"]
        return self._post(
            "/api/v1/setup",
            json_data={"components": components},
            params={"platform": platform},
        )

    # ── Build ────────────────────────────────────────────────────────────────

    def build(
        self,
        source_dir: str,
        platform: str = "rock3c",
        component: str = "all",
        trap_type: str = "detection",
        rebuild: bool = False,
        changed_files_only: bool = True,
    ) -> dict:
        """
        Create a git bundle of committed source files and send it to the build server.

        Uses `git bundle create` to produce a single file containing all committed
        files from the current HEAD. This guarantees consistency between local and
        remote builds. Uncommitted changes must be committed before building.

        Args:
            source_dir: Root directory of the project (e.g., /Users/steve/naturesense/ai-traps)
            platform: Target platform
            component: What to build ('model', 'toolkit', 'target', 'all')
            trap_type: Trap type ('detection', 'classification')
            rebuild: Full rebuild from scratch
            changed_files_only: If True, create a shallow bundle (HEAD only).
                               If False, include full history (--all).
        """
        # Create git bundle
        bundle_path = os.path.join(tempfile.gettempdir(), f"ai-traps-src-{platform}.bundle")

        # Handle case where changed_files_only might be passed as string "false"
        if isinstance(changed_files_only, str):
            changed_files_only = changed_files_only.lower() == "true"

        self._create_git_bundle(source_dir, bundle_path, shallow=changed_files_only)

        try:
            # Upload and trigger build
            with open(bundle_path, "rb") as f:
                file_content = f.read()

            with httpx.Client(base_url=self.base_url, timeout=self.timeout * 10) as client:
                files = {"file": ("source.bundle", file_content, "application/octet-stream")}
                data = {
                    "platform": platform,
                    "component": component,
                    "trap_type": trap_type,
                    "rebuild": str(rebuild).lower(),
                }
                resp = client.post("/api/v1/build", files=files, data=data)
                resp.raise_for_status()
                return resp.json()
        finally:
            # Clean up bundle
            try:
                os.unlink(bundle_path)
            except Exception:
                pass

    def _create_git_bundle(self, source_dir: str, output_path: str, shallow: bool = True) -> None:
        """Create a git bundle from the current HEAD.

        The bundle contains all committed files - no uncommitted changes are included.
        This guarantees the build server receives exactly the same source tree as
        the local repository at the given commit.

        Args:
            source_dir: Root directory of the git repository
            output_path: Path for the output .bundle file
            shallow: If True, bundle only HEAD (smaller). If False, bundle all refs.
        """
        if shallow:
            # Shallow bundle: just the current HEAD commit
            result = subprocess.run(
                ["git", "bundle", "create", output_path, "HEAD", "--tags"],
                capture_output=True, text=True, timeout=30,
                cwd=source_dir,
            )
        else:
            # Full bundle: all refs
            result = subprocess.run(
                ["git", "bundle", "create", output_path, "--all"],
                capture_output=True, text=True, timeout=30,
                cwd=source_dir,
            )

        if result.returncode != 0:
            raise RuntimeError(
                f"git bundle create failed: {result.stderr}"
            )

    def get_build_status(self, build_id: str, platform: str = "rock3c") -> dict:
        """Get the status of a build."""
        return self._get(f"/api/v1/build/{build_id}", params={"platform": platform})

    def get_build_log(self, build_id: str, platform: str = "rock3c", lines: int = 500) -> dict:
        """Get the log output for a build."""
        return self._get(
            f"/api/v1/build/{build_id}/log",
            params={"platform": platform, "lines": lines},
        )

    # ── Runtime ──────────────────────────────────────────────────────────────

    def create_runtime(
        self,
        build_id: str,
        platform: str = "rock3c",
        config_yaml: Optional[str] = None,
        install_path: str = "/usr/local/bin",
        model_path: Optional[str] = None,
        start_trap: bool = True,
    ) -> dict:
        """Create a runtime from a completed build."""
        data = {
            "build_id": build_id,
            "platform": platform,
            "install_path": install_path,
            "start_trap": str(start_trap).lower(),
        }
        if config_yaml:
            data["config_yaml"] = config_yaml
        if model_path:
            data["model_path"] = model_path

        with httpx.Client(base_url=self.base_url, timeout=self.timeout * 2) as client:
            resp = client.post("/api/v1/runtime", data=data)
            resp.raise_for_status()
            return resp.json()

    # ── Trap lifecycle ───────────────────────────────────────────────────────

    def start_trap(
        self,
        binary_path: str,
        config_path: str,
        args: str = "",
        platform: str = "rock3c",
    ) -> dict:
        """Start the trap binary."""
        return self._post(
            "/api/v1/trap/start",
            json_data={
                "binary_path": binary_path,
                "config_path": config_path,
                "args": args,
            },
            params={"platform": platform},
        )

    def stop_trap(self, signal: str = "TERM", platform: str = "rock3c") -> dict:
        """Stop the trap binary."""
        return self._post(
            "/api/v1/trap/stop",
            json_data={"signal": signal},
            params={"platform": platform},
        )

    def get_trap_status(self, platform: str = "rock3c") -> dict:
        """Get the status of the running trap."""
        return self._get("/api/v1/trap/status", params={"platform": platform})

    def get_trap_log(self, platform: str = "rock3c", lines: int = 100, source: str = "file") -> dict:
        """Get recent trap log output."""
        return self._get(
            "/api/v1/trap/log",
            params={"platform": platform, "lines": lines, "source": source},
        )
