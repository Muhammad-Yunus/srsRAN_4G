# srsRAN_4G Cell Search Report - Band 8 (900 MHz)

**Tanggal**: 25 Agustus 2026  
**Platform**: Raspberry Pi 5 (ARM64/AArch64)  
**OS**: Debian 13 (Bookworm)  
**Kernel**: 6.18.34+rpt-rpi-2712

---

## Hardware

| Komponen | Spesifikasi |
|----------|-------------|
| SDR Dongle | RTL-SDR v3 (RTL2832U + R820T) |
| Serial | 00000001 |
| Tuner | Rafael Micro R820T |
| Range Frekuensi | 24 - 1766 MHz |
| Sample Rate | 2.048 MSPS (default) |
| Gain | 40 dB |

---

## Build Configuration

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_UHD=OFF \
  -DENABLE_BLADERF=ON \
  -DENABLE_SOAPYSDR=ON \
  -DENABLE_SRSUE=ON \
  -DENABLE_SRSENB=OFF \
  -DENABLE_SRSEPC=OFF \
  -DENABLE_GUI=OFF \
  -DENABLE_RF_PLUGINS=ON \
  -DENABLE_ZEROMQ=OFF \
  -DENABLE_HARDSIM=OFF \
  -DENABLE_TIMEPROF=ON \
  -DASSERTS_ENABLED=OFF \
  -DENABLE_WERROR=OFF
```

**RF Driver**: SoapySDR (librfsdrSupport.so)

---

## Command Executed

```bash
./build/lib/examples/cell_search -b 8 -a "driver=rtlsdr,index=0" -g 40 -n 2
```

| Parameter | Nilai | Keterangan |
|-----------|-------|------------|
| `-b` | 8 | LTE Band 8 (900 MHz) |
| `-a` | driver=rtlsdr,index=0 | SoapySDR RTL-SDR device |
| `-g` | 40 | RF gain 40 dB |
| `-n` | 2 | Max frames untuk PSS detection |

---

## Hasil Deteksi Sel

| # | Frekuensi | EARFCN | Cell ID (PCI) | PRB | Ports | Power (dBm) |
|---|-----------|--------|---------------|-----|-------|-------------|
| 1 | 929.9 MHz | 3499 | 0 | 150 | 2 | -16.9 |
| 2 | 929.9 MHz | 3499 | 1 | 50 | 2 | -19.7 |
| 3 | 930.0 MHz | 3500 | 0 | 75 | 1 | -15.9 |
| 4 | 930.1 MHz | 3501 | 243 | 50 | 2 | **-13.5** ⭐ |
| 5 | 930.1 MHz | 3501 | 416 | 50 | 2 | -13.0 |
| 6 | 930.2 MHz | 3502 | 0 | 100 | 2 | -15.0 |
| 7 | 930.3 MHz | 3503 | 2 | 15 | 1 | -16.1 |
| 8 | 945.5 MHz | 3655 | 2 | 150 | 4 | -18.2 |
| 9 | 957.5 MHz | 3775 | 306 | 25 | 2 | -35.7 |
| 10 | 957.5 MHz | 3775 | 314 | 25 | 2 | -29.0 |

**Total sel terdeteksi: 10**

---

## Analisis

### Signal Terkuat
- **EARFCN 3501 (930.1 MHz)** dengan PCI 416: **-13.0 dBm**
- **EARFCN 3501 (930.1 MHz)** dengan PCI 243: **-13.5 dBm**
- **EARFCN 3500 (930.0 MHz)** dengan PCI 0: **-15.9 dBm**

### Distribusi Bandwidth (PRB)
| PRB | Bandwidth | Jumlah Sel |
|-----|-----------|------------|
| 150 | 20 MHz | 3 sel |
| 100 | 15 MHz | 1 sel |
| 75 | 10 MHz | 1 sel |
| 50 | 5 MHz | 4 sel |
| 25 | 5 MHz | 1 sel |
| 15 | 5 MHz | 1 sel |

### MIMO Configuration
| Ports | Jumlah Sel |
|-------|------------|
| 4 ports | 1 sel (945.5 MHz) |
| 2 ports | 7 sel |
| 1 port | 2 sel |

### Range Frekuensi Aktif
- Terendah: **929.9 MHz** (EARFCN 3499)
- Tertinggi: **957.5 MHz** (EARFCN 3775)
- Cluster utama: **929.9 - 930.3 MHz** (5 sel)
- Cluster sekunder: **945.5 MHz** dan **957.5 MHz**

---

## Catatan Teknis

1. **Band 3 (1800 MHz) tidak bisa di-scan** karena R820T max frequency ~1766 MHz
2. **Band 8 (900 MHz)** adalah pilihan optimal untuk RTL-SDR v3 di Indonesia
3. SoapySDR module `librtlsdrSupport.so` versi 0.3.3 berfungsi dengan baik
4. PLL warnings (`[R82XX] PLL not locked!`) muncul saat switching frekuensi, tapi tidak mempengaruhi hasil

---

## Rekomendasi

1. Untuk scanning lebih luas, coba band lain yang tersedia di R820T:
   - Band 20 (800 MHz): `./build/lib/examples/cell_search -b 20 -a "driver=rtlsdr,index=0" -g 40 -n 2`
   - Band 5 (850 MHz): `./build/lib/examples/cell_search -b 5 -a "driver=rtlsdr,index=0" -g 40 -n 2`

2. Untuk koneksi LTE penuh, gunakan srsue:
   ```bash
   ./build/srsue/src/srsue ue.conf
   ```

3. Untuk scanning lebih detail dengan gain lebih tinggi:
   ```bash
   ./build/lib/examples/cell_search -b 8 -a "driver=rtlsdr,index=0" -g 49.6 -n 5
   ```
