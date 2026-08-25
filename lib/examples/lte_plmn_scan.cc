/**
 * lte_plmn_scan - Scan LTE cells and extract MCC/MNC from SIB1
 *
 * Designed for RTL-SDR V3 with limited bandwidth (~3.2 MHz max).
 * PSS+MIB decode at 1.92 MHz (works with any SDR).
 * Attempts SIB1 decode at reduced bandwidth - only succeeds for cells
 * where PDCCH+SIB1 fit within the center PRBs we can receive.
 */

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>

#include "srsran/common/crash_handler.h"
#include "srsran/phy/rf/rf.h"
#include "srsran/phy/rf/rf_utils.h"
#include "srsran/srsran.h"

#include "srsran/asn1/rrc.h"

#define MHZ 1000000
#define SAMP_FREQ 1920000
#define MAX_EARFCN 1000
#define RTLSDR_MAX_PRB 15
#define MAX_SIB1_ATTEMPTS 300

using namespace asn1::rrc;

static cell_search_cfg_t cell_detect_config = {
    .max_frames_pbch      = SRSRAN_DEFAULT_MAX_FRAMES_PBCH,
    .max_frames_pss       = SRSRAN_DEFAULT_MAX_FRAMES_PSS,
    .nof_valid_pss_frames = SRSRAN_DEFAULT_NOF_VALID_PSS_FRAMES,
    .init_agc             = 0,
    .force_tdd            = false,
};

struct cell_result {
  srsran_cell_t cell;
  float         freq;
  int           dl_earfcn;
  float         power;
  uint16_t      mcc;
  uint16_t      mnc;
  bool          mnc_3digit;
  uint32_t      tac;
  uint32_t      cell_id_global;
  bool          sib1_decoded;
};

static struct cell_result results[1024];
static int                n_results = 0;

static int      band         = -1;
static int      earfcn_start = -1, earfcn_end = -1;
static float    rf_gain      = 42.0;
static const char* rf_args   = "";
static const char* rf_dev    = "";
static uint32_t max_prb      = RTLSDR_MAX_PRB;
static bool     go_exit      = false;

static void sig_int_handler(int signo)
{
  if (signo == SIGINT) {
    go_exit = true;
  }
}

static void usage(const char* prog)
{
  printf("Usage: %s -b band [options]\n", prog);
  printf("\t-b Band number (required). e.g. 3, 5, 8\n");
  printf("\t-a RF device args [Default \"%s\"]\n", rf_args);
  printf("\t-d RF device name [Default \"%s\"]\n", rf_dev);
  printf("\t-g RF gain [Default %.0f dB]\n", rf_gain);
  printf("\t-s EARFCN start [Default: band start]\n");
  printf("\t-e EARFCN end [Default: band end]\n");
  printf("\t-p Max PRBs for SIB1 decode [Default %d]\n", max_prb);
}

static void parse_args(int argc, char** argv)
{
  int opt;
  while ((opt = getopt(argc, argv, "a:b:d:g:s:e:p:h")) != -1) {
    switch (opt) {
      case 'a':
        rf_args = optarg;
        break;
      case 'b':
        band = (int)strtol(optarg, NULL, 10);
        break;
      case 'd':
        rf_dev = optarg;
        break;
      case 'g':
        rf_gain = strtof(optarg, NULL);
        break;
      case 's':
        earfcn_start = (int)strtol(optarg, NULL, 10);
        break;
      case 'e':
        earfcn_end = (int)strtol(optarg, NULL, 10);
        break;
      case 'p':
        max_prb = (uint32_t)strtol(optarg, NULL, 10);
        break;
      default:
        usage(argv[0]);
        exit(-1);
    }
  }
  if (band == -1) {
    usage(argv[0]);
    exit(-1);
  }
  if (max_prb > RTLSDR_MAX_PRB) {
    printf("Warning: capping PRB from %u to %u for RTL-SDR\n", max_prb, RTLSDR_MAX_PRB);
    max_prb = RTLSDR_MAX_PRB;
  }
}

static int recv_wrapper(void* h, void* data, uint32_t nsamples, srsran_timestamp_t* t)
{
  return srsran_rf_recv_with_time((srsran_rf_t*)h, data, nsamples, 1, NULL, NULL);
}

static int recv_wrapper_multi(void* h, cf_t* data[SRSRAN_MAX_CHANNELS], uint32_t nsamples, srsran_timestamp_t* t)
{
  return srsran_rf_recv_with_time_multi((srsran_rf_t*)h, (void**)data, nsamples, 1, NULL, NULL);
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
  srsran_rf_set_rx_srate(rf, (float)srate);
  srsran_rf_start_rx_stream(rf, false);

  if (cfo) {
    ue_mib.ue_sync.cfo_current_value       = *cfo / 15000;
    ue_mib.ue_sync.cfo_is_copied           = true;
    ue_mib.ue_sync.cfo_correct_enable_find = true;
    srsran_sync_set_cfo_cp_enable(&ue_mib.ue_sync.sfind, false, 0);
  }

  int ret = srsran_ue_mib_sync_decode(&ue_mib, cell_detect_config.max_frames_pbch, bch_payload, &cell->nof_ports, NULL);
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

static void print_plmn(uint16_t mcc, uint16_t mnc, bool mnc_3digit)
{
  printf("MCC=%03u  MNC=%0*u", mcc, mnc_3digit ? 3 : 2, mnc);
}

static int parse_sib1_plmn(uint8_t* buf, uint32_t len, struct cell_result* res)
{
  bcch_dl_sch_msg_s bcch_msg;
  asn1::cbit_ref          bref(buf, len);

  if (bcch_msg.unpack(bref) != asn1::SRSASN_SUCCESS) {
    return -1;
  }

  if (bcch_msg.msg.c1().type() != bcch_dl_sch_msg_type_c::c1_c_::types::sib_type1) {
    return -1;
  }

  auto& sib1 = bcch_msg.msg.c1().sib_type1();
  auto& ca   = sib1.cell_access_related_info;

  res->sib1_decoded   = true;
  res->cell_id_global = ca.cell_id.to_number();
  res->tac            = ca.tac.to_number();

  if (ca.plmn_id_list.size() > 0) {
    auto& plmn = ca.plmn_id_list[0].plmn_id;
    if (plmn.mcc_present) {
      res->mcc = plmn.mcc[0] * 100 + plmn.mcc[1] * 10 + plmn.mcc[2];
    }
    res->mnc_3digit = (plmn.mnc.size() == 3);
    if (plmn.mnc.size() >= 2) {
      res->mnc = plmn.mnc[0] * 10 + plmn.mnc[1];
      if (plmn.mnc.size() == 3) {
        res->mnc = res->mnc * 10 + plmn.mnc[2];
      }
    }
  }
  return 0;
}

static int try_decode_sib1(srsran_rf_t* rf, srsran_cell_t* cell, float cfo, struct cell_result* res)
{
  uint32_t sib_prb = cell->nof_prb;
  if (sib_prb > max_prb) {
    sib_prb = max_prb;
    printf("  Capping PRB %u -> %u for SIB1 decode\n", cell->nof_prb, sib_prb);
  }

  int srate = srsran_sampling_freq_hz(sib_prb);
  if (srate <= 0) {
    return -1;
  }

  float actual_srate = srsran_rf_set_rx_srate(rf, (double)srate);
  if (fabs(actual_srate - (float)srate) > 1000) {
    printf("  RF sample rate mismatch: wanted %d, got %.0f\n", srate, actual_srate);
    return -1;
  }

  srsran_cell_t sib_cell = *cell;
  sib_cell.nof_prb       = sib_prb;

  cf_t* sf_buffer[SRSRAN_MAX_PORTS] = {};
  for (int i = 0; i < SRSRAN_MAX_PORTS; i++) {
    sf_buffer[i] = (cf_t*)srsran_vec_malloc(SRSRAN_SF_LEN_PRB(sib_prb) * sizeof(cf_t));
    if (!sf_buffer[i]) {
      goto clean_exit;
    }
  }

  srsran_ue_sync_t ue_sync;
  memset(&ue_sync, 0, sizeof(ue_sync));
  if (srsran_ue_sync_init_multi(&ue_sync, sib_prb, false, recv_wrapper_multi, 1, (void*)rf)) {
    printf("  Error init ue_sync\n");
    goto clean_exit;
  }
  if (srsran_ue_sync_set_cell(&ue_sync, sib_cell)) {
    printf("  Error set cell ue_sync\n");
    srsran_ue_sync_free(&ue_sync);
    goto clean_exit;
  }
  srsran_ue_sync_set_cfo_ref(&ue_sync, cfo);

  srsran_ue_dl_t ue_dl;
  memset(&ue_dl, 0, sizeof(ue_dl));
  if (srsran_ue_dl_init(&ue_dl, sf_buffer, sib_prb, 1)) {
    printf("  Error init ue_dl\n");
    srsran_ue_sync_free(&ue_sync);
    goto clean_exit;
  }
  if (srsran_ue_dl_set_cell(&ue_dl, sib_cell)) {
    printf("  Error set cell ue_dl\n");
    srsran_ue_dl_free(&ue_dl);
    srsran_ue_sync_free(&ue_sync);
    goto clean_exit;
  }

  {
    srsran_dl_sf_cfg_t dl_sf = {};
    dl_sf.sf_type            = SRSRAN_SF_NORM;

    srsran_ue_dl_cfg_t ue_dl_cfg = {};
    ue_dl_cfg.cfg.tm             = (sib_cell.nof_ports > 1) ? SRSRAN_TM2 : SRSRAN_TM1;
    ue_dl_cfg.cfg.pdsch.use_tbs_index_alt = false;

    srsran_pdsch_cfg_t pdsch_cfg = {};
    pdsch_cfg.rnti               = SRSRAN_SIRNTI;

    srsran_softbuffer_rx_t softbuffer_rx;
    memset(&softbuffer_rx, 0, sizeof(softbuffer_rx));
    if (srsran_softbuffer_rx_init(&softbuffer_rx, sib_prb)) {
      srsran_ue_dl_free(&ue_dl);
      srsran_ue_sync_free(&ue_sync);
      goto clean_exit;
    }

    srsran_rf_start_rx_stream(rf, false);

    int  n_trial    = 0;
    bool sib1_found = false;
    bool acks[SRSRAN_MAX_CODEWORDS] = {};
    uint8_t* data[SRSRAN_MAX_CODEWORDS] = {};
    uint8_t  data_buf[SRSRAN_MAX_CODEWORDS][16640 / 8];
    data[0] = data_buf[0];
    data[1] = data_buf[1];

    printf("  Attempting SIB1 decode...\n");

    while (!sib1_found && n_trial < MAX_SIB1_ATTEMPTS && !go_exit) {
      int ret = srsran_ue_sync_zerocopy(&ue_sync, sf_buffer, SRSRAN_SF_LEN_PRB(sib_prb));
      if (ret < 0) {
        break;
      }
      if (ret == 0) {
        continue;
      }

      uint32_t sf_idx = srsran_ue_sync_get_sfidx(&ue_sync);
      uint32_t sfn    = srsran_ue_sync_get_sfn(&ue_sync);

      bool decode_this = false;
      if (sf_idx == 5 && (sfn % 2) == 0) {
        decode_this = true;
      }

      if (decode_this) {
        dl_sf.tti = sfn * 10 + sf_idx;

        if (srsran_ue_dl_find_and_decode(&ue_dl, &dl_sf, &ue_dl_cfg, &pdsch_cfg, data, acks) > 0) {
          if (acks[0]) {
            sib1_found = true;
            printf("  SIB1 decoded!\n");
            if (parse_sib1_plmn(data[0], pdsch_cfg.grant.tb[0].tbs / 8, res) == 0) {
              printf("  ");
              print_plmn(res->mcc, res->mnc, res->mnc_3digit);
              printf("\n");
              printf("  TAC:      %u\n", res->tac);
              printf("  Cell ID:  %u\n", res->cell_id_global);
            } else {
              printf("  [SIB1 ASN.1 parse failed]\n");
            }
          }
        }
      }
      n_trial++;
    }

    srsran_rf_stop_rx_stream(rf);
    srsran_softbuffer_rx_free(&softbuffer_rx);

    if (!sib1_found && !go_exit) {
      printf("  SIB1 not decoded (%d attempts)\n", n_trial);
    }

    srsran_ue_dl_free(&ue_dl);
    srsran_ue_sync_free(&ue_sync);
    goto clean_exit;
  }

clean_exit:
  for (int i = 0; i < SRSRAN_MAX_PORTS; i++) {
    if (sf_buffer[i]) {
      free(sf_buffer[i]);
      sf_buffer[i] = NULL;
    }
  }
  return 0;
}

int main(int argc, char** argv)
{
  srsran_rf_t rf;

  srsran_debug_handle_crash(argc, argv);
  parse_args(argc, argv);

  signal(SIGINT, sig_int_handler);

  printf("Opening RF device...\n");
  if (srsran_rf_open_devname(&rf, rf_dev, (char*)rf_args, 1)) {
    ERROR("Error opening RF");
    exit(-1);
  }
  srsran_rf_set_rx_gain(&rf, rf_gain);
  srsran_rf_suppress_stdout(&rf);

  srsran_earfcn_t channels[MAX_EARFCN];
  int nof_freqs = srsran_band_get_fd_band(band, channels, earfcn_start, earfcn_end, MAX_EARFCN);
  if (nof_freqs <= 0) {
    ERROR("No EARFCNs for band %d", band);
    srsran_rf_close(&rf);
    exit(-1);
  }

  printf("=== lte_plmn_scan ===\n");
  printf("Band %d: %d EARFCNs (%.1f - %.1f MHz)\n", band, nof_freqs, channels[0].fd, channels[nof_freqs - 1].fd);
  printf("SIB1 decode: max %u PRB (~%.1f MHz BW)\n", max_prb, srsran_sampling_freq_hz(max_prb) / 1e6);
  printf("Gain: %.0f dB\n\n", rf_gain);

  for (int freq = 0; freq < nof_freqs && !go_exit; freq++) {
    srsran_rf_set_rx_freq(&rf, 0, (double)channels[freq].fd * MHZ);

    printf("\r[%3d/%d] EARFCN %d  %.2f MHz ... ", freq + 1, nof_freqs, channels[freq].id, channels[freq].fd);
    fflush(stdout);

    srsran_rf_set_rx_srate(&rf, SAMP_FREQ);
    srsran_rf_start_rx_stream(&rf, false);

    srsran_ue_cellsearch_t        cs;
    srsran_ue_cellsearch_result_t found_cells[3];

    if (srsran_ue_cellsearch_init(&cs, cell_detect_config.max_frames_pss, recv_wrapper, (void*)&rf)) {
      continue;
    }
    srsran_ue_cellsearch_set_nof_valid_frames(&cs, cell_detect_config.nof_valid_pss_frames);

    bzero(found_cells, sizeof(found_cells));
    int n = srsran_ue_cellsearch_scan(&cs, found_cells, NULL);

    srsran_rf_stop_rx_stream(&rf);

    if (n > 0) {
      for (int i = 0; i < 3; i++) {
        if (found_cells[i].psr > 2.0 && !go_exit) {
          srsran_cell_t cell;
          cell.id         = found_cells[i].cell_id;
          cell.cp         = found_cells[i].cp;
          cell.frame_type = found_cells[i].frame_type;
          float cfo       = found_cells[i].cfo;

          printf("\n  Cell ID %d | %.2f MHz | PSR %.2f ... ", cell.id, channels[freq].fd, found_cells[i].psr);
          fflush(stdout);

          int mib_ret = decode_mib(&rf, &cell, &cfo);

          if (mib_ret == SRSRAN_UE_MIB_FOUND) {
            printf("MIB OK (%d PRB, %d ports)\n", cell.nof_prb, cell.nof_ports);

            struct cell_result* res = &results[n_results];
            memset(res, 0, sizeof(*res));
            res->cell      = cell;
            res->freq      = channels[freq].fd;
            res->dl_earfcn = channels[freq].id;
            res->power     = srsran_convert_power_to_dB(found_cells[i].peak);

            try_decode_sib1(&rf, &cell, cfo, res);

            if (!res->sib1_decoded) {
              printf("  (SIB1 not available - need wider BW SDR)\n");
            }

            n_results++;
          } else {
            printf("MIB failed\n");
          }
        }
      }
    }

    srsran_ue_cellsearch_free(&cs);
  }

  printf("\n\n");
  printf("========================================\n");
  printf("  SCAN RESULTS: %d cell(s) found\n", n_results);
  printf("========================================\n\n");

  for (int i = 0; i < n_results; i++) {
    printf("Cell #%d\n", i + 1);
    printf("  EARFCN:  %d\n", results[i].dl_earfcn);
    printf("  Freq:    %.1f MHz\n", results[i].freq);
    printf("  PCI:     %d\n", results[i].cell.id);
    printf("  PRB:     %d\n", results[i].cell.nof_prb);
    printf("  Ports:   %d\n", results[i].cell.nof_ports);
    printf("  Power:   %.1f dBm\n", results[i].power);

    if (results[i].sib1_decoded) {
      printf("  MCC:     %03u\n", results[i].mcc);
      printf("  MNC:     %0*u\n", results[i].mnc_3digit ? 3 : 2, results[i].mnc);
      printf("  TAC:     %u\n", results[i].tac);
      printf("  CellID:  %u\n", results[i].cell_id_global);
    } else {
      printf("  MCC/MNC: (SIB1 not decoded)\n");
    }
    printf("\n");
  }

  if (n_results == 0) {
    printf("No cells found on Band %d.\n", band);
  } else {
    int n_plmn = 0;
    for (int i = 0; i < n_results; i++) {
      if (results[i].sib1_decoded)
        n_plmn++;
    }
    if (n_plmn == 0) {
      printf("MCC/MNC: not decoded for any cell.\n");
      printf("Typical LTE cells (25-100 PRB = 5-20 MHz) exceed\n");
      printf("RTL-SDR bandwidth (~3 MHz). A wider-bandwidth SDR\n");
      printf("is needed (USRP, HackRF, BladeRF, LimeSDR).\n");
    } else {
      printf("MCC/MNC decoded for %d/%d cells.\n", n_plmn, n_results);
    }
  }

  srsran_rf_close(&rf);
  printf("\nDone.\n");
  return 0;
}
