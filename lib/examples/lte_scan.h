/**
 * lte_scan.h - LTE Cell Scanner with Operator Identification for Indonesia
 *
 * Scans LTE cells using srsRAN + RTL-SDR (or any SDR supported by SoapySDR).
 * Identifies operators via EARFCN→operator mapping table for Indonesia.
 *
 * 2-step workflow (fast):
 *   lte_scan_t scan;
 *   lte_scan_init(&scan, NULL, NULL);
 *
 *   // Step 1: coarse scan — PSS only, ~30s per band
 *   int n = lte_scan_coarse(&scan, 8, -1, -1);
 *
 *   // Step 2: fine scan — MIB + operator for each EARFCN found
 *   for (int i = 0; i < scan.nof_coarse; i++) {
 *       lte_scan_fine(&scan, scan.coarse_earfcns[i]);
 *   }
 *
 *   for (int i = 0; i < scan.nof_results; i++) {
 *       printf("%s\n", scan.results[i].operator_name);
 *   }
 *   lte_scan_free(&scan);
 *
 * One-step workflow (slow, scans every EARFCN):
 *   lte_scan_band(&scan, 8, -1, -1);
 */

#ifndef LTE_SCAN_H
#define LTE_SCAN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LTE_SCAN_MAX_RESULTS   256
#define LTE_SCAN_MAX_EARFCN    2000
#define LTE_SCAN_MAX_BANDS     16
#define LTE_SCAN_OP_NAME_LEN   64

/* Operator info from EARFCN lookup */
typedef struct {
    uint16_t    earfcn_min;
    uint16_t    earfcn_max;
    uint16_t    mcc;
    uint16_t    mnc;
    bool        mnc_3digit;
    const char* operator_name;
    const char* band_name;
} lte_operator_entry_t;

/* Single cell scan result */
typedef struct {
    /* RF measurements */
    int         earfcn;
    float       freq_mhz;
    int         pci;
    int         nof_prb;
    int         nof_ports;
    float       rsrp_dbm;
    float       psr;
    float       cfo_hz;

    /* Operator identification (from EARFCN table or SIB1) */
    uint16_t    mcc;
    uint16_t    mnc;
    bool        mnc_3digit;
    char        operator_name[LTE_SCAN_OP_NAME_LEN];
    bool        from_sib1;  /* true if MCC/MNC decoded from SIB1, false if from table */

    /* SIB1 data (if decoded) */
    uint32_t    tac;
    uint32_t    cell_id;
    bool        sib1_decoded;
} lte_scan_result_t;

/* Scanner configuration */
typedef struct {
    const char* rf_device;      /* SoapySDR device string, e.g. "driver=rtlsdr" */
    const char* rf_args;        /* Device arguments */
    float       rf_gain_dB;     /* RX gain in dB */
    uint32_t    max_prb_sib1;   /* Max PRBs for SIB1 decode attempt (RTL-SDR: 15) */
    float       psr_threshold;  /* Minimum PSR to consider a cell valid (default: 2.0) */
    bool        try_sib1;       /* Attempt SIB1 decode (slow, rarely works on RTL-SDR) */
} lte_scan_config_t;

/* Scanner state */
typedef struct {
    void*       rf_handle;
    lte_scan_result_t results[LTE_SCAN_MAX_RESULTS];
    int         nof_results;
    int         coarse_earfcns[LTE_SCAN_MAX_EARFCN];
    float       coarse_freqs[LTE_SCAN_MAX_EARFCN];
    float       coarse_power[LTE_SCAN_MAX_EARFCN];
    int         nof_coarse;
    lte_scan_config_t cfg;
    volatile bool stop;
} lte_scan_t;

/* Operator table for Indonesia */
extern const lte_operator_entry_t lte_operator_table_id[];
extern const int lte_operator_table_id_size;

/**
 * Initialize scanner with default config (RTL-SDR, gain 42dB).
 * Returns 0 on success, -1 on error.
 */
int lte_scan_init(lte_scan_t* scan, const char* rf_device, const char* rf_args);

/**
 * Initialize scanner with custom config.
 */
int lte_scan_init_ex(lte_scan_t* scan, const lte_scan_config_t* cfg);

/**
 * Scan a specific band (slow — every EARFCN).
 * @param band      Band number (3, 5, 8, 40, etc.)
 * @param earfcn_start  Start EARFCN (-1 = band default)
 * @param earfcn_end    End EARFCN (-1 = band default)
 * @return Number of cells found, or -1 on error.
 */
int lte_scan_band(lte_scan_t* scan, int band, int earfcn_start, int earfcn_end);

/**
 * Coarse scan — fast PSS-only scan across band.
 * Populates scan->coarse_earfcns[] with EARFCNs that have cells.
 * @return Number of EARFCNs with cells found.
 */
int lte_scan_coarse(lte_scan_t* scan, int band, int earfcn_start, int earfcn_end);

/**
 * Fine scan — decode MIB + operator lookup for a specific EARFCN.
 * Call this after lte_scan_coarse() for each EARFCN you want to identify.
 * @return 1 if cell decoded, 0 if no cell, -1 on error.
 */
int lte_scan_fine(lte_scan_t* scan, int earfcn);

/**
 * Fast scan — PSS only + operator table lookup (no MIB decode).
 * ~1s per EARFCN. Results have PCI + operator but no PRB/ports.
 * @return Number of cells found.
 */
int lte_scan_fast(lte_scan_t* scan, int band, int earfcn_start, int earfcn_end);

/**
 * Scan a single EARFCN (PSS + MIB + operator).
 * @return 1 if cell found, 0 if no cell, -1 on error.
 */
int lte_scan_earfcn(lte_scan_t* scan, int earfcn);

/**
 * Stop ongoing scan (can be called from signal handler or another thread).
 */
void lte_scan_stop(lte_scan_t* scan);

/**
 * Free scanner resources.
 */
void lte_scan_free(lte_scan_t* scan);

/**
 * Lookup operator info from EARFCN.
 * Returns entry from table, or NULL if not found.
 */
const lte_operator_entry_t* lte_scan_lookup_operator(int earfcn);

/**
 * Get human-readable string for a scan result.
 * Output is written to buf (at least 256 bytes).
 */
void lte_scan_result_str(const lte_scan_result_t* r, char* buf, int buflen);

#ifdef __cplusplus
}
#endif

#endif /* LTE_SCAN_H */
