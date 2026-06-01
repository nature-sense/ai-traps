"""
Platform configuration and SSH helpers.

Defines platform-specific settings for ROCK 3C, Cubie A7S, and RDK X5,
along with SSH execution utilities for remote command execution.
"""

import subprocess
import os
from pathlib import Path
from typing import Dict, Any, Optional

# ============================================================================
# Platform Configuration
# ============================================================================

PLATFORM_CONFIGS = {
    "rock3c": {
        "build_dir": "build-rock3c",
        "trap_path_template": "traps/targets/build/ai-trap-detection",
        "default_host": "rock-3c.local",
        "default_user": "radxa",
        "default_trap": "rock3c-imx219",
        "remote_dir": "~/ai-traps",
        "model_type": "rknn",
        "model_path": "models/detection/insects/yolo26n",
        "model_output": "yolo26n.rknn",
        "model_convert_script": "models/detection/insects/yolo26n/convert/convert_rknn.py",
    },
    "cubie-a7s": {
        "build_dir": "build-cubie-a7s",
        "trap_path_template": "traps/targets/build/ai-trap-detection",
        "default_host": "cubie-a7s.local",
        "default_user": "radxa",
        "default_trap": "cubie-a7s",
        "remote_dir": "~/ai-traps",
        "model_type": "nbg",
        "model_path": "models/detection/insects/yolo26n",
        "model_output": "nbg_output/network_binary.nb",
        "model_convert_script": "models/detection/insects/yolo26n/convert/convert_nbg_docker.sh",
    },
    "rdk-x5": {
        "build_dir": "build-rdk-x5",
        "trap_path_template": "traps/targets/build/ai-trap-detection",
        "default_host": "rdk-x5.local",
        "default_user": "root",
        "default_trap": "rdk-x5",
        "remote_dir": "~/ai-traps",
        "model_type": None,
        "model_path": None,
        "model_output": None,
        "model_convert_script": None,
    }
}


def get_platform_config(platform: str) -> Optional[Dict[str, Any]]:
    """Get platform configuration, returns None if unknown"""
    return PLATFORM_CONFIGS.get(platform)


def resolve_config(platform: str, params: Dict[str, Any]) -> Dict[str, Any]:
    """Resolve platform config with user-provided overrides"""
    config = get_platform_config(platform)
    if not config:
        return {"error": f"Unknown platform: {platform}"}

    return {
        "host": params.get("host", config["default_host"]),
        "user": params.get("user", config["default_user"]),
        "trap_name": params.get("trap_name", config["default_trap"]),
        "build_dir": config["build_dir"],
        "trap_path_template": config["trap_path_template"],
        "remote_dir": config["remote_dir"],
        "project_root": params.get("project_root", str(Path.home() / "naturesense" / "ai-traps")),
        "model_type": config["model_type"],
        "model_path": config["model_path"],
        "model_output": config["model_output"],
        "model_convert_script": config["model_convert_script"],
    }


def get_binary_path(remote_dir: str, build_dir: str, trap_path_template: str, trap_name: str) -> str:
    """Get the full path to the built binary on the board"""
    trap_path = trap_path_template.format(trap=trap_name)
    return f"{remote_dir}/{build_dir}/{trap_path}"


def get_run_command(user: str, host: str, binary_path: str, sudo: bool = True, args: str = "") -> str:
    """Get the SSH command to run the binary"""
    prefix = "sudo " if sudo else ""
    cd_cmd = f"cd {os.path.dirname(binary_path)} && " if os.path.dirname(binary_path) else ""
    return f"ssh {user}@{host} '{cd_cmd}{prefix}{binary_path} {args}'".strip()


# ============================================================================
# SSH Helpers
# ============================================================================

def ssh_execute(user: str, host: str, command: str, timeout: int = 60) -> Dict[str, Any]:
    """Execute a command on the remote board via SSH and return structured result"""
    try:
        ssh_cmd = ["ssh", "-o", "ConnectTimeout=10", f"{user}@{host}", command]
        result = subprocess.run(ssh_cmd, capture_output=True, text=True, timeout=timeout)
        return {
            "success": result.returncode == 0,
            "returncode": result.returncode,
            "stdout": result.stdout,
            "stderr": result.stderr,
            "combined": result.stdout + result.stderr
        }
    except subprocess.TimeoutExpired:
        return {
            "success": False,
            "returncode": -1,
            "stdout": "",
            "stderr": "",
            "combined": f"SSH command timed out after {timeout} seconds"
        }
    except Exception as e:
        return {
            "success": False,
            "returncode": -1,
            "stdout": "",
            "stderr": "",
            "combined": f"SSH error: {str(e)}"
        }


def ssh_execute_background(user: str, host: str, command: str, pid_file: str) -> Dict[str, Any]:
    """Launch a command in the background on the remote board via SSH"""
    try:
        # Use nohup and store PID
        bg_script = f"nohup sh -c '{command}' > /tmp/{os.path.basename(pid_file)}.out 2>&1 & echo $!"
        ssh_cmd = ["ssh", "-o", "ConnectTimeout=10", f"{user}@{host}", bg_script]
        result = subprocess.run(ssh_cmd, capture_output=True, text=True, timeout=15)
        pid = result.stdout.strip()

        if pid and pid.isdigit():
            # Store PID on the board for later management
            store_cmd = f"echo {pid} > {pid_file}"
            subprocess.run(["ssh", f"{user}@{host}", store_cmd], capture_output=True, timeout=10)
            return {
                "success": True,
                "pid": int(pid),
                "pid_file": pid_file,
                "message": f"Process started with PID {pid}"
            }
        else:
            return {
                "success": False,
                "pid": None,
                "message": f"Failed to start background process. Output: {result.stdout.strip() or result.stderr.strip()}"
            }
    except Exception as e:
        return {
            "success": False,
            "pid": None,
            "message": f"Error starting background process: {str(e)}"
        }
