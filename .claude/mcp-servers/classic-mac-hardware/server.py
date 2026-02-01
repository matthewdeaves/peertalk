#!/usr/bin/env python3
"""
Classic Mac Hardware MCP Server

Provides FTP-based access to Classic Macintosh test machines.
Designed for code execution pattern (98%+ token savings).

Compatible with RumpusFTP server on Classic Macs:
- Plain FTP (not SFTP)
- Passive mode (PASV) for NAT traversal
- Mac-specific path handling
"""

import asyncio
import json
import os
import sys
from ftplib import FTP
from pathlib import Path
from typing import Any, Optional
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


class ClassicMacHardwareServer:
    """MCP Server for Classic Mac hardware access via FTP."""

    def __init__(self, config_path: str):
        """Initialize with machines configuration."""
        self.config_path = config_path
        self.machines = {}
        self._config_mtime = 0
        self._first_load = True
        self._reload_if_changed()  # Initial load
        self._first_load = False
        self.server = Server("classic-mac-hardware")

        # Register handlers
        self.server.list_resources()(self.list_resources)
        self.server.read_resource()(self.read_resource)
        self.server.list_tools()(self.list_tools)
        self.server.call_tool()(self.call_tool)
        self.server.list_prompts()(self.list_prompts)
        self.server.get_prompt()(self.get_prompt)

    def _reload_if_changed(self) -> bool:
        """
        Hot-reload configuration if machines.json has changed.
        Returns True if config was reloaded.
        """
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
                print(f"ℹ No machines configured yet. Run /setup-machine to add Classic Mac hardware.", file=sys.stderr)
                print(f"  Expected config at: {self.config_path}", file=sys.stderr)
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
            available = ', '.join(self.machines.keys()) if self.machines else '(none configured)'
            raise ValueError(
                f"Unknown machine: '{machine_id}'\n\n"
                f"Configured machines: {available}\n\n"
                f"To add a new machine, run: /setup-machine"
            )

    def _connect_ftp(self, machine_id: str) -> FTP:
        """
        Create FTP connection to Classic Mac.

        Uses passive mode (PASV) for RumpusFTP compatibility.
        Plain FTP, not SFTP (Classic Macs don't support SFTP).
        """
        self._validate_machine_id(machine_id)

        machine = self.machines[machine_id]
        ftp_config = machine['ftp']

        ftp = FTP()
        ftp.set_pasv(True)  # Passive mode for RumpusFTP
        ftp.connect(ftp_config['host'], ftp_config.get('port', 21))
        ftp.login(ftp_config['username'], ftp_config['password'])

        return ftp

    async def list_resources(self) -> list[Resource]:
        """
        List available resources (read-only data).

        Resources follow URI scheme: mac://{machine}/{type}/{identifier}
        """
        self._reload_if_changed()  # Hot-reload config
        resources = []

        for machine_id, machine in self.machines.items():
            # Logs resource - latest session
            resources.append(Resource(
                uri=f"mac://{machine_id}/logs/latest",
                name=f"Latest logs from {machine['name']}",
                description=f"Most recent PT_Log output from {machine['name']} ({machine['system']})",
                mimeType="text/plain"
            ))

            # Binary info resource
            resources.append(Resource(
                uri=f"mac://{machine_id}/binary/{machine['platform']}",
                name=f"Binary info for {machine['name']}",
                description=f"Deployed binary version and timestamp for {machine['platform']}",
                mimeType="application/json"
            ))

            # Files resource - list directory
            resources.append(Resource(
                uri=f"mac://{machine_id}/files/",
                name=f"Files on {machine['name']}",
                description=f"Browse files on {machine['name']}",
                mimeType="application/json"
            ))

        return resources

    async def read_resource(self, uri: str) -> str:
        """
        Read resource content via FTP.

        Returns data without side effects (read-only).
        """
        self._reload_if_changed()  # Hot-reload config

        # Parse URI: mac://{machine}/{type}/{identifier}
        if not uri.startswith("mac://"):
            raise ValueError(f"Invalid URI scheme: {uri}")

        parts = uri[6:].split('/', 2)
        if len(parts) < 2:
            raise ValueError(f"Invalid URI format: {uri}")

        machine_id = parts[0]
        resource_type = parts[1]
        identifier = parts[2] if len(parts) > 2 else ''

        ftp = self._connect_ftp(machine_id)
        machine = self.machines[machine_id]

        try:
            if resource_type == "logs":
                # Get log file
                log_path = machine['ftp']['paths']['logs']
                if identifier == "latest":
                    # Find most recent log file
                    files = []
                    ftp.cwd(log_path)
                    ftp.retrlines('LIST', files.append)
                    # Parse and find latest .log file
                    log_files = [f for f in files if f.endswith('.log')]
                    if not log_files:
                        return "No logs found"
                    # Get the newest one (simple approach: last in list)
                    latest = log_files[-1].split()[-1]
                    identifier = latest

                # Download log file
                lines = []
                ftp.cwd(log_path)
                ftp.retrlines(f'RETR {identifier}', lines.append)
                return '\n'.join(lines)

            elif resource_type == "binary":
                # Get binary metadata
                binary_path = machine['ftp']['paths']['binaries']
                ftp.cwd(binary_path)

                # Check for .version file
                version_file = f"PeerTalk-{machine['platform']}.version"
                try:
                    lines = []
                    ftp.retrlines(f'RETR {version_file}', lines.append)
                    return '\n'.join(lines)
                except:
                    return json.dumps({
                        "error": "No binary deployed",
                        "platform": machine['platform']
                    })

            elif resource_type == "files":
                # List files in directory
                path = machine['ftp']['paths'].get(identifier, '/')
                files = []
                ftp.cwd(path)
                ftp.retrlines('LIST', files.append)
                return json.dumps({
                    "path": path,
                    "files": files
                })

            else:
                raise ValueError(f"Unknown resource type: {resource_type}")

        finally:
            ftp.quit()

    async def list_tools(self) -> list[Tool]:
        """
        List available tools (actions with side effects).

        Designed for code execution pattern - minimal, focused tools.

        Note: Machine enums are not included in schemas to allow hot-reloading.
        Validation happens at execution time instead.
        """
        # Get current machine IDs for descriptions (informational only)
        machine_ids = ', '.join(self.machines.keys()) if self.machines else '(none configured yet)'

        return [
            Tool(
                name="list_machines",
                description="List all configured Classic Mac machines",
                inputSchema={
                    "type": "object",
                    "properties": {}
                }
            ),
            Tool(
                name="test_connection",
                description="Test FTP and LaunchAPPL connectivity to Classic Mac",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {
                            "type": "string",
                            "description": f"Machine ID (configured machines: {machine_ids})"
                        },
                        "test_launchappl": {
                            "type": "boolean",
                            "description": "Test LaunchAPPL TCP connection (port 1984)",
                            "default": True
                        }
                    },
                    "required": ["machine"]
                }
            ),
            Tool(
                name="list_directory",
                description="List files and directories on Classic Mac via FTP",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {
                            "type": "string",
                            "description": f"Machine ID (configured machines: {machine_ids})"
                        },
                        "path": {
                            "type": "string",
                            "description": "Directory path (e.g., /Applications/ or / for root)",
                            "default": "/"
                        }
                    },
                    "required": ["machine"]
                }
            ),
            Tool(
                name="create_directory",
                description="Create directory on Classic Mac via FTP",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {
                            "type": "string",
                            "description": f"Machine ID (configured machines: {machine_ids})"
                        },
                        "path": {
                            "type": "string",
                            "description": "Directory path to create (e.g., /Applications/PeerTalk/)"
                        }
                    },
                    "required": ["machine", "path"]
                }
            ),
            Tool(
                name="delete_files",
                description="Delete files or directories on Classic Mac via FTP (requires user consent)",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {
                            "type": "string",
                            "description": f"Machine ID (configured machines: {machine_ids})"
                        },
                        "path": {
                            "type": "string",
                            "description": "Path to delete (file or directory)"
                        },
                        "recursive": {
                            "type": "boolean",
                            "description": "Recursively delete directory contents",
                            "default": False
                        }
                    },
                    "required": ["machine", "path"]
                }
            ),
            Tool(
                name="deploy_binary",
                description="Deploy compiled binary to Classic Mac via FTP (requires user consent)",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {
                            "type": "string",
                            "description": f"Machine ID (configured machines: {machine_ids})"
                        },
                        "platform": {
                            "type": "string",
                            "description": "Platform: mactcp, opentransport, appletalk",
                            "enum": ["mactcp", "opentransport", "appletalk"]
                        },
                        "binary_path": {
                            "type": "string",
                            "description": "Local path to compiled binary (e.g., build/mactcp/PeerTalk)"
                        }
                    },
                    "required": ["machine", "platform", "binary_path"]
                }
            ),
            Tool(
                name="fetch_logs",
                description="Download PT_Log output from Classic Mac via FTP",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {
                            "type": "string",
                            "description": f"Machine ID (configured machines: {machine_ids})"
                        },
                        "session_id": {
                            "type": "string",
                            "description": "Session ID (optional, defaults to latest)",
                            "default": "latest"
                        },
                        "destination": {
                            "type": "string",
                            "description": "Local destination path (optional, defaults to logs/{machine}/)",
                            "default": None
                        }
                    },
                    "required": ["machine"]
                }
            ),
            Tool(
                name="execute_binary",
                description="Run deployed binary on Classic Mac via LaunchAPPL (requires user consent)",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {
                            "type": "string",
                            "description": f"Machine ID (configured machines: {machine_ids})"
                        },
                        "platform": {
                            "type": "string",
                            "description": "Platform: mactcp, opentransport, appletalk",
                            "enum": ["mactcp", "opentransport", "appletalk"]
                        },
                        "binary_path": {
                            "type": "string",
                            "description": "Local path to binary to execute (e.g., build/mactcp/PeerTalk.bin)",
                            "default": ""
                        },
                        "args": {
                            "type": "array",
                            "items": {"type": "string"},
                            "description": "Command-line arguments (optional)",
                            "default": []
                        }
                    },
                    "required": ["machine", "platform"]
                }
            ),
            Tool(
                name="cleanup_machine",
                description="Clean files and directories on Classic Mac (requires user consent)",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {
                            "type": "string",
                            "description": f"Machine ID (configured machines: {machine_ids})"
                        },
                        "scope": {
                            "type": "string",
                            "description": "What to clean",
                            "enum": ["old_files", "binaries", "logs", "all", "specific_path"],
                            "default": "old_files"
                        },
                        "specific_path": {
                            "type": "string",
                            "description": "Path to clean if scope=specific_path (e.g., /Applications/)",
                            "default": "/"
                        },
                        "keep_latest": {
                            "type": "boolean",
                            "description": "Keep most recent files when cleaning (only for old_files scope)",
                            "default": True
                        }
                    },
                    "required": ["machine"]
                }
            ),
            Tool(
                name="upload_file",
                description="Upload any file to Classic Mac via FTP (requires user consent for non-test files)",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {
                            "type": "string",
                            "description": f"Machine ID (configured machines: {machine_ids})"
                        },
                        "local_path": {
                            "type": "string",
                            "description": "Local file path to upload"
                        },
                        "remote_path": {
                            "type": "string",
                            "description": "Remote destination path on Mac (Mac-style path with colons, e.g., 'Documents:TestData:file.txt')"
                        }
                    },
                    "required": ["machine", "local_path", "remote_path"]
                }
            ),
            Tool(
                name="download_file",
                description="Download any file from Classic Mac via FTP",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {
                            "type": "string",
                            "description": f"Machine ID (configured machines: {machine_ids})"
                        },
                        "remote_path": {
                            "type": "string",
                            "description": "Remote file path on Mac to download (Mac-style path with colons)"
                        },
                        "local_path": {
                            "type": "string",
                            "description": "Local destination path (optional, defaults to downloads/{machine}/{filename})",
                            "default": None
                        }
                    },
                    "required": ["machine", "remote_path"]
                }
            ),
            Tool(
                name="reload_config",
                description="Reload machines.json configuration without restarting server",
                inputSchema={
                    "type": "object",
                    "properties": {}
                }
            )
        ]

    async def call_tool(self, name: str, arguments: dict) -> list[TextContent]:
        """Execute tool with side effects."""
        self._reload_if_changed()  # Hot-reload config

        if name == "list_machines":
            machines_info = []
            for machine_id, machine in self.machines.items():
                machines_info.append({
                    "id": machine_id,
                    "name": machine['name'],
                    "platform": machine['platform'],
                    "system": machine['system'],
                    "cpu": machine['cpu'],
                    "host": machine['ftp']['host']
                })
            return [TextContent(
                type="text",
                text=json.dumps(machines_info, indent=2)
            )]

        elif name == "reload_config":
            old_count = len(self.machines)
            self.machines = self._load_config()
            self._config_mtime = os.path.getmtime(self.config_path)
            new_count = len(self.machines)

            return [TextContent(
                type="text",
                text=f"✅ Configuration reloaded (forced)\n\n" +
                     f"Machines: {old_count} → {new_count}\n\n" +
                     json.dumps(list(self.machines.keys()), indent=2) +
                     f"\n\nNote: Config is automatically hot-reloaded when machines.json changes."
            )]

        elif name == "test_connection":
            machine_id = arguments["machine"]
            test_launchappl = arguments.get("test_launchappl", True)
            machine = self.machines[machine_id]

            results = []

            # Test FTP connection
            try:
                ftp = self._connect_ftp(machine_id)
                ftp.quit()
                results.append(f"✓ FTP: Connected to {machine['ftp']['host']}:{machine['ftp'].get('port', 21)}")
            except Exception as e:
                results.append(f"✗ FTP: Failed - {str(e)}")
                return [TextContent(type="text", text="\n".join(results))]

            # Test LaunchAPPL connection if requested
            if test_launchappl and machine.get('launchappl', {}).get('enabled'):
                import socket
                try:
                    port = machine['launchappl'].get('port', 1984)
                    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    sock.settimeout(2)
                    result = sock.connect_ex((machine['ftp']['host'], port))
                    sock.close()

                    if result == 0:
                        results.append(f"✓ LaunchAPPL: Listening on port {port}")
                    else:
                        results.append(f"✗ LaunchAPPL: Not listening on port {port}")
                except Exception as e:
                    results.append(f"✗ LaunchAPPL: Test failed - {str(e)}")

            return [TextContent(
                type="text",
                text=f"Connection test for {machine['name']}:\n\n" + "\n".join(results)
            )]

        elif name == "list_directory":
            machine_id = arguments["machine"]
            path = arguments.get("path", "/")

            ftp = self._connect_ftp(machine_id)
            machine = self.machines[machine_id]

            try:
                ftp.cwd(path)
                items = []
                ftp.retrlines('LIST', items.append)

                return [TextContent(
                    type="text",
                    text=f"Directory listing: {machine['name']}:{path}\n\n" + "\n".join(items)
                )]
            finally:
                ftp.quit()

        elif name == "create_directory":
            machine_id = arguments["machine"]
            path = arguments["path"]

            ftp = self._connect_ftp(machine_id)
            machine = self.machines[machine_id]

            try:
                ftp.mkd(path)
                return [TextContent(
                    type="text",
                    text=f"✅ Created directory: {machine['name']}:{path}"
                )]
            except Exception as e:
                return [TextContent(
                    type="text",
                    text=f"✗ Failed to create directory: {str(e)}"
                )]
            finally:
                ftp.quit()

        elif name == "delete_files":
            machine_id = arguments["machine"]
            path = arguments["path"]
            recursive = arguments.get("recursive", False)

            ftp = self._connect_ftp(machine_id)
            machine = self.machines[machine_id]

            try:
                deleted = []

                if recursive:
                    # Recursively delete directory and contents
                    def delete_recursive(ftp, dir_path):
                        try:
                            # Try to CWD - if it works, it's a directory
                            original_dir = ftp.pwd()
                            ftp.cwd(dir_path)

                            # List contents
                            items = []
                            ftp.retrlines('LIST', items.append)

                            for item in items:
                                parts = item.split(None, 8)
                                if len(parts) < 9:
                                    continue
                                name = parts[8]
                                if name in ['.', '..']:
                                    continue

                                item_path = name  # Relative to current dir

                                # Check if directory (first char is 'd')
                                if item.startswith('d'):
                                    delete_recursive(ftp, item_path)
                                else:
                                    try:
                                        ftp.delete(item_path)
                                        deleted.append(f"{dir_path}/{name}")
                                    except:
                                        pass

                            # Go back and remove the directory
                            ftp.cwd(original_dir)
                            try:
                                ftp.rmd(dir_path)
                                deleted.append(f"{dir_path}/ (directory)")
                            except:
                                pass
                        except:
                            # Not a directory, try to delete as file
                            try:
                                ftp.delete(dir_path)
                                deleted.append(dir_path)
                            except:
                                pass

                    delete_recursive(ftp, path)
                else:
                    # Delete single file/empty directory
                    try:
                        ftp.delete(path)
                        deleted.append(path)
                    except:
                        try:
                            ftp.rmd(path)
                            deleted.append(f"{path}/ (directory)")
                        except Exception as e:
                            return [TextContent(
                                type="text",
                                text=f"✗ Failed to delete {path}: {str(e)}"
                            )]

                return [TextContent(
                    type="text",
                    text=f"✅ Deleted from {machine['name']}:\n\n" + "\n".join(deleted) +
                         f"\n\nTotal: {len(deleted)} items"
                )]
            finally:
                ftp.quit()

        elif name == "deploy_binary":
            machine_id = arguments["machine"]
            platform = arguments["platform"]
            binary_path = arguments["binary_path"]

            # Verify binary exists locally
            if not Path(binary_path).exists():
                raise FileNotFoundError(f"Binary not found: {binary_path}")

            ftp = self._connect_ftp(machine_id)
            machine = self.machines[machine_id]

            try:
                # Upload to binaries directory
                remote_path = machine['ftp']['paths']['binaries']
                ftp.cwd(remote_path)

                # Determine base path (remove .bin extension if present)
                base_path = str(binary_path).replace('.bin', '')
                binary_name = f"PeerTalk-{platform}"

                # Upload both .dsk and .bin files
                files_uploaded = []

                # Try to upload .dsk file (disk image with resource fork)
                dsk_path = f"{base_path}.dsk"
                if Path(dsk_path).exists():
                    with open(dsk_path, 'rb') as f:
                        ftp.storbinary(f'STOR {binary_name}.dsk', f)
                    files_uploaded.append(f"{binary_name}.dsk ({Path(dsk_path).stat().st_size} bytes)")

                # Upload .bin file (for BinUnpk or LaunchAPPL)
                bin_path = f"{base_path}.bin" if not binary_path.endswith('.bin') else binary_path
                if Path(bin_path).exists():
                    with open(bin_path, 'rb') as f:
                        ftp.storbinary(f'STOR {binary_name}.bin', f)
                    files_uploaded.append(f"{binary_name}.bin ({Path(bin_path).stat().st_size} bytes)")
                elif Path(binary_path).exists():
                    # Fallback: upload whatever binary_path points to
                    with open(binary_path, 'rb') as f:
                        ftp.storbinary(f'STOR {binary_name}', f)
                    files_uploaded.append(f"{binary_name} ({Path(binary_path).stat().st_size} bytes)")

                # Create version file
                version_info = {
                    "platform": platform,
                    "uploaded": datetime.now().isoformat(),
                    "source": binary_path,
                    "files": files_uploaded
                }
                version_data = json.dumps(version_info, indent=2)
                ftp.storlines(f'STOR {binary_name}.version',
                             iter(version_data.split('\n')))

                return [TextContent(
                    type="text",
                    text=f"✅ Deployed to {machine['name']} ({machine_id})\n\nFiles:\n" + "\n".join(f"  - {f}" for f in files_uploaded) + f"\n\n{version_data}"
                )]

            finally:
                ftp.quit()

        elif name == "fetch_logs":
            machine_id = arguments["machine"]
            session_id = arguments.get("session_id", "latest")
            destination = arguments.get("destination")

            # Use read_resource to get logs
            uri = f"mac://{machine_id}/logs/{session_id}"
            log_content = await self.read_resource(uri)

            # Save to destination if specified
            if destination:
                Path(destination).parent.mkdir(parents=True, exist_ok=True)
                Path(destination).write_text(log_content)
                return [TextContent(
                    type="text",
                    text=f"Saved logs to {destination}\n\n{log_content[:500]}..."
                )]
            else:
                return [TextContent(
                    type="text",
                    text=log_content
                )]

        elif name == "execute_binary":
            # Execute binary on Classic Mac using LaunchAPPL over TCP
            import subprocess

            machine_id = arguments["machine"]
            platform = arguments["platform"]
            binary_path = arguments.get("binary_path", "")
            args = arguments.get("args", [])

            self._validate_machine_id(machine_id)
            machine = self.machines[machine_id]
            machine_ip = machine['ftp']['host']

            # Path to LaunchAPPL client
            launchappl = "/opt/Retro68-build/toolchain/bin/LaunchAPPL"

            if not os.path.exists(launchappl):
                return [TextContent(
                    type="text",
                    text=f"❌ LaunchAPPL client not found at {launchappl}"
                )]

            # Verify binary exists (supports both absolute and relative paths)
            if not binary_path or not Path(binary_path).exists():
                return [TextContent(
                    type="text",
                    text=f"❌ Binary not found: {binary_path}\n\n"
                         f"Provide binary_path argument with:\n"
                         f"  - Absolute path: /workspace/build/mactcp/PeerTalk.bin\n"
                         f"  - Relative path: LaunchAPPL-build/Dialog.bin\n"
                         f"  - Relative path: build/ppc/PeerTalk.bin"
                )]

            # Resolve to absolute path for subprocess
            binary_path = str(Path(binary_path).resolve())

            # Run LaunchAPPL with TCP backend
            try:
                cmd = [launchappl, "-e", "tcp", "--tcp-address", machine_ip, binary_path]
                if args:
                    cmd.extend(args)

                result = subprocess.run(
                    cmd,
                    capture_output=True,
                    text=True,
                    timeout=60
                )

                if result.returncode == 0:
                    return [TextContent(
                        type="text",
                        text=f"✅ Executed on {machine['name']}:\n\n{result.stdout}"
                    )]
                else:
                    return [TextContent(
                        type="text",
                        text=f"⚠️ Execution failed on {machine['name']}:\n\n"
                             f"Error: {result.stderr}\n\n"
                             f"Make sure LaunchAPPLServer is running on {machine['name']} with TCP enabled."
                    )]
            except subprocess.TimeoutExpired:
                return [TextContent(
                    type="text",
                    text=f"⏱️ Execution timed out after 60 seconds.\n\n"
                         f"The binary may still be running on {machine['name']}.\n"
                         f"Use /fetch-logs {machine_id} to check results."
                )]
            except Exception as e:
                return [TextContent(
                    type="text",
                    text=f"❌ Error executing binary: {str(e)}"
                )]

        elif name == "cleanup_machine":
            machine_id = arguments["machine"]
            scope = arguments.get("scope", "old_files")
            specific_path = arguments.get("specific_path", "/")
            keep_latest = arguments.get("keep_latest", True)

            ftp = self._connect_ftp(machine_id)
            machine = self.machines[machine_id]

            try:
                removed = []

                if scope == "old_files":
                    # Clean .old files from binaries and logs directories
                    for path_key in ['binaries', 'logs']:
                        if path_key in machine['ftp']['paths']:
                            try:
                                path = machine['ftp']['paths'][path_key]
                                ftp.cwd(path)
                                files = []
                                ftp.retrlines('LIST', files.append)

                                for file_line in files:
                                    filename = file_line.split()[-1]
                                    if filename.endswith('.old') or filename.endswith('.bak'):
                                        ftp.delete(filename)
                                        removed.append(f"{path}{filename}")
                            except:
                                pass

                elif scope == "binaries":
                    # Clean entire binaries directory
                    binary_path = machine['ftp']['paths'].get('binaries', '/Applications/PeerTalk/')
                    try:
                        ftp.cwd(binary_path)
                        files = []
                        ftp.retrlines('LIST', files.append)

                        for file_line in files:
                            filename = file_line.split()[-1]
                            if filename not in ['.', '..']:
                                try:
                                    ftp.delete(filename)
                                    removed.append(f"{binary_path}{filename}")
                                except:
                                    pass
                    except:
                        pass

                elif scope == "logs":
                    # Clean logs directory
                    logs_path = machine['ftp']['paths'].get('logs', '/Documents/PeerTalk-Logs/')
                    try:
                        ftp.cwd(logs_path)
                        files = []
                        ftp.retrlines('LIST', files.append)

                        for file_line in files:
                            filename = file_line.split()[-1]
                            if filename not in ['.', '..']:
                                try:
                                    ftp.delete(filename)
                                    removed.append(f"{logs_path}{filename}")
                                except:
                                    pass
                    except:
                        pass

                elif scope == "all":
                    # Clean everything (FTP root)
                    ftp.cwd('/')
                    items = []
                    ftp.retrlines('LIST', items.append)

                    def delete_recursive(ftp, dir_path, from_root=False):
                        try:
                            original_dir = ftp.pwd()
                            ftp.cwd(dir_path)

                            items = []
                            ftp.retrlines('LIST', items.append)

                            for item in items:
                                parts = item.split(None, 8)
                                if len(parts) < 9:
                                    continue
                                name = parts[8]
                                if name in ['.', '..']:
                                    continue

                                if item.startswith('d'):
                                    delete_recursive(ftp, name)
                                else:
                                    try:
                                        ftp.delete(name)
                                        removed.append(f"{dir_path}/{name}")
                                    except:
                                        pass

                            ftp.cwd(original_dir)
                            if not from_root:
                                try:
                                    ftp.rmd(dir_path)
                                    removed.append(f"{dir_path}/ (directory)")
                                except:
                                    pass
                        except:
                            pass

                    for item in items:
                        parts = item.split(None, 8)
                        if len(parts) < 9:
                            continue
                        name = parts[8]
                        if name in ['.', '..']:
                            continue

                        if item.startswith('d'):
                            delete_recursive(ftp, name, from_root=True)
                            try:
                                ftp.rmd(name)
                                removed.append(f"/{name}/ (directory)")
                            except:
                                pass
                        else:
                            try:
                                ftp.delete(name)
                                removed.append(f"/{name}")
                            except:
                                pass

                elif scope == "specific_path":
                    # Clean specific path
                    try:
                        ftp.cwd(specific_path)
                        files = []
                        ftp.retrlines('LIST', files.append)

                        for file_line in files:
                            parts = file_line.split(None, 8)
                            if len(parts) < 9:
                                continue
                            filename = parts[8]
                            if filename not in ['.', '..']:
                                if file_line.startswith('d'):
                                    # Directory - skip for now
                                    pass
                                else:
                                    try:
                                        ftp.delete(filename)
                                        removed.append(f"{specific_path}{filename}")
                                    except:
                                        pass
                    except Exception as e:
                        return [TextContent(
                            type="text",
                            text=f"✗ Failed to clean {specific_path}: {str(e)}"
                        )]

                return [TextContent(
                    type="text",
                    text=f"✅ Cleaned {machine['name']} (scope: {scope}):\n\n" +
                         ('\n'.join(removed) if removed else "No files to remove") +
                         f"\n\nTotal: {len(removed)} items"
                )]

            finally:
                ftp.quit()

        elif name == "upload_file":
            machine_id = arguments["machine"]
            local_path = arguments["local_path"]
            remote_path = arguments["remote_path"]

            # Verify local file exists
            if not Path(local_path).exists():
                raise FileNotFoundError(f"Local file not found: {local_path}")

            ftp = self._connect_ftp(machine_id)
            machine = self.machines[machine_id]

            try:
                # Parse remote path to get directory and filename
                # Mac path format: "Documents:TestData:file.txt"
                path_parts = remote_path.split(':')
                filename = path_parts[-1]
                directory = ':'.join(path_parts[:-1]) if len(path_parts) > 1 else ''

                # Navigate to directory if specified
                if directory:
                    ftp.cwd(directory)

                # Upload file
                file_size = Path(local_path).stat().st_size
                with open(local_path, 'rb') as f:
                    ftp.storbinary(f'STOR {filename}', f)

                return [TextContent(
                    type="text",
                    text=f"✅ Uploaded to {machine['name']}:\n\n"
                         f"Local:  {local_path}\n"
                         f"Remote: {remote_path}\n"
                         f"Size:   {file_size:,} bytes"
                )]
            finally:
                ftp.quit()

        elif name == "download_file":
            machine_id = arguments["machine"]
            remote_path = arguments["remote_path"]
            local_path = arguments.get("local_path")

            # Parse remote path to get filename
            path_parts = remote_path.split(':')
            filename = path_parts[-1]
            directory = ':'.join(path_parts[:-1]) if len(path_parts) > 1 else ''

            # Determine local destination
            if not local_path:
                download_dir = Path(f"downloads/{machine_id}")
                download_dir.mkdir(parents=True, exist_ok=True)
                local_path = download_dir / filename

            ftp = self._connect_ftp(machine_id)
            machine = self.machines[machine_id]

            try:
                # Navigate to directory if specified
                if directory:
                    ftp.cwd(directory)

                # Download file
                with open(local_path, 'wb') as f:
                    ftp.retrbinary(f'RETR {filename}', f.write)

                file_size = Path(local_path).stat().st_size

                return [TextContent(
                    type="text",
                    text=f"✅ Downloaded from {machine['name']}:\n\n"
                         f"Remote: {remote_path}\n"
                         f"Local:  {local_path}\n"
                         f"Size:   {file_size:,} bytes"
                )]
            finally:
                ftp.quit()

        else:
            raise ValueError(f"Unknown tool: {name}")

    async def list_prompts(self) -> list:
        """List available prompt templates."""
        return [
            {
                "name": "deploy-and-test",
                "description": "Deploy binary to Classic Mac, run tests, fetch logs",
                "arguments": [
                    {
                        "name": "machine",
                        "description": "Machine to deploy to",
                        "required": True
                    },
                    {
                        "name": "platform",
                        "description": "Platform: mactcp, opentransport, appletalk",
                        "required": True
                    },
                    {
                        "name": "test_name",
                        "description": "Test to run (e.g., tcp-connect)",
                        "required": True
                    }
                ]
            },
            {
                "name": "compare-platforms",
                "description": "Deploy to all machines, run same test, compare logs",
                "arguments": [
                    {
                        "name": "test_name",
                        "description": "Test to run on all platforms",
                        "required": True
                    }
                ]
            },
            {
                "name": "debug-crash",
                "description": "Fetch crash logs and correlate with source code",
                "arguments": [
                    {
                        "name": "machine",
                        "description": "Machine that crashed",
                        "required": True
                    }
                ]
            }
        ]

    async def get_prompt(self, name: str, arguments: dict) -> dict:
        """Get prompt template with arguments filled in."""
        if name == "deploy-and-test":
            machine = arguments["machine"]
            platform = arguments["platform"]
            test_name = arguments["test_name"]

            return {
                "messages": [
                    {
                        "role": "user",
                        "content": {
                            "type": "text",
                            "text": f"""Deploy and test PeerTalk on {machine}:

1. Deploy binary:
   /deploy {machine} {platform}

2. Run test:
   Test: {test_name}

3. Fetch logs:
   /fetch-logs {machine}

4. Analyze results and report any errors
"""
                        }
                    }
                ]
            }

        elif name == "compare-platforms":
            test_name = arguments["test_name"]

            machines_list = '\n'.join([
                f"   - {mid}: {m['name']} ({m['platform']})"
                for mid, m in self.machines.items()
            ])

            return {
                "messages": [
                    {
                        "role": "user",
                        "content": {
                            "type": "text",
                            "text": f"""Compare test results across all platforms:

Test: {test_name}

Machines:
{machines_list}

For each machine:
1. Deploy appropriate binary
2. Run test: {test_name}
3. Fetch logs
4. Compare results

Report differences in behavior, errors, or performance.
"""
                        }
                    }
                ]
            }

        elif name == "debug-crash":
            machine = arguments["machine"]

            return {
                "messages": [
                    {
                        "role": "user",
                        "content": {
                            "type": "text",
                            "text": f"""Debug crash on {machine}:

1. Fetch latest logs from {machine}
2. Identify crash location (last successful operation)
3. Read corresponding source file
4. Check for common Classic Mac pitfalls:
   - ISR safety violations
   - Byte ordering issues
   - Alignment problems
   - Memory allocation in callbacks
5. Suggest fix with line numbers
"""
                        }
                    }
                ]
            }

        else:
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
