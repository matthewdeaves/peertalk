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
        self.machines = self._load_config()
        self.server = Server("classic-mac-hardware")

        # Register handlers
        self.server.list_resources()(self.list_resources)
        self.server.read_resource()(self.read_resource)
        self.server.list_tools()(self.list_tools)
        self.server.call_tool()(self.call_tool)
        self.server.list_prompts()(self.list_prompts)
        self.server.get_prompt()(self.get_prompt)

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

    def _connect_ftp(self, machine_id: str) -> FTP:
        """
        Create FTP connection to Classic Mac.

        Uses passive mode (PASV) for RumpusFTP compatibility.
        Plain FTP, not SFTP (Classic Macs don't support SFTP).
        """
        if machine_id not in self.machines:
            raise ValueError(f"Unknown machine: {machine_id}")

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
        """
        return [
            Tool(
                name="deploy_binary",
                description="Deploy compiled binary to Classic Mac via FTP (requires user consent)",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {
                            "type": "string",
                            "description": f"Machine ID: {', '.join(self.machines.keys())}",
                            "enum": list(self.machines.keys())
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
                            "description": f"Machine ID: {', '.join(self.machines.keys())}",
                            "enum": list(self.machines.keys())
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
                description="Run deployed binary on Classic Mac (requires user consent)",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {
                            "type": "string",
                            "description": f"Machine ID: {', '.join(self.machines.keys())}",
                            "enum": list(self.machines.keys())
                        },
                        "platform": {
                            "type": "string",
                            "description": "Platform: mactcp, opentransport, appletalk",
                            "enum": ["mactcp", "opentransport", "appletalk"]
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
                description="Remove old binaries and logs from Classic Mac (requires user consent)",
                inputSchema={
                    "type": "object",
                    "properties": {
                        "machine": {
                            "type": "string",
                            "description": f"Machine ID: {', '.join(self.machines.keys())}",
                            "enum": list(self.machines.keys())
                        },
                        "keep_latest": {
                            "type": "boolean",
                            "description": "Keep most recent binary and log",
                            "default": True
                        }
                    },
                    "required": ["machine"]
                }
            ),
            Tool(
                name="list_machines",
                description="List all configured Classic Mac machines",
                inputSchema={
                    "type": "object",
                    "properties": {}
                }
            )
        ]

    async def call_tool(self, name: str, arguments: dict) -> list[TextContent]:
        """Execute tool with side effects."""
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
            import os

            machine_id = arguments["machine"]
            platform = arguments["platform"]
            binary_path = arguments.get("binary_path", "")
            args = arguments.get("args", [])

            machine = self.machines[machine_id]
            machine_ip = machine['ftp']['host']

            # Path to LaunchAPPL client
            launchappl = "/opt/Retro68-build/toolchain/bin/LaunchAPPL"

            if not os.path.exists(launchappl):
                return [TextContent(
                    type="text",
                    text=f"❌ LaunchAPPL client not found at {launchappl}"
                )]

            if not binary_path or not os.path.exists(binary_path):
                return [TextContent(
                    type="text",
                    text=f"❌ Binary not found: {binary_path}\n\n"
                         f"Provide binary_path argument (e.g., /workspace/build/mactcp/PeerTalk.bin)"
                )]

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
            keep_latest = arguments.get("keep_latest", True)

            ftp = self._connect_ftp(machine_id)
            machine = self.machines[machine_id]

            try:
                removed = []

                # Clean binaries
                binary_path = machine['ftp']['paths']['binaries']
                ftp.cwd(binary_path)
                files = []
                ftp.retrlines('LIST', files.append)

                # Remove old binaries (keep latest if requested)
                # Simplified: remove all .old files
                for file_line in files:
                    filename = file_line.split()[-1]
                    if filename.endswith('.old'):
                        ftp.delete(filename)
                        removed.append(filename)

                return [TextContent(
                    type="text",
                    text=f"Cleaned {machine['name']}:\nRemoved {len(removed)} files\n" +
                         '\n'.join(removed)
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
