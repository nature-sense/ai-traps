#!/usr/bin/env python3
"""
AI Camera Trap Build Server - Entry Point

Runs as a root systemd service on port 8081 on target boards.
Provides a REST API for building, deploying, and managing AI Camera Trap binaries.

Usage:
  # Run directly (for testing)
  python3 build_server.py

  # Run with uvicorn
  uvicorn build_server.main:app --host 0.0.0.0 --port 8081

  # Install as systemd service
  cp build_server.py /usr/local/bin/ai-trap-build-server
  chmod +x /usr/local/bin/ai-trap-build-server
  # Create systemd unit (see deploy section below)
"""

import sys
import os

# When installed to /usr/local/bin/ai-trap-build-server, the build_server
# package lives in /usr/local/lib/ai-trap-build-server/build_server/.
# When running from the source tree, the package is in the same directory.
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SYSTEM_DIR = "/usr/local/lib/ai-trap-build-server"

# Try the source tree location first, then the system-deployed location
if os.path.isdir(os.path.join(SCRIPT_DIR, "build_server")):
    sys.path.insert(0, SCRIPT_DIR)
elif os.path.isdir(os.path.join(SYSTEM_DIR, "build_server")):
    sys.path.insert(0, SYSTEM_DIR)
else:
    # Fallback: assume we're in the same directory as the package
    sys.path.insert(0, SCRIPT_DIR)

from build_server.main import app


def main():
    """Run the build server with uvicorn."""
    import uvicorn
    uvicorn.run(
        "build_server.main:app",
        host="0.0.0.0",
        port=8081,
        log_level="info",
    )


if __name__ == "__main__":
    main()
