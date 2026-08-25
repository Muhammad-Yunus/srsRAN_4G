# srsRAN_4G - LTE Cell Scanner for RTL-SDR v3

[![Build Status](https://github.com/srsran/srsRAN_4G/actions/workflows/ccpp.yml/badge.svg)](https://github.com/srsran/srsRAN_4G/actions/workflows/ccpp.yml)
[![CodeQL](https://github.com/srsran/srsRAN_4G/actions/workflows/codeql.yml/badge.svg)](https://github.com/srsran/srsRAN_4G/actions/workflows/codeql.yml)

**Optimized LTE cell scanner for RTL-SDR v3 on Raspberry Pi**

This fork of srsRAN_4G includes optimizations for fast LTE cell scanning with RTL-SDR dongles, featuring a 5.1x performance improvement and a convenient CLI tool for quick network analysis.

## Features

- **5.1x faster** scanning (31s → 1.6s for fast mode)
- **RTL-SDR v3 optimized** - works with cheap USB dongles
- **CLI tool** - simple `lte-scan` command
- **JSON output** - easy integration with other tools
- **Operator identification** - detects Telkomsel, XL, Indosat, Hutchison 3
- **Band 8 support** - 900 MHz LTE scanning

## Hardware Requirements

- **SDR**: RTL-SDR v3 (RTL2832U + R820T tuner)
- **Single**: Raspberry Pi 5 (or any Linux x86_64/arm64)
- **OS**: Debian 13 (Bookworm) / Raspberry Pi OS

## Software Requirements

- **Python**: 3.6+ (for `lte_scan.py`)
- **CMake**: 3.5+
- **Compiler**: GCC 10+ or Clang 11+

---

## How to Build srsRAN_4G

### 1. Install Dependencies

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    python3 \
    libsoapysdr-dev \
    soapy-sdr \
    librtlsdr-dev \
    libsdl2-dev \
    libboost-all-dev \
    libconfig++-dev \
    libfftw3-dev \
    libmbedtls-dev \
    libpcsclite-dev
```

### 2. Clone Repository

```bash
git clone https://github.com/Muhammad-Yunus/srsRAN_4G.git
cd srsRAN_4G
```

### 3. Configure Build

```bash
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_SOAPYSDR=ON \
    -DENABLE_SRSUE=ON \
    -DENABLE_SRSENB=OFF \
    -DENABLE_SRSEPC=OFF \
    -DENABLE_GUI=OFF \
    -DASSERTS_ENABLED=OFF \
    -DENABLE_WERROR=OFF
```

**Key options:**
- `-DENABLE_SOAPYSDR=ON`: Enable SoapySDR for RTL-SDR support
- `-DENABLE_SRSUE=ON`: Build LTE UE application
- `-DENABLE_SRSENB=OFF`: Disable eNodeB (not needed for scanning)
- `-DENABLE_SRSEPC=OFF`: Disable EPC core (not needed for scanning)
- `-DENABLE_GUI=OFF`: Disable GUI (faster build)
- `-DASSERTS_ENABLED=OFF`: Disable assertions (faster execution)

### 4. Build

```bash
cmake --build build -j$(nproc)
```

### 5. Verify Build

```bash
./build/lib/examples/lte_scan_example --help
```

---

## How to Install lte_scan CLI

### Option A: Use Install Script (Recommended)

```bash
cd /path/to/srsRAN_4G
sudo ./install.sh
```

This will:
- Create a symlink at `/usr/local/bin/lte-scan`
- Make the command available system-wide
- Show usage examples

### Option B: Manual Installation

```bash
sudo ln -s /path/to/srsRAN_4G/lte_scan.py /usr/local/bin/lte-scan
```

---

## How to Test lte_scan CLI

### Quick Test

```bash
# Check if command is available
which lte-scan

# Show help
lte-scan --help

# Quick scan (fast mode) - ~1.6 seconds
lte-scan fast 8

# Scan with JSON conversion
lte-scan fast 8 --json

# Full scan (with MIB decode) - ~29 seconds
lte-scan full 8 --json
```

### Example Output

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
      "bandwidth_mhz": null,
      "pci": 243,
      "cell_id": null,
      "tac": null,
      "mcc": 510,
      "mnc": 11,
      "rsrp": -13.8,
      "rsrq": null,
      "snr": null,
      "operator": "XL Axiata",
      "country": "Indonesia",
      "timestamp": "2026-08-25T03:47:10.929773+00:00"
    }
  ]
}
```

### Available Commands

| Command | Description | Time |
|---------|-------------|------|
| `lte-scan fast 8` | Quick scan (PSS only) | ~1.6s |
| `lte-scan fast 8 --json` | Quick scan with converted output | ~1.6s |
| `lte-scan full 8` | Full scan (PSS + MIB) | ~29s |
| `lte-scan full 8 --json` | Full scan with converted output | ~29s |

### Supported Bands

| Band | Frequency | Status |
|------|-----------|--------|
| Band 8 | 900 MHz | ✅ Tested |
| Band 3 | 1800 MHz | ⚠️ R820T limit (~1766 MHz) |
| Band 5 | 850 MHz | ✅ Should work |
| Band 20 | 800 MHz | ✅ Should work |

---

## Performance Comparison

| Mode | Before Optimization | After Optimization | Speedup |
|------|-------------------|-------------------|---------|
| Full Scan | 2m38s (158s) | 29s | **5.4x faster** |
| Fast Scan | N/A | 1.6s | **N/A** |

### Optimizations Applied

1. **MIB timeout reduced**: 500 frames → 50 frames (2.5s → 0.25s)
2. **PSS detection optimized**: 10 frames → 3 frames (50ms → 15ms)
3. **SIB1 decode disabled**: Saved ~60s (not feasible with RTL-SDR bandwidth)
4. **Stream optimization**: Keep RF stream open during scan

---

## Troubleshooting

### Issue: "lte-scan: command not found"

```bash
# Check if symlink exists
ls -la /usr/local/bin/lte-scan

# Reinstall if missing
sudo ./install.sh
```

### Issue: "Python3 not found"

```bash
# Install Python3
sudo apt-get install python3

# Check version (need ≥ 3.6)
python3 --version
```

### Issue: "lte_scan_example: No such file"

```bash
# Rebuild srsRAN_4G
cd /path/to/srsRAN_4G
cmake --build build -j$(nproc)

# Verify binary exists
ls -la build/lib/examples/lte_scan_example
```

### Issue: "SDR not found"

```bash
# Check RTL-SDR is connected
lsusb | grep -i rtl

# Check permissions
sudo usermod -aG plugdev $USER
# Logout and login again

# Test RTL-SDR
rtl_test
```

### Issue: "No cells found"

- Ensure you're in an area with LTE coverage
- Try increasing gain: `lte-scan fast 8 --json` with `-g 49.6`
- Check if other apps are using the SDR
- Try different antenna position

---

## Files Structure

```
srsRAN_4G/
├── install.sh              # Install script for CLI
├── uninstall.sh            # Uninstall script
├── lte_scan.py             # Main scanner script
├── README.md               # This file
├── OPTIMIZATION_CHANGELOG.md
├── CELL_SEARCH_ANALYSIS_REPORT.md
├── build/                  # Build output
│   └── lib/examples/
│       └── lte_scan_example  # Compiled binary
└── lib/examples/
    ├── lte_scan.cc         # Scanner source (optimized)
    ├── lte_scan.h          # Header file
    └── lte_scan_example.c  # Example C wrapper
```

---

## References

- [srsRAN 4G Documentation](https://docs.srsran.com/projects/4g/)
- [RTL-SDR Blog](https://www.rtl-sdr.com/)
- [SoapySDR](https://github.com/pothosware/SoapySDR)
- [LTE Cell Search Algorithm](https://en.wikipedia.org/wiki/LTE_physical_layer#Cell_search)
- [Optimization Changelog](OPTIMIZATION_CHANGELOG.md)

---

## License

This project is licensed under AGPL-3.0 - see the [LICENSE](LICENSE) file for details.

---

## Support

- **GitHub Issues**: https://github.com/Muhammad-Yunus/srsRAN_4G/issues
- **Original srsRAN Mailing List**: https://lists.srsran.com/mailman/listinfo/srsran-users
