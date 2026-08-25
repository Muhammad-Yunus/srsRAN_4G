#!/usr/bin/env python3
"""
Converter untuk output lte_scan_example ke format JSON target.

Usage:
  python3 convert_scan.py [fast|full] [band] [--json]
  
Examples:
  python3 convert_scan.py fast 8              # Output as-is (raw lte_scan_example JSON)
  python3 convert_scan.py full 8 --json       # Output converted to target format
  python3 convert_scan.py fast 8 --json       # Quick scan with converted output
"""

import json
import os
import sys
import subprocess
import argparse
from datetime import datetime, timezone
from typing import List, Dict, Any, Optional

# Konfigurasi bandwidth per PRB (standar LTE)
PRB_TO_BANDWIDTH = {
    6: 1.4,
    15: 3.0,
    25: 5.0,
    50: 10.0,
    75: 15.0,
    100: 20.0,
}

def run_lte_scan(band: int, full_mode: bool = False) -> Dict[str, Any]:
    """Jalankan lte_scan_example dan kembalikan hasil."""
    # Dapatkan path absolut ke binary (berapapun dari mana dipanggil)
    # Resolve symlink to get actual script location
    script_path = os.path.realpath(__file__)
    script_dir = os.path.dirname(script_path)
    binary_path = os.path.join(script_dir, "build", "lib", "examples", "lte_scan_example")
    
    cmd = [
        binary_path,
        "-b", str(band),
        "-a", "driver=rtlsdr,index=0",
        "-g", "40",
        "-j",
        "-q"
    ]
    
    if full_mode:
        cmd.insert(4, "-f")  # Full scan mode
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        if result.returncode != 0:
            print(f"Error: {result.stderr}", file=sys.stderr)
            return None
        
        # Parse JSON dari output
        lines = result.stdout.strip().split('\n')
        json_lines = []
        in_json = False
        for line in lines:
            if line.strip().startswith('{'):
                in_json = True
            if in_json:
                json_lines.append(line)
        
        if json_lines:
            return json.loads('\n'.join(json_lines))
        return None
        
    except subprocess.TimeoutExpired:
        print("Timeout menjalankan lte_scan_example", file=sys.stderr)
        return None
    except json.JSONDecodeError as e:
        print(f"Error parsing JSON: {e}", file=sys.stderr)
        return None

def convert_to_target_format(scan_result: Dict[str, Any]) -> Dict[str, Any]:
    """Konversi hasil scan ke format JSON target."""
    converted_cells = []
    timestamp = datetime.now(timezone.utc).isoformat()
    
    cells = scan_result.get('cells', [])
    
    for cell in cells:
        earfcn = cell.get('earfcn')
        freq_mhz = cell.get('freq_mhz')
        pci = cell.get('pci')
        prb = cell.get('prb')
        ports = cell.get('ports')
        rsrp = cell.get('rsrp')
        operator = cell.get('operator')
        mcc = cell.get('mcc')
        mnc = cell.get('mnc')
        
        # Hitung bandwidth dari PRB
        bandwidth_mhz = PRB_TO_BANDWIDTH.get(prb) if prb else None
        
        # Buat entry sesuai format target
        entry = {
            "frequency_mhz": freq_mhz,
            "earfcn": earfcn,
            "band": str(scan_result.get('band')),
            "bandwidth_mhz": bandwidth_mhz,
            "pci": pci,
            "cell_id": None,  # Butuh SIB1 decode
            "tac": None,      # Butuh SIB1 decode
            "mcc": mcc,
            "mnc": mnc,
            "rsrp": rsrp,
            "rsrq": None,     # Butuh channel estimation
            "snr": None,      # Butuh channel estimation
            "operator": operator,
            "country": "Indonesia",
            "timestamp": timestamp
        }
        
        # Tambahkan info tambahan jika ada
        if ports:
            entry["ports"] = ports
        
        converted_cells.append(entry)
    
    return {
        "scan_info": {
            "band": scan_result.get('band'),
            "gain_db": scan_result.get('gain_db'),
            "mode": scan_result.get('mode'),
            "total_cells": len(converted_cells),
            "timestamp": timestamp
        },
        "cells": converted_cells
    }

def main():
    parser = argparse.ArgumentParser(
        description='Convert lte_scan_example output to target JSON format',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 lte_scan.py fast 8              # Raw output (as-is from lte_scan_example)
  python3 lte_scan.py full 8 --json       # Converted to target format
  python3 lte_scan.py fast 8 -j           # Short form for --json
        """
    )
    parser.add_argument('mode', nargs='?', default='fast', choices=['fast', 'full'],
                       help='Scan mode: fast (default) or full (with MIB decode)')
    parser.add_argument('band', type=int, nargs='?', default=8,
                       help='LTE band number (default: 8)')
    parser.add_argument('--json', '-j', action='store_true',
                       help='Output in target JSON format (with bandwidth, country, etc.)')
    
    args = parser.parse_args()
    
    print(f"Running lte_scan_example (band={args.band}, mode={args.mode})...")
    scan_result = run_lte_scan(args.band, full_mode=(args.mode == 'full'))
    
    if not scan_result:
        print("Failed to run scan", file=sys.stderr)
        sys.exit(1)
    
    if args.json:
        # Convert to target format
        converted = convert_to_target_format(scan_result)
        output = json.dumps(converted, indent=2)
        print(output)
        
        # Save to file
        output_file = f"cell_scan_band{args.band}_{args.mode}_converted.json"
        with open(output_file, 'w') as f:
            f.write(output)
        print(f"\nResult saved to: {output_file}")
    else:
        # Output as-is (raw JSON from lte_scan_example)
        output = json.dumps(scan_result, indent=2)
        print(output)
        
        # Save to file
        output_file = f"cell_scan_band{args.band}_{args.mode}.json"
        with open(output_file, 'w') as f:
            f.write(output)
        print(f"\nResult saved to: {output_file}")

if __name__ == "__main__":
    main()
