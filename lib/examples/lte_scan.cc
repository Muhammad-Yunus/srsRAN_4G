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
 * ============================== */

const lte_operator_entry_t lte_operator_table_id[] = {
    /* === Band 3 (1800 MHz) - DL: 1805-1880 MHz === */
    /* EARFCN 1200-1949, F_DL = 1805 + 0.1 * (EARFCN - 1200) MHz */
    { 1200, 1399, 510, 10,  false, "Telkomsel",          "Band 3" },
    { 1400, 1499, 510, 11,  false, "XL Axiata",          "Band 3" },
    { 1500, 1599, 510, 11,  false, "XL Axiata (AXIS)",   "Band 3" },
    { 1600, 1799, 510, 21,  false, "Indosat Ooredoo",    "Band 3" },
    { 1800, 1949, 510, 89,  false, "Hutchison 3",        "Band 3" },

    /* === Band 5 (850 MHz) - DL: 869-894 MHz === */
    /* EARFCN 2400-2649, F_DL = 869 + 0.1 * (EARFCN - 2400) MHz */
    { 2400, 2499, 510, 10,  false, "Telkomsel",          "Band 5" },
    { 2500, 2599, 510, 9,   false, "Smartfren",          "Band 5" },
    { 2600, 2649, 510, 9,   false, "Smartfren",          "Band 5" },

    /* === Band 8 (900 MHz) - DL: 925-960 MHz === */
    /* EARFCN 3450-3799, F_DL = 925 + 0.1 * (EARFCN - 3450) MHz */
    { 3450, 3499, 510, 10,  false, "Telkomsel",          "Band 8" },
    { 3500, 3549, 510, 10,  false, "Telkomsel",          "Band 8" },
    { 3550, 3599, 510, 11,  false, "XL Axiata",          "Band 8" },
    { 3600, 3649, 510, 11,  false, "XL Axiata",          "Band 8" },
    { 3650, 3699, 510, 21,  false, "Indosat Ooredoo",    "Band 8" },
    { 3700, 3749, 510, 89,  false, "Hutchison 3",        "Band 8" },
    { 3750, 3799, 510, 89,  false, "Hutchison 3",        "Band 8" },

    /* === Band 28 (700 MHz) - DL: 758-803 MHz === */
    /* EARFCN 9000-9649, F_DL = 758 + 0.1 * (EARFCN - 9000) MHz */
    { 9000, 9149, 510, 10,  false, "Telkomsel",          "Band 28" },
    { 9150, 9299, 510, 21,  false, "Indosat Ooredoo",    "Band 28" },
    { 9300, 9449, 510, 11,  false, "XL Axiata",          "Band 28" },
    { 9450, 9599, 510, 89,  false, "Hutchison 3",        "Band 28" },

    /* === Band 40 (2300 MHz TDD) - EARFCN 38650-39649 === */
    { 38650, 38799, 510, 10,  false, "Telkomsel",         "Band 40" },
    { 38800, 38949, 510, 11,  false, "XL Axiata",         "Band 40" },
    { 38950, 39099, 510, 21,  false, "Indosat Ooredoo",   "Band 40" },
    { 39100, 39249, 510, 89,  false, "Hutchison 3",       "Band 40" },
    { 39250, 39649, 510, 8,   false, "Smartfren",          "Band 40" },
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

static int recv_wrapper(void* h, void* data, uint32_t nsamples, srsran_timestamp_t* t)
{
    return srsran_rf_recv_with_time((srsran_rf_t*)h, data, nsamples, 1, NULL, NULL);
}

static int recv_wrapper_multi(void* h, cf_t* data[MAX_CHANNELS], uint32_t nsamples, srsran_timestamp_t* t)
{
    return srsran_rf_recv_with_time_multi((srsran_rf_t*)h, (void**)data, nsamples, 1, NULL, NULL);
}

/* --- EARFCN → frequency helper --- */

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
    .max_frames_pbch      = 50,       /* Optimized: 50 frames = 250ms (was 500 = 2.5s) */
    .max_frames_pss       = 3,        /* Optimized: 3 frames = 15ms (was 10 = 50ms) */
    .nof_valid_pss_frames = 3,        /* Optimized: require only 3 valid frames */
    .init_agc             = 0,
    .force_tdd            = false,
};

/* Balance mode config: intermediate between fast and full */
static const cell_search_cfg_t cell_detect_config_balance = {
    .max_frames_pbch      = 200,      /* Balanced: 200 frames = 1s */
    .max_frames_pss       = 5,        /* Balanced: 5 frames = 25ms */
    .nof_valid_pss_frames = 5,        /* Balanced: require 5 valid frames */
    .init_agc             = 0,
    .force_tdd            = false,
};

/* Set cell search config based on mode (1=full, 0=fast, 2=balance) */
void set_cell_search_mode(int mode)
{
    if (mode == 1) {
        /* Full mode: optimized values with MIB decode (same as fast, but enables MIB) */
        cell_detect_config.max_frames_pbch      = 50;          /* 50 frames = 250ms (optimized from 500) */
        cell_detect_config.max_frames_pss       = 3;           /* 3 frames = 15ms (optimized from 10) */
        cell_detect_config.nof_valid_pss_frames = 3;           /* require 3 valid frames */
    } else if (mode == 2) {
        /* Balance mode: intermediate values */
        cell_detect_config.max_frames_pbch      = cell_detect_config_balance.max_frames_pbch;
        cell_detect_config.max_frames_pss       = cell_detect_config_balance.max_frames_pss;
        cell_detect_config.nof_valid_pss_frames = cell_detect_config_balance.nof_valid_pss_frames;
    } else {
        /* Fast mode: optimized values */
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

/* --- Scan one EARFCN for a cell --- */

static int recv_wrapper_single(void* h, cf_t* data[SRSRAN_MAX_CHANNELS], uint32_t nsamples, srsran_timestamp_t* t)
{
    return srsran_rf_recv_with_time_multi((srsran_rf_t*)h, (void**)data, nsamples, 1, NULL, NULL);
}

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

static volatile lte_scan_t* g_stop_scan = NULL;

static void sigint_handler(int signo)
{
    (void)signo;
    if (g_stop_scan) {
        g_stop_scan->stop = true;
    }
}

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

int lte_scan_fine(lte_scan_t* scan, int earfcn)
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

    /* Step size: 50 EARFCNs = 5 MHz — LTE cells are ≥1.4 MHz wide */
    int step = 50;

    /* Start stream once before the loop — do NOT stop/restart per frequency.
     * SoapyRTLSDR zero-copy buffers trigger a glibc robust mutex bug on aarch64
     * when the stream is restarted. cell_search.c uses the same pattern. */
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
