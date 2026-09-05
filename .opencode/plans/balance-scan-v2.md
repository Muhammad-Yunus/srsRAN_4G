# LTE Scan Balance Mode Implementation

## Goal
Balance scan = Fast scan (PSS step=50) + Fine scan (full MIB decode) per candidate. Simple and working.

## Current Problem
- PSS scan di step 50 berhasil menemukan cell (PSR > 2.0)
- Tapi MIB decode setelahnya selalu gagal karena timing/CFO drift
- Fast scan bekerja karena PSS + MIB di EARFCN yang SAMA tanpa delay

## Solution: Simplified Balance Mode

### Phase 1: Fast Scan (PSS only, step 50)
- Loop setiap 50 EARFCN (sama seperti fast mode)
- Kumpulkan candidate EARFCN yang ketemu PSS
- Jangan stop stream, continue streaming

### Phase 2: Fine Scan (full decode per candidate)
- Untuk setiap candidate, panggil `scan_earfcn()` yang sama dengan full scan
- `scan_earfcn()` sudah include PSS + MIB + SIB1 (working version)
- Gabung hasil dari kedua phase

## Implementation Plan

### File: `lib/examples/lte_scan.cc`

**Replace `lte_scan_balance()` function:**
```c
int lte_scan_balance(lte_scan_t* scan, int band, int earfcn_start, int earfcn_end)
{
    if (!scan->rf_handle) return -1;

    srsran_rf_t* rf = (srsran_rf_t*)scan->rf_handle;
    srsran_earfcn_t channels[LTE_SCAN_MAX_EARFCN];
    int step = 50; // 5 MHz spacing

    int nof_freqs = srsran_band_get_fd_band(band, channels, earfcn_start, earfcn_end, LTE_SCAN_MAX_EARFCN);
    if (nof_freqs <= 0) {
        printf("[lte_scan] No EARFCNs for band %d\n", band);
        return -1;
    }

    printf("[lte_scan] Balance scan Band %d (fast scan step=50, then fine)\n", band);
    printf("[lte_scan] Probing %d EARFCNs with step=%d (%.1f - %.1f MHz)\n",
           nof_freqs, step, channels[0].fd, channels[nof_freqs - 1].fd);

    /* Phase 1: Fast scan (PSS only) to find candidates */
    printf("[lte_scan] Phase 1: Fast scan (PSS only)...\n");
    srsran_rf_set_rx_srate(rf, (double)SAMP_FREQ);
    srsran_rf_set_rx_freq(rf, 0, (double)channels[0].fd * MHZ);
    srsran_rf_start_rx_stream(rf, false);

    srsran_ue_cellsearch_t cs;
    if (srsran_ue_cellsearch_init(&cs, cell_detect_config.max_frames_pss, recv_wrapper, (void*)rf)) {
        srsran_rf_stop_rx_stream(rf);
        return -1;
    }
    srsran_ue_cellsearch_set_nof_valid_frames(&cs, cell_detect_config.nof_valid_pss_frames);

    /* Collect candidate EARFCNs */
    int candidates[20];
    int nof_candidates = 0;

    for (int i = 0; i < nof_freqs && !scan->stop && nof_candidates < 20; i += step) {
        srsran_rf_set_rx_freq(rf, 0, (double)channels[i].fd * MHZ);

        srsran_ue_cellsearch_result_t found_cells[3];
        memset(found_cells, 0, sizeof(found_cells));
        int n = srsran_ue_cellsearch_scan(&cs, found_cells, NULL);

        if (n > 0) {
            for (int j = 0; j < 3 && nof_candidates < 20; j++) {
                if (found_cells[j].psr > scan->cfg.psr_threshold) {
                    printf("  EARFCN %d PCI %d PSR %.1f\n", channels[i].id, found_cells[j].cell_id, found_cells[j].psr);
                    candidates[nof_candidates++] = channels[i].id;
                    break;
                }
            }
        }
    }

    srsran_ue_cellsearch_free(&cs);
    srsran_rf_stop_rx_stream(rf);

    printf("[lte_scan] Phase 1: %d candidate(s) found\n", nof_candidates);

    /* Phase 2: Fine scan (full decode) per candidate */
    scan->nof_results = 0;
    if (nof_candidates > 0) {
        printf("[lte_scan] Phase 2: Fine scan %d cell(s)...\n", nof_candidates);
        for (int k = 0; k < nof_candidates && !scan->stop; k++) {
            printf("  [%d/%d] EARFCN %d ... ", k + 1, nof_candidates, candidates[k]);
            fflush(stdout);

            /* Use scan_earfcn() - same as full scan */
            float freq;
            earfcn_to_freq(candidates[k], &freq);
            if (scan_earfcn(rf, candidates[k], freq,
                           &scan->results[scan->nof_results], &scan->cfg, 0.0f) > 0) {
                lte_scan_result_t* r = &scan->results[scan->nof_results];
                printf("OK | PCI %d | %d PRB | %s\n", r->pci, r->nof_prb, r->operator_name);
                scan->nof_results++;
            } else {
                printf("MIB failed\n");
            }
        }
    }

    printf("[lte_scan] Balance scan done: %d cell(s) found on Band %d\n",
           scan->nof_results, band);
    return scan->nof_results;
}
```

### Key Changes:
1. Phase 1: PSS scan step=50, collect candidates
2. Phase 2: Full scan (PSS + MIB + SIB1) per candidate using `scan_earfcn()`
3. No manual MIB decode - reuse working code
4. Result = union of both phases

## Test Plan
```bash
# Build
make -j2 lte_scan_example

# Test
./lib/examples/lte_scan_example -b 8 -m
```

## Expected Result
- Should find same cells as fast scan + some with MIB info
- Time: faster than full scan, slower than fast scan
- Output: earfcn, pci, prb, operator, plmn
