# srsRAN_4G - LTE Cell Scanner for RTL-SDR v3

[![Build Status](https://github.com/srsran/srsRAN_4G/actions/workflows/ccpp.yml/badge.svg)](https://github.com/srsran/srsRAN_4G/actions/workflows/ccpp.yml)
[![CodeQL](https://github.com/srsran/srsRAN_4G/actions/workflows/codeql.yml/badge.svg)](https://github.com/srsran/srsRAN_4G/actions/workflows/codeql.yml)
[![Latest Release](https://img.shields.io/github/v/release/Muhammad-Yunus/srsRAN_4G?label=Release)](https://github.com/Muhammad-Yunus/srsRAN_4G/releases/tag/v1.3)

**Optimized LTE cell scanner for RTL-SDR v3 on Raspberry Pi**

This fork of srsRAN_4G includes optimizations for fast LTE cell scanning with RTL-SDR dongles, featuring a 4.8x performance improvement and a convenient CLI tool with 3 scan modes for different use cases.

## 🚀 Quick Install (Pre-built Binary)

**No compilation needed!** Download the pre-built binary and start scanning in under 5 minutes:

```bash
# 1. Download latest release
wget https://github.com/Muhammad-Yunus/srsRAN_4G/releases/download/v1.3/srsran_rpi_aarch64_v1.3.tar.gz

# 2. Extract & Install
tar xzf srsran_rpi_aarch64_v1.3.tar.gz
cd srsran-release
sudo ./install.sh

# 3. Scan!
lte-scan fast 8 --json
```

📖 **Full installation guide:** [INSTALL.md](INSTALL.md)

---

## Features

- **4.8x faster** full scan (158s → 33s)
- **3 scan modes**: Fast (4.5s), Balance (10.5s), Full (33s)
- **RTL-SDR v3 optimized** - works with cheap USB dongles
- **CLI tool** - simple `lte-scan` command
- **JSON output** - easy integration with other tools
- **Operator identification** - detects Telkomsel, XL, Indosat, Hutchison 3, Smartfren
- **5 Band support** - Band 3, 5, 8, 28, 40 (Indonesia operators)
- **aarch64 stable** - fixed mutex bug for Raspberry Pi 4/5

---

## Scan Modes Comparison

### Quick Reference

| Mode | Command | Time | Cells Found | Coverage | Best For |
|------|---------|------|-------------|----------|----------|
| **Fast** | `lte-scan fast 8` | ~4.5s | ~6 | ~20% | Real-time monitoring |
| **Balance** | `lte-scan balance 8` | ~10.5s | ~20 | ~57% | Comprehensive survey |
| **Full** | `lte-scan full 8` | ~33s | ~7 | ~100% | Detailed cell analysis |

### Detailed Parameter Comparison

| Parameter | Fast Mode | Balance Mode | Full Mode |
|-----------|-----------|--------------|-----------|
| **Scan Step** | 5 (500 kHz spacing) | 1 (100 kHz spacing) | 1 (100 kHz spacing) |
| **PSS Detection** | 3 frames (15ms) | 5 frames (25ms) | 3 frames (15ms) |
| **MIB Decode** | 50 frames (250ms) | 200 frames (1s) | 50 frames (250ms) |
| **SIB1 Decode** | Disabled | Disabled | Optional |
| **Stream Pattern** | Single (keep open) | Single (keep open) | Per-candidate |
| **Throughput** | ~70 points / 4.5s | ~350 points / 10.5s | ~350 points + decode |
| **Data Collected** | PCI, RSRP, Operator | PCI, RSRP, Operator | PCI, RSRP, BW, Ports, Operator |

### When to Use Each Mode

**Fast Mode** — Use when:
- You need quick status checks (<5 seconds)
- Monitoring cell availability in real-time
- Battery/constraint scenarios
- Initial site survey to find approximate locations

**Balance Mode** — Use when:
- You want to find ALL cells in a band (~10 seconds)
- Mapping cell density and coverage
- Comparing operators across the band
- Need maximum PSS detection without MIB overhead

**Full Mode** — Use when:
- You need detailed cell information (bandwidth, ports)
- Building a complete cell database
- Validating MIB decode success rate
- Troubleshooting specific cells

---

## Hardware Requirements

- **SDR**: RTL-SDR v3 (RTL2832U + R820T tuner)
- **Single**: Raspberry Pi 5 (or any Linux x86_64/arm64)
- **OS**: Debian 13 (Bookworm) / Raspberry Pi OS
- **Antenna**: LTE 700/800/900 MHz recommended for Band 8

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

## How to Use lte_scan CLI

### Quick Test

```bash
# Check if command is available
which lte-scan

# Show help
lte-scan --help

# Quick scan (fast mode) - ~4.5 seconds
lte-scan fast 8

# Scan with JSON conversion
lte-scan fast 8 --json

# Balanced scan (intermediate speed/accuracy) - ~10.5 seconds
lte-scan balance 8 --json

# Full scan (with MIB decode) - ~33 seconds
lte-scan full 8 --json
```

### Example Output (Band 8, Fast Mode)

```json
{
  "scan_info": {
    "band": 8,
    "gain_db": 43,
    "mode": "fast",
    "total_cells": 6,
    "timestamp": "2026-09-05T17:35:48.700175+00:00"
  },
  "cells": [
    {
      "frequency_mhz": 931.0,
      "earfcn": 3510,
      "band": "8",
      "pci": 243,
      "mcc": 510,
      "mnc": 10,
      "rsrp": -12.8,
      "operator": "Telkomsel",
      "country": "Indonesia"
    }
  ]
}
```

### Example Output (Band 8, Balance Mode)

```json
{
  "scan_info": {
    "band": 8,
    "gain_db": 43,
    "mode": "balance",
    "total_cells": 20,
    "timestamp": "2026-09-05T17:36:10.359163+00:00"
  },
  "cells": [
    {
      "frequency_mhz": 930.1,
      "earfcn": 3501,
      "band": "8",
      "pci": 243,
      "mcc": 510,
      "mnc": 10,
      "rsrp": -14.3,
      "operator": "Telkomsel",
      "country": "Indonesia"
    }
  ]
}
```

### Example Output (Band 8, Full Mode)

```json
{
  "scan_info": {
    "band": 8,
    "gain_db": 43,
    "mode": "full",
    "total_cells": 7,
    "timestamp": "2026-09-05T17:37:15.245503+00:00"
  },
  "cells": [
    {
      "frequency_mhz": 930.3,
      "earfcn": 3503,
      "band": "8",
      "pci": 243,
      "mcc": 510,
      "mnc": 10,
      "rsrp": -13.1,
      "operator": "Telkomsel",
      "country": "Indonesia"
    }
  ]
}
```

### Available Commands

| Command | Description | Time | Cells Found |
|---------|-------------|------|-------------|
| `lte-scan fast 8` | Quick scan (PSS only, step=5) | ~4.5s | ~6 |
| `lte-scan fast 8 --json` | Quick scan with JSON output | ~4.5s | ~6 |
| `lte-scan balance 8` | Balanced scan (PSS only, step=1) | ~10.5s | ~20 |
| `lte-scan balance 8 --json` | Balanced scan with JSON output | ~10.5s | ~20 |
| `lte-scan full 8` | Full scan (PSS + MIB, step=1) | ~33s | ~7 |
| `lte-scan full 8 --json` | Full scan with JSON output | ~33s | ~7 |
| `lte-scan fast 5` | Quick scan Band 5 (850 MHz) | ~2s | ~2 |
| `lte-scan balance 5` | Balanced scan Band 5 | ~8s | ~5 |
| `lte-scan full 5` | Full scan Band 5 | ~25s | ~3 |

### Supported Bands

| Band | Frequency | Status | Operators (Indonesia) | Notes |
|------|-----------|--------|----------------------|-------|
| Band 8 | 900 MHz | ✅ Tested | Telkomsel, XL Axiata, Indosat Ooredoo, Hutchison 3 | Primary test band |
| Band 5 | 850 MHz | ✅ Tested | Smartfren | Narrow band, fewer cells |
| Band 28 | 700 MHz | ✅ Supported | Telkomsel, Indosat, XL, Hutchison 3 | Larger EARFCN range |
| Band 3 | 1800 MHz | ⚠️ Limited | Telkomsel, XL, Indosat, Hutchison 3 | Exceeds R820T tuner limit |
| Band 40 | 2300 MHz TDD | ⚠️ Limited | Telkomsel, XL, Indosat, Hutchison 3, Smartfren | TDD band, higher freq |

**Note:** RTL-SDR v3 (R820T tuner) has frequency range **24-1766 MHz**. This limits LTE scanning to lower bands only.

### Indonesia Band Availability

| Band | Freq | Status | Operators in Indonesia | Scannable? |
|------|------|--------|----------------------|------------|
| Band 8 | 900 MHz | ✅ Active | Telkomsel, XL, Indosat, Hutchison 3 | ✅ Yes |
| Band 5 | 850 MHz | ✅ Active | Smartfren | ✅ Yes |
| Band 28 | 700 MHz | ✅ Active | Telkomsel, Indosat, XL, Hutchison 3 | ✅ Yes |
| Band 20 | 800 MHz | ❌ Not Deployed | None | N/A |
| Band 3 | 1800 MHz | ✅ Active | Telkomsel, XL, Indosat, Hutchison 3 | ⚠️ Partial (tuner limit) |
| Band 40 | 2300 MHz | ✅ Active | Telkomsel, XL, Indosat, Hutchison 3, Smartfren | ⚠️ Partial (tuner limit) |

---

## Performance Comparison

| Mode | Before (Original) | After (Optimized) | Speedup |
|------|-------------------|-------------------|---------|
| Full Scan | 158s (2m38s) | **32.7s** | **4.8x faster** |
| Balance Scan | N/A | **10.5s** | New mode |
| Fast Scan | N/A | **4.5s** | New mode |

### Optimization Summary

| Optimization | Before | After | Impact |
|-------------|--------|-------|--------|
| PSS Frames | 10 (50ms) | 3 (15ms) | 3.3x faster detection |
| MIB Frames | 500 (2.5s) | 50 (250ms) | 10x faster decode |
| SIB1 Decode | Enabled | Disabled | Saves ~1.5s per cell |
| Stream Pattern | Start/stop per freq | Single stream | Fixes aarch64 crash |
| Scan Step (Fast) | N/A | 5 (500 kHz) | 5x fewer points |

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
- Try increasing gain: `lte-scan fast 8 --json --gain 49`
- Check if other apps are using the SDR
- Try different antenna position
- Use balance mode for better coverage: `lte-scan balance 8 --json`

### Issue: "usb_claim_interface error -6"

This is the known aarch64 mutex bug. The optimized code handles this automatically by keeping the RF stream open during scanning. If you still see this error:

```bash
# Make sure you're using the latest optimized binary
ls -la /usr/local/bin/lte-scan
# Reinstall if needed
sudo ./install.sh
```

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
