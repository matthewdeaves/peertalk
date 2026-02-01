#!/bin/bash
# Setup Classic Mac Hardware MCP Server

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MACHINES_FILE="$SCRIPT_DIR/machines.json"
EXAMPLE_FILE="$SCRIPT_DIR/machines.example.json"

echo "Classic Mac Hardware MCP Server Setup"
echo "======================================"
echo ""

# Check if machines.json already exists
if [ -f "$MACHINES_FILE" ]; then
    echo "⚠️  machines.json already exists"
    read -p "Overwrite? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Keeping existing configuration."
        exit 0
    fi
fi

# Copy example
cp "$EXAMPLE_FILE" "$MACHINES_FILE"
echo "✓ Created machines.json from template"
echo ""

# Prompt for configuration
echo "Configure your Classic Mac machines:"
echo ""
echo "For each machine, you'll need:"
echo "  - Machine name (e.g., 'SE/30', 'IIci')"
echo "  - Platform: mactcp, opentransport, or appletalk"
echo "  - FTP host (IP address)"
echo "  - FTP username"
echo "  - FTP password (stored in environment variable)"
echo ""

read -p "Configure machines now? (y/N) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo ""
    echo "Skipping configuration."
    echo "Edit $MACHINES_FILE manually to add your machines."
    echo ""
    echo "Then set environment variables:"
    echo "  export PEERTALK_SE30_FTP_PASSWORD='your-password'"
    echo "  export PEERTALK_IICI_FTP_PASSWORD='your-password'"
    echo ""
    exit 0
fi

# Interactive configuration
echo ""
echo "Machine configuration (Ctrl+C to skip):"
echo ""

configure_machine() {
    local id=$1
    local default_name=$2
    local default_platform=$3

    echo "=== Machine: $id ==="

    read -p "Name [$default_name]: " name
    name=${name:-$default_name}

    read -p "Platform (mactcp/opentransport/appletalk) [$default_platform]: " platform
    platform=${platform:-$default_platform}

    read -p "FTP host (IP address): " host
    if [ -z "$host" ]; then
        echo "Skipping $id (no host specified)"
        return
    fi

    read -p "FTP username [peertalk]: " username
    username=${username:-peertalk}

    read -p "FTP password: " -s password
    echo

    # Update machines.json using jq
    if command -v jq >/dev/null 2>&1; then
        tmp_file=$(mktemp)
        jq --arg id "$id" \
           --arg name "$name" \
           --arg platform "$platform" \
           --arg host "$host" \
           --arg username "$username" \
           '.[$id].name = $name |
            .[$id].platform = $platform |
            .[$id].ftp.host = $host |
            .[$id].ftp.username = $username' \
           "$MACHINES_FILE" > "$tmp_file"
        mv "$tmp_file" "$MACHINES_FILE"
    fi

    # Add to environment
    env_var="PEERTALK_${id^^}_FTP_PASSWORD"
    echo "export $env_var='$password'" >> ~/.bashrc
    export "$env_var=$password"

    echo "✓ Configured $id ($name)"
    echo "  Added $env_var to ~/.bashrc"
    echo ""
}

# Configure machines
configure_machine "se30" "SE/30" "mactcp"
configure_machine "iici" "IIci" "opentransport"

echo ""
echo "✓ Setup complete!"
echo ""
echo "machines.json is user-specific and NOT committed to git."
echo "Other developers should run this script to configure their own machines."
echo ""
echo "To add more machines, edit:"
echo "  $MACHINES_FILE"
echo ""
echo "To test the configuration:"
echo "  python $SCRIPT_DIR/server.py"
echo ""
