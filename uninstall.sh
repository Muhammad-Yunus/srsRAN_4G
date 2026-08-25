#!/bin/bash
#
# Uninstall script for lte_scan CLI
# Removes the 'lte-scan' command
#
# Usage: sudo ./uninstall.sh
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

SYMLINK="/usr/local/bin/lte-scan"

echo "======================================"
echo "  lte-scan Uninstaller"
echo "======================================"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${YELLOW}Warning: Not running as root.${NC}"
    echo "Please run with sudo: sudo $0"
    exit 1
fi

# Check if symlink exists
if [ ! -L "$SYMLINK" ]; then
    echo -e "${YELLOW}Warning: $SYMLINK not found.${NC}"
    echo "Is lte-scan already uninstalled?"
    exit 0
fi

# Remove symlink
echo "Removing symlink: $SYMLINK"
rm "$SYMLINK"

if [ ! -e "$SYMLINK" ]; then
    echo -e "${GREEN}✓ Uninstallation successful!${NC}"
    echo ""
    echo "The 'lte-scan' command has been removed."
    echo "To reinstall later, run: sudo ./install.sh"
else
    echo -e "${RED}Error: Failed to remove symlink.${NC}"
    exit 1
fi
