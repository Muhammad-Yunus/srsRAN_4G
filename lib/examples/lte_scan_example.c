/**
 * lte_scan_cli - LTE Network Discovery CLI
 *
 * Outputs human-readable or JSON format for app consumption.
 *
 * Modes:
 *   (default)  Fast scan: PSS + operator table (~1s/EARFCN)
 *   -f         Full scan: PSS + MIB + operator (~8s/cell)
 *   -1         One-step: scan every EARFCN (slow)
 *
 * Exit codes: 0=cells found, 1=no cells, 2=error
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "lte_scan.h"

static volatile int g_running = 1;
static lte_scan_t*  g_scan    = NULL;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
    if (g_scan) lte_scan_stop(g_scan);
}

static void usage(const char* prog)
{
    fprintf(stderr,
        "LTE Network Discovery CLI\n\n"
        "Usage: %s -b band [options]\n\n"
        "Options:\n"
        "  -b band          Band (3,5,8,28,40) [required]\n"
        "  -d rf_device     SoapySDR device [auto]\n"
        "  -a rf_args       Device arguments\n"
        "  -g gain          RX gain dB [42]\n"
        "  -s earfcn_start  Start EARFCN\n"
        "  -e earfcn_end    End EARFCN\n"
        "  -f               Full scan (MIB decode, slower)\n"
        "  -j               JSON output\n"
        "  -q               Quiet (JSON only, no progress)\n"
        "  -h               Help\n\n"
        "Examples:\n"
        "  %s -b 8              Fast scan Band 8\n"
        "  %s -b 8 -j           JSON output\n"
        "  %s -b 8 -j -q        JSON only, no progress\n"
        "  %s -b 8 -f           Full scan (with MIB decode)\n"
        "  %s -b 8 -s 3500 -e 3510\n",
        prog, prog, prog, prog, prog, prog);
}

static void print_json(const lte_scan_t* scan, int band, float gain, const char* mode)
{
    printf("{\n  \"band\": %d, \"gain_db\": %.0f, \"mode\": \"%s\",\n", band, gain, mode);
    printf("  \"cells\": [\n");
    for (int i = 0; i < scan->nof_results; i++) {
        const lte_scan_result_t* r = &scan->results[i];
        if (r->nof_prb) {
            printf("    {\"earfcn\":%d,\"freq_mhz\":%.1f,\"pci\":%d,"
                   "\"prb\":%d,\"ports\":%d,\"rsrp\":%.1f,"
                   "\"operator\":\"%s\",\"mcc\":%d,\"mnc\":%d,"
                   "\"plmn\":\"%03d%0*d\"}%s\n",
                   r->earfcn, r->freq_mhz, r->pci,
                   r->nof_prb, r->nof_ports, r->rsrp_dbm,
                   r->operator_name, r->mcc, r->mnc,
                   r->mcc, r->mnc_3digit ? 3 : 2, r->mnc,
                   i < scan->nof_results - 1 ? "," : "");
        } else {
            printf("    {\"earfcn\":%d,\"freq_mhz\":%.1f,\"pci\":%d,"
                   "\"prb\":null,\"ports\":null,\"rsrp\":%.1f,"
                   "\"operator\":\"%s\",\"mcc\":%d,\"mnc\":%d,"
                   "\"plmn\":\"%03d%0*d\"}%s\n",
                   r->earfcn, r->freq_mhz, r->pci,
                   r->rsrp_dbm,
                   r->operator_name, r->mcc, r->mnc,
                   r->mcc, r->mnc_3digit ? 3 : 2, r->mnc,
                   i < scan->nof_results - 1 ? "," : "");
        }
    }
    printf("  ],\n  \"total\": %d\n}\n", scan->nof_results);
}

int main(int argc, char* argv[])
{
    int         band        = -1;
    const char* rf_device   = "";
    const char* rf_args     = "";
    float       gain        = 42.0f;
    int         earfcn_s    = -1;
    int         earfcn_e    = -1;
    int         full_mode   = 0;
    int         json_mode   = 0;
    int         quiet       = 0;
    int         opt;

    while ((opt = getopt(argc, argv, "b:d:a:g:s:e:fjqh")) != -1) {
        switch (opt) {
            case 'b': band      = atoi(optarg); break;
            case 'd': rf_device = optarg;       break;
            case 'a': rf_args   = optarg;       break;
            case 'g': gain      = atof(optarg); break;
            case 's': earfcn_s  = atoi(optarg); break;
            case 'e': earfcn_e  = atoi(optarg); break;
            case 'f': full_mode = 1;            break;
            case 'j': json_mode = 1;            break;
            case 'q': quiet     = 1;            break;
            case 'h': usage(argv[0]); return 0;
            default:  usage(argv[0]); return 2;
        }
    }

    if (band < 0) { usage(argv[0]); return 2; }
    if (quiet) json_mode = 1;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    lte_scan_t scan;
    g_scan = &scan;

    lte_scan_config_t cfg = {
        .rf_device = "soapy", .rf_args = rf_args,
        .rf_gain_dB = gain, .max_prb_sib1 = 15,
        .psr_threshold = 2.0f, .try_sib1 = false,
    };

    if (lte_scan_init_ex(&scan, &cfg) != 0) {
        if (json_mode) printf("{\"error\":\"SDR not found\"}\n");
        else fprintf(stderr, "Error: SDR not found\n");
        return 2;
    }

    int n;

    if (full_mode) {
        if (!quiet) fprintf(stderr, "Full scan Band %d...\n", band);
        n = lte_scan_coarse(&scan, band, earfcn_s, earfcn_e);
        if (n > 0 && g_running) {
            for (int i = 0; i < n && g_running; i++) {
                if (!quiet) fprintf(stderr, "  [%d/%d] EARFCN %d\n", i+1, n, scan.coarse_earfcns[i]);
                lte_scan_fine(&scan, scan.coarse_earfcns[i]);
            }
        }
        n = scan.nof_results;
    } else {
        if (!quiet) fprintf(stderr, "Fast scan Band %d...\n", band);
        n = lte_scan_fast(&scan, band, earfcn_s, earfcn_e);
    }

    if (json_mode) {
        print_json(&scan, band, gain, full_mode ? "full" : "fast");
    } else {
        printf("\n=== %d cell(s) on Band %d ===\n\n", n, band);
        for (int i = 0; i < n; i++) {
            const lte_scan_result_t* r = &scan.results[i];
            printf("EARFCN %d | %.1f MHz | PCI %d | %s | PLMN %03d%0*d\n",
                   r->earfcn, r->freq_mhz, r->pci, r->operator_name,
                   r->mcc, r->mnc_3digit ? 3 : 2, r->mnc);
        }
        if (n == 0) printf("No cells found.\n");
    }

    lte_scan_free(&scan);
    g_scan = NULL;
    fflush(stdout);
    return (n > 0) ? 0 : 1;
}
