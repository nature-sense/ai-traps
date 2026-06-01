"""
MCP Build Server Package - AI Camera Trap Build System

Split into focused modules for maintainability:
- config.py: Platform configurations and SSH helpers
- parsers.py: Compiler error and crash output parsers
- model.py: Model compilation (RKNN, NBG)
- build.py: Toolkit and target build functions
- deploy.py: Deploy pipeline (config generation, systemd, deploy)
- tools.py: MCP tool definitions (@server.list_tools)
- handler.py: MCP tool call handler (@server.call_tool)
"""

from mcp.server import Server

# Shared server instance - imported by tools.py and handler.py
server = Server("ai-trap-build", version="1.2.0")
