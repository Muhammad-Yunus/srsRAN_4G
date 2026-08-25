# srsRAN_4G LTE Cell Scan Analysis Report

**Tanggal**: 25 Agustus 2026  
**Platform**: Raspberry Pi 5 (ARM64/AArch64)  
**OS**: Debian 13 (Bookworm)  
**SDR Hardware**: RTL-SDR v3 (RTL2832U + R820T)

---

## Executive Summary

Build srsRAN_4G berhasil dengan dukungan SoapySDR untuk RTL-SDR v3. Tool `lte_scan_example` dapat mendeteksi **30 sel LTE** di Band 8 (900 MHz) dengan output JSON yang mendekati format target.

**Hasil Scan Band 8:**
- Total sel terdeteksi: **30 cells**
- Operator teridentifikasi: Telkomsel (MCC 510, MNC 10), XL Axiata (MNC 11), Indosat Ooredoo (MNC 21), Hutchison 3 (MNC 89)
- Signal terkuat: **-13.5 dBm** (EARFCN 3501, PCI 243, Telkomsel)
- Bandwidth decoded: 1.4 - 20 MHz (dari MIB)
- RSRP range: -36.6 dBm s/d -13.5 dBm

---

## Build Configuration

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SOAPYSDR=ON \
  -DENABLE_SRSUE=ON \
  -DENABLE_SRSENB=OFF \
  -DENABLE_SRSEPC=OFF \
  -DENABLE_GUI=OFF \
  -DENABLE_ZEROMQ=OFF \
  -DASSERTS_ENABLED=OFF \
  -DENABLE_WERROR=OFF
```

**RF Driver**: SoapySDR (librtlsdrSupport.so v0.3.3)

---

## Tools yang Tersedia

| Tool | Binary Path | Kegunaan |
|------|-------------|----------|
| **cell_search** | `build/lib/examples/cell_search` | PSS + MIB decode (PCI, PRB, ports, power) |
| **lte_scan_example** | `build/lib/examples/lte_scan_example` | **Paling lengkap** - PSS + MIB + operator ID + JSON output |
| **lte_plmn_scan** | `build/lib/examples/lte_plmn_scan` | Attempt SIB1 decode untuk MCC/MNC/TAC |
| **srsue** | `build/srsue/src/srsue` | Full LTE UE stack (attach ke network) |

---

## Cara Penggunaan

### Fast Scan (PSS + Operator Table)
```bash
./build/lib/examples/lte_scan_example -b 8 -a "driver=rtlsdr,index=0" -g 40 -j -q
```

### Full Scan (PSS + MIB + Operator Table)
```bash
./build/lib/examples/lte_scan_example -b 8 -a "driver=rtlsdr,index=0" -g 40 -f -j -q
```

### Dengan SIB1 Decode Attempt
```bash
./build/lib/examples/lte_scan_example -b 8 -a "driver=rtlsdr,index=0" -g 40 -f -j -q
```
*(SIB1 decode sudah di-enable di code, tapi hanya untuk sel ≤15 PRB)*

### Convert ke Format JSON Target
```bash
python3 lte_scan.py full 8
```

---

## Field Comparison: Target vs Actual

| Field | Target | Actual | Source |
|-------|--------|--------|--------|
| `frequency_mhz` | ✅ | ��� | EARFCN lookup |
| `earfcn` | ✅ | ✅ | Config |
| `band` | ✅ | ✅ | String |
| `bandwidth_mhz` | ✅ | ⚠️ Conditional | MIB decode (PRB) |
| `pci` | ✅ | ✅ | PSS detection |
| `cell_id` | ❌ (null) | ❌ (null) | Requires SIB1 |
| `tac` | ❌ (null) | ❌ (null) | Requires SIB1 |
| `mcc` | ✅ | ✅ | Lookup table (510) |
| `mnc` | ✅ | ✅ | Lookup table |
| `rsrp` | ✅ | ✅ | PSS peak power |
| `rsrq` | ❌ (null) | ❌ (null) | Requires channel est. |
| `snr` | ❌ (null) | ❌ (null) | Requires channel est. |
| `operator` | ✅ | ✅ | Lookup table |
| `country` | ✅ | ✅ | Hardcoded "Indonesia" |
| `timestamp` | ✅ | ✅ | ISO 8601 |

**Coverage**: 10/15 fields fully available, 1 conditional, 4 not available with RTL-SDR v3

---

## Sample Output (Band 8)

```json
{
  "scan_info": {
    "band": 8,
    "gain_db": 40,
    "total_cells": 30,
    "timestamp": "2026-08-25T02:23:39.012531+00:00"
  },
  "cells": [
    {
      "frequency_mhz": 930.1,
      "earfcn": 3501,
      "band": "8",
      "bandwidth_mhz": 10.0,
      "pci": 243,
      "cell_id": null,
      "tac": null,
      "mcc": 510,
      "mnc": 10,
      "rsrp": -13.8,
      "rsrq": null,
      "snr": null,
      "operator": "Telkomsel",
      "country": "Indonesia",
      "timestamp": "2026-08-25T02:23:39.012531+00:00",
      "ports": 2
    }
  ]
}
```

---

## Keterbatasan RTL-SDR v3

### 1. Bandwidth Terbatas (~3.2 MHz max)
- **Band 8 (900 MHz)**: ✅ Dapat discan (frekuensi mendukung)
- **Band 3 (1800 MHz)**: ❌ R820T max ~1766 MHz
- **Band 40 (2300 MHz)**: ❌ Melebihi kemampuan hardware

### 2. SIB1 Decoding
SIB1 membutuhkan bandwidth penuh sel untuk decode:
| Cell Bandwidth | PRB | Sample Rate | RTL-SDR v3 |
|---------------|-----|-------------|------------|
| 1.4 MHz | 6 | 1.92 MHz | ✅ Bekerja |
| 3 MHz | 15 | 3.84 MHz | ⚠️ Batas atas |
| 5 MHz | 25 | 5.76 MHz | ❌ Tidak cukup |
| 10 MHz | 50 | 11.52 MHz | ❌ Mustahil |
| 20 MHz | 100 | 23.04 MHz | ❌ Mustahil |

**Kesimpulan**: SIB1 decode hanya berhasil untuk sel narrowband (≤15 PRB). Sel dengan 50-150 PRB (yang dominan di Band 8) tidak bisa di-decode SIB1-nya.

### 3. RSRQ dan SNR
- RSRQ membutuhkan channel estimation dari reference signals (CRS)
- SNR membutuhkan analisis noise floor
- Keduanya memerlukan full subframe capture yang tidak feasible dengan bandwidth terbatas

---

## Rekomendasi Upgrade Hardware

Untuk mendapatkan data lengkap (termasuk TAC, cell_id, RSRQ, SNR):

| SDR | Max BW | Estimasi Harga | Keunggulan |
|-----|--------|----------------|------------|
| **USRP B200** | 20 MHz | ~$500-800 | Wideband, stabil, banyak dokumentasi |
| **HackRF One** | 20 MHz | ~$300 | Dual-band (Tx/Rx), portable |
| **BladeRF 2.0** | 40 MHz | ~$400 | USB 3.0, low latency |
| **LimeSDR Mini** | 61.44 MHz | ~$250 | Multi-band, open source |

Dengan SDR wideband, Anda bisa:
- Decode SIB1 untuk semua sel → dapatkan **TAC, cell_id**
- Channel estimation → dapatkan **RSRQ, SNR**
- Scan multiple bands sekaligus (Band 3, 5, 8, 28)

---

## File Outputs

| File | Deskripsi |
|------|-----------|
| `cell_scan_band8_result.json` | Hasil scan Band 8 dalam format target |
| `lte_scan.py` | Script Python untuk konversi output |
| `CELL_SEARCH_BAND8_REPORT.md` | Report awal cell_search |
| `CELL_SEARCH_ANALYSIS_REPORT.md` | Report ini |

---

## Next Steps

1. **✅ DONE**: Build srsRAN_4G dengan SoapySDR
2. **✅ DONE**: Jalankan lte_scan_example pada Band 8
3. **✅ DONE**: Generate JSON output sesuai format target
4. **✅ DONE**: Enable SIB1 decode attempt
5. **TODO**: Coba scan Band lain (Band 5, Band 20) jika tersedia
6. **TODO**: Integrasi dengan database TAC external untuk enrichment
7. **TODO**: Upgrade SDR ke wideband untuk SIB1 decode penuh

---

## References

- srsRAN_4G Documentation: https://srsran.com/docs
- SoapySDR: https://github.com/pothosware/SoapySDR
- RTL-SDR Blog: https://www.rtl-sdr.com/
- LTE EARFCN Table: https://en.wikipedia.org/wiki/E-UTRA_Absolute_Radio_Channel_Frequency_Number
