# Indonesia LTE Operator Database

**Source**: `lib/examples/lte_scan.cc`  
**MCC**: 510 (Indonesia)  
**Last Updated**: September 2026  
**Accuracy**: ~95% based on Kominfo spectrum allocation data

---

## Quick Reference Table

| Operator | Short Name | MNC | Bands Active |
|----------|-----------|-----|--------------|
| Telkomsel | TSel | 10 | 3, 5*, 8, 28, 40 |
| XL Axiata | XL | 11 | 3, 8, 28, 40 |
| XL Axiata (AXIS) | AXIS | 11 | 3, 8, 28, 40 |
| Indosat Ooredoo | Indosat | 21 | 3, 8, 28, 40 |
| Hutchison 3 (Tri) | Tri | 89 | 3, 8, 28, 40 |
| Smartfren | Smartfren | 9, 8 | 5, 40 |

> *Band 5 only Smartfren in Indonesia

---

## Band-by-Band EARFCN Mapping

### Band 3 (1800 MHz) — DL: 1805-1880 MHz

| EARFCN Range | F_DL (MHz) | MNC | Operator |
|--------------|------------|-----|----------|
| 1200-1399 | 1805-1824.9 | 10 | Telkomsel |
| 1400-1499 | 1825-1834.9 | 11 | XL Axiata |
| 1500-1599 | 1835-1844.9 | 11 | XL Axiata (AXIS) |
| 1600-1799 | 1845-1864.9 | 21 | Indosat Ooredoo |
| 1800-1949 | 1865-1884.9 | 89 | Hutchison 3 |

**Formula**: `F_DL = 1805 + 0.1 × (EARFCN - 1200)` MHz

---

### Band 5 (850 MHz) — DL: 869-894 MHz

| EARFCN Range | F_DL (MHz) | MNC | Operator |
|--------------|------------|-----|----------|
| 2400-2649 | 869-893.9 | 9 | Smartfren |

> **Note**: In Indonesia, Band 5 is exclusively used by Smartfren (MNC 9). Telkomsel does NOT operate in Band 5 in Indonesia.

**Formula**: `F_DL = 869 + 0.1 × (EARFCN - 2400)` MHz

---

### Band 8 (900 MHz) — DL: 925-960 MHz

| EARFCN Range | F_DL (MHz) | MNC | Operator |
|--------------|------------|-----|----------|
| 3450-3499 | 925-929.9 | 10 | Telkomsel |
| 3500-3549 | 930-934.9 | 10 | Telkomsel |
| 3550-3649 | 935-944.9 | 11 | XL Axiata |
| 3650-3699 | 945-949.9 | 21 | Indosat Ooredoo |
| 3700-3749 | 950-954.9 | 89 | Hutchison 3 |
| 3750-3799 | 955-959.9 | 89 | Hutchison 3 |

**Formula**: `F_DL = 925 + 0.1 × (EARFCN - 3450)` MHz

---

### Band 28 (700 MHz) — DL: 758-803 MHz

| EARFCN Range | F_DL (MHz) | MNC | Operator |
|--------------|------------|-----|----------|
| 9000-9149 | 758-772.9 | 10 | Telkomsel |
| 9150-9299 | 773-787.9 | 21 | Indosat Ooredoo |
| 9300-9449 | 788-802.9 | 11 | XL Axiata |
| 9450-9599 | 803-817.9 | 89 | Hutchison 3 |

**Formula**: `F_DL = 758 + 0.1 × (EARFCN - 9000)` MHz

---

### Band 40 (2300 MHz TDD) — TDD Uplink/Downlink

| EARFCN Range | F_DL (MHz) | MNC | Operator |
|--------------|------------|-----|----------|
| 38650-38799 | 2300-2314.9 | 10 | Telkomsel |
| 38800-38949 | 2315-2329.9 | 11 | XL Axiata |
| 38950-39099 | 2330-2344.9 | 21 | Indosat Ooredoo |
| 39100-39249 | 2345-2359.9 | 89 | Hutchison 3 |
| 39250-39649 | 2360-2399.9 | 8 | Smartfren |

**Formula**: `F_DL = 2300 + 0.1 × (EARFCN - 38650)` MHz (TDD)

---

## MNC Reference

| MNC | Operator | Notes |
|-----|----------|-------|
| 10 | Telkomsel | Largest operator in Indonesia |
| 11 | XL Axiata | Includes AXIS prepaid brand |
| 21 | Indosat Ooredoo | Former Indosat, merged with Ooredoo |
| 89 | Hutchison 3 | Known as "Tri" (Three) |
| 9 | Smartfren | CDMA-to-LTE refarmed, Band 5 exclusive |
| 8 | Smartfren | TDD operations on Band 40 |

---

## Data Structure (C)

```c
typedef struct {
    int earfcn_min;
    int earfcn_max;
    int mcc;           // 510 = Indonesia
    int mnc;           // Mobile Network Code
    bool sif_enabled;  // Not currently used
    const char* name;  // Operator display name
    const char* band;  // Band identifier
} lte_operator_entry_t;
```

---

## Usage Examples

### Check Operator by EARFCN
```c
// Example: EARFCN 1250 → Telkomsel (Band 3)
lte_scan_lookup_operator(1250) → {"Telkomsel", "Band 3"}

// Example: EARFCN 2500 → Smartfren (Band 5)
lte_scan_lookup_operator(2500) → {"Smartfren", "Band 5"}

// Example: EARFCN 3600 → XL Axiata (Band 8)
lte_scan_lookup_operator(3600) → {"XL Axiata", "Band 8"}
```

### Filter by Operator
```bash
# Find all Telkomsel cells
lte-scan full 8 | grep Telkomsel

# Find all Smartfren cells (Band 5 & 40)
lte-scan full 5 | grep Smartfren
lte-scan full 40 | grep Smartfren
```

---

## References

- [3GPP TS 36.101](https://www.3gpp.org/technologies/standards/release-17-work-1490) — LTE UE radio access capabilities
- [Kominfo Spectrum Allocation](https://www.kominfo.go.id) — Indonesian Ministry of Communication and Informatics
- [ITU MNC Registry](https://www.itu.int) — International Telecommunication Union mobile country codes

---

## Changelog

| Date | Change | Author |
|------|--------|--------|
| 2026-09-05 | Fixed Band 5 mapping (removed incorrect Telkomsel entry) | Muhammad Yunus |
| 2026-09-01 | Added Indonesia operator database documentation | Muhammad Yunus |
