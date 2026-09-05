# Plan: Fix Balance Mode - PSS Detection Threshold

## Problem
Balance scan with step=1 missed Hutchison 3 cells that always appear in Fast scan (step=50).

## Root Cause Analysis
| Config Parameter | Fast Mode | Balance Mode | Issue |
|-----------------|-----------|--------------|-------|
| `max_frames_pss` | 3 | 5 | Balance needs more PSS frames |
| `nof_valid_pss_frames` | **3** | **5** ⚠️ | Balance requires 5 valid frames but only ~100ms per frequency |
| Step | 50 | 1 | Balance scans 350x more frequencies |

**Result**: With step=1, each frequency gets only ~100ms. Need 5 valid PSS frames but only 3 fit in time window → detection fails for weaker signals (Hutchison 3 at -18 dBm).

## Solution

### Change Balance Config
File: `lib/examples/lte_scan.cc`, lines 145-151

```c
// SEBELUM
static const cell_search_cfg_t cell_detect_config_balance = {
    .max_frames_pbch      = 200,      /* 200 frames = 1s */
    .max_frames_pss       = 5,        /* 5 frames = 25ms */
    .nof_valid_pss_frames = 5,        /* require 5 valid frames */
    ...
};

// SESUDAH
static const cell_search_cfg_t cell_detect_config_balance = {
    .max_frames_pbch      = 50,          /* Same as fast: 250ms */
    .max_frames_pss       = 3,           /* Same as fast: 15ms */
    .nof_valid_pss_frames = 3,           /* Same as fast: 3 valid frames */
    ...
};
```

### Expected Impact
- Balance will now use same PSS detection sensitivity as Fast
- With step=1, should detect ALL cells Fast finds + additional ones
- Execution time: ~16 seconds (350 frequencies × ~45ms each)

## Build Command
```bash
cd /home/pi/srsRAN_4G/build
cmake --build . --target lte_scan_example -j$(nproc)
```

## Test Command
```bash
lte-scan balance 8 --json --gain 43
```

## Verification Checklist
- [ ] Hutchison 3 cells detected at EARFCN 3700, 3750
- [ ] All Fast mode cells also detected in Balance
- [ ] Balance finds additional cells Fast misses (due to step=1)
- [ ] Execution time ~15-20 seconds
