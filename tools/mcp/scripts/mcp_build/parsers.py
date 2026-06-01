"""
Compiler error and crash output parsers.

Provides structured parsing of GCC/Clang compiler output and GDB crash backtraces
for AI-friendly consumption.
"""

import re
from typing import Dict, Any, Optional


class CompilerErrorParser:
    """Parse GCC/Clang compiler output into structured errors"""

    # GCC/Clang error pattern: file:line:column: error: message
    GCC_ERROR_PATTERN = re.compile(
        r'^(?P<file>[^:]+):(?P<line>\d+):(?P<col>\d+):\s+(?P<severity>error|warning|note):\s+(?P<message>.*)$'
    )

    # Linker error pattern (file:line: error: message)
    LINKER_ERROR_PATTERN = re.compile(
        r'^(?P<file>[^:]+):(?P<line>\d+):\s+(?P<severity>error|warning):\s+(?P<message>.*)$'
    )

    # Undefined reference (linker)
    UNDEFINED_PATTERN = re.compile(
        r'undefined reference to [`\'](?P<symbol>[^\'\"]+)'
    )

    # Missing include
    MISSING_INCLUDE_PATTERN = re.compile(
        r'[`\'](?P<header>[^\'\"]+\.h)[`\']: No such file or directory'
    )

    @classmethod
    def parse_line(cls, line: str) -> Optional[Dict[str, Any]]:
        """Parse a single compiler output line"""

        # Try GCC pattern
        match = cls.GCC_ERROR_PATTERN.match(line)
        if match:
            return {
                "type": "compiler",
                "file": match.group("file"),
                "line": int(match.group("line")),
                "column": int(match.group("col")),
                "severity": match.group("severity"),
                "message": match.group("message").strip()
            }

        # Try linker pattern
        match = cls.LINKER_ERROR_PATTERN.match(line)
        if match:
            error = {
                "type": "linker",
                "file": match.group("file"),
                "line": int(match.group("line")),
                "severity": match.group("severity"),
                "message": match.group("message").strip()
            }

            # Check for undefined reference
            undef_match = cls.UNDEFINED_PATTERN.search(line)
            if undef_match:
                error["symbol"] = undef_match.group("symbol")
                error["suggestion"] = f"Missing implementation or library for '{undef_match.group('symbol')}'"

            return error

        return None

    @classmethod
    def parse_output(cls, output: str) -> Dict[str, Any]:
        """Parse full compiler/linker output"""
        errors = []
        warnings = []
        linker_errors = []

        lines = output.split('\n')

        for line in lines:
            parsed = cls.parse_line(line)

            if parsed:
                if parsed["severity"] == "error":
                    if parsed["type"] == "linker":
                        linker_errors.append(parsed)
                    else:
                        errors.append(parsed)
                elif parsed["severity"] == "warning":
                    warnings.append(parsed)

        # Summary
        summary = {
            "total_errors": len(errors),
            "total_warnings": len(warnings),
            "total_linker_errors": len(linker_errors),
            "build_failed": len(errors) > 0 or len(linker_errors) > 0
        }

        # Generate AI-friendly suggestions
        suggestions = []
        for err in errors:
            if "implicit declaration" in err["message"]:
                suggestions.append(f"Missing #include in {err['file']}:{err['line']}")
            elif "unknown type name" in err["message"]:
                suggestions.append(f"Missing header or forward declaration in {err['file']}:{err['line']}")

        for err in linker_errors:
            if "undefined reference" in err["message"]:
                symbol = err.get("symbol", "unknown")
                suggestions.append(f"Linker error: '{symbol}' not defined. Check library linkage or implement missing function.")

        return {
            "errors": errors,
            "warnings": warnings,
            "linker_errors": linker_errors,
            "summary": summary,
            "suggestions": suggestions,
            "raw_output": output[-2000:]  # Last 2000 chars for context
        }


class CrashParser:
    """Parse GDB backtrace and crash output into structured data"""

    # Signal pattern: "Program received signal SIGSEGV, Segmentation fault."
    SIGNAL_PATTERN = re.compile(
        r'Program received signal\s+(?P<signal>SIG\w+),\s+(?P<signal_desc>.+)$'
    )

    # Backtrace frame: "#0  function (arg=...) at file:line"
    BT_FRAME_PATTERN = re.compile(
        r'^#(?P<frame>\d+)\s+(0x[0-9a-f]+\s+in\s+)?(?P<function>\S+)\s+'
        r'(\(.*\)\s+)?at\s+(?P<file>[^:]+):(?P<line>\d+)'
    )

    # Backtrace frame without file info: "#0  0x0000... in function_name"
    BT_FRAME_NO_FILE_PATTERN = re.compile(
        r'^#(?P<frame>\d+)\s+(0x[0-9a-f]+\s+in\s+)?(?P<function>\S+)'
    )

    # Fault address: "Address 0x1234 is not stack'd, malloc'd or (recently) free'd"
    FAULT_ADDR_PATTERN = re.compile(
        r'[Aa]ddress\s+(?P<address>0x[0-9a-f]+)'
    )

    @classmethod
    def parse_gdb_output(cls, output: str) -> Dict[str, Any]:
        """Parse GDB output into structured crash data"""
        result = {
            "crashed": False,
            "signal": None,
            "signal_description": None,
            "fault_address": None,
            "backtrace": [],
            "raw_output": output[-3000:]  # Last 3000 chars
        }

        lines = output.split('\n')

        for line in lines:
            # Check for signal
            signal_match = cls.SIGNAL_PATTERN.search(line)
            if signal_match:
                result["crashed"] = True
                result["signal"] = signal_match.group("signal")
                result["signal_description"] = signal_match.group("signal_desc").strip()

            # Check for fault address
            addr_match = cls.FAULT_ADDR_PATTERN.search(line)
            if addr_match:
                result["fault_address"] = addr_match.group("address")

            # Check for backtrace frame with file info
            bt_match = cls.BT_FRAME_PATTERN.match(line)
            if bt_match:
                result["backtrace"].append({
                    "frame": int(bt_match.group("frame")),
                    "function": bt_match.group("function"),
                    "file": bt_match.group("file"),
                    "line": int(bt_match.group("line"))
                })
                continue

            # Check for backtrace frame without file info
            bt_match = cls.BT_FRAME_NO_FILE_PATTERN.match(line)
            if bt_match:
                result["backtrace"].append({
                    "frame": int(bt_match.group("frame")),
                    "function": bt_match.group("function"),
                    "file": None,
                    "line": None
                })

        # Check for segfault in output even without GDB signal pattern
        if not result["signal"]:
            for line in lines:
                if "Segmentation fault" in line or "SIGSEGV" in line:
                    result["crashed"] = True
                    result["signal"] = "SIGSEGV"
                    result["signal_description"] = "Segmentation fault"
                    break
                elif "SIGABRT" in line or "Aborted" in line:
                    result["crashed"] = True
                    result["signal"] = "SIGABRT"
                    result["signal_description"] = "Aborted (assertion failure or abort())"
                    break

        return result
