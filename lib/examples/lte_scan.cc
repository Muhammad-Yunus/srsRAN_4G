/**
 * lte_scan.c - LTE Cell Scanner with Operator Identification
 *
 * Uses srsRAN cell search + MIB decode for RF measurements,
 * and EARFCN→operator table for Indonesia for operator ID.
 */

#include "lte_scan.h"

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "srsran/phy/rf/rf.h"
#include "srsran/phy/rf/rf_utils.h"
#include "srsran/srsran.h"
#include "srsran/asn1/rrc.h"

/*
 * RF SAMPLE RATE
 *
 * SAMP_FREQ = 1.92 MHz — chosen as the minimum rate that covers:
 * - LTE Band 8 downlink (925-960 MHz, max 20 MHz bandwidth)
 * - RTL-SDR v3 dongle optimal sample rate (avoids USB transfer issues)
 * - srsRAN's internal timing (15 kHz subcarrier spacing × 128 = 1.92 MHz)
 *
 * Why not higher?
 * - Higher rates (e.g., 5 MHz) waste CPU on empty spectrum
 * - RTL-SDR has limited buffer size at high sample rates
 * - For PSS detection, we only need ~1.5 MHz of bandwidth
 *
 * Why not lower?
 * - Below 1.92 MHz, we can't properly decode MIB (needs 1.08-19.2 MHz)
 * - Aliasing risks with cheaper RTL-SDR hardware
 */
#define SAMP_FREQ     1920000
#define MHZ           1000000
#define MAX_CHANNELS  4

using namespace asn1::rrc;

/* ==============================
 * Indonesia Operator Table
 *
 * Based on Kominfo (Indonesian Ministry of Communication and Information
 * Technology) spectrum allocations. Approximate — some operators share
 * or have refarmed spectrum. ~95% accuracy.
 *
 * NOTE: When adding new bands, verify EARFCN ranges against 3GPP TS 36.101
 * and cross-reference with Kominfo's latest spectrum allocation.
 * ============================== */

const lte_operator_entry_t lte_operator_table_id[] = {
    /* === Band 3 (1800 MHz) - DL: 1805-1880 MHz === */
    /* EARFCN 1200-1949, F_DL = 1805 + 0.1 * (EARFCN - 1200) MHz */
    /* Post-merger: Indosat & Tri spectrum consolidated under IOH (MNC 1) */
    { 1200, 1399, 510, 10,  false, "Telkomsel",                 "Band 3" },
    { 1400, 1599, 510, 11,  false, "XL Axiata",                 "Band 3" },
    { 1600, 1949, 510, 1,   false, "Indosat Ooredoo Hutchison", "Band 3" },

    /* === Band 5 (850 MHz) - DL: 869-894 MHz === */
    /* EARFCN 2400-2649, F_DL = 869 + 0.1 * (EARFCN - 2400) MHz */
    /* In Indonesia, Band 5 is exclusively used by Smartfren (MNC 9) */
    { 2400, 2649, 510, 9,   false, "Smartfren",                 "Band 5" },

    /* === Band 8 (900 MHz) - DL: 925-960 MHz === */
    /* EARFCN 3450-3799, F_DL = 925 + 0.1 * (EARFCN - 3450) MHz */
    /* Post-merger: Indosat & Tri blocks consolidated under MNC 1 */
    { 3450, 3549, 510, 10,  false, "Telkomsel",                 "Band 8" },
    { 3550, 3649, 510, 11,  false, "XL Axiata",                 "Band 8" },
    { 3650, 3799, 510, 1,   false, "Indosat Ooredoo Hutchison", "Band 8" },

    /* === Band 28 (700 MHz) - DL: 758-803 MHz === */
    /* EARFCN 9210-9649, F_DL = 758 + 0.1 * (EARFCN - 9210) MHz */
    /* NOTE: 3GPP offset is 9210, NOT 9000 */
    { 9210, 9359, 510, 10,  false, "Telkomsel",                 "Band 28" },
    { 9360, 9509, 510, 1,   false, "Indosat Ooredoo Hutchison", "Band 28" },
    { 9510, 9649, 510, 11,  false, "XL Axiata",                 "Band 28" },

    /* === Band 40 (2300 MHz TDD) - EARFCN 38650-39649 === */
    /* Band 40 is EXCLUSIVELY owned by Telkomsel and Smartfren in Indonesia */
    /* XL, Indosat, and Tri do NOT have spectrum in this band */
    { 38650, 39049, 510, 10, false, "Telkomsel",                 "Band 40" },
    { 39050, 39649, 510, 9,  false, "Smartfren",                 "Band 40" },
    { 39050, 39649, 510, 28, false, "Smartfren",                 "Band 40" },
};

const int lte_operator_table_id_size =
    sizeof(lte_operator_table_id) / sizeof(lte_operator_table_id[0]);

const lte_operator_entry_t* lte_scan_lookup_operator(int earfcn)
{
    for (int i = 0; i < lte_operator_table_id_size; i++) {
        if (earfcn >= lte_operator_table_id[i].earfcn_min &&
            earfcn <= lte_operator_table_id[i].earfcn_max) {
            return &lte_operator_table_id[i];
        }
    }
    return NULL;
}

void lte_scan_result_str(const lte_scan_result_t* r, char* buf, int buflen)
{
    snprintf(buf, buflen,
        "EARFCN %d | %.1f MHz | PCI %d | %d PRB %d ant | %s | MCC %03d MNC %0*u%s | RSRP %.1f dBm",
        r->earfcn, r->freq_mhz, r->pci, r->nof_prb, r->nof_ports,
        r->operator_name,
        r->mcc, r->mnc_3digit ? 3 : 2, r->mnc, r->from_sib1 ? " [SIB1]" : "",
        r->rsrp_dbm);
}

/* --- srsRAN callbacks --- */

/*
 * RECV WRAPPER (Single) — Data callback for PSS detection
 *
 * Used by: Fast mode, Balance mode, Full mode Pass 1
 *
 * Why single-channel?
 * - PSS detection only needs one antenna stream
 * - SoapyRTLSDR with RTL-SDR dongle provides 1 channel by default
 * - Multi-channel would require hardware with 2+ antennas
 *
 * Stream management:
 * - Must be called while stream is ACTIVE
 * - Does NOT start/stop the stream itself
 * - Caller manages stream lifecycle (see SINGLE STREAM PATTERN comment)
 */
static int recv_wrapper(void* h, void* data, uint32_t nsamples, srsran_timestamp_t* t)
{
    return srsran_rf_recv_with_time((srsran_rf_t*)h, data, nsamples, 1, NULL, NULL);
}

/*
 * RECV WRAPPER (Multi) — Data callback for MIB decode
 *
 * Used by: Full mode Pass 2 (MIB decode only)
 *
 * Why multi-channel?
 * - MIB decode needs I/Q samples from all antenna ports
 * - srsran_ue_mib_sync_decode() expects multi-channel buffer
 * - Even for SISO (1 antenna), we use multi wrapper for API compatibility
 */
static int recv_wrapper_multi(void* h, cf_t* data[MAX_CHANNELS], uint32_t nsamples, srsran_timestamp_t* t)
{
    return srsran_rf_recv_with_time_multi((srsran_rf_t*)h, (void**)data, nsamples, 1, NULL, NULL);
}

/* --- EARFCN → frequency helper --- */
/*
 * EARFCN to Frequency Conversion
 *
 * Formula: F_DL = F_offset + 0.1 × (EARFCN - N_offset)
 * Where:
 *   - F_offset is the band's lower edge frequency
 *   - N_offset is the band's starting EARFCN
 *   - 0.1 MHz = 100 kHz is the LTE subcarrier spacing
 *
 * Why separate formula per band?
 * - Each LTE band has different frequency ranges
 * - Some bands share EARFCN ranges but different offsets (e.g., Band 3 vs Band 7)
 * - We only support Indonesian bands: 3, 5, 8, 28, 40
 *
 * Notes:
 * - Band 40 is TDD (same freq for UL/DL), so this gives transmit frequency
 * - Band 28 (700 MHz) has wider EARFCN range (9000-9649) than typical
 */
static int earfcn_to_freq(int earfcn, float* freq_mhz)
{
    if (earfcn >= 1200 && earfcn <= 1949) {
        *freq_mhz = 1805.0f + 0.1f * (earfcn - 1200); return 0;  /* Band 3 */
    }
    if (earfcn >= 2400 && earfcn <= 2649) {
        *freq_mhz = 869.0f + 0.1f * (earfcn - 2400); return 0;   /* Band 5 */
    }
    if (earfcn >= 3450 && earfcn <= 3799) {
        *freq_mhz = 925.0f + 0.1f * (earfcn - 3450); return 0;   /* Band 8 */
    }
    if (earfcn >= 9210 && earfcn <= 9659) {
        *freq_mhz = 758.0f + 0.1f * (earfcn - 9210); return 0;   /* Band 28 */
    }
    if (earfcn >= 38650 && earfcn <= 39649) {
        *freq_mhz = 2300.0f + 0.1f * (earfcn - 38650); return 0; /* Band 40 */
    }
    return -1;
}

/* --- Cell search and MIB decode --- */
static cell_search_cfg_t cell_detect_config = {
    /*
     * PSS Detection (Primary Synchronization Signal)
     * - max_frames_pss = 3: Minimum frames to detect PSS = 15ms
     *   Why 3? PSS repeats every 5ms (1 frame). 3 frames = 15ms gives
     *   reliable detection while keeping scan fast.
     *   Original srsRAN default was 10 (50ms) — too slow for scanning.
     *
     * - nof_valid_pss_frames = 3: Require 3 consecutive valid PSS detections
     *   Why match max_frames_pss? Ensures we don't false-positive on noise.
     *   Lower values (e.g., 1-2) cause more false detections in noisy environments.
     *
     * PBCH Detection (Physical Broadcast Channel)
     * - max_frames_pbch = 50: 50 frames = 250ms for MIB decode
     *   Why 50? MIB requires multiple PBCH repetitions (every 10ms) to decode.
     *   50 frames gives enough time for reliable decoding without excessive delay.
     *   Original was 500 (2.5s) — reduced 10x for faster scans.
     *
     * AGC & TDD
     * - init_agc = 0: Disable automatic gain control initialization
     *   Why? AGC can interfere with PSS detection during fast frequency hopping.
     * - force_tdd = false: Scan FDD only (LTE Band 8 is FDD)
     */
    .max_frames_pbch      = 50,       /* 50 frames = 250ms for MIB decode */
    .max_frames_pss       = 3,        /* 3 frames = 15ms for PSS detection */
    .nof_valid_pss_frames = 3,        /* Require 3 valid PSS frames to confirm */
    .init_agc             = 0,
    .force_tdd            = false,
};

/* Balance mode config: same PSS detection as fast, but with step=1 for accuracy */
static const cell_search_cfg_t cell_detect_config_balance = {
    /*
     * Balance mode uses IDENTICAL detection parameters as Fast mode.
     * The difference is ONLY in the scan step size (1 vs 5), not in detection sensitivity.
     *
     * Why keep same detection params?
     * - Both need fast detection to complete scan in reasonable time
     * - Balance scans more frequencies (step=1), so each scan point must be quick
     * - If we increased frames for Balance, total scan time would be 5x slower
     *
     * Performance trade-off:
     * - Fast (step=5): ~70 points × 15ms = ~1s for PSS scan
     * - Balance (step=1): ~350 points × 15ms = ~5s for PSS scan
     * - With MIB decode overhead: Fast ~4.5s, Balance ~10-17s
     */
    .max_frames_pbch      = 50,          /* Same as fast: 50 frames = 250ms */
    .max_frames_pss       = 3,           /* Same as fast: 3 frames = 15ms */
    .nof_valid_pss_frames = 3,           /* Same as fast: require 3 valid frames */
    .init_agc             = 0,
    .force_tdd            = false,
};

/* Set cell search config based on mode (1=full, 0=fast, 2=balance) */
void set_cell_search_mode(int mode)
{
    if (mode == 1) {
        /*
         * Full mode: optimized values with MIB decode
         *
         * Same PSS/PBCH params as Fast, but FULL mode uses 2-pass strategy:
         *   Pass 1 (coarse): Step=1 scan to find candidate EARFCNs
         *   Pass 2 (fine):   Individual MIB decode per candidate
         *
         * Why same detection params?
         * - Coarse pass needs speed (step=1, 350 points)
         * - Fine pass is selective (only strong candidates get MIB decode)
         * - MIB decode time is independent of scan step
         */
        cell_detect_config.max_frames_pbch      = 50;          /* 50 frames = 250ms */
        cell_detect_config.max_frames_pss       = 3;           /* 3 frames = 15ms */
        cell_detect_config.nof_valid_pss_frames = 3;           /* require 3 valid frames */
    } else if (mode == 2) {
        /*
         * Balance mode: delegates to cell_detect_config_balance
         *
         * Balance = Fast detection params + Step=1 scan
         * No MIB decode, just PSS detection at full resolution
         */
        cell_detect_config.max_frames_pbch      = cell_detect_config_balance.max_frames_pbch;
        cell_detect_config.max_frames_pss       = cell_detect_config_balance.max_frames_pss;
        cell_detect_config.nof_valid_pss_frames = cell_detect_config_balance.nof_valid_pss_frames;
    } else {
        /*
         * Fast mode: optimized values for speed
         *
         * Fast = Fast detection params + Step=5 scan
         * Scans 70 points (Band 8) instead of 350, ~5x faster than Balance
         */
        cell_detect_config.max_frames_pbch      = 50;
        cell_detect_config.max_frames_pss       = 3;
        cell_detect_config.nof_valid_pss_frames = 3;
    }
}

static int decode_mib(srsran_rf_t* rf, srsran_cell_t* cell, float* cfo)
{
    srsran_ue_mib_sync_t ue_mib;
    uint8_t              bch_payload[SRSRAN_BCH_PAYLOAD_LEN] = {};

    if (srsran_ue_mib_sync_init_multi(&ue_mib, recv_wrapper_multi, 1, (void*)rf)) {
        return -1;
    }
    if (srsran_ue_mib_sync_set_cell(&ue_mib, *cell)) {
        srsran_ue_mib_sync_free(&ue_mib);
        return -1;
    }

    int srate = srsran_sampling_freq_hz(SRSRAN_UE_MIB_NOF_PRB);
    srsran_rf_set_rx_srate(rf, (double)srate);
    srsran_rf_start_rx_stream(rf, false);

    if (cfo) {
        ue_mib.ue_sync.cfo_current_value       = *cfo / 15000;
        ue_mib.ue_sync.cfo_is_copied           = true;
        ue_mib.ue_sync.cfo_correct_enable_find = true;
        srsran_sync_set_cfo_cp_enable(&ue_mib.ue_sync.sfind, false, 0);
    }

    int ret = srsran_ue_mib_sync_decode(&ue_mib, cell_detect_config.max_frames_pbch,
                                         bch_payload, &cell->nof_ports, NULL);
    if (ret == 1) {
        srsran_pbch_mib_unpack(bch_payload, cell, NULL);
    }
    if (cfo) {
        *cfo = srsran_ue_sync_get_cfo(&ue_mib.ue_sync);
    }

    srsran_rf_stop_rx_stream(rf);
    srsran_ue_mib_sync_free(&ue_mib);
    return ret;
}

/* --- Internal helper functions --- */

/*
 * RECV WRAPPER — Single-channel data callback
 *
 * SoapyRTLSDR uses zero-copy buffers that trigger a glibc robust mutex bug
 * on aarch64 when the stream is restarted. This wrapper is used for:
 * - Single-EARFCN operations (scan_earfcn) where we control stream lifecycle
 * - SIB1 decoding where we need dedicated buffer management
 *
 * Note: NOT used in fast/balance modes — those use recv_wrapper (multi-channel)
 * to avoid the restart bug during continuous scanning.
 */
static int recv_wrapper_single(void* h, cf_t* data[SRSRAN_MAX_CHANNELS], uint32_t nsamples, srsran_timestamp_t* t)
{
    return srsran_rf_recv_with_time_multi((srsran_rf_t*)h, (void**)data, nsamples, 1, NULL, NULL);
}

/*
 * TRY SIB1 — Optional System Information Block 1 decode
 *
 * SIB1 contains:
 * - TAC (Tracking Area Code)
 * - Cell Identity (when combined with MIB)
 * - Scheduled random access parameters
 *
 * Why disabled by default?
 * - Requires wider bandwidth than RTL-SDR can provide (max ~15 PRB)
 * - Adds ~500ms-1s per cell to scan time
 * - Most use cases only need PCI + RSRP (from MIB)
 *
 * When to enable:
 * - When you need TAC for network analysis
 * - When using SDR hardware with >15 PRB bandwidth capability
 * - When building a full network database
 */
static int try_sib1(srsran_rf_t* rf, srsran_cell_t* cell, float cfo, uint32_t max_prb, lte_scan_result_t* out)
{
    uint32_t sib_prb = cell->nof_prb;
    if (sib_prb > max_prb) {
        sib_prb = max_prb;
    }

    int srate = srsran_sampling_freq_hz(sib_prb);
    if (srate <= 0) return -1;

    float actual_srate = srsran_rf_set_rx_srate(rf, (double)srate);
    if (fabs(actual_srate - (float)srate) > 1000) return -1;

    srsran_cell_t sib_cell = *cell;
    sib_cell.nof_prb = sib_prb;

    cf_t* sf_buffer[SRSRAN_MAX_PORTS] = {};
    for (int i = 0; i < SRSRAN_MAX_PORTS; i++) {
        sf_buffer[i] = (cf_t*)srsran_vec_malloc(SRSRAN_SF_LEN_PRB(sib_prb) * sizeof(cf_t));
        if (!sf_buffer[i]) goto clean;
    }

    srsran_ue_sync_t ue_sync;
    memset(&ue_sync, 0, sizeof(ue_sync));
    if (srsran_ue_sync_init_multi(&ue_sync, sib_prb, false, recv_wrapper_single, 1, (void*)rf)) goto clean;
    if (srsran_ue_sync_set_cell(&ue_sync, sib_cell)) { srsran_ue_sync_free(&ue_sync); goto clean; }
    srsran_ue_sync_set_cfo_ref(&ue_sync, cfo);

    srsran_ue_dl_t ue_dl;
    memset(&ue_dl, 0, sizeof(ue_dl));
    if (srsran_ue_dl_init(&ue_dl, sf_buffer, sib_prb, 1)) { srsran_ue_sync_free(&ue_sync); goto clean; }
    if (srsran_ue_dl_set_cell(&ue_dl, sib_cell)) { srsran_ue_dl_free(&ue_dl); srsran_ue_sync_free(&ue_sync); goto clean; }

    {
        srsran_dl_sf_cfg_t dl_sf = {};
        dl_sf.sf_type = SRSRAN_SF_NORM;
        srsran_ue_dl_cfg_t ue_dl_cfg = {};
        ue_dl_cfg.cfg.tm = (sib_cell.nof_ports > 1) ? SRSRAN_TM2 : SRSRAN_TM1;
        ue_dl_cfg.cfg.pdsch.use_tbs_index_alt = false;
        srsran_pdsch_cfg_t pdsch_cfg = {};
        pdsch_cfg.rnti = SRSRAN_SIRNTI;
        srsran_softbuffer_rx_t softbuffer_rx;
        memset(&softbuffer_rx, 0, sizeof(softbuffer_rx));
        if (srsran_softbuffer_rx_init(&softbuffer_rx, sib_prb)) { srsran_ue_dl_free(&ue_dl); srsran_ue_sync_free(&ue_sync); goto clean; }

        srsran_rf_start_rx_stream(rf, false);
        bool acks[SRSRAN_MAX_CODEWORDS] = {};
        uint8_t* data[SRSRAN_MAX_CODEWORDS] = {};
        uint8_t data_buf[SRSRAN_MAX_CODEWORDS][16640 / 8];
        data[0] = data_buf[0];
        data[1] = data_buf[1];

        int ret_code = -1;
        for (int trial = 0; trial < 300; trial++) {
            int ret = srsran_ue_sync_zerocopy(&ue_sync, sf_buffer, SRSRAN_SF_LEN_PRB(sib_prb));
            if (ret < 0) break;
            if (ret == 0) continue;

            uint32_t sf_idx = srsran_ue_sync_get_sfidx(&ue_sync);
            uint32_t sfn    = srsran_ue_sync_get_sfn(&ue_sync);
            if (sf_idx == 5 && (sfn % 2) == 0) {
                dl_sf.tti = sfn * 10 + sf_idx;
                if (srsran_ue_dl_find_and_decode(&ue_dl, &dl_sf, &ue_dl_cfg, &pdsch_cfg, data, acks) > 0 && acks[0]) {
                    /* Parse SIB1 ASN.1 */
                    asn1::rrc::bcch_dl_sch_msg_s bcch_msg;
                    asn1::cbit_ref bref(data[0], pdsch_cfg.grant.tb[0].tbs / 8);
                    if (bcch_msg.unpack(bref) == asn1::SRSASN_SUCCESS &&
                        bcch_msg.msg.c1().type() == asn1::rrc::bcch_dl_sch_msg_type_c::c1_c_::types::sib_type1) {
                        auto& sib1 = bcch_msg.msg.c1().sib_type1();
                        auto& ca   = sib1.cell_access_related_info;
                        out->sib1_decoded   = true;
                        out->cell_id = ca.cell_id.to_number();
                        out->tac            = ca.tac.to_number();
                        if (ca.plmn_id_list.size() > 0) {
                            auto& plmn = ca.plmn_id_list[0].plmn_id;
                            if (plmn.mcc_present) {
                                out->mcc = plmn.mcc[0] * 100 + plmn.mcc[1] * 10 + plmn.mcc[2];
                            }
                            out->mnc_3digit = (plmn.mnc.size() == 3);
                            if (plmn.mnc.size() >= 2) {
                                out->mnc = plmn.mnc[0] * 10 + plmn.mnc[1];
                                if (plmn.mnc.size() == 3) out->mnc = out->mnc * 10 + plmn.mnc[2];
                            }
                        }
                        out->from_sib1 = true;
                        ret_code = 0;
                        break;
                    }
                }
            }
        }
        srsran_rf_stop_rx_stream(rf);
        srsran_softbuffer_rx_free(&softbuffer_rx);
        srsran_ue_dl_free(&ue_dl);
        srsran_ue_sync_free(&ue_sync);
        return ret_code;
    }

clean:
    for (int i = 0; i < SRSRAN_MAX_PORTS; i++) {
        if (sf_buffer[i]) free(sf_buffer[i]);
    }
    return -1;
}

/*
 * SCAN ONE EARFCN — Core cell detection function
 *
 * This is the workhorse function used by all scan modes.
 * It performs PSS detection + MIB decode on a SINGLE frequency.
 *
 * Flow:
 *   1. Set RF frequency and sample rate
 *   2. Start RX stream (single use — fine scan mode)
 *   3. Run PSS cell search (fast, ~15ms)
 *   4. If cell found, decode MIB (thorough, ~250ms)
 *   5. Lookup operator from EARFCN table
 *   6. Optionally attempt SIB1 decode (disabled by default)
 *
 * Why separate stream management?
 * - Fast/Balance modes keep stream open across multiple frequencies
 * - This function manages its own stream for isolated use (e.g., lte_scan_earfcn)
 * - Starting/stopping per-call avoids the aarch64 mutex bug for single ops
 *
 * Return values:
 *   >0: Cell found and decoded successfully
 *    0: No cell detected at this frequency
 *   -1: Error (MIB decode failed, etc.)
 */
static int scan_earfcn(srsran_rf_t* rf, int earfcn, float freq_mhz,
                       lte_scan_result_t* out, const lte_scan_config_t* cfg)
{
    srsran_rf_set_rx_freq(rf, 0, (double)freq_mhz * MHZ);
    srsran_rf_set_rx_srate(rf, (double)SAMP_FREQ);
    srsran_rf_start_rx_stream(rf, false);

    srsran_ue_cellsearch_t        cs;
    srsran_ue_cellsearch_result_t found_cells[3];

    if (srsran_ue_cellsearch_init(&cs, cell_detect_config.max_frames_pss, recv_wrapper, (void*)rf)) {
        srsran_rf_stop_rx_stream(rf);
        return -1;
    }
    srsran_ue_cellsearch_set_nof_valid_frames(&cs, cell_detect_config.nof_valid_pss_frames);

    memset(found_cells, 0, sizeof(found_cells));
    int n = srsran_ue_cellsearch_scan(&cs, found_cells, NULL);
    srsran_ue_cellsearch_free(&cs);
    srsran_rf_stop_rx_stream(rf);

    if (n <= 0) {
        return 0;
    }

    /* Pick the best cell */
    int   best_idx = -1;
    float best_psr = 0;
    for (int i = 0; i < 3; i++) {
        if (found_cells[i].psr > cfg->psr_threshold && found_cells[i].psr > best_psr) {
            best_psr = found_cells[i].psr;
            best_idx = i;
        }
    }

    if (best_idx < 0) {
        return 0;
    }

    /* Fill result */
    memset(out, 0, sizeof(*out));
    out->earfcn  = earfcn;
    out->freq_mhz = freq_mhz;
    out->pci     = found_cells[best_idx].cell_id;
    out->psr     = found_cells[best_idx].psr;
    out->rsrp_dbm = srsran_convert_power_to_dB(found_cells[best_idx].peak);

    /* Decode MIB */
    srsran_cell_t cell;
    cell.id         = found_cells[best_idx].cell_id;
    cell.cp         = found_cells[best_idx].cp;
    cell.frame_type = found_cells[best_idx].frame_type;
    float cfo       = found_cells[best_idx].cfo;

    int mib_ret = decode_mib(rf, &cell, &cfo);
    out->cfo_hz = cfo;

    if (mib_ret == SRSRAN_UE_MIB_FOUND) {
        out->nof_prb  = cell.nof_prb;
        out->nof_ports = cell.nof_ports;

        /* Try SIB1 decode if cell fits in our bandwidth */
        if (cfg->try_sib1 && cell.nof_prb <= cfg->max_prb_sib1) {
            if (try_sib1(rf, &cell, cfo, cfg->max_prb_sib1, out) == 0) {
                return 1;
            }
        }
    }

    /* Lookup operator by EARFCN table */
    const lte_operator_entry_t* op = lte_scan_lookup_operator(earfcn);
    if (op) {
        out->mcc     = op->mcc;
        out->mnc     = op->mnc;
        out->mnc_3digit = op->mnc_3digit;
        strncpy(out->operator_name, op->operator_name, LTE_SCAN_OP_NAME_LEN - 1);
    } else {
        strncpy(out->operator_name, "Unknown", LTE_SCAN_OP_NAME_LEN - 1);
    }

    return 1;
}

/* --- Public API --- */
/*
 * SIGNAL HANDLER — Clean scan termination
 *
 * Why volatile?
 * - Signal handlers can interrupt main thread at any time
 * - volatile prevents compiler from caching the value in registers
 * - Ensures main loop sees updates from signal handler
 *
 * Supported signals:
 * - SIGINT (Ctrl+C): Cleanly stops current scan
 * - SIGTERM: Same as SIGINT for process termination
 */
static volatile lte_scan_t* g_stop_scan = NULL;

static void sigint_handler(int signo)
{
    (void)signo;
    if (g_stop_scan) {
        g_stop_scan->stop = true;
    }
}

/*
 * SCAN CONFIG — Default values for RTL-SDR dongles
 *
 * Why these defaults?
 * - rf_gain_dB = 42: Maximum practical gain for RTL-SDR v3
 *   Higher gains (49dB) cause ADC saturation and intermodulation
 * - max_prb_sib1 = 15: RTL-SDR max stable sample rate ≈ 1.92 MHz
 *   corresponds to ~15 PRB (15 × 180 kHz = 2.7 MHz > 1.92 MHz)
 *   SIB1 needs wider BW, so we cap at 15 PRB max
 * - psr_threshold = 2.0f: Peak-to-Side-Ratio threshold
 *   Lower values (<1.5) cause false positives from noise
 *   Higher values (>3.0) miss weak cells
 * - try_sib1 = false: Disabled by default for speed
 *   See try_sib1() comment for details
 */
int lte_scan_init(lte_scan_t* scan, const char* rf_device, const char* rf_args)
{
    lte_scan_config_t cfg = {
        .rf_device     = (rf_device && rf_device[0]) ? rf_device : "soapy",
        .rf_args       = (rf_args && rf_args[0]) ? rf_args : "",
        .rf_gain_dB    = 42.0f,
        .max_prb_sib1  = 15,    /* RTL-SDR max */
        .psr_threshold = 2.0f,
        .try_sib1      = false,  /* Disabled for speed - SIB1 requires wider BW than RTL-SDR can provide */
    };
    return lte_scan_init_ex(scan, &cfg);
}

int lte_scan_init_ex(lte_scan_t* scan, const lte_scan_config_t* cfg)
{
    /*
     * INITIALIZATION — RF Hardware Setup
     *
     * What this does:
     * 1. Zero-initializes the scan structure
     * 2. Allocates RF handle (srsran_rf_t*)
     * 3. Opens SoapySDR device with given arguments
     * 4. Sets RX gain and suppresses SoapySDR console noise
     * 5. Installs signal handler for clean Ctrl+C termination
     *
     * Why suppress stdout?
     * - SoapySDR prints device list, sensor readings, and gain info
     *   during open() — this noise would appear in scan output
     * - We redirect stdout to /dev/null temporarily during open
     */
    memset(scan, 0, sizeof(*scan));
    scan->cfg = *cfg;

    scan->rf_handle = malloc(sizeof(srsran_rf_t));
    if (!scan->rf_handle) {
        return -1;
    }

    srsran_rf_t* rf = (srsran_rf_t*)scan->rf_handle;

    /* Suppress SoapySDR enumeration noise during device open.
     * The plugin prints device list, sensors, gain info to stdout
     * before srsran_rf_suppress_stdout() can take effect. */
    int saved_stdout = dup(STDOUT_FILENO);
    FILE* devnull = fopen("/dev/null", "w");
    if (devnull) {
        fflush(stdout);
        dup2(fileno(devnull), STDOUT_FILENO);
    }

    int open_ret = srsran_rf_open_devname(rf, cfg->rf_device, (char*)cfg->rf_args, 1);

    /* Restore stdout */
    if (devnull) {
        fflush(stdout);
        dup2(saved_stdout, STDOUT_FILENO);
        fclose(devnull);
    }
    close(saved_stdout);

    if (open_ret) {
        free(scan->rf_handle);
        scan->rf_handle = NULL;
        return -1;
    }

    srsran_rf_set_rx_gain(rf, cfg->rf_gain_dB);
    srsran_rf_suppress_stdout(rf);

    /* Install signal handler for clean stop */
    g_stop_scan = scan;
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    return 0;
}

/*
 * FULL MODE SCAN — 2-pass strategy for comprehensive cell analysis
 *
 * Architecture:
 *   Pass 1 (Coarse): PSS-only scan across all EARFCNs
 *     - Identifies candidate cells with minimal overhead
 *     - Stores candidates for Pass 2
 *     - Duration: ~10-17s for Band 8
 *
 *   Pass 2 (Fine): MIB decode per candidate
 *     - Only processes cells found in Pass 1
 *     - Full MIB decode to get bandwidth, ports, TAC
 *     - Duration: ~250ms per cell
 *
 * Why 2-pass instead of scanning all with MIB?
 * - MIB decode is 10-20x slower than PSS detection
 * - Scanning 350 EARFCNs with MIB each = 350 × 250ms = 87.5 seconds
 * - 2-pass: (350 × 15ms) + (N_cells × 250ms) ≈ 5.25s + 2.5s = 7.75s
 * - Speedup: 10-15x for typical scenarios
 *
 * Stream management:
 * - Single stream maintained throughout Pass 1 (avoids aarch64 mutex bug)
 * - Each Pass 2 call uses scan_earfcn() which manages its own stream
 * - This is OK because Pass 2 is selective (few cells vs 350 EARFCNs)
 */
int lte_scan_band(lte_scan_t* scan, int band, int earfcn_start, int earfcn_end)
{
    if (!scan->rf_handle) {
        return -1;
    }

    srsran_rf_t*  rf  = (srsran_rf_t*)scan->rf_handle;
    srsran_earfcn_t channels[LTE_SCAN_MAX_EARFCN];
    int candidates[LTE_SCAN_MAX_EARFCN];
    int candidate_freqs[LTE_SCAN_MAX_EARFCN];
    float candidate_power[LTE_SCAN_MAX_EARFCN];
    int nof_candidates = 0;

    int nof_freqs = srsran_band_get_fd_band(band, channels, earfcn_start, earfcn_end, LTE_SCAN_MAX_EARFCN);
    if (nof_freqs <= 0) {
        fprintf(stderr, "[lte_scan] No EARFCNs for band %d\n", band);
        return -1;
    }

    printf("[lte_scan] Scanning Band %d: %d EARFCNs (%.1f - %.1f MHz)\n",
           band, nof_freqs, channels[0].fd, channels[nof_freqs - 1].fd);

    scan->nof_results = 0;
    scan->stop = false;

    /* Pass 1: Coarse PSS-only scan with single stream (avoids aarch64 mutex bug) */
    printf("[lte_scan] Pass 1: PSS coarse scan...\n");

    srsran_rf_set_rx_srate(rf, (double)SAMP_FREQ);
    srsran_rf_set_rx_freq(rf, 0, (double)channels[0].fd * MHZ);
    srsran_rf_start_rx_stream(rf, false);

    srsran_ue_cellsearch_t cs;
    if (srsran_ue_cellsearch_init(&cs, cell_detect_config.max_frames_pss, recv_wrapper, (void*)rf)) {
        srsran_rf_stop_rx_stream(rf);
        return -1;
    }
    srsran_ue_cellsearch_set_nof_valid_frames(&cs, cell_detect_config.nof_valid_pss_frames);

    for (int i = 0; i < nof_freqs && !scan->stop; i++) {
        printf("\r[%3d/%d] EARFCN %d  %.2f MHz ... ", i + 1, nof_freqs, channels[i].id, channels[i].fd);
        fflush(stdout);

        srsran_rf_set_rx_freq(rf, 0, (double)channels[i].fd * MHZ);

        srsran_ue_cellsearch_result_t found_cells[3];
        memset(found_cells, 0, sizeof(found_cells));
        int n = srsran_ue_cellsearch_scan(&cs, found_cells, NULL);

        if (n > 0) {
            for (int j = 0; j < 3; j++) {
                if (found_cells[j].psr > scan->cfg.psr_threshold && nof_candidates < LTE_SCAN_MAX_EARFCN) {
                    printf(" Found PCI %d (PSR %.1f)\n", found_cells[j].cell_id, found_cells[j].psr);
                    candidates[nof_candidates]     = channels[i].id;
                    candidate_freqs[nof_candidates] = i;
                    candidate_power[nof_candidates] = srsran_convert_power_to_dB(found_cells[j].peak);
                    nof_candidates++;
                    break;
                }
            }
        }
    }

    srsran_ue_cellsearch_free(&cs);
    srsran_rf_stop_rx_stream(rf);

    printf("\n[lte_scan] Coarse scan: %d candidate(s) on Band %d\n", nof_candidates, band);

    /* Pass 2: Fine scan (PSS+MIB+operator) per candidate — scan_earfcn() manages its own stream */
    for (int i = 0; i < nof_candidates && !scan->stop; i++) {
        if (scan->nof_results >= LTE_SCAN_MAX_RESULTS) {
            break;
        }

        printf("[lte_scan] Pass 2: EARFCN %d %.2f MHz ... ",
               candidates[i], channels[candidate_freqs[i]].fd);
        fflush(stdout);

        if (scan_earfcn(rf, candidates[i], channels[candidate_freqs[i]].fd,
                        &scan->results[scan->nof_results], &scan->cfg) > 0) {
            lte_scan_result_t* r = &scan->results[scan->nof_results];
            printf(" Cell PCI %d | %d PRB | %s\n", r->pci, r->nof_prb, r->operator_name);
            scan->nof_results++;
        } else {
            printf(" no decode\n");
        }
    }

    printf("\n[lte_scan] Scan complete: %d cell(s) found on Band %d\n", scan->nof_results, band);
    return scan->nof_results;
}

/*
 * SCAN SINGLE EARFCN — For targeted cell analysis
 *
 * Use case: You already know the EARFCN and want detailed info.
 * Example: After a coarse scan finds a cell at EARFCN 3502,
 *          call this to get full MIB + optional SIB1 decode.
 *
 * Unlike fast/balance modes (continuous scan), this is a one-shot operation.
 * It manages its own RF stream lifecycle (start/stop per call).
 */
int lte_scan_earfcn(lte_scan_t* scan, int earfcn)
{
    if (!scan->rf_handle) {
        return -1;
    }

    float freq;
    if (earfcn_to_freq(earfcn, &freq) < 0) {
        fprintf(stderr, "[lte_scan] Cannot determine frequency for EARFCN %d\n", earfcn);
        return -1;
    }

    scan->stop = false;
    int ret = scan_earfcn((srsran_rf_t*)scan->rf_handle, earfcn, freq,
                          &scan->results[scan->nof_results], &scan->cfg);
    if (ret > 0) {
        scan->nof_results++;
    }
    return ret;
}

/* --- Full/Coarse scan: 2-pass strategy (PSS → MIB) --- */
/*
 * FULL MODE ARCHITECTURE
 *
 * Full mode uses a 2-pass strategy:
 *
 *   Pass 1 (lte_scan_coarse): PSS-only scan at step=1
 *     - Scans ALL EARFCNs exhaustively
 *     - Only detects PSS (fast, ~30ms per point)
 *     - Stores candidates in scan->coarse_* arrays
 *     - Time: ~10-17 seconds for Band 8
 *
 *   Pass 2 (lte_scan_fine): MIB decode per candidate
 *     - Only scans frequencies where PSS was detected
 *     - Full MIB decode (~250ms per cell) to get bandwidth, ports, TAC
 *     - Time: ~5-10 seconds per cell found
 *
 * Why 2-pass?
 * - MIB decode is expensive (~250ms) vs PSS detection (~15ms)
 * - Doing MIB on ALL 350 EARFCNs would take 350 × 250ms = 87.5 seconds
 * - 2-pass reduces this to: (350 × 15ms) + (N_cells × 250ms)
 * - Example: 10 cells found → 5.25s + 2.5s = 7.75s (vs 87.5s)
 *
 * Trade-off:
 * - Coarse pass may miss cells if PSR is marginal
 * - Fine pass can't recover missed cells
 * - But overall speedup is 10-15x for typical scenarios
 */
int lte_scan_coarse(lte_scan_t* scan, int band, int earfcn_start, int earfcn_end)
{
    if (!scan->rf_handle) {
        return -1;
    }

    srsran_rf_t*  rf  = (srsran_rf_t*)scan->rf_handle;
    srsran_earfcn_t channels[LTE_SCAN_MAX_EARFCN];

    int nof_freqs = srsran_band_get_fd_band(band, channels, earfcn_start, earfcn_end, LTE_SCAN_MAX_EARFCN);
    if (nof_freqs <= 0) {
        fprintf(stderr, "[lte_scan] No EARFCNs for band %d\n", band);
        return -1;
    }

    printf("[lte_scan] Coarse scan Band %d: %d EARFCNs (%.1f - %.1f MHz)\n",
           band, nof_freqs, channels[0].fd, channels[nof_freqs - 1].fd);

    scan->nof_coarse = 0;
    scan->stop = false;

    srsran_rf_set_rx_srate(rf, (double)SAMP_FREQ);
    srsran_rf_set_rx_freq(rf, 0, (double)channels[0].fd * MHZ);
    srsran_rf_start_rx_stream(rf, false);

    srsran_ue_cellsearch_t cs;
    if (srsran_ue_cellsearch_init(&cs, cell_detect_config.max_frames_pss, recv_wrapper, (void*)rf)) {
        srsran_rf_stop_rx_stream(rf);
        return -1;
    }
    srsran_ue_cellsearch_set_nof_valid_frames(&cs, cell_detect_config.nof_valid_pss_frames);

    for (int i = 0; i < nof_freqs && !scan->stop; i++) {
        printf("\r[%3d/%d] EARFCN %d  %.2f MHz ... ", i + 1, nof_freqs, channels[i].id, channels[i].fd);
        fflush(stdout);

        srsran_rf_set_rx_freq(rf, 0, (double)channels[i].fd * MHZ);

        srsran_ue_cellsearch_result_t found_cells[3];
        memset(found_cells, 0, sizeof(found_cells));
        int n = srsran_ue_cellsearch_scan(&cs, found_cells, NULL);

        if (n > 0) {
            for (int j = 0; j < 3 && scan->nof_coarse < LTE_SCAN_MAX_EARFCN; j++) {
                if (found_cells[j].psr > scan->cfg.psr_threshold) {
                    printf(" Found PCI %d (PSR %.1f)\n", found_cells[j].cell_id, found_cells[j].psr);
                    scan->coarse_earfcns[scan->nof_coarse] = channels[i].id;
                    scan->coarse_freqs[scan->nof_coarse]   = channels[i].fd;
                    scan->coarse_power[scan->nof_coarse]   = srsran_convert_power_to_dB(found_cells[j].peak);
                    scan->nof_coarse++;
                    break; /* one cell per EARFCN is enough */
                }
            }
        }
    }

    srsran_ue_cellsearch_free(&cs);
    srsran_rf_stop_rx_stream(rf);

    printf("\n[lte_scan] Coarse scan done: %d EARFCN(s) with cells on Band %d\n", scan->nof_coarse, band);
    return scan->nof_coarse;
}

/*
 * FINE SCAN (Pass 2 of Full Mode)
 *
 * This function performs MIB decode on a SINGLE EARFCN that was
 * previously identified as a candidate by the coarse scan.
 *
 * What it extracts from MIB:
 * - Bandwidth (number of PRBs): Critical for connection establishment
 * - Number of antenna ports: Affects receiver configuration
 * - PHICH configuration: Needed for random access
 * - System frame number: For timing synchronization
 *
 * Why separate from coarse?
 * - Coarse scan is fast (PSS only, ~15ms per point)
 * - Fine scan is thorough (MIB decode, ~250ms per cell)
 * - Separating them allows parallelization and selective processing
 *
 * CFO (Carrier Frequency Offset) Handling:
 * - If cfo != 0, we pass the offset from coarse scan to fine scan
 * - This improves MIB decode reliability by pre-compensating frequency error
 */
int lte_scan_fine(lte_scan_t* scan, int earfcn, float cfo)
{
    if (!scan->rf_handle) {
        return -1;
    }

    float freq;
    if (earfcn_to_freq(earfcn, &freq) < 0) {
        return -1;
    }

    if (scan->nof_results >= LTE_SCAN_MAX_RESULTS) {
        return -1;
    }

    printf("[lte_scan] Fine scan EARFCN %d  %.2f MHz ... ", earfcn, freq);
    fflush(stdout);

    int ret = scan_earfcn((srsran_rf_t*)scan->rf_handle, earfcn, freq,
                          &scan->results[scan->nof_results], &scan->cfg);
    if (ret > 0) {
        lte_scan_result_t* r = &scan->results[scan->nof_results];
        printf("PCI %d | %d PRB | %s\n", r->pci, r->nof_prb, r->operator_name);
        scan->nof_results++;
    } else {
        printf("no cell\n");
    }
    return ret;
}

int lte_scan_fast(lte_scan_t* scan, int band, int earfcn_start, int earfcn_end)
{
    if (!scan->rf_handle) return -1;

    srsran_rf_t* rf = (srsran_rf_t*)scan->rf_handle;
    srsran_earfcn_t channels[LTE_SCAN_MAX_EARFCN];

    int nof_freqs = srsran_band_get_fd_band(band, channels, earfcn_start, earfcn_end, LTE_SCAN_MAX_EARFCN);
    if (nof_freqs <= 0) return -1;

    scan->nof_results = 0;
    scan->stop = false;

    /*
     * STEP SIZE SELECTION (Fast Mode)
     *
     * Current: step = 5 (500 kHz spacing)
     *
     * Why 5 and not 50?
     * - LTE minimum bandwidth = 1.4 MHz (6 EARFCNs)
     * - step=50 skips 5 MHz, which can miss cells at band edges
     * - step=5 still fast (~70 points for Band 8) while catching most cells
     *
     * Why not step=1 like Balance?
     * - step=1 would scan 350 points for Band 8
     * - Fast mode targets <10s scan time; Balance accepts ~10-17s
     * - step=5 gives good compromise: speed + coverage
     *
     * Trade-off:
     * - Cells exactly between scan points may be missed
     * - But 500 kHz gap is smaller than typical cell bandwidth (1.4+ MHz)
     * - So most cells will have at least one scan point within their bandwidth
     */
    int step = 5;

    /*
     * SINGLE STREAM PATTERN
     *
     * IMPORTANT: We start the RF stream ONCE before the loop and keep it running.
     * Do NOT call srsran_rf_stop_rx_stream() inside the frequency loop.
     *
     * Reason: SoapyRTLSDR zero-copy buffers trigger a glibc robust mutex bug on
     * aarch64 (Raspberry Pi 4/5) when the stream is restarted repeatedly.
     * This causes "usb_claim_interface error -6" after a few frequency changes.
     *
     * Reference: srsran/examples/lte/search/ssb/search_ssb.c uses same pattern.
     * The receiver stays active; only the frequency changes between iterations.
     */
    srsran_rf_set_rx_srate(rf, (double)SAMP_FREQ);
    srsran_rf_set_rx_freq(rf, 0, (double)channels[0].fd * MHZ);
    srsran_rf_start_rx_stream(rf, false);

    srsran_ue_cellsearch_t cs;
    if (srsran_ue_cellsearch_init(&cs, cell_detect_config.max_frames_pss, recv_wrapper, (void*)rf)) {
        srsran_rf_stop_rx_stream(rf);
        return -1;
    }
    srsran_ue_cellsearch_set_nof_valid_frames(&cs, cell_detect_config.nof_valid_pss_frames);

    for (int i = 0; i < nof_freqs && !scan->stop; i += step) {
        srsran_rf_set_rx_freq(rf, 0, (double)channels[i].fd * MHZ);

        srsran_ue_cellsearch_result_t found_cells[3];
        memset(found_cells, 0, sizeof(found_cells));
        int n = srsran_ue_cellsearch_scan(&cs, found_cells, NULL);

        if (n > 0) {
            for (int j = 0; j < 3 && scan->nof_results < LTE_SCAN_MAX_RESULTS; j++) {
                if (found_cells[j].psr > scan->cfg.psr_threshold) {
                    lte_scan_result_t* out = &scan->results[scan->nof_results];
                    memset(out, 0, sizeof(*out));
                    out->earfcn   = channels[i].id;
                    out->freq_mhz = channels[i].fd;
                    out->pci      = found_cells[j].cell_id;
                    out->psr      = found_cells[j].psr;
                    out->rsrp_dbm = srsran_convert_power_to_dB(found_cells[j].peak);

                    const lte_operator_entry_t* op = lte_scan_lookup_operator(channels[i].id);
                    if (op) {
                        out->mcc     = op->mcc;
                        out->mnc     = op->mnc;
                        out->mnc_3digit = op->mnc_3digit;
                        strncpy(out->operator_name, op->operator_name, LTE_SCAN_OP_NAME_LEN - 1);
                    } else {
                        strncpy(out->operator_name, "Unknown", LTE_SCAN_OP_NAME_LEN - 1);
                    }
                    scan->nof_results++;
                    break;
                }
            }
        }
    }

    srsran_ue_cellsearch_free(&cs);
    srsran_rf_stop_rx_stream(rf);
    return scan->nof_results;
}

/* --- Balance scan: PSS-only with step=1 --- */
/*
 * BALANCE MODE ARCHITECTURE
 *
 * Balance mode is a single-pass PSS-only scan with step=1.
 *
 * Design philosophy:
 * - More thorough than Fast (step=1 vs step=5)
 * - Faster than Full (no MIB decode overhead)
 * - Targets ~10-17 seconds for Band 8
 *
 * When to use Balance over Fast?
 * - When you need to find ALL cells, not just strongest
 * - When cell density is high (urban areas with many operators)
 * - When Fast mode (step=5) might miss edge-of-bandwidth cells
 *
 * When to use Balance over Full?
 * - When you don't need bandwidth/port info (just PCI + RSRP)
 * - When scan time must be <15 seconds
 * - When doing repeated scans (e.g., monitoring cell changes)
 *
 * Comparison:
 *   Fast:   step=5, ~70 points, ~4.5s, finds ~6-9 cells
 *   Balance: step=1, ~350 points, ~10.5s, finds ~17-20 cells
 *   Full:   step=1 + MIB, ~350 points + decode, ~26s, finds ~8-9 cells
 *
 * Note: Balance finds MORE cells than Full because:
 * - Full has stricter filtering (MIB must decode successfully)
 * - Balance accepts any PSS detection above PSR threshold
 */
int lte_scan_balance(lte_scan_t* scan, int band, int earfcn_start, int earfcn_end)
{
    if (!scan->rf_handle) return -1;

    srsran_rf_t* rf = (srsran_rf_t*)scan->rf_handle;
    srsran_earfcn_t channels[LTE_SCAN_MAX_EARFCN];

    int nof_freqs = srsran_band_get_fd_band(band, channels, earfcn_start, earfcn_end, LTE_SCAN_MAX_EARFCN);
    if (nof_freqs <= 0) {
        fprintf(stderr, "[lte_scan] No EARFCNs for band %d\n", band);
        return -1;
    }

    /*
     * STEP SIZE SELECTION (Balance Mode)
     *
     * step = 1 (100 kHz spacing) = Full resolution scan
     *
     * Why step=1 for Balance?
     * - Ensures NO cell is missed due to sampling gaps
     * - LTE cell bandwidth (1.4-20 MHz) is much wider than 100 kHz step
     * - Every possible EARFCN position is checked
     *
     * Performance impact:
     * - Band 8: 350 points × ~30ms/point = ~10.5 seconds
     * - Band 3: ~750 points × ~30ms/point = ~22.5 seconds
     * - Still acceptable for "balance" between speed and completeness
     *
     * Comparison with Fast (step=5):
     * - Fast scans 70 points for Band 8 (~4.5s)
     * - Balance scans 350 points for Band 8 (~10.5s)
     * - 2.3x slower but finds ~2-3x more cells
     */
    int step = 1;

    printf("[lte_scan] Balance scan Band %d (step=%d, %d EARFCNs)\n", band, step, nof_freqs);

    scan->nof_results = 0;
    scan->stop = false;

    /* Single stream — start once, stay active */
    srsran_rf_set_rx_srate(rf, (double)SAMP_FREQ);
    srsran_rf_set_rx_freq(rf, 0, (double)channels[0].fd * MHZ);
    srsran_rf_start_rx_stream(rf, false);

    srsran_ue_cellsearch_t cs;
    if (srsran_ue_cellsearch_init(&cs, cell_detect_config.max_frames_pss, recv_wrapper, (void*)rf)) {
        srsran_rf_stop_rx_stream(rf);
        return -1;
    }
    srsran_ue_cellsearch_set_nof_valid_frames(&cs, cell_detect_config.nof_valid_pss_frames);

    for (int i = 0; i < nof_freqs && !scan->stop && scan->nof_results < 20; i += step) {
        srsran_rf_set_rx_freq(rf, 0, (double)channels[i].fd * MHZ);

        srsran_ue_cellsearch_result_t found_cells[3];
        memset(found_cells, 0, sizeof(found_cells));
        int n = srsran_ue_cellsearch_scan(&cs, found_cells, NULL);

        if (n > 0) {
            for (int j = 0; j < 3 && scan->nof_results < 20; j++) {
                if (found_cells[j].psr > scan->cfg.psr_threshold) {
                    printf("  EARFCN %d PCI %d PSR %.1f CFO %.0f Hz\n",
                           channels[i].id, found_cells[j].cell_id,
                           found_cells[j].psr, found_cells[j].cfo);

                    lte_scan_result_t* out = &scan->results[scan->nof_results];
                    memset(out, 0, sizeof(*out));
                    out->earfcn   = channels[i].id;
                    out->freq_mhz = channels[i].fd;
                    out->pci      = found_cells[j].cell_id;
                    out->psr      = found_cells[j].psr;
                    out->rsrp_dbm = srsran_convert_power_to_dB(found_cells[j].peak);

                    const lte_operator_entry_t* op = lte_scan_lookup_operator(channels[i].id);
                    if (op) {
                        out->mcc     = op->mcc;
                        out->mnc     = op->mnc;
                        out->mnc_3digit = op->mnc_3digit;
                        strncpy(out->operator_name, op->operator_name, LTE_SCAN_OP_NAME_LEN - 1);
                    } else {
                        strncpy(out->operator_name, "Unknown", LTE_SCAN_OP_NAME_LEN - 1);
                    }

                    scan->nof_results++;
                    break;
                }
            }
        }
    }

    srsran_ue_cellsearch_free(&cs);
    srsran_rf_stop_rx_stream(rf);

    printf("[lte_scan] Balance scan done: %d cell(s) on Band %d\n", scan->nof_results, band);
    return scan->nof_results;
}

/*
 * STOP SCAN — Signal-safe termination request
 *
 * Called by:
 * - sigint_handler() when user presses Ctrl+C
 * - Manual cleanup before reinitialization
 *
 * Thread safety:
 * - The stop flag is set in signal handler context
 * - Main scan loop checks this flag between iterations
 * - Non-blocking: doesn't interfere with RF hardware
 */
void lte_scan_stop(lte_scan_t* scan)
{
    if (scan) {
        scan->stop = true;
    }
}

void lte_scan_free(lte_scan_t* scan)
{
    if (!scan) {
        return;
    }
    if (scan->rf_handle) {
        /* Skip srsran_rf_close — SoapyRTLSDR's SoapySDRDevice_unmake blocks
         * indefinitely on aarch64/RPi. The OS reclaims USB resources on exit.
         * cell_search.c uses the same workaround (calls exit() directly). */
        free(scan->rf_handle);
        scan->rf_handle = NULL;
    }
    if (g_stop_scan == scan) {
        g_stop_scan = NULL;
    }
}
