#!/usr/bin/env python3
"""
Classic Mac Hardware MCP Server

Provides FTP-based access to Classic Macintosh test machines running RumpusFTP.

Key design decisions for RumpusFTP compatibility:
- Plain FTP (not SFTP) with passive mode
- Rate limiting between operations (old Macs are slow)
- Mac-style colon paths internally, normalize on input
- Single operation per connection for stability
"""

import asyncio
import io
import json
import os
import sys
import time
from ftplib import FTP
from pathlib import Path
from typing import Any, Optional, Tuple
from datetime import datetime

from mcp.server import Server
from mcp.types import (
    Resource,
    Tool,
    TextContent,
    ImageContent,
    EmbeddedResource,
    LoggingLevel
)
import mcp.server.stdio


# Rate limiting for RumpusFTP stability (old Macs need time between operations)
FTP_OPERATION_DELAY = 0.5  # seconds between FTP operations
FTP_RETRY_DELAY = 2.0      # seconds before retry after failure
FTP_MAX_RETRIES = 2


class ClassicMacHardwareServer:
    """MCP Server for Classic Mac hardware access via FTP."""

    def __init__(self, config_path: str):
        """Initialize with machines configuration."""
        self.config_path = config_path
        self.machines = {}
        self._config_mtime = 0
        self._first_load = True
        self._last_ftp_time = 0  # For rate limiting
        self._reload_if_changed()
        self._first_load = False
        self.server = Server("classic-mac-hardware")

        # Register handlers
        self.server.list_resources()(self.list_resources)
        self.server.read_resource()(self.read_resource)
        self.server.list_tools()(self.list_tools)
        self.server.call_tool()(self.call_tool)
        self.server.list_prompts()(self.list_prompts)
        self.server.get_prompt()(self.get_prompt)

    # =========================================================================
    # Path Normalization
    # =========================================================================

    def _normalize_path(self, path: str) -> str:
        """
        Normalize path to Mac colon format for FTP.

        Input formats accepted:
        - "/" or empty -> root (no cwd needed)
        - "/folder/subfolder" -> "folder:subfolder"
        - "folder:subfolder" -> unchanged
        - "folder/subfolder" -> "folder:subfolder"
        """
        if not path or path == "/" or path == ".":
            return ""

        # Remove leading/trailing slashes
        path = path.strip("/")

        # Convert forward slashes to colons (Mac format)
        path = path.replace("/", ":")

        return path

    def _split_path(self, path: str) -> Tuple[str, str]:
        """
        Split path into directory and filename.

        Returns (directory, filename) where directory may be empty.
        """
        path = self._normalize_path(path)
        if ":" in path:
            parts = path.rsplit(":", 1)
            return (parts[0], parts[1])
        return ("", path)

    # =========================================================================
    # FTP Operations with Rate Limiting
    # =========================================================================

    def _rate_limit(self):
        """Wait if needed to avoid overwhelming RumpusFTP."""
        elapsed = time.time() - self._last_ftp_time
        if elapsed < FTP_OPERATION_DELAY:
            time.sleep(FTP_OPERATION_DELAY - elapsed)
        self._last_ftp_time = time.time()

    def _connect_ftp(self, machine_id: str) -> FTP:
        """Create FTP connection with rate limiting."""
        self._validate_machine_id(machine_id)

        machine = self.machines[machine_id]
        if 'ftp' not in machine:
            raise ValueError(f"FTP not configured for {machine['name']}. This machine uses LaunchAPPL only.")

        self._rate_limit()
        ftp_config = machine['ftp']

        ftp = FTP()
        ftp.set_pasv(True)
        ftp.connect(ftp_config['host'], ftp_config.get('port', 21), timeout=30)
        ftp.login(ftp_config['username'], ftp_config['password'])

        return ftp

    def _ftp_operation(self, machine_id: str, operation, *args, **kwargs):
        """
        Execute FTP operation with retry logic.

        Args:
            machine_id: Machine to connect to
            operation: Callable that takes (ftp, *args, **kwargs)

        Returns:
            Result from operation
        """
        last_error = None
        for attempt in range(FTP_MAX_RETRIES):
            try:
                ftp = self._connect_ftp(machine_id)
                try:
                    result = operation(ftp, *args, **kwargs)
                    return result
                finally:
                    try:
                        ftp.quit()
                    except:
                        pass
            except Exception as e:
                last_error = e
                if attempt < FTP_MAX_RETRIES - 1:
                    time.sleep(FTP_RETRY_DELAY)

        raise last_error

    # =========================================================================
    # Configuration
    # =========================================================================

    def _reload_if_changed(self) -> bool:
        """Hot-reload configuration if machines.json has changed."""
        try:
            current_mtime = os.path.getmtime(self.config_path)
            if current_mtime > self._config_mtime:
                self.machines = self._load_config()
                self._config_mtime = current_mtime
                print(f"✓ Reloaded config: {len(self.machines)} machines", file=sys.stderr)
                return True
            return False
        except FileNotFoundError:
            if self._first_load:
                print(f"ℹ No machines configured. Run /setup-machine to add Classic Macs.", file=sys.stderr)
            return False
        except Exception as e:
            print(f"⚠ Config reload failed: {e}", file=sys.stderr)
            return False

    def _load_config(self) -> dict:
        """Load and validate machines configuration."""
        try:
            with open(self.config_path) as f:
                config = json.load(f)

            # Expand environment variables in passwords
            for machine_id, machine in config.items():
                if 'ftp' in machine and 'password' in machine['ftp']:
                    password = machine['ftp']['password']
                    if password.startswith('${') and password.endswith('}'):
                        env_var = password[2:-1]
                        machine['ftp']['password'] = os.environ.get(env_var, '')

            return config
        except Exception as e:
            print(f"Error loading config: {e}", file=sys.stderr)
            return {}

    def _validate_machine_id(self, machine_id: str) -> None:
        """Validate machine ID and raise helpful error if invalid."""
        if machine_id not in self.machines:
            available = ', '.join(self.machines.keys()) if self.machines else '(none)'
            raise ValueError(
                f"Unknown machine: '{machine_id}'\n"
                f"Available: {available}\n"
                f"Run /setup-machine to add machines."
            )

    # =========================================================================
    # Resources (Read-only)
    # =========================================================================

    async def list_resources(self) -> list[Resource]:
        """List available resources."""
        self._reload_if_changed()
        resources = []

        for machine_id, machine in self.machines.items():
            resources.append(Resource(
                uri=f"mac://{machine_id}/logs/latest",
                name=f"Logs: {machine['name']}",
                description=f"PT_Log output from {machine['name']}",
                mimeType="text/plain"
            ))

        return resources

    async def read_resource(self, uri: str) -> str:
        """Read resource content via FTP."""
        self._reload_if_changed()

        if not uri.startswith("mac://"):
            raise ValueError(f"Invalid URI: {uri}")

        parts = uri[6:].split('/', 2)
        machine_id = parts[0]
        resource_type = parts[1] if len(parts) > 1 else ''
        identifier = parts[2] if len(parts) > 2 else 'latest'

        if resource_type == "logs":
            return self._fetch_log_content(machine_id, identifier)
        else:
            raise ValueError(f"Unknown resource type: {resource_type}")

    def _fetch_log_content(self, machine_id: str, identifier: str) -> str:
        """Fetch PT_Log content from machine."""
        def operation(ftp):
            machine = self.machines[machine_id]

            # PT_Log writes to a file called "PT_Log" (no extension)
            # Try multiple common locations
            log_locations = ["PT_Log", "pt_log", "PT_Log.txt"]

            for log_name in log_locations:
                try:
                    lines = []
                    ftp.retrlines(f'RETR {log_name}', lines.append)
                    if lines:
                        return '\n'.join(lines)
                except:
                    pass

            return "No PT_Log file found in FTP root"

        return self._ftp_operation(machine_id, operation)

    # =========================================================================
    # Tools
    # =========================================================================

    async def list_tools(self) -> list[Tool]:
        """List available tools."""
        machine_ids = ', '.join(self.machines.keys()) if self.machines else 'none'

        return [
            Tool(
                name="list_machines",
                description="List configured Classic Mac machines",
                inputSchema={"type": "object", "properties": {}}
            ),
            Tool(
                name="reload_config",
                description="Force reload machines.json",
                inputSchema={"type": "object", "properties": {}}
            ),
            Tool(
                name="test_connection",
                description="Test FTP connectivity to a machine",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {"type": "string", "description": f"Machine ID ({machine_ids})"},
                        "test_launchappl": {"type": "boolean", "default": False}
                    },
                    "required": ["machine"]
                }
            ),
            Tool(
                name="list_directory",
                description="List files in directory. Path can use / or : separators.",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {"type": "string", "description": f"Machine ID ({machine_ids})"},
                        "path": {"type": "string", "description": "Directory path (default: /)", "default": "/"}
                    },
                    "required": ["machine"]
                }
            ),
            Tool(
                name="create_directory",
                description="Create directory (and parent directories if needed)",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {"type": "string", "description": f"Machine ID ({machine_ids})"},
                        "path": {"type": "string", "description": "Directory path to create"}
                    },
                    "required": ["machine", "path"]
                }
            ),
            Tool(
                name="delete_files",
                description="Delete file or directory",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {"type": "string", "description": f"Machine ID ({machine_ids})"},
                        "path": {"type": "string", "description": "Path to delete"},
                        "recursive": {"type": "boolean", "default": False}
                    },
                    "required": ["machine", "path"]
                }
            ),
            Tool(
                name="upload_file",
                description="Upload file to Classic Mac. Creates parent directories if needed.",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {"type": "string", "description": f"Machine ID ({machine_ids})"},
                        "local_path": {"type": "string", "description": "Local file (relative to /workspace)"},
                        "remote_path": {"type": "string", "description": "Remote path (e.g., 'file.bin' or 'folder:file.bin')"}
                    },
                    "required": ["machine", "local_path", "remote_path"]
                }
            ),
            Tool(
                name="download_file",
                description="Download file from Classic Mac",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {"type": "string", "description": f"Machine ID ({machine_ids})"},
                        "remote_path": {"type": "string", "description": "Remote file path"},
                        "local_path": {"type": "string", "description": "Local destination (optional)"}
                    },
                    "required": ["machine", "remote_path"]
                }
            ),
            Tool(
                name="fetch_logs",
                description="Download PT_Log from Classic Mac",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {"type": "string", "description": f"Machine ID ({machine_ids})"}
                    },
                    "required": ["machine"]
                }
            ),
            Tool(
                name="execute_binary",
                description="Run binary via LaunchAPPL (requires LaunchAPPLServer on Mac)",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {"type": "string", "description": f"Machine ID ({machine_ids})"},
                        "platform": {"type": "string", "enum": ["mactcp", "opentransport", "appletalk"]},
                        "binary_path": {"type": "string", "description": "Path to .bin file"}
                    },
                    "required": ["machine", "platform", "binary_path"]
                }
            ),
            Tool(
                name="cleanup_machine",
                description="Remove old files from Classic Mac",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {"type": "string", "description": f"Machine ID ({machine_ids})"},
                        "scope": {"type": "string", "enum": ["old_files", "all"], "default": "old_files"}
                    },
                    "required": ["machine"]
                }
            )
        ]

    async def call_tool(self, name: str, arguments: dict) -> list[TextContent]:
        """Execute tool."""
        self._reload_if_changed()

        try:
            if name == "list_machines":
                return self._tool_list_machines()
            elif name == "reload_config":
                return self._tool_reload_config()
            elif name == "test_connection":
                return self._tool_test_connection(arguments)
            elif name == "list_directory":
                return self._tool_list_directory(arguments)
            elif name == "create_directory":
                return self._tool_create_directory(arguments)
            elif name == "delete_files":
                return self._tool_delete_files(arguments)
            elif name == "upload_file":
                return self._tool_upload_file(arguments)
            elif name == "download_file":
                return self._tool_download_file(arguments)
            elif name == "fetch_logs":
                return await self._tool_fetch_logs(arguments)
            elif name == "execute_binary":
                return self._tool_execute_binary(arguments)
            elif name == "cleanup_machine":
                return self._tool_cleanup_machine(arguments)
            else:
                raise ValueError(f"Unknown tool: {name}")
        except Exception as e:
            return [TextContent(type="text", text=f"❌ Error: {str(e)}")]

    def _tool_list_machines(self) -> list[TextContent]:
        """List all configured machines."""
        if not self.machines:
            return [TextContent(type="text", text="No machines configured.\nRun /setup-machine to add Classic Macs.")]

        lines = ["Configured machines:\n"]
        for mid, m in self.machines.items():
            # Get host from ftp or launchappl config
            host = m.get('ftp', {}).get('host') or m.get('launchappl', {}).get('host', 'unknown')
            features = []
            if 'ftp' in m:
                features.append('FTP')
            if 'launchappl' in m:
                features.append('LaunchAPPL')
            features_str = '+'.join(features) if features else 'no remote'

            # Show build requirement and RAM
            build_type = m.get('build', 'standard')
            ram = m.get('ram', '')
            build_info = f" [{build_type}]" if build_type == 'lowmem' else ""
            ram_info = f" ({ram})" if ram else ""

            lines.append(f"  {mid}: {m['name']} ({m['platform']}) - {host} [{features_str}]{ram_info}{build_info}")

        # Add helpful note if any machine requires lowmem builds
        if any(m.get('build') == 'lowmem' for m in self.machines.values()):
            lines.append("")
            lines.append("⚠️ Machines marked [lowmem] require *_lowmem.bin builds!")
            lines.append("   Build with: make -f Makefile.retro68 PLATFORM=mactcp lowmem_tests")

        return [TextContent(type="text", text="\n".join(lines))]

    def _tool_reload_config(self) -> list[TextContent]:
        """Force reload configuration."""
        old_count = len(self.machines)
        self.machines = self._load_config()
        self._config_mtime = os.path.getmtime(self.config_path)
        new_count = len(self.machines)

        return [TextContent(
            type="text",
            text=f"✅ Reloaded: {old_count} → {new_count} machines\n" +
                 "\n".join(f"  - {mid}" for mid in self.machines.keys())
        )]

    def _tool_test_connection(self, args: dict) -> list[TextContent]:
        """Test FTP and/or LaunchAPPL connection."""
        machine_id = args["machine"]
        self._validate_machine_id(machine_id)
        machine = self.machines[machine_id]

        results = []

        # Test FTP only if configured
        if 'ftp' in machine:
            try:
                ftp = self._connect_ftp(machine_id)
                pwd = ftp.pwd()
                ftp.quit()
                results.append(f"✓ FTP: Connected (root: {pwd})")
            except Exception as e:
                results.append(f"✗ FTP: {str(e)}")
        else:
            results.append("- FTP: Not configured")

        # Test LaunchAPPL if configured or explicitly requested
        if 'launchappl' in machine or args.get("test_launchappl"):
            import socket
            try:
                # Get LaunchAPPL config or fall back to FTP host
                la_config = machine.get('launchappl', {})
                host = la_config.get('host') or machine.get('ftp', {}).get('host')
                port = la_config.get('port', 1984)

                if not host:
                    results.append("✗ LaunchAPPL: No host configured")
                else:
                    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    sock.settimeout(2)
                    result = sock.connect_ex((host, port))
                    sock.close()
                    if result == 0:
                        results.append(f"✓ LaunchAPPL: Port {port} open")
                    else:
                        results.append(f"✗ LaunchAPPL: Port {port} not responding")
            except Exception as e:
                results.append(f"✗ LaunchAPPL: {str(e)}")

        return [TextContent(
            type="text",
            text=f"Connection test: {machine['name']}\n\n" + "\n".join(results)
        )]

    def _tool_list_directory(self, args: dict) -> list[TextContent]:
        """List directory contents."""
        machine_id = args["machine"]
        path = self._normalize_path(args.get("path", "/"))

        def operation(ftp):
            if path:
                ftp.cwd(path)
            items = []
            ftp.retrlines('LIST', items.append)
            return items

        items = self._ftp_operation(machine_id, operation)
        machine = self.machines[machine_id]
        display_path = path if path else "/"

        return [TextContent(
            type="text",
            text=f"Directory listing: {machine['name']}:{display_path}\n\n" +
                 ("\n".join(items) if items else "(empty)")
        )]

    def _tool_create_directory(self, args: dict) -> list[TextContent]:
        """Create directory with parent directories."""
        machine_id = args["machine"]
        path = self._normalize_path(args["path"])

        if not path:
            return [TextContent(type="text", text="❌ Cannot create root directory")]

        def operation(ftp):
            # Create each directory in the path
            parts = path.split(":")
            created = []
            current = ""

            for part in parts:
                current = f"{current}:{part}" if current else part
                try:
                    ftp.mkd(current)
                    created.append(current)
                except Exception as e:
                    # Directory might already exist, that's OK
                    if "exists" not in str(e).lower() and "550" not in str(e):
                        pass  # Continue anyway

            return created

        created = self._ftp_operation(machine_id, operation)
        machine = self.machines[machine_id]

        if created:
            return [TextContent(type="text", text=f"✅ Created on {machine['name']}:\n" +
                               "\n".join(f"  - {d}" for d in created))]
        else:
            return [TextContent(type="text", text=f"✅ Directory exists: {path}")]

    def _tool_delete_files(self, args: dict) -> list[TextContent]:
        """Delete file or directory."""
        machine_id = args["machine"]
        path = self._normalize_path(args["path"])
        recursive = args.get("recursive", False)

        if not path:
            return [TextContent(type="text", text="❌ Cannot delete root")]

        deleted = []

        def delete_recursive(ftp, target):
            try:
                # Try as file first
                ftp.delete(target)
                deleted.append(target)
            except:
                # Try as directory
                try:
                    original = ftp.pwd()
                    ftp.cwd(target)

                    if recursive:
                        items = []
                        ftp.retrlines('LIST', items.append)
                        for item in items:
                            parts = item.split(None, 8)
                            if len(parts) >= 9:
                                name = parts[8]
                                if name not in ['.', '..']:
                                    delete_recursive(ftp, name)

                    ftp.cwd(original)
                    ftp.rmd(target)
                    deleted.append(f"{target}/")
                except Exception as e:
                    raise ValueError(f"Cannot delete {target}: {e}")

        def operation(ftp):
            delete_recursive(ftp, path)
            return True

        self._ftp_operation(machine_id, operation)
        machine = self.machines[machine_id]

        return [TextContent(
            type="text",
            text=f"✅ Deleted from {machine['name']}:\n" +
                 "\n".join(f"  - {d}" for d in deleted)
        )]

    def _has_ftp(self, machine_id: str) -> bool:
        """Check if machine has FTP configured."""
        self._validate_machine_id(machine_id)
        return 'ftp' in self.machines[machine_id]

    def _has_launchappl(self, machine_id: str) -> bool:
        """Check if machine has LaunchAPPL configured."""
        self._validate_machine_id(machine_id)
        return 'launchappl' in self.machines[machine_id]

    def _tool_upload_file(self, args: dict) -> list[TextContent]:
        """Upload file to Classic Mac."""
        machine_id = args["machine"]
        local_path = args["local_path"]
        remote_path = args["remote_path"]

        self._validate_machine_id(machine_id)
        machine = self.machines[machine_id]

        # Check if FTP is configured
        if not self._has_ftp(machine_id):
            if self._has_launchappl(machine_id):
                return [TextContent(
                    type="text",
                    text=f"❌ Error: FTP not configured for {machine['name']}. This machine uses LaunchAPPL only.\n\n"
                         f"Use execute_binary instead to transfer and run in one step:\n"
                         f"  mcp__classic-mac-hardware__execute_binary(machine=\"{machine_id}\", platform=\"mactcp\", binary_path=\"{local_path}\")"
                )]
            return [TextContent(type="text", text=f"❌ Error: No FTP or LaunchAPPL configured for {machine['name']}.")]

        # Verify local file exists
        if not Path(local_path).exists():
            return [TextContent(type="text", text=f"❌ Local file not found: {local_path}")]

        directory, filename = self._split_path(remote_path)
        file_size = Path(local_path).stat().st_size

        def operation(ftp):
            # Navigate to directory if specified
            if directory:
                try:
                    ftp.cwd(directory)
                except:
                    # Try to create directory
                    parts = directory.split(":")
                    current = ""
                    for part in parts:
                        current = f"{current}:{part}" if current else part
                        try:
                            ftp.mkd(current)
                        except:
                            pass
                    ftp.cwd(directory)

            # Upload
            with open(local_path, 'rb') as f:
                ftp.storbinary(f'STOR {filename}', f)

            return True

        self._ftp_operation(machine_id, operation)
        machine = self.machines[machine_id]

        return [TextContent(
            type="text",
            text=f"✅ Uploaded to {machine['name']}:\n\n"
                 f"  Local:  {local_path}\n"
                 f"  Remote: {remote_path}\n"
                 f"  Size:   {file_size:,} bytes"
        )]

    def _tool_download_file(self, args: dict) -> list[TextContent]:
        """Download file from Classic Mac."""
        machine_id = args["machine"]
        remote_path = args["remote_path"]
        local_path = args.get("local_path")

        directory, filename = self._split_path(remote_path)

        # Determine local destination
        if not local_path:
            download_dir = Path(f"downloads/{machine_id}")
            download_dir.mkdir(parents=True, exist_ok=True)
            local_path = str(download_dir / filename)

        def operation(ftp):
            if directory:
                ftp.cwd(directory)

            with open(local_path, 'wb') as f:
                ftp.retrbinary(f'RETR {filename}', f.write)

            return True

        self._ftp_operation(machine_id, operation)
        machine = self.machines[machine_id]
        file_size = Path(local_path).stat().st_size

        return [TextContent(
            type="text",
            text=f"✅ Downloaded from {machine['name']}:\n\n"
                 f"  Remote: {remote_path}\n"
                 f"  Local:  {local_path}\n"
                 f"  Size:   {file_size:,} bytes"
        )]

    async def _tool_fetch_logs(self, args: dict) -> list[TextContent]:
        """Fetch PT_Log from Classic Mac."""
        machine_id = args["machine"]

        content = self._fetch_log_content(machine_id, "latest")
        machine = self.machines[machine_id]

        # Save to downloads
        download_dir = Path(f"downloads/{machine_id}")
        download_dir.mkdir(parents=True, exist_ok=True)
        local_path = download_dir / "PT_Log"
        local_path.write_text(content)

        return [TextContent(
            type="text",
            text=f"✅ Downloaded from {machine['name']}:\n\n"
                 f"Remote: PT_Log\n"
                 f"Local:  {local_path}\n"
                 f"Size:   {len(content):,} bytes\n\n"
                 f"--- Content ---\n{content}"
        )]

    def _tool_execute_binary(self, args: dict) -> list[TextContent]:
        """Execute binary via LaunchAPPL."""
        import subprocess

        machine_id = args["machine"]
        binary_path = args["binary_path"]

        self._validate_machine_id(machine_id)
        machine = self.machines[machine_id]

        launchappl = "/opt/Retro68-build/toolchain/bin/LaunchAPPL"
        if not os.path.exists(launchappl):
            return [TextContent(type="text", text=f"❌ LaunchAPPL not found at {launchappl}")]

        if not binary_path or not Path(binary_path).exists():
            return [TextContent(type="text", text=f"❌ Binary not found: {binary_path}")]

        # Get host from launchappl config first, fall back to ftp host
        la_config = machine.get('launchappl', {})
        machine_ip = la_config.get('host') or machine.get('ftp', {}).get('host')

        if not machine_ip:
            return [TextContent(type="text", text=f"❌ No host configured for {machine['name']}. Add 'launchappl.host' or 'ftp.host' to machines.json")]

        binary_path = str(Path(binary_path).resolve())

        try:
            cmd = [launchappl, "-e", "tcp", "--tcp-address", machine_ip, binary_path]
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)

            if result.returncode == 0:
                return [TextContent(type="text", text=f"✅ Executed on {machine['name']}:\n\n{result.stdout}")]
            else:
                return [TextContent(
                    type="text",
                    text=f"⚠️ Execution failed:\n\n{result.stderr}\n\n"
                         f"Ensure LaunchAPPLServer is running on {machine['name']}"
                )]
        except subprocess.TimeoutExpired:
            return [TextContent(
                type="text",
                text=f"⏱️ Timed out after 60s. Binary may still be running.\nUse fetch_logs to check."
            )]
        except Exception as e:
            return [TextContent(type="text", text=f"❌ Error: {e}")]

    def _tool_cleanup_machine(self, args: dict) -> list[TextContent]:
        """Clean up old files."""
        machine_id = args["machine"]
        scope = args.get("scope", "old_files")

        removed = []

        def operation(ftp):
            items = []
            ftp.retrlines('LIST', items.append)

            for item in items:
                parts = item.split(None, 8)
                if len(parts) < 9:
                    continue
                filename = parts[8]

                if filename in ['.', '..']:
                    continue

                should_delete = False
                if scope == "old_files":
                    should_delete = filename.endswith(('.old', '.bak', '.tmp'))
                elif scope == "all":
                    should_delete = True

                if should_delete and not item.startswith('d'):
                    try:
                        ftp.delete(filename)
                        removed.append(filename)
                    except:
                        pass

            return True

        self._ftp_operation(machine_id, operation)
        machine = self.machines[machine_id]

        if removed:
            return [TextContent(
                type="text",
                text=f"✅ Cleaned {machine['name']}:\n\n" +
                     "\n".join(f"  - {f}" for f in removed)
            )]
        else:
            return [TextContent(type="text", text=f"✅ Nothing to clean on {machine['name']}")]

    # =========================================================================
    # Prompts
    # =========================================================================

    async def list_prompts(self) -> list:
        """List available prompt templates."""
        return [
            {
                "name": "deploy-and-test",
                "description": "Deploy binary, run on Mac, fetch logs",
                "arguments": [
                    {"name": "machine", "required": True},
                    {"name": "platform", "required": True},
                    {"name": "binary_path", "required": True}
                ]
            }
        ]

    async def get_prompt(self, name: str, arguments: dict) -> dict:
        """Get prompt template."""
        if name == "deploy-and-test":
            machine = arguments.get("machine", "performa6200")
            platform = arguments.get("platform", "mactcp")
            binary = arguments.get("binary_path", "build/mac/test_mactcp.bin")

            return {
                "messages": [{
                    "role": "user",
                    "content": {
                        "type": "text",
                        "text": f"""Deploy and test on {machine}:

1. Upload: {binary}
2. Run on Mac
3. Fetch PT_Log
4. Report results
"""
                    }
                }]
            }

        raise ValueError(f"Unknown prompt: {name}")


async def main():
    """Run MCP server."""
    config_path = os.environ.get(
        'MACHINES_CONFIG',
        '.claude/mcp-servers/classic-mac-hardware/machines.json'
    )

    server_instance = ClassicMacHardwareServer(config_path)

    async with mcp.server.stdio.stdio_server() as (read_stream, write_stream):
        await server_instance.server.run(
            read_stream,
            write_stream,
            server_instance.server.create_initialization_options()
        )


if __name__ == "__main__":
    asyncio.run(main())
