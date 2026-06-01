"""
Build functions for toolkit and target binaries.

Handles rsync-based source sync and meson-based compilation on remote boards.
"""

import os
import subprocess
from typing import Dict, Any

from mcp_build.config import resolve_config, get_binary_path, get_run_command, ssh_execute
from mcp_build.parsers import CompilerErrorParser


def build_toolkit(platform: str, params: Dict[str, Any]) -> Dict[str, Any]:
    """Build the toolkit static library (libtoolkit.a) on the target board"""
    cfg = resolve_config(platform, params)
    if "error" in cfg:
        return cfg

    host = cfg["host"]
    user = cfg["user"]
    build_dir = cfg["build_dir"]
    remote_dir = cfg["remote_dir"]
    project_root = cfg["project_root"]
    rebuild = params.get("rebuild", False)

    result = {
        "component": "toolkit",
        "platform": platform,
        "success": False,
        "message": "",
        "binary_path": None,
        "run_command": None
    }

    rsync_exclude = [
        "--exclude", f"{build_dir}",
        "--exclude", ".git",
        "--exclude", "*.o",
        "--exclude", "captures/",
        "--exclude", "build-*",
    ]

    toolkit_src = os.path.join(project_root, "traps", "toolkit")
    remote_toolkit = f"{remote_dir}/traps/toolkit"

    rsync_cmd = ["rsync", "-avz", "--delete"] + rsync_exclude + [
        f"{toolkit_src}/",
        f"{user}@{host}:{remote_toolkit}/"
    ]

    try:
        subprocess.run(rsync_cmd, capture_output=True, text=True, timeout=60)
    except Exception as e:
        result["message"] = f"rsync failed: {str(e)}"
        return result

    if rebuild:
        setup_cmd = f"cd {remote_toolkit} && rm -rf {build_dir} && meson setup {build_dir} -Dplatform={platform}"
    else:
        setup_cmd = f"cd {remote_toolkit} && (test -d {build_dir} || meson setup {build_dir} -Dplatform={platform})"

    setup_result = ssh_execute(user, host, setup_cmd, timeout=60)
    if not setup_result["success"]:
        result["message"] = f"meson setup failed:\n{setup_result['combined'][-1000:]}"
        return result

    compile_cmd = f"cd {remote_toolkit} && meson compile -C {build_dir}"
    compile_result = ssh_execute(user, host, compile_cmd, timeout=300)

    if compile_result["success"]:
        result["success"] = True
        result["message"] = f"Toolkit built successfully for {platform}"
    else:
        parsed = CompilerErrorParser.parse_output(compile_result["combined"])
        result["message"] = f"Toolkit build failed for {platform}"
        result["errors"] = parsed

    return result


def build_target(platform: str, params: Dict[str, Any]) -> Dict[str, Any]:
    """Build the trap target binary on the target board"""
    cfg = resolve_config(platform, params)
    if "error" in cfg:
        return cfg

    host = cfg["host"]
    user = cfg["user"]
    build_dir = cfg["build_dir"]
    remote_dir = cfg["remote_dir"]
    project_root = cfg["project_root"]
    trap_name = cfg["trap_name"]
    trap_type = params.get("trap_type", "detection")
    rebuild = params.get("rebuild", False)

    result = {
        "component": "target",
        "platform": platform,
        "trap_type": trap_type,
        "success": False,
        "message": "",
        "binary_path": None,
        "run_command": None
    }

    rsync_exclude = [
        "--exclude", f"{build_dir}",
        "--exclude", ".git",
        "--exclude", "*.o",
        "--exclude", "captures/",
        "--exclude", "build-*",
    ]

    targets_src = os.path.join(project_root, "traps", "targets")
    remote_targets = f"{remote_dir}/traps/targets"

    rsync_cmd = ["rsync", "-avz", "--delete"] + rsync_exclude + [
        f"{targets_src}/",
        f"{user}@{host}:{remote_targets}/"
    ]

    try:
        subprocess.run(rsync_cmd, capture_output=True, text=True, timeout=60)
    except Exception as e:
        result["message"] = f"rsync failed: {str(e)}"
        return result

    toolkit_build_dir = f"{remote_dir}/traps/toolkit/{build_dir}"

    if rebuild:
        setup_cmd = (
            f"cd {remote_targets} && rm -rf {build_dir} && "
            f"meson setup {build_dir} -Dplatform={platform} "
            f"-Dtoolkit_build_dir={toolkit_build_dir}"
        )
    else:
        setup_cmd = (
            f"cd {remote_targets} && (test -d {build_dir} || "
            f"meson setup {build_dir} -Dplatform={platform} "
            f"-Dtoolkit_build_dir={toolkit_build_dir})"
        )

    setup_result = ssh_execute(user, host, setup_cmd, timeout=60)
    if not setup_result["success"]:
        result["message"] = f"meson setup failed:\n{setup_result['combined'][-1000:]}"
        return result

    compile_cmd = f"cd {remote_targets} && meson compile -C {build_dir}"
    compile_result = ssh_execute(user, host, compile_cmd, timeout=300)

    if compile_result["success"]:
        binary_path = get_binary_path(remote_dir, build_dir, cfg["trap_path_template"], trap_name)
        run_cmd = get_run_command(user, host, binary_path, sudo=True)
        result["success"] = True
        result["message"] = f"Target built successfully for {platform}/{trap_type}"
        result["binary_path"] = binary_path
        result["run_command"] = run_cmd
    else:
        parsed = CompilerErrorParser.parse_output(compile_result["combined"])
        result["message"] = f"Target build failed for {platform}/{trap_type}"
        result["errors"] = parsed

    return result
