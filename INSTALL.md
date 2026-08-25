# Quick Install Guide - Pre-built Binary

This guide shows how to install the pre-built LTE scanner on Raspberry Pi in under 5 minutes.

## Prerequisites

- Raspberry Pi (ARM64/aarch64)
- Debian 13 (Bookworm) or Raspberry Pi OS
- RTL-SDR v3 dongle connected
- Python 3.6+ (usually pre-installed)

## Installation Steps

### 1. Download the Package

```bash
wget https://github.com/Muhammad-Yunus/srsRAN_4G/releases/download/cli-release-v1.0/srsran_rpi_aarch64_v1.0.tar.gz
```

### 2. Verify Checksum (Optional)

```bash
echo "5291d19925fa447e23e071d930d05cd76019e92dc5b3095f7894fe32a41a1046  srsran_rpi_aarch64_v1.0.tar.gz" | sha256sum -c
```

Expected output: `srsran_rpi_aarch64_v1.0.tar.gz: OK`

### 3. Extract

```bash
tar xzf srsran_rpi_aarch64_v1.0.tar.gz
cd srsran-release
```

### 4. Install System-wide

```bash
sudo ./install.sh
```

This will:
- Copy binary to `/usr/local/bin/lte-scan`
- Make command available system-wide
- Show usage examples

### 5. Verify Installation

```bash
which lte-scan
lte-scan --help
```

## Quick Test

```bash
# Fast scan (1.6 seconds)
lte-scan fast 8

# Fast scan with JSON output
lte-scan fast 8 --json

# Full scan with MIB decode (29 seconds)
lte-scan full 8 --json
```

## Example Output

```json
{
  "scan_info": {
    "band": 8,
    "gain_db": 40,
    "mode": "fast",
    "total_cells": 3,
    "timestamp": "2026-08-25T03:47:10.929773+00:00"
  },
  "cells": [
    {
      "frequency_mhz": 935.0,
      "earfcn": 3550,
      "band": "8",
      "pci": 243,
      "mcc": 510,
      "mnc": 11,
      "rsrp": -13.8,
      "operator": "XL Axiata",
      "country": "Indonesia"
    }
  ]
}
```

## Troubleshooting

### "lte-scan: command not found"
```bash
sudo ./uninstall.sh && sudo ./install.sh
```

### "Python3 not found"
```bash
sudo apt-get install python3
python3 --version  # Should be 3.6+
```

### "SDR not found"
```bash
lsusb | grep -i rtl
rtl_test
```

### Permission Issues
```bash
sudo usermod -aG plugdev $USER
# Logout and login again
```

## Uninstall

```bash
sudo ./uninstall.sh
```

## Links

- [GitHub Repository](https://github.com/Muhammad-Yunus/srsRAN_4G)
- [Full Documentation](README.md)
- [Issue Tracker](https://github.com/Muhammad-Yunus/srsRAN_4G/issues)
