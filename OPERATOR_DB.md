# Indonesia LTE Operator Database

**Source**: `lib/examples/lte_scan.cc`  
**MCC**: 510 (Indonesia)  
**Last Updated**: September 2026  
**Accuracy**: Based on Kominfo spectrum allocation & 3GPP TS 36.101

---

## Quick Reference Table

| Operator | Short Name | MNC | Bands Active |
|----------|-----------|-----|--------------|
| Telkomsel | TSel | 10 | 3, 5*, 8, 28, 40 |
| XL Axiata | XL | 11 | 3, 8, 28 |
| Indosat Ooredoo Hutchison (IOH) | IOH | 1 | 3, 8, 28 |
| Smartfren | Smartfren | 9, 28 | 5, 40 |

> *Band 5 only Smartfren in Indonesia  
> **Note**: Post-merger (Jan 2022), Indosat & Tri spectrum has been refarmed under single IOH entity

---

## Band-by-Band EARFCN Mapping

### Band 3 (1800 MHz) — DL: 1805-1880 MHz

| EARFCN Range | F_DL (MHz) | MNC | Operator |
|--------------|------------|-----|----------|
| 1200-1399 | 1805-1824.9 | 10 | Telkomsel |
| 1400-1599 | 1825-1844.9 | 11 | XL Axiata |
| 1600-1949 | 1845-1884.9 | 1 | Indosat Ooredoo Hutchison |

**Formula**: `F_DL = 1805 + 0.1 × (EARFCN - 1200)` MHz

> **Note**: Post-merger, Indosat & Tri blocks consolidated under MNC 1 (IOH).

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
| 3450-3549 | 925-934.9 | 10 | Telkomsel |
| 3550-3649 | 935-944.9 | 11 | XL Axiata |
| 3650-3799 | 945-959.9 | 1 | Indosat Ooredoo Hutchison |

**Formula**: `F_DL = 925 + 0.1 × (EARFCN - 3450)` MHz

> **Note**: Post-merger, Indosat & Tri blocks consolidated under MNC 1.

---

### Band 28 (700 MHz) — DL: 758-803 MHz

| EARFCN Range | F_DL (MHz) | MNC | Operator |
|--------------|------------|-----|----------|
| 9210-9359 | 758-772.9 | 10 | Telkomsel |
| 9360-9509 | 773-787.9 | 1 | Indosat Ooredoo Hutchison |
| 9510-9649 | 788-802.9 | 11 | XL Axiata |

**Formula**: `F_DL = 758 + 0.1 × (EARFCN - 9210)` MHz

> **⚠️ IMPORTANT**: The official 3GPP offset for Band 28 is **9210**, NOT 9000. Using incorrect offset will cause frequency calculation errors.

---

### Band 40 (2300 MHz TDD) — TDD Uplink/Downlink

| EARFCN Range | F_DL (MHz) | MNC | Operator |
|--------------|------------|-----|----------|
| 38650-39049 | 2300-2339.9 | 10 | Telkomsel |
| 39050-39649 | 2340-2399.9 | 9, 28 | Smartfren |

> **⚠️ CRITICAL**: Band 40 is **EXCLUSIVELY** owned by Telkomsel and Smartfren in Indonesia. XL Axiata, Indosat Ooredoo, and Hutchison 3 **DO NOT** have spectrum in this band.

**Formula**: `F_DL = 2300 + 0.1 × (EARFCN - 38650)` MHz (TDD)

---

## MNC Reference

| MNC | Operator | Notes |
|-----|----------|-------|
| 10 | Telkomsel | Largest operator in Indonesia |
| 11 | XL Axiata | Includes former AXIS brand (now merged) |
| 1 | Indosat Ooredoo Hutchison | Post-merger entity (Jan 2022), combines former Indosat & Tri |
| 9 | Smartfren | CDMA-to-LTE refarmed, Band 5 exclusive |
| 28 | Smartfren | TDD operations on Band 40 |

### Deprecated MNCs (Historical Reference)

| MNC | Former Operator | Status |
|-----|----------------|--------|
| 21 | Indosat (old) | Merged into IOH (MNC 1) |
| 89 | Hutchison 3 / Tri | Spectrum merged into IOH (MNC 1) |
| 8 | AXIS (former) | Fully merged into XL Axiata (MNC 11) |

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
lte_scan_lookup_operator(1250) → {"Telkomsel", "Band 3", MNC 10}

// Example: EARFCN 2500 → Smartfren (Band 5)
lte_scan_lookup_operator(2500) → {"Smartfren", "Band 5", MNC 9}

// Example: EARFCN 3600 → XL Axiata (Band 8)
lte_scan_lookup_operator(3600) → {"XL Axiata", "Band 8", MNC 11}

// Example: EARFCN 9360 → IOH (Band 28)
lte_scan_lookup_operator(9360) → {"Indosat Ooredoo Hutchison", "Band 28", MNC 1}

// Example: EARFCN 39100 → Smartfren (Band 40)
lte_scan_lookup_operator(39100) → {"Smartfren", "Band 40", MNC 9/28}
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
| 2026-09-06 | Final correction: Telkomsel MNC 10, XL MNC 11, IOH MNC 1 | Muhammad Yunus |
| 2026-09-06 | Fixed Band 40 exclusion, Band 28 formula offset | Muhammad Yunus |
| 2026-09-05 | Fixed Band 5 mapping (removed incorrect Telkomsel entry) | Muhammad Yunus |
| 2026-09-01 | Added Indonesia operator database documentation | Muhammad Yunus |
