"""
Model compilation functions.

Supports compiling ONNX models to:
- RKNN format for Rockchip NPU (ROCK 3C)
- NBG format for Vivante VIPLite NPU (Cubie A7S)
"""

import os
import sys
import subprocess
from typing import Dict, Any

from mcp_build.config import PLATFORM_CONFIGS, ssh_execute


def compile_rknn_model(project_root: str, trap_type: str) -> Dict[str, Any]:
    """Compile ONNX model to RKNN format for Rockchip NPU (ROCK 3C)"""
    result = {
        "component": "model",
        "platform": "rock3c",
        "model_type": "rknn",
        "success": False,
        "message": "",
        "output_path": None
    }

    # Determine model path based on trap_type
    if trap_type == "detection":
        model_dir = os.path.join(project_root, "models", "detection", "insects", "yolo26n")
        onnx_path = os.path.join(model_dir, "best.onnx")
        output_path = os.path.join(model_dir, "yolo26n.rknn")
        convert_script = os.path.join(model_dir, "convert", "convert_rknn.py")
    else:
        result["message"] = f"Unknown trap_type '{trap_type}' for RKNN compilation"
        return result

    # Validate inputs
    if not os.path.isfile(onnx_path):
        result["message"] = f"ONNX model not found: {onnx_path}"
        return result

    if not os.path.isfile(convert_script):
        result["message"] = f"Convert script not found: {convert_script}"
        return result

    # Run RKNN conversion (remote via SSH to ROCK 3C, or local if rknn-toolkit2 is installed)
    try:
        # Try local conversion first (if rknn-toolkit2 is installed)
        import importlib.util
        rknn_spec = importlib.util.find_spec("rknn")
        if rknn_spec is not None:
            # Local conversion
            cmd = [
                sys.executable, convert_script,
                "--input", onnx_path,
                "--output", output_path,
                "--target", "rk3566",
                "--imgsz", "320",
                "--quantize"
            ]
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
            if proc.returncode == 0:
                result["success"] = True
                result["message"] = f"RKNN model compiled locally: {output_path}"
                result["output_path"] = output_path
            else:
                result["message"] = f"Local RKNN conversion failed:\n{proc.stderr[-1000:]}"
        else:
            # Remote conversion via SSH to ROCK 3C
            rock3c_host = PLATFORM_CONFIGS["rock3c"]["default_host"]
            rock3c_user = PLATFORM_CONFIGS["rock3c"]["default_user"]
            remote_dir = "/tmp/rknn_convert"
            remote_onnx = f"{remote_dir}/best.onnx"
            remote_output = f"{remote_dir}/yolo26n.rknn"
            calib_dir = os.path.join(model_dir, "calib_images")

            # Create remote dir
            ssh_execute(rock3c_user, rock3c_host, f"mkdir -p {remote_dir}", timeout=10)

            # Copy ONNX and script to board
            subprocess.run(["scp", onnx_path, f"{rock3c_user}@{rock3c_host}:{remote_onnx}"],
                           capture_output=True, timeout=30)
            subprocess.run(["scp", convert_script, f"{rock3c_user}@{rock3c_host}:{remote_dir}/convert_rknn.py"],
                           capture_output=True, timeout=30)

            # Copy calibration images for quantization
            if os.path.isdir(calib_dir):
                calib_files = sorted(os.listdir(calib_dir))[:10]
                if calib_files:
                    remote_calib_dir = f"{remote_dir}/calib_images"
                    ssh_execute(rock3c_user, rock3c_host, f"mkdir -p {remote_calib_dir}", timeout=10)
                    for fname in calib_files:
                        local_path = os.path.join(calib_dir, fname)
                        if os.path.isfile(local_path):
                            subprocess.run(
                                ["scp", local_path, f"{rock3c_user}@{rock3c_host}:{remote_calib_dir}/{fname}"],
                                capture_output=True, timeout=30
                            )
                    dataset_content = "\n".join([f"{remote_calib_dir}/{f}" for f in calib_files])
                    dataset_cmd = f"cat > {remote_dir}/dataset.txt << 'DATASET_EOF'\n{dataset_content}\nDATASET_EOF"
                    ssh_execute(rock3c_user, rock3c_host, dataset_cmd, timeout=10)

            # Run conversion on board
            remote_cmd = (
                f"python3 {remote_dir}/convert_rknn.py"
                f" --input {remote_onnx}"
                f" --output {remote_output}"
                f" --target rk3566 --imgsz 320 --quantize"
            )
            if os.path.isdir(calib_dir):
                remote_cmd += f" --dataset {remote_dir}/dataset.txt"
            ssh_result = ssh_execute(rock3c_user, rock3c_host, remote_cmd, timeout=600)

            if ssh_result["success"]:
                subprocess.run(["scp",
                    f"{rock3c_user}@{rock3c_host}:{remote_output}", output_path],
                    capture_output=True, timeout=30
                )
                ssh_execute(rock3c_user, rock3c_host, f"rm -rf {remote_dir}", timeout=10)

                if os.path.isfile(output_path):
                    result["success"] = True
                    result["message"] = f"RKNN model compiled on {rock3c_host}: {output_path}"
                    result["output_path"] = output_path
                else:
                    result["message"] = f"Remote RKNN conversion completed but output not found at {output_path}"
            else:
                result["message"] = f"Remote RKNN conversion failed:\n{ssh_result['combined'][-1000:]}"

    except subprocess.TimeoutExpired:
        result["message"] = "RKNN conversion timed out after 600 seconds"
    except Exception as e:
        result["message"] = f"RKNN conversion error: {str(e)}"

    return result


def compile_nbg_model(project_root: str, trap_type: str) -> Dict[str, Any]:
    """Compile ONNX model to NBG format for Vivante VIPLite NPU (Cubie A7S)"""
    result = {
        "component": "model",
        "platform": "cubie-a7s",
        "model_type": "nbg",
        "success": False,
        "message": "",
        "output_path": None
    }

    if trap_type == "detection":
        model_dir = os.path.join(project_root, "models", "detection", "insects", "yolo26n")
        convert_dir = os.path.join(model_dir, "convert")
        nbg_output_dir = os.path.join(model_dir, "nbg_output")
        gen_dir = os.path.join(nbg_output_dir, "generated")
        onnx_path = os.path.join(model_dir, "best.onnx")
        output_path = os.path.join(nbg_output_dir, "network_binary.nb")
        docker_script = os.path.join(convert_dir, "convert_nbg_docker.sh")
    else:
        result["message"] = f"Unknown trap_type '{trap_type}' for NBG compilation"
        return result

    if not os.path.isfile(onnx_path):
        result["message"] = f"ONNX model not found: {onnx_path}"
        return result

    if not os.path.isfile(docker_script):
        result["message"] = f"Docker script not found: {docker_script}"
        return result

    try:
        docker_cmd = [
            "docker", "run", "--rm",
            "-v", f"{model_dir}:/models",
            "ubuntu-npu-with-acuity:latest",
            "bash", f"/models/convert/convert_nbg_docker.sh"
        ]

        proc = subprocess.run(docker_cmd, capture_output=True, text=True, timeout=600)

        if os.path.isfile(output_path) and os.path.getsize(output_path) > 0:
            result["success"] = True
            result["message"] = f"NBG model compiled via Docker: {output_path}"
            result["output_path"] = output_path
            return result

        cubie_host = PLATFORM_CONFIGS["cubie-a7s"]["default_host"]
        cubie_user = PLATFORM_CONFIGS["cubie-a7s"]["default_user"]

        reachable = ssh_execute(cubie_user, cubie_host, "echo ok", timeout=10)
        if not reachable["success"]:
            result["message"] = (
                "NBG packing requires native ARM64. "
                f"Generated files are in {gen_dir}/. "
                f"To complete, copy to Cubie A7S and run gen_nbg there. "
                f"Docker output: {proc.stdout[-500:]}"
            )
            return result

        remote_dir = "/tmp/nbg"
        ssh_execute(cubie_user, cubie_host, f"mkdir -p {remote_dir}", timeout=10)
        rsync_cmd = [
            "rsync", "-avz", "--delete",
            f"{gen_dir}/",
            f"{cubie_user}@{cubie_host}:{remote_dir}/"
        ]
        subprocess.run(rsync_cmd, capture_output=True, text=True, timeout=60)

        build_cmd = f"cd {remote_dir} && make -f makefile.linux && ./gen_nbg nbg_output.export.data images_392_0.tensor"
        build_result = ssh_execute(cubie_user, cubie_host, build_cmd, timeout=120)

        if build_result["success"]:
            subprocess.run(
                ["scp", f"{cubie_user}@{cubie_host}:{remote_dir}/network_binary.nb", output_path],
                capture_output=True, timeout=30
            )
            ssh_execute(cubie_user, cubie_host, f"rm -rf {remote_dir}", timeout=10)

            if os.path.isfile(output_path) and os.path.getsize(output_path) > 0:
                result["success"] = True
                result["message"] = f"NBG model compiled on {cubie_host}: {output_path}"
                result["output_path"] = output_path
            else:
                result["message"] = f"NBG compilation on {cubie_host} completed but output not found"
        else:
            result["message"] = f"NBG gen_nbg failed on {cubie_host}:\n{build_result['combined'][-1000:]}"

    except subprocess.TimeoutExpired:
        result["message"] = "NBG conversion timed out after 600 seconds"
    except Exception as e:
        result["message"] = f"NBG conversion error: {str(e)}"

    return result
