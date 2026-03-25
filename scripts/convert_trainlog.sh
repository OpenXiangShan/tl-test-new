#!/bin/bash
sed 's/|/ /g' | awk --bignum '
function get_pfsource(pf) {
    if (pf == "") return "Unknown"
    if (pf == 0) return "NoWhere"
    if (pf == 1) return "SMS"
    if (pf == 2) return "BOP"
    if (pf == 3) return "PBOP"
    if (pf == 4) return "Stream"
    if (pf == 5) return "Stride"
    if (pf == 6) return "TP"
    return sprintf("PF(%d)", pf)
}
function get_reqsource(req) {
    if (req == "") return "Unknown"
    if (req == 0) return "NoWhere"
    if (req == 1) return "CPUInst"
    if (req == 2) return "CPULoad"
    if (req == 3) return "CPUStore"
    if (req == 4) return "CPUAtomic"
    if (req == 5) return "L1InstPF"
    if (req == 6) return "L1DataPF"
    if (req == 7) return "PTW"
    if (req == 8) return "L2_BOP"
    if (req == 9) return "L2_PBOP"
    if (req == 10) return "L2_SMS"
    if (req == 11) return "L2_Stream"
    if (req == 12) return "L2_Stride"
    if (req == 13) return "L2_TP"
    if (req == 18) return "MatrixData"
    return sprintf("Req(%d)", req)
}
{
  if ($1 == "ID" || $1 == "id" || $1 == "") next;
  reqsource  = $2
  pfsource   = $3
  source_id  = $4
  prefetched = $5
  hit        = $6
  needt      = $7
  vaddr_0    = $8
  vaddr_1    = $9
  paddr      = $10
  stamp      = $11
  site       = $12
  addr_hex   = sprintf("%08lx", paddr)
  vaddr_hex  = sprintf("%08lx", vaddr_0)
  hit_str    = (hit == 1) ? "Hit" : "Miss"
  pref_str   = (prefetched == 1) ? "Yes" : "No "
  pf_name    = get_pfsource(pfsource)
  req_name   = get_reqsource(reqsource)
  printf("Cycle: %8d | PAddr: 0x%-8s | VAddr: 0x%-10s | %-4s | Prefetched: %-3s | reqSrc: %-10s | pfSrc: %-7s | Site: %s\n", stamp, addr_hex, vaddr_hex, hit_str, pref_str, req_name, pf_name, site)
}
'
