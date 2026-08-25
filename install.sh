#!/bin/bash
#
# Install script for lte_scan CLI
# Makes lte_scan.py available as global 'lte-scan' command
#
# Usage: sudo ./install.sh
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Script directory (where install.sh is located)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LTE_SCAN_SCRIPT="$SCRIPT_DIR/lte_scan.py"
SYMLINK="/usr/local/bin/lte-scan"

echo "======================================"
echo "  lte_scan CLI Installer"
echo "======================================"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${YELLOW}Warning: Not running as root.${NC}"
    echo "Please run with sudo: sudo $0"
    echo ""
    echo "Attempting to continue without sudo..."
    SUDO=""
else
    SUDO="sudo"
    echo -e "${GREEN}Running as root.${NC}"
fi

# Check if lte_scan.py exists
if [ ! -f "$LTE_SCAN_SCRIPT" ]; then
    echo -e "${RED}Error: lte_scan.py not found at $LTE_SCAN_SCRIPT${NC}"
    echo "Please run this script from the srsRAN_4G directory."
    exit 1
fi

# Check if lte_scan_example binary exists
if [ ! -f "$SCRIPT_DIR/build/lib/examples/lte_scan_example" ]; then
    echo -e "${YELLOW}Warning: lte_scan_example binary not found.${NC}"
    echo "Please build srsRAN_4G first:"
    echo "  cmake -B build -DENABLE_SOAPYSDR=ON -DENABLE_SRSUE=ON"
    echo "  cmake --build build -j$(nproc)"
    echo ""
fi

# Check Python3
echo "Checking Python3..."
if command -v python3 &> /dev/null; then
    PYTHON_VERSION=$(python3 --version 2>&1 | awk '{print $2}')
    PYTHON_MAJOR=$(echo $PYTHON_VERSION | cut -d. -f1)
    echo -e "${GREEN}  ✓ Python3 found: $PYTHON_VERSION${NC}"
else
    echo -e "${RED}Error: Python3 is required but not installed.${NC}"
    echo "Install with: sudo apt-get install python3"
    exit 1
fi

# Check write permission to /usr/local/bin
if [ ! -w "/usr/local/bin" ] && [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: Permission denied. Please run with sudo.${NC}"
    echo "Usage: sudo $0"
    exit 1
fi

# Remove existing symlink if present
if [ -L "$SYMLINK" ]; then
    echo "Removing existing symlink: $SYMLINK"
    $SUDO rm "$SYMLINK"
elif [ -f "$SYMLINK" ]; then
    echo -e "${YELLOW}Warning: $SYMLINK exists but is not a symlink.${NC}"
    read -p "Overwrite? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Installation cancelled."
        exit 1
    fi
    $SUDO rm "$SYMLINK"
fi

# Create symlink
echo "Creating symlink: $SYMLINK -> $LTE_SCAN_SCRIPT"
$SUDO ln -s "$LTE_SCAN_SCRIPT" "$SYMLINK"

# Verify installation
if [ -L "$SYMLINK" ]; then
    echo -e "${GREEN}✓ Installation successful!${NC}"
    echo ""
    echo "======================================"
    echo "  Usage Examples"
    echo "======================================"
    echo ""
    echo "  # Quick scan (fast mode, ~1.6 seconds)"
    echo "  lte-scan fast 8"
    echo ""
    echo "  # Scan with JSON conversion"
    echo "  lte-scan fast 8 --json"
    echo ""
    echo "  # Full scan (with MIB decode, ~29 seconds)"
    echo "  lte-scan full 8 --json"
    echo ""
    echo "  # See help"
    echo "  lte-scan --help"
    echo ""
    echo "======================================"
    echo ""
    echo -e "${GREEN}You can now use 'lte-scan' command anywhere!${NC}"
else
    echo -e "${RED}Error: Failed to create symlink.${NC}"
    exit 1
fi
