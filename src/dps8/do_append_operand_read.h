word24 do_append_operand_read (word36 * data, uint nWords)
  {
    DCDstruct * i = & cpu.currentInstruction;
    DBGAPP ("do_append_cycle(Entry) thisCycle=OPERAND_READ\n");
    DBGAPP ("do_append_cycle(Entry) lastCycle=%s\n",
            str_pct (cpu.apu.lastCycle));
    DBGAPP ("do_append_cycle(Entry) CA %06o\n",
            cpu.TPR.CA);
    DBGAPP ("do_append_cycle(Entry) n=%2u\n",
            nWords);
    DBGAPP ("do_append_cycle(Entry) PPR.PRR=%o PPR.PSR=%05o\n",
            cpu.PPR.PRR, cpu.PPR.PSR);
    DBGAPP ("do_append_cycle(Entry) TPR.TRR=%o TPR.TSR=%05o\n",
            cpu.TPR.TRR, cpu.TPR.TSR);

    if (i->b29)
      {
        DBGAPP ("do_append_cycle(Entry) isb29 PRNO %o\n",
                GET_PRN (IWB_IRODD));
      }

#ifdef WAM
    // AL39: The associative memory is ignored (forced to "no match") during
    // address preparation.
    // lptp,lptr,lsdp,lsdr,sptp,sptr,ssdp,ssdr
    // Unfortunately, ISOLTS doesn't try to execute any of these in append mode.
    // XXX should this be only for OPERAND_READ and OPERAND_STORE?
#if 1
    bool nomatch = i->opcode10 == 01232 ||
                   i->opcode10 == 01254 ||
                   i->opcode10 == 01154 ||
                   i->opcode10 == 01173 ||
                   i->opcode10 == 00557 ||
                   i->opcode10 == 00257;
#else
    bool nomatch = ((i->opcode == 0232 || i->opcode == 0254 ||
                     i->opcode == 0154 || i->opcode == 0173) &&
                     i->opcodeX ) ||
                    ((i->opcode == 0557 || i->opcode == 0257) &&
                     ! i->opcodeX);
#endif
#else
    const bool nomatch = true;
#endif

    processor_cycle_type lastCycle = cpu.apu.lastCycle;
    cpu.apu.lastCycle = OPERAND_READ;

    DBGAPP ("do_append_cycle(Entry) XSF %o\n", cpu.cu.XSF);

    PNL (L68_ (cpu.apu.state = 0;))

    cpu.RSDWH_R1 = 0;
    
    cpu.acvFaults = 0;

//#define FMSG(x) x
#define FMSG(x) 
    FMSG (char * acvFaultsMsg = "<unknown>";)

    word24 finalAddress = (word24) -1;  // not everything requires a final
                                        // address
    
////////////////////////////////////////
//
// Sheet 1: "START APPEND"
//
////////////////////////////////////////

// START APPEND
    word3 n = 0; // PRn to be saved to TSN_PRNO

    // goto A;

////////////////////////////////////////
//
// Sheet 2: "A"
//
////////////////////////////////////////

//
//  A:
//    Get SDW

// A:;

    //PNL (cpu.APUMemAddr = address;)
    PNL (cpu.APUMemAddr = cpu.TPR.CA;)

    DBGAPP ("do_append_cycle(A)\n");
    
#ifdef WAM
    // is SDW for C(TPR.TSR) in SDWAM?
    if (nomatch || ! fetch_sdw_from_sdwam (cpu.TPR.TSR))
#endif
      {
        // No
        DBGAPP ("do_append_cycle(A):SDW for segment %05o not in SDWAM\n",
                 cpu.TPR.TSR);
        
        DBGAPP ("do_append_cycle(A):DSBR.U=%o\n",
                cpu.DSBR.U);
        
        if (cpu.DSBR.U == 0)
          {
            fetch_dsptw (cpu.TPR.TSR);
            
            if (! cpu.PTW0.DF)
              doFault (FAULT_DF0 + cpu.PTW0.FC, fst_zero,
                       "do_append_cycle(A): PTW0.F == 0");
            
            if (! cpu.PTW0.U)
              modify_dsptw (cpu.TPR.TSR);
            
            fetch_psdw (cpu.TPR.TSR);
          }
        else
          fetch_nsdw (cpu.TPR.TSR); // load SDW0 from descriptor segment table.
        
        if (cpu.SDW0.DF == 0)
          {
            DBGAPP ("do_append_cycle(A): SDW0.F == 0! "
                    "Initiating directed fault\n");
            // initiate a directed fault ...
            doFault (FAULT_DF0 + cpu.SDW0.FC, fst_zero, "SDW0.F == 0");
          }
        // load SDWAM .....
        load_sdwam (cpu.TPR.TSR, nomatch);
      }
    DBGAPP ("do_append_cycle(A) R1 %o R2 %o R3 %o E %o\n",
            cpu.SDW->R1, cpu.SDW->R2, cpu.SDW->R3, cpu.SDW->E);

    // Yes...
    cpu.RSDWH_R1 = cpu.SDW->R1;

////////////////////////////////////////
//
// Sheet 3: "B"
//
////////////////////////////////////////

//
// B: Check the ring
//

    DBGAPP ("do_append_cycle(B)\n");

    // check ring bracket consistency
    
    //C(SDW.R1) <= C(SDW.R2) <= C(SDW .R3)?
    if (! (cpu.SDW->R1 <= cpu.SDW->R2 && cpu.SDW->R2 <= cpu.SDW->R3))
      {
        // Set fault ACV0 = IRO
        cpu.acvFaults |= ACV0;
        PNL (L68_ (cpu.apu.state |= apu_FLT;))
        FMSG (acvFaultsMsg = "acvFaults(B) C(SDW.R1) <= C(SDW.R2) <= "
                              "C(SDW .R3)";)
      }

    // lastCycle == RTCD_OPERAND_FETCH
    // if a fault happens between the RTCD_OPERAND_FETCH and the INSTRUCTION_FETCH
    // of the next instruction - this happens about 35 time for just booting  and
    // shutting down multics -- a stored lastCycle is useless.
    // the opcode is preserved accross faults and only replaced as the
    // INSTRUCTION_FETCH succeeds.
    if (lastCycle == RTCD_OPERAND_FETCH)
      sim_warn ("%s: lastCycle == RTCD_OPERAND_FETCH opcode %0#o\n", __func__, i->opcode10);

    //
    // B1: The operand is one of: an instruction, data to be read or data to be
    //     written
    //

    // Is OPCODE call6?
    if (/* thisCycle == OPERAND_READ && */ (i->info->flags & CALL6_INS))
      goto E;


    // Transfer or instruction fetch?
    if (/*thisCycle == OPERAND_READ &&*/ (i->info->flags & TRANSFER_INS))
      goto F;

    //
    // check read bracket for read access
    //

    DBGAPP ("do_append_cycle(B):!STR-OP\n");
        
    // No
    // C(TPR.TRR) > C(SDW .R2)?
    if (cpu.TPR.TRR > cpu.SDW->R2)
      {
        DBGAPP ("ACV3\n");
        DBGAPP ("do_append_cycle(B) ACV3\n");
        //Set fault ACV3 = ORB
        cpu.acvFaults |= ACV3;
        PNL (L68_ (cpu.apu.state |= apu_FLT;))
        FMSG (acvFaultsMsg = "acvFaults(B) C(TPR.TRR) > C(SDW .R2)";)
      }
        
    if (cpu.SDW->R == 0)
      {
          // isolts 870
          cpu.TPR.TRR = cpu.PPR.PRR;

        //C(PPR.PSR) = C(TPR.TSR)?
        if (cpu.PPR.PSR != cpu.TPR.TSR)
          {
            DBGAPP ("ACV4\n");
            DBGAPP ("do_append_cycle(B) ACV4\n");
            //Set fault ACV4 = R-OFF
            cpu.acvFaults |= ACV4;
            PNL (L68_ (cpu.apu.state |= apu_FLT;))
            FMSG (acvFaultsMsg = "acvFaults(B) C(PPR.PSR) = C(TPR.TSR)";)
          }
        else
          {
// sim_warn ("do_append_cycle(B) SDW->R == 0 && cpu.PPR.PSR == cpu.TPR.TSR: %0#o\n", cpu.PPR.PSR);
          }
      }

    goto G;
    
////////////////////////////////////////
//
// Sheet 4: "C" "D"
//
////////////////////////////////////////


D:;
    DBGAPP ("do_append_cycle(D)\n");
    
    // transfer or instruction fetch

    // check ring alarm to catch outbound transfers

    if (cpu.rRALR == 0)
        goto G;
    
    // C(PPR.PRR) < RALR?
    if (! (cpu.PPR.PRR < cpu.rRALR))
      {
        DBGAPP ("ACV13\n");
        DBGAPP ("acvFaults(D) C(PPR.PRR) %o < RALR %o\n", 
                cpu.PPR.PRR, cpu.rRALR);
        cpu.acvFaults |= ACV13;
        PNL (L68_ (cpu.apu.state |= apu_FLT;))
        FMSG (acvFaultsMsg = "acvFaults(D) C(PPR.PRR) < RALR";)
      }
    
    goto G;
    
////////////////////////////////////////
//
// Sheet 5: "E"
//
////////////////////////////////////////

E:;

    //
    // check ring bracket for instruction fetch after call6 instruction
    //   (this is the call6 read operand)
    //

    DBGAPP ("do_append_cycle(E): CALL6\n");
    DBGAPP ("do_append_cycle(E): E %o G %o PSR %05o TSR %05o CA %06o "
            "EB %06o R %o%o%o TRR %o PRR %o\n",
            cpu.SDW->E, cpu.SDW->G, cpu.PPR.PSR, cpu.TPR.TSR, cpu.TPR.CA,
            cpu.SDW->EB, cpu.SDW->R1, cpu.SDW->R2, cpu.SDW->R3,
            cpu.TPR.TRR, cpu.PPR.PRR);

    //SDW.E set ON?
    if (! cpu.SDW->E)
      {
        DBGAPP ("ACV2 b\n");
        DBGAPP ("do_append_cycle(E) ACV2\n");
        // Set fault ACV2 = E-OFF
        cpu.acvFaults |= ACV2;
        PNL (L68_ (cpu.apu.state |= apu_FLT;))
        FMSG (acvFaultsMsg = "acvFaults(E) SDW .E set OFF";)
      }
    
    //SDW .G set ON?
    if (cpu.SDW->G)
      goto E1;
    
    // C(PPR.PSR) = C(TPR.TSR)?
    if (cpu.PPR.PSR == cpu.TPR.TSR && ! TST_I_ABS)
      goto E1;
    
    // XXX This doesn't seem right
    // EB is word 15; masking address makes no sense; rather 0-extend EB
    // Fixes ISOLTS 880-01
    if (cpu.TPR.CA >= (word18) cpu.SDW->EB)
      {
        DBGAPP ("ACV7\n");
        DBGAPP ("do_append_cycle(E) ACV7\n");
        // Set fault ACV7 = NO GA
        cpu.acvFaults |= ACV7;
        PNL (L68_ (cpu.apu.state |= apu_FLT;))
        FMSG (acvFaultsMsg = "acvFaults(E) TPR.CA4-17 >= SDW.CL";)
      }
    
E1:
    DBGAPP ("do_append_cycle(E1): CALL6 (cont'd)\n");

    // C(TPR.TRR) > SDW.R3?
    if (cpu.TPR.TRR > cpu.SDW->R3)
      {
        DBGAPP ("ACV8\n");
        DBGAPP ("do_append_cycle(E) ACV8\n");
        //Set fault ACV8 = OCB
        cpu.acvFaults |= ACV8;
        PNL (L68_ (cpu.apu.state |= apu_FLT;))
        FMSG (acvFaultsMsg = "acvFaults(E1) C(TPR.TRR) > SDW.R3";)
      }
    
    // C(TPR.TRR) < SDW.R1?
    if (cpu.TPR.TRR < cpu.SDW->R1)
      {
        DBGAPP ("ACV9\n");
        DBGAPP ("do_append_cycle(E) ACV9\n");
        // Set fault ACV9 = OCALL
        cpu.acvFaults |= ACV9;
        PNL (L68_ (cpu.apu.state |= apu_FLT;))
        FMSG (acvFaultsMsg = "acvFaults(E1) C(TPR.TRR) < SDW.R1";)
      }
    
    
    // C(TPR.TRR) > C(PPR.PRR)?
    if (cpu.TPR.TRR > cpu.PPR.PRR)
      {
        // C(PPR.PRR) < SDW.R2?
        if (cpu.PPR.PRR < cpu.SDW->R2)
          {
            DBGAPP ("ACV10\n");
            DBGAPP ("do_append_cycle(E) ACV10\n");
            // Set fault ACV10 = BOC
            cpu.acvFaults |= ACV10;
            PNL (L68_ (cpu.apu.state |= apu_FLT;))
            FMSG (acvFaultsMsg = "acvFaults(E1) C(TPR.TRR) > C(PPR.PRR) && "
                  "C(PPR.PRR) < SDW.R2";)
          }
      }

    
    DBGAPP ("do_append_cycle(E1): CALL6 TPR.TRR %o SDW->R2 %o\n",
            cpu.TPR.TRR, cpu.SDW->R2);

    // C(TPR.TRR) > SDW.R2?
    if (cpu.TPR.TRR > cpu.SDW->R2)
      {
        // SDW.R2 -> C(TPR.TRR)
        cpu.TPR.TRR = cpu.SDW->R2;
      }

    DBGAPP ("do_append_cycle(E1): CALL6 TPR.TRR %o\n", cpu.TPR.TRR);
    
    goto G;
    
////////////////////////////////////////
//
// Sheet 6: "F"
//
////////////////////////////////////////

F:;
    PNL (L68_ (cpu.apu.state |= apu_PIAU;))
    DBGAPP ("do_append_cycle(F): transfer or instruction fetch\n");

    //
    // check ring bracket for instruction fetch
    //

    // C(TPR.TRR) < C(SDW .R1)?
    // C(TPR.TRR) > C(SDW .R2)?
    if (cpu.TPR.TRR < cpu.SDW->R1 ||
        cpu.TPR.TRR > cpu.SDW->R2)
      {
        DBGAPP ("ACV1 a/b\n");
        DBGAPP ("acvFaults(F) ACV1 !( C(SDW .R1) %o <= C(TPR.TRR) %o <= C(SDW .R2) %o )\n",
                cpu.SDW->R1, cpu.TPR.TRR, cpu.SDW->R2);
        cpu.acvFaults |= ACV1;
        PNL (L68_ (cpu.apu.state |= apu_FLT;))
        FMSG (acvFaultsMsg = "acvFaults(F) C(TPR.TRR) < C(SDW .R1)";)
      }
    // SDW .E set ON?
    if (! cpu.SDW->E)
      {
        DBGAPP ("ACV2 c \n");
        DBGAPP ("do_append_cycle(F) ACV2\n");
        cpu.acvFaults |= ACV2;
        PNL (L68_ (cpu.apu.state |= apu_FLT;))
        FMSG (acvFaultsMsg = "acvFaults(F) SDW .E set OFF";)
      }
    
    // C(PPR.PRR) = C(TPR.TRR)?
    if (cpu.PPR.PRR != cpu.TPR.TRR)
      {
        DBGAPP ("ACV12\n");
        DBGAPP ("do_append_cycle(F) ACV12\n");
        //Set fault ACV12 = CRT
        cpu.acvFaults |= ACV12;
        PNL (L68_ (cpu.apu.state |= apu_FLT;))
        FMSG (acvFaultsMsg = "acvFaults(F) C(PPR.PRR) != C(TPR.TRR)";)
      }
    
    goto D;

////////////////////////////////////////
//
// Sheet 7: "G"
//
////////////////////////////////////////

G:;
    
    DBGAPP ("do_append_cycle(G)\n");
    
    //C(TPR.CA)0,13 > SDW.BOUND?
    if (((cpu.TPR.CA >> 4) & 037777) > cpu.SDW->BOUND)
      {
        DBGAPP ("ACV15\n");
        DBGAPP ("do_append_cycle(G) ACV15\n");
        cpu.acvFaults |= ACV15;
        PNL (L68_ (cpu.apu.state |= apu_FLT;))
        FMSG (acvFaultsMsg = "acvFaults(G) C(TPR.CA)0,13 > SDW.BOUND";)
        DBGAPP ("acvFaults(G) C(TPR.CA)0,13 > SDW.BOUND\n"
                "   CA %06o CA>>4 & 037777 %06o SDW->BOUND %06o",
                cpu.TPR.CA, ((cpu.TPR.CA >> 4) & 037777), cpu.SDW->BOUND);
      }
    
    if (cpu.acvFaults)
      {
        DBGAPP ("do_append_cycle(G) acvFaults\n");
        PNL (L68_ (cpu.apu.state |= apu_FLT;))
        // Initiate an access violation fault
        doFault (FAULT_ACV, (_fault_subtype) {.fault_acv_subtype=cpu.acvFaults},
                 "ACV fault");
      }

    // is segment C(TPR.TSR) paged?
    if (cpu.SDW->U)
      goto H; // Not paged
    
    // Yes. segment is paged ...
    // is PTW for C(TPR.CA) in PTWAM?
    
    DBGAPP ("do_append_cycle(G) CA %06o\n", cpu.TPR.CA);
#ifdef WAM
    if (nomatch ||
        ! fetch_ptw_from_ptwam (cpu.SDW->POINTER, cpu.TPR.CA))  //TPR.CA))
#endif
      {
        fetch_ptw (cpu.SDW, cpu.TPR.CA);
        if (! cpu.PTW0.DF)
          {
            // initiate a directed fault
            doFault (FAULT_DF0 + cpu.PTW0.FC, (_fault_subtype) {.bits=0},
                     "PTW0.F == 0");
          }
        loadPTWAM (cpu.SDW->POINTER, cpu.TPR.CA, nomatch); // load PTW0 to PTWAM
      }
    
    // Prepage mode?
    // check for "uninterruptible" EIS instruction
    // ISOLTS-878 02: mvn,cmpn,mvne,ad3d; obviously also
    // ad2/3d,sb2/3d,mp2/3d,dv2/3d
    // DH03 p.8-13: probably also mve,btd,dtb
#if 0
    //works
    if (i->opcodeX && ((i->opcode & 0770) == 0200|| (i->opcode & 0770) == 0220
        || (i->opcode & 0770)== 020|| (i->opcode & 0770) == 0300))
#endif
#if 1
    if ((i->opcode10 & 01750) == 01200 || // ad2d sb2d mp2d dv2d ad3d sb3d mp3d dv3d
        (i->opcode10 & 01770) == 01020 || // mve mvne
        (i->opcode10 & 01770) == 01300)   // mvn btd cmpn dtb
#endif
      {
        do_ptw2 (cpu.SDW, cpu.TPR.CA);
      } 
    goto I;
    
////////////////////////////////////////
//
// Sheet 8: "H", "I"
//
////////////////////////////////////////

H:;
    DBGAPP ("do_append_cycle(H): FANP\n");

    PNL (L68_ (cpu.apu.state |= apu_FANP;))
    set_apu_status (apuStatus_FANP);

    DBGAPP ("do_append_cycle(H): SDW->ADDR=%08o CA=%06o \n",
            cpu.SDW->ADDR, cpu.TPR.CA);

    finalAddress = (cpu.SDW->ADDR & 077777760) + cpu.TPR.CA;
    finalAddress &= 0xffffff;

    PNL (cpu.APUMemAddr = finalAddress;)
    
    DBGAPP ("do_append_cycle(H:FANP): (%05o:%06o) finalAddress=%08o\n",
            cpu.TPR.TSR, cpu.TPR.CA, finalAddress);
    
    goto HI;
    
I:;

// Set PTW.M

    DBGAPP ("do_append_cycle(I): FAP\n");
    
    // final address paged
    set_apu_status (apuStatus_FAP);
    PNL (L68_ (cpu.apu.state |= apu_FAP;))
    
    word24 y2 = cpu.TPR.CA % 1024;
    
    // AL39: The hardware ignores low order bits of the main memory page
    // address according to page size    
    finalAddress = (((word24)cpu.PTW->ADDR & 0777760) << 6) + y2; 
    finalAddress &= 0xffffff;
    PNL (cpu.APUMemAddr = finalAddress;)
    
#ifdef L68
    if (cpu.MR_cache.emr && cpu.MR_cache.ihr)
      add_APU_history (APUH_FAP);
#endif
    DBGAPP ("do_append_cycle(H:FAP): (%05o:%06o) finalAddress=%08o\n",
            cpu.TPR.TSR, cpu.TPR.CA, finalAddress);

    // goto HI;

HI:
    DBGAPP ("do_append_cycle(HI)\n");

    // isolts 870
    cpu.cu.XSF = 1;
    sim_debug (DBG_TRACEEXT, & cpu_dev, "loading of cpu.TPR.TSR sets XSF to 1\n");

    core_readN (finalAddress, data, nWords, "OPERAND_READ");

    // is OPCODE call6?
    if (/*thisCycle == OPERAND_READ &&*/ (i->info->flags & CALL6_INS))
      goto N;

    // Transfer or instruction fetch?
    if (/*thisCycle == OPERAND_READ &&*/ (i->info->flags & TRANSFER_INS))
      goto L;

    // APU data movement?
    //  handled above
   goto Exit;

    
////////////////////////////////////////
//
// Sheet 10: "K", "L", "M", "N"
//
////////////////////////////////////////


L:; // Transfer or instruction fetch

    DBGAPP ("do_append_cycle(L)\n");

    // Is OPCODE tspn?
    if (/*thisCycle == OPERAND_READ &&*/ (i->info->flags & TSPN_INS))
      {
        //word3 n;
        if (i->opcode <= 0273)
          n = (i->opcode & 3);
        else
          n = (i->opcode & 3) + 4;

        // C(PPR.PRR) -> C(PRn .RNR)
        // C(PPR.PSR) -> C(PRn .SNR)
        // C(PPR.IC) -> C(PRn .WORDNO)
        // 000000 -> C(PRn .BITNO)
        cpu.PR[n].RNR = cpu.PPR.PRR;
// According the AL39, the PSR is 'undefined' in absolute mode.
// ISOLTS thinks means don't change the operand
        if (get_addr_mode () == APPEND_mode)
          cpu.PR[n].SNR = cpu.PPR.PSR;
        cpu.PR[n].WORDNO = (cpu.PPR.IC + 1) & MASK18;
        SET_PR_BITNO (n, 0);
        HDBGRegPRW (n, "app tspn");
      }

    goto KL;

KL:
    DBGAPP ("do_append_cycle(KL)\n");

    // C(TPR.TSR) -> C(PPR.PSR)
    cpu.PPR.PSR = cpu.TPR.TSR;
    // C(TPR.CA) -> C(PPR.IC) 
    cpu.PPR.IC = cpu.TPR.CA;

    goto M;


M: // Set P
    DBGAPP ("do_append_cycle(M)\n");

    // C(TPR.TRR) = 0?
    if (cpu.TPR.TRR == 0)
      {
        // C(SDW.P) -> C(PPR.P) 
        cpu.PPR.P = cpu.SDW->P;
      }
    else
      {
        // 0 C(PPR.P)
        cpu.PPR.P = 0;
      }

    goto Exit; 

N: // CALL6
    DBGAPP ("do_append_cycle(N)\n");

    // C(TPR.TRR) = C(PPR.PRR)?
    if (cpu.TPR.TRR == cpu.PPR.PRR)
      {
        // C(PR6.SNR) -> C(PR7.SNR) 
        cpu.PR[7].SNR = cpu.PR[6].SNR;
        DBGAPP ("do_append_cycle(N) PR7.SNR = PR6.SNR %05o\n", cpu.PR[7].SNR);
      }
    else
      {
        // C(DSBR.STACK) || C(TPR.TRR) -> C(PR7.SNR)
        cpu.PR[7].SNR = ((word15) (cpu.DSBR.STACK << 3)) | cpu.TPR.TRR;
        DBGAPP ("do_append_cycle(N) STACK %05o TRR %o\n",
                cpu.DSBR.STACK, cpu.TPR.TRR);
        DBGAPP ("do_append_cycle(N) PR7.SNR = STACK||TRR  %05o\n", cpu.PR[7].SNR);
      }

    // C(TPR.TRR) -> C(PR7.RNR)
    cpu.PR[7].RNR = cpu.TPR.TRR;
    // 00...0 -> C(PR7.WORDNO)
    cpu.PR[7].WORDNO = 0;
    // 000000 -> C(PR7.BITNO)
    SET_PR_BITNO (7, 0);
    HDBGRegPRW (7, "app call6");
    // C(TPR.TRR) -> C(PPR.PRR)
    cpu.PPR.PRR = cpu.TPR.TRR;
    // C(TPR.TSR) -> C(PPR.PSR)
    cpu.PPR.PSR = cpu.TPR.TSR;
    // C(TPR.CA) -> C(PPR.IC)
    cpu.PPR.IC = cpu.TPR.CA;

    goto M;

Exit:;

    PNL (cpu.APUDataBusOffset = cpu.TPR.CA;)
    PNL (cpu.APUDataBusAddr = finalAddress;)

    PNL (L68_ (cpu.apu.state |= apu_FA;))

    DBGAPP ("do_append_cycle (Exit) PRR %o PSR %05o P %o IC %06o\n",
            cpu.PPR.PRR, cpu.PPR.PSR, cpu.PPR.P, cpu.PPR.IC);
    DBGAPP ("do_append_cycle (Exit) TRR %o TSR %05o TBR %02o CA %06o\n",
            cpu.TPR.TRR, cpu.TPR.TSR, cpu.TPR.TBR, cpu.TPR.CA);

    return finalAddress;    // or 0 or -1???
  }

