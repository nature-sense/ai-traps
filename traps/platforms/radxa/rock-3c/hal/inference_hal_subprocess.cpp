#include "inference_hal_subprocess.hpp"
#include <iostream>
#include <cstring>
#include <chrono>
#include <sstream>
#include <cstdlib>
#include <poll.h>
#include <fcntl.h>
#include <thread>
#include <signal.h>

extern char** environ;

namespace ct {

// ─── Constructor / Destructor ────────────────────────────────────────────────

InferenceHalSubprocess::InferenceHalSubprocess() = default;

InferenceHalSubprocess::~InferenceHalSubprocess() {
    shutdown();
}

// ─── spawn_server ─────────────────────────────────────────────────────────────

bool InferenceHalSubprocess::spawn_server(const std::string& model_path,
                                          float conf_thresh) {
    int stdin_pipe[2];  // parent → child stdin
    int stdout_pipe[2]; // child stdout → parent

    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        std::cerr << "[InferenceHalSubprocess] pipe() failed\n";
        return false;
    }

    child_pid_ = fork();
    if (child_pid_ < 0) {
        std::cerr << "[InferenceHalSubprocess] fork() failed\n";
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return false;
    }

    if (child_pid_ == 0) {
        // ── Child process (Python server) ────────────────────────────────────
        close(stdin_pipe[1]);   // close write end of stdin pipe
        close(stdout_pipe[0]);  // close read end of stdout pipe

        // Redirect stdin from pipe
        dup2(stdin_pipe[0], STDIN_FILENO);
        close(stdin_pipe[0]);

        // Redirect stdout to pipe
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdout_pipe[1]);

        // stderr remains connected to the parent's stderr for logging

        // Path to inference_server.py — deployed alongside the binary
        const char* server_script = getenv("RKNN_SERVER_PATH");
        std::string script_path;
        if (server_script) {
            script_path = server_script;
        } else {
            script_path = "/usr/share/ai-trap/inference_server.py";
        }

        std::string conf_str = std::to_string(conf_thresh);

        // Execute Python
        execlp("python3", "python3", script_path.c_str(),
               model_path.c_str(), conf_str.c_str(), nullptr);

        // If execlp returns, it failed
        std::cerr << "[InferenceHalSubprocess] exec failed: "
                  << strerror(errno) << "\n";
        _exit(1);
    }

    // ── Parent process ───────────────────────────────────────────────────────
    close(stdin_pipe[0]);   // close read end of stdin pipe
    close(stdout_pipe[1]);  // close write end of stdout pipe

    stdin_fd_  = stdin_pipe[1];   // write to child stdin
    stdout_fd_ = stdout_pipe[0];  // read from child stdout

    // Make stdout_fd non-blocking for poll-based reads
    int flags = fcntl(stdout_fd_, F_GETFL, 0);
    fcntl(stdout_fd_, F_SETFL, flags | O_NONBLOCK);

    // Wait for the server to print "[RKNN-Server] Ready" on stderr
    // The server writes status to stderr which goes to our stderr directly.
    // We just need to give it time to initialise the model.
    std::cout << "[InferenceHalSubprocess] spawned pid=" << child_pid_
              << " model=" << model_path << "\n";

    // Small delay to let Python initialise the NPU
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    return true;
}

// ─── read_line ────────────────────────────────────────────────────────────────

std::string InferenceHalSubprocess::read_line() {
    std::string line;
    char buf[4096];

    while (true) {
        struct pollfd pfd;
        pfd.fd = stdout_fd_;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 2000);  // 2s timeout

        if (ret <= 0) {
            // Timeout or error
            break;
        }

        ssize_t n = read(stdout_fd_, buf, sizeof(buf) - 1);
        if (n <= 0) {
            break;
        }
        buf[n] = '\0';

        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                return line;
            }
            line += buf[i];
        }
        // If no newline yet, keep reading
    }

    return line;
}

// ─── init ─────────────────────────────────────────────────────────────────────

bool InferenceHalSubprocess::init(const std::string& model_path,
                                  float conf_thresh) {
    if (child_pid_ > 0) {
        std::cerr << "[InferenceHalSubprocess] already initialised\n";
        return false;
    }

    conf_thresh_ = conf_thresh;
    std::cout << "[InferenceHalSubprocess] starting Python inference server...\n";

    if (!spawn_server(model_path, conf_thresh_)) {
        std::cerr << "[InferenceHalSubprocess] spawn_server failed\n";
        return false;
    }

    std::cout << "[InferenceHalSubprocess] ready (conf_thresh="
              << conf_thresh_ << ", model=" << model_path << ")\n";
    return true;
}

// ─── detect ───────────────────────────────────────────────────────────────────

std::vector<Detection> InferenceHalSubprocess::detect(const FrameBuffer& frame) {
    if (child_pid_ < 0) {
        std::cerr << "[InferenceHalSubprocess] not initialised\n";
        return {};
    }

    auto t0 = std::chrono::steady_clock::now();

    // Write header: width (u32) + height (u32)
    uint32_t w = frame.width;
    uint32_t h = frame.height;
    uint32_t header[2] = {w, h};
    write(stdin_fd_, header, sizeof(header));

    // Write NV12 frame data
    write(stdin_fd_, frame.data, frame.size);

    // Read JSON response
    std::string json_line = read_line();

    auto t1 = std::chrono::steady_clock::now();
    last_inference_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
                             t1 - t0).count();

    // Parse JSON detections
    std::vector<Detection> detections;

    if (!json_line.empty()) {
        // Simple JSON array parser: [[x,y,w,h,conf,cls], ...]
        // Format: [0.123,0.456,0.1,0.2,0.95,0]
        if (json_line[0] == '[') {
            size_t pos = 1;
            while (pos < json_line.size() && json_line[pos] != ']') {
                // Skip whitespace
                while (pos < json_line.size() &&
                       (json_line[pos] == ' ' || json_line[pos] == '\t'))
                    ++pos;
                if (pos >= json_line.size() || json_line[pos] == ']')
                    break;

                // Expect '['
                if (json_line[pos] != '[') {
                    ++pos;
                    continue;
                }
                ++pos;

                // Parse 6 floats
                float vals[6];
                int val_idx = 0;
                while (val_idx < 6 && pos < json_line.size() &&
                       json_line[pos] != ']') {
                    // Skip commas, spaces
                    while (pos < json_line.size() &&
                           (json_line[pos] == ',' || json_line[pos] == ' ' ||
                            json_line[pos] == '\t'))
                        ++pos;
                    if (pos >= json_line.size() || json_line[pos] == ']')
                        break;

                    char* end = nullptr;
                    vals[val_idx] = strtof(json_line.c_str() + pos, &end);
                    if (end == json_line.c_str() + pos)
                        break; // parse error
                    pos = end - json_line.c_str();
                    ++val_idx;
                }

                // Skip closing ']' and comma
                while (pos < json_line.size() && json_line[pos] != ']' &&
                       json_line[pos] != ',')
                    ++pos;
                if (pos < json_line.size() && json_line[pos] == ']')
                    ++pos;

                if (val_idx >= 6) {
                    Detection det;
                    det.x = vals[0];
                    det.y = vals[1];
                    det.w = vals[2];
                    det.h = vals[3];
                    det.confidence = vals[4];
                    det.class_id = static_cast<int>(vals[5]);
                    detections.push_back(det);
                }
            }
        }
    }

    static int log_cnt = 0;
    if (++log_cnt <= 10) {
        std::cout << "[InferenceHalSubprocess] "
                  << (last_inference_us_ / 1000.0f) << "ms, "
                  << "detections=" << detections.size() << "\n";
    }

    return detections;
}

// ─── shutdown ─────────────────────────────────────────────────────────────────

void InferenceHalSubprocess::shutdown() {
    if (child_pid_ > 0) {
        // Close pipes to signal EOF to child
        if (stdin_fd_ >= 0) {
            close(stdin_fd_);
            stdin_fd_ = -1;
        }
        if (stdout_fd_ >= 0) {
            close(stdout_fd_);
            stdout_fd_ = -1;
        }

        // Wait for child to exit gracefully
        int status;
        pid_t result = waitpid(child_pid_, &status, WNOHANG);
        if (result == 0) {
            // Still running — send SIGTERM
            kill(child_pid_, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            result = waitpid(child_pid_, &status, WNOHANG);
        }
        if (result == 0) {
            // Force kill
            kill(child_pid_, SIGKILL);
            waitpid(child_pid_, &status, 0);
        }

        std::cout << "[InferenceHalSubprocess] shutdown (pid="
                  << child_pid_ << ")\n";
        child_pid_ = -1;
    }
}

} // namespace ct