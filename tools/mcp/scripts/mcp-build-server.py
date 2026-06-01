#!/usr/bin/env python3
"""
AI Camera Trap Build MCP Server

Multi-platform build tools for the AI Camera Trap project, enabling Cline/DeepSeek
to build, run, and debug code on remote SBCs (Single Board Computers) via SSH + rsync + meson.

This is the entry point. The implementation is split into the mcp_build package:
  - config.py:    Platform configurations and SSH helpers
  - parsers.py:   Compiler error and crash output parsers
  - model.py:     Model compilation (RKNN, NBG)
  - build.py:     Toolkit and target build functions
  - deploy.py:    Deploy pipeline (config generation, systemd, deploy)
  - tools.py:     MCP tool definitions (@server.list_tools)
  - handler.py:   MCP tool call handler (@server.call_tool)
"""

import sys
import os
import asyncio

# Ensure the package directory is on the path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Import modules to register tools and handlers
import mcp_build.tools      # noqa: F401 - registers @server.list_tools()
import mcp_build.handler    # noqa: F401 - registers @server.call_tool()

from mcp_build import server
from mcp.server.stdio import stdio_server


async def main():
    """Run the MCP build server"""
    async with stdio_server() as (read_stream, write_stream):
        await server.run(
            read_stream,
            write_stream,
            server.create_initialization_options()
        )


if __name__ == "__main__":
    asyncio.run(main())
