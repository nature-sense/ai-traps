#pragma once

#include "hal/api/inference_hal.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>

namespace ct {

// ─── InferenceHalSubprocess ─────────────────────────────────────────────────
// IInferenceHAL implementation that spawns a persistent Python subprocess
// (inference_server.py) and communicates via stdin/stdout pipes.
//
// This avoids the C ABI incompatibility with librknnrt.so v2.3.2 by running
// the inference in a stable Python environment with rknn-toolkit2 v2.3.2.
//
// Protocol per frame:
//   C++ writes:  <4-byte uint32: width><4-byte uint32: height><NV12 bytes>
//   Python reads: <JSON array of detections>\n
class InferenceHalSubprocess : public IInferenceHAL {
public:
    InferenceHalSubprocess();
    ~InferenceHalSubprocess() override;

    InferenceHalSubprocess(const InferenceHalSubprocess&) = delete;
    InferenceHalSubprocess& operator=(const InferenceHalSubprocess&) = delete;

    bool init(const std::string& model_path, float confidence_threshold) override;
    std::vector<Detection> detect(const FrameBuffer& frame) override;

    int64_t last_inference_us() const override { return last_inference_us_; }
    uint32_t input_width() const override { return static_cast<uint32_t>(model_in_w_); }
    uint32_t input_height() const override { return static_cast<uint32_t>(model_in_h_); }

    void shutdown() override;

private:
    // Spawn the Python server process, return true on success
    bool spawn_server(const std::string& model_path, float conf_thresh);
    // Read a JSON line from the server's stdout
    std::string read_line();

    // ── Process state ────────────────────────────────────────────────────────
    pid_t child_pid_ = -1;
    int stdin_fd_  = -1;  // write end of pipe to child stdin
    int stdout_fd_ = -1;  // read end of pipe from child stdout

    // ── Model info ───────────────────────────────────────────────────────────
    int model_in_w_ = 320;
    int model_in_h_ = 320;
    float conf_thresh_ = 0.5f;

    // ── Timing ───────────────────────────────────────────────────────────────
    int64_t last_inference_us_ = 0;
};

} // namespace ct