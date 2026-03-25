sed 's/|/ /g' | awk --bignum '

func chnstr(chn) {
    if(chn == 1){
        return "A"
    } else if(chn == 2){
        return "B"
    } else if(chn == 4){
        return "C"
    }
    return "Unknown Channel"
}

func opstr(chn, op, msTask) {
    a_op[1] = "PutFullData"
    a_op[2] = "PutPartialData"
    a_op[3] = "ArithmeticData"
    a_op[4] = "LogicalData"
    a_op[5] = "Get"
    a_op[6] = "Hint"
    a_op[7] = "AcquireBlock"
    a_op[8] = "AcquirePerm"

    b_op[1] = "PutFullData"
    b_op[2] = "PutPartialData"
    b_op[3] = "ArithmeticData"
    b_op[4] = "LogicalData"
    b_op[5] = "Get"
    b_op[6] = "Hint"
    b_op[7] = "Probe"

    c_op[1] = "AccessAck"
    c_op[2] = "AccessAckData"
    c_op[3] = "HintAck"
    c_op[4] = "Invalid Opcode"
    c_op[5] = "ProbeAck"
    c_op[6] = "ProbeAckData"
    c_op[7] = "Release"
    c_op[8] = "ReleaseData"

    d_op[1] = "AccessAck"
    d_op[2] = "AccessAckData"
    d_op[3] = "HintAck"
    d_op[4] = "Invalid Opcode"
    d_op[5] = "Grant"
    d_op[6] = "GrantData"
    d_op[7] = "ReleaseAck"

    msa_op[2] = "AccessAckData"
    msa_op[3] = "HintAck"
    msa_op[5] = "Grant"
    msa_op[6] = "GrantData"
    msa_op[7] = "Release"
    msa_op[8] = "ReleaseData"

    ret = "Unknown OP"

    if(msTask == 0){
        switch(chn) {
            case 1:
                ret = a_op[op+1]
                break;
            case 2:
                ret = b_op[op+1]
                break;
            case 4:
                ret = c_op[op+1]
                break;
        }
    } else {
        switch(chn) {
            case 1:
                ret = msa_op[op+1]
                break;
            case 2:
                ret = c_op[op+1]
                break;
        }
    }
    return ret
}
func paramstr(op, param) {
    split("Grow NtoB_Grow NtoT_Grow BtoT", grow, "_")
    split("Cap toT_Cap toB_Cap toN", cap, "_")
    split("Shrink TtoB_Shrink TtoN_Shrink BtoN_Report TotT_Report BtoB_Report NtoN", report, "_")

    ret = "Reserved"
    if(op == 'AcquireBlock' || op == 'AcquirePerm'){
        ret = grow[param+1]
    } else if(op == 'Probe'){
        ret = cap[param+1]
    } else if(op == 'Release' || op == 'ReleaseData' || op == 'ProbeAck' || op == 'ProbeAckData'){
        ret = report[param+1]
    } else if(op == 'Grant' || op == 'GrantData'){
        ret = cap[param+1]
    }
    return ret
}
func taskstr(msTask) {
    if(msTask == 0){
        return "Chn "
    } else {
        return "Mshr"
    }
}
func fulladdr_tltest(tag, set, bank,   setbits, bankbits, offsetbits) {
    # Defaults for current TL test config: sets=128 (setBits=7), 4 banks (bankBits=2), blockBytes=64 (offsetBits=6).
    # You can override via env: L2_SET_BITS / L2_BANK_BITS / L2_OFFSET_BITS.
    setbits = (("L2_SET_BITS" in ENVIRON) ? ENVIRON["L2_SET_BITS"] + 0 : 7)
    bankbits = (("L2_BANK_BITS" in ENVIRON) ? ENVIRON["L2_BANK_BITS"] + 0 : 2)
    offsetbits = (("L2_OFFSET_BITS" in ENVIRON) ? ENVIRON["L2_OFFSET_BITS"] + 0 : 6)
    return (tag * (2^(setbits + bankbits)) + (set * (2^bankbits)) + bank) * (2^offsetbits);
}
func fulladdr_xs(tag, set, bank,   setbits, bankbits, offsetbits) {
    setbits = (("L2_SET_BITS" in ENVIRON) ? ENVIRON["L2_SET_BITS"] + 0 : 9)
    bankbits = (("L2_BANK_BITS" in ENVIRON) ? ENVIRON["L2_BANK_BITS"] + 0 : 2)
    offsetbits = (("L2_OFFSET_BITS" in ENVIRON) ? ENVIRON["L2_OFFSET_BITS"] + 0 : 6)
    return (tag * (2^(setbits + bankbits)) + (set * (2^bankbits)) + bank) * (2^offsetbits);
}

# TODO: add param
{
    # Reverse the order of fields, except for the first field 'ID'
    # - because the newly added fields will occur at the front of the record (ChiselDB to blame)
    # - so we reverse the record to make new fileds appear at the end, easier for parser script
    num_fields = 1
    for (i = 2; i <= NF; i++) {
        if ($i != "") {
            num_fields++
            fields[num_fields] = $i
        }
    }
    for (i = 2; i <= num_fields; i++) {
        $i = fields[num_fields - i + 2]
    }

    ID = $1;
    SITE = $2;
    STAMP = $3;
    MSHRTASK = $4;
    CHANNEL = $5;
    OPCODE = $6;
    TAG = $7;
    SSET = $8;
    DIRHIT = $9;
    DIRWAY = $10;
    ALLOCVALID = $11;
    ALLOCPTR = $12;
    MSHRID = $13;
    METAWVALID = $14;
    METAWWAY = $15;
    RMW = $16;

    match(SITE, /[0-9]+$/)
    BANK = substr(SITE, RSTART, RLENGTH)
    ADDR = fulladdr_tltest(TAG, SSET, BANK)

    $1 = STAMP;
    $2 = SITE;
    $3 = taskstr(MSHRTASK);
    $4 = chnstr(CHANNEL);
    $5 = sprintf("%14s |", opstr(CHANNEL, OPCODE, MSHRTASK));
    $6 = sprintf("%6lx", TAG);
    $7 = sprintf("%6lx", SSET);
    $8 = sprintf("%12lx", ADDR);

    $9 = sprintf("|DIR %d %d", DIRHIT, DIRWAY);
    $10 = sprintf("|ALLOC %d %2d", ALLOCVALID, ALLOCPTR);
    $11 = sprintf("|MSHRID %2d", MSHRID);
    $12 = sprintf("|METAW %d %d", METAWVALID, METAWWAY);
    $13 = sprintf("|RMW %d", RMW);
    $14 = "";
    $15 = "";
    $16 = "";
}

1                                   # print every line
'
