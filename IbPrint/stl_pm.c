/* BEGIN_ICS_COPYRIGHT7 ****************************************

Copyright (c) 2015, Intel Corporation

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

** END_ICS_COPYRIGHT7   ****************************************/


#include "stl_print.h"
#include <iba/stl_pm.h>

void PrintStlPortStatusRsp(PrintDest_t *dest, int indent, const STL_PORT_STATUS_RSP *pStlPortStatusRsp)
{
	int i, j;

	PrintFunc(dest, "%*sPort Number           %10u\n",
		indent, "",
		pStlPortStatusRsp->PortNumber);
	PrintFunc(dest, "%*sVL Select Mask        0x%08x\n",
		indent, "",
		pStlPortStatusRsp->VLSelectMask);

	PrintFunc(dest, "%*sPerformance: Transmit\n",
		indent, "");
	PrintFunc(dest, "%*s    Xmit Data   %20"PRIu64" MB (%"PRIu64" Flits)\n",
		indent, "",
		pStlPortStatusRsp->PortXmitData/FLITS_PER_MB,
		pStlPortStatusRsp->PortXmitData);
	PrintFunc(dest, "%*s    Xmit Pkts   %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortXmitPkts);
	PrintFunc(dest, "%*s    MC Xmt Pkts %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortMulticastXmitPkts);

	PrintFunc(dest, "%*sPerformance: Receive\n",
		indent, "");
	PrintFunc(dest, "%*s    Rcv Data    %20"PRIu64" MB (%"PRIu64" Flits)\n",
		indent, "",
		pStlPortStatusRsp->PortRcvData/FLITS_PER_MB,
		pStlPortStatusRsp->PortRcvData);
	PrintFunc(dest, "%*s    Rcv Pkts    %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortRcvPkts);
	PrintFunc(dest, "%*s    MC Rcv Pkts %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortMulticastRcvPkts);

	PrintFunc(dest, "%*sPerformance: Congestion\n",
		indent, "");
	PrintFunc(dest, "%*s    Xmit Wait             %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortXmitWait);
	PrintFunc(dest, "%*s    Congestion Discards   %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->SwPortCongestion);
	PrintFunc(dest, "%*s    Xmit Time Congestion  %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortXmitTimeCong);
	PrintFunc(dest, "%*s    Mark FECN             %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortMarkFECN);
	PrintFunc(dest, "%*s    Rcv FECN              %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortRcvFECN);
	PrintFunc(dest, "%*s    Rcv BECN              %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortRcvBECN);

	PrintFunc(dest, "%*sPerformance: Bubbles\n",
		indent, "");
	PrintFunc(dest, "%*s    Rcv Bubble            %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortRcvBubble);
	PrintFunc(dest, "%*s    Xmit Wasted BW        %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortXmitWastedBW);
	PrintFunc(dest, "%*s    Xmit Wait Data        %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortXmitWaitData);

	PrintFunc(dest, "%*sLink Qual Indicator       %20u (%s)\n",
		indent, "",
		pStlPortStatusRsp->lq.s.LinkQualityIndicator,
		StlLinkQualToText(pStlPortStatusRsp->lq.s.LinkQualityIndicator));

	PrintFunc(dest, "%*sErrors: Signal Integrity\n",
		indent, "");
	PrintFunc(dest, "%*s    Local Link Integ Err  %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->LocalLinkIntegrityErrors);
	PrintFunc(dest, "%*s    Rcv Errors            %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortRcvErrors);
	PrintFunc(dest, "%*s    Exc. Buffer Overrun   %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->ExcessiveBufferOverruns);
	PrintFunc(dest, "%*s    Link Error Recovery   %20u\n",	// 32 bit
		indent, "",
		pStlPortStatusRsp->LinkErrorRecovery);
	PrintFunc(dest, "%*s    Link Downed           %20u\n",	// 32 bit
		indent, "",
		pStlPortStatusRsp->LinkDowned);
	PrintFunc(dest, "%*s    Uncorrectable Errors  %20u\n",	//8 bit
		indent, "",
		pStlPortStatusRsp->UncorrectableErrors);
	PrintFunc(dest, "%*s    FM Config Errors      %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->FMConfigErrors);

	PrintFunc(dest, "%*sErrors: Security\n",
		indent, "");
	PrintFunc(dest, "%*s    Xmit Constraint       %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortXmitConstraintErrors);
	PrintFunc(dest, "%*s    Rcv Constraint        %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortRcvConstraintErrors);

	PrintFunc(dest, "%*sErrors: Other\n",
		indent, "");
	PrintFunc(dest, "%*s    Rcv Sw Relay Err      %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortRcvSwitchRelayErrors);
	PrintFunc(dest, "%*s    Xmit Discards         %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortXmitDiscards);
	PrintFunc(dest, "%*s    Rcv Rmt Phys Err      %20"PRIu64"\n",
		indent, "",
		pStlPortStatusRsp->PortRcvRemotePhysicalErrors);

	/* per_VL counters */
	for (i = 0, j = 0; i < MAX_PM_VLS; i++) {
		if ((pStlPortStatusRsp->VLSelectMask >> i) & (uint64)1) {
			PrintFunc(dest, "\n");
			PrintFunc(dest, "%*s    VL Number             %10d\n",
				indent, "",
				i);
			PrintFunc(dest, "%*s    Performance: Transmit\n",
				indent, "");
			PrintFunc(dest, "%*s         Xmit Data   %20"PRIu64" MB (%"PRIu64" Flits)\n",
				indent, "",
				pStlPortStatusRsp->VLs[j].PortVLXmitData/FLITS_PER_MB,
				pStlPortStatusRsp->VLs[j].PortVLXmitData);
			PrintFunc(dest, "%*s         Xmit Pkts   %20"PRIu64"\n",
				indent, "",
				pStlPortStatusRsp->VLs[j].PortVLXmitPkts);

			PrintFunc(dest, "%*s    Performance: Receive\n",
				indent, "");
			PrintFunc(dest, "%*s         Rcv Data    %20"PRIu64" MB (%"PRIu64" Flits)\n",
				indent, "",
				pStlPortStatusRsp->VLs[j].PortVLRcvData/FLITS_PER_MB,
				pStlPortStatusRsp->VLs[j].PortVLRcvData);
			PrintFunc(dest, "%*s         Rcv Pkts    %20"PRIu64"\n",
				indent, "",
				pStlPortStatusRsp->VLs[j].PortVLRcvPkts);

			PrintFunc(dest, "%*s    Performance: Congestion\n",
				indent, "");
			PrintFunc(dest, "%*s         Xmit Wait             %20"PRIu64"\n",
				indent, "",
				pStlPortStatusRsp->VLs[j].PortVLXmitWait);
			PrintFunc(dest, "%*s         Congestion Discards   %20"PRIu64"\n",
				indent, "",
				pStlPortStatusRsp->VLs[j].SwPortVLCongestion);
			PrintFunc(dest, "%*s         Xmit Time Congestion  %20"PRIu64"\n",
				indent, "",
				pStlPortStatusRsp->VLs[j].PortVLXmitTimeCong);
			PrintFunc(dest, "%*s         Mark FECN             %20"PRIu64"\n",
				indent, "",
				pStlPortStatusRsp->VLs[j].PortVLMarkFECN);
			PrintFunc(dest, "%*s         Rcv FECN              %20"PRIu64"\n",
				indent, "",
				pStlPortStatusRsp->VLs[j].PortVLRcvFECN);
			PrintFunc(dest, "%*s         Rcv BECN              %20"PRIu64"\n",
				indent, "",
				pStlPortStatusRsp->VLs[j].PortVLRcvBECN);

			PrintFunc(dest, "%*s    Performance: Bubbles\n",
				indent, "");
			PrintFunc(dest, "%*s         Rcv Bubble            %20"PRIu64"\n",
				indent, "",
				pStlPortStatusRsp->VLs[j].PortVLRcvBubble);
			PrintFunc(dest, "%*s         Xmit Wasted BW        %20"PRIu64"\n",
				indent, "",
				pStlPortStatusRsp->VLs[j].PortVLXmitWastedBW);
			PrintFunc(dest, "%*s         Xmit Wait Data        %20"PRIu64"\n",
				indent, "",
				pStlPortStatusRsp->VLs[j].PortVLXmitWaitData);

			PrintFunc(dest, "%*s    Errors: Other\n",
				indent, "");
			PrintFunc(dest, "%*s         Xmit Discards         %20"PRIu64"\n",
				indent, "",
				pStlPortStatusRsp->VLs[j].PortVLXmitDiscards);

			j++;
		}
	}
	return;
}

void PrintStlPortStatusRspSummary(PrintDest_t *dest, int indent, const STL_PORT_STATUS_RSP *pStlPortStatusRsp, int printLineByLine)
{
    if (printLineByLine) {
		PrintIntWithDots(dest, indent, "XmitDataMB", pStlPortStatusRsp->PortXmitData/FLITS_PER_MB);
		PrintIntWithDots(dest, indent, "XmitPkts", pStlPortStatusRsp->PortXmitPkts);
		PrintIntWithDots(dest, indent, "RcvDataMB", pStlPortStatusRsp->PortRcvData/FLITS_PER_MB);
		PrintIntWithDots(dest, indent, "RcvPkts", pStlPortStatusRsp->PortRcvPkts);
		PrintIntWithDots(dest, indent, "LinkQualityIndicator", pStlPortStatusRsp->lq.s.LinkQualityIndicator);
		PrintIntWithDots(dest, indent, "LocalLinkIntegrityErrors", pStlPortStatusRsp->LocalLinkIntegrityErrors);
		PrintIntWithDots(dest, indent, "LinkErrorRecovery", pStlPortStatusRsp->LinkErrorRecovery);	// 32 bit
	} else {
		PrintFunc(dest, "%*sXmit Data: %18"PRIu64" MB Pkts: %20"PRIu64"\n",
			indent, "",
			pStlPortStatusRsp->PortXmitData/FLITS_PER_MB,
			pStlPortStatusRsp->PortXmitPkts);
		PrintFunc(dest, "%*sRecv Data: %18"PRIu64" MB Pkts: %20"PRIu64"\n",
			indent, "",
			pStlPortStatusRsp->PortRcvData/FLITS_PER_MB,
			pStlPortStatusRsp->PortRcvPkts);
		PrintFunc(dest, "%*sLink Quality: %u (%s)\n",
			indent, "",
			pStlPortStatusRsp->lq.s.LinkQualityIndicator,
			StlLinkQualToText(pStlPortStatusRsp->lq.s.LinkQualityIndicator));
		PrintFunc(dest, "%*sIntegrity Err: %20"PRIu64" Err Recovery %10u\n",
			indent, "",
			pStlPortStatusRsp->LocalLinkIntegrityErrors,
			pStlPortStatusRsp->LinkErrorRecovery);	// 32 bit

	}
}

void PrintStlClearPortStatus(PrintDest_t *dest, int indent, const STL_CLEAR_PORT_STATUS *pStlClearPortStatus)
{
    if (pStlClearPortStatus->PortSelectMask[0]) {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx %016llx %016llx %016llx\n",
    		indent, "",
    		pStlClearPortStatus->PortSelectMask[0],
    		pStlClearPortStatus->PortSelectMask[1],
    		pStlClearPortStatus->PortSelectMask[2],
    		pStlClearPortStatus->PortSelectMask[3]);
    }
    else if (pStlClearPortStatus->PortSelectMask[1]) {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx %016llx %016llx\n",
    		indent, "",
    		pStlClearPortStatus->PortSelectMask[1],
    		pStlClearPortStatus->PortSelectMask[2],
    		pStlClearPortStatus->PortSelectMask[3]);
    }
    else if (pStlClearPortStatus->PortSelectMask[2]) {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx %016llx\n",
    		indent, "",
    		pStlClearPortStatus->PortSelectMask[2],
    		pStlClearPortStatus->PortSelectMask[3]);
    }
    else {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx\n",
    		indent, "",
    		pStlClearPortStatus->PortSelectMask[3]);
    }
	PrintFunc(dest, "%*s Counter Sel Mask   0x%08x\n",
		indent, "",
		pStlClearPortStatus->CounterSelectMask.AsReg32);
}

void PrintStlDataPortCountersRsp(PrintDest_t *dest, int indent, const STL_DATA_PORT_COUNTERS_RSP *pStlDataPortCountersRsp)
{
	int i, j, ii, jj;
	struct _port_dpctrs *port;

    if (pStlDataPortCountersRsp->PortSelectMask[0]) {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx %016llx %016llx %016llx\n",
    		indent, "",
    		pStlDataPortCountersRsp->PortSelectMask[0],
    		pStlDataPortCountersRsp->PortSelectMask[1],
    		pStlDataPortCountersRsp->PortSelectMask[2],
    		pStlDataPortCountersRsp->PortSelectMask[3]);
    }
    else if (pStlDataPortCountersRsp->PortSelectMask[1]) {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx %016llx %016llx\n",
    		indent, "",
    		pStlDataPortCountersRsp->PortSelectMask[1],
    		pStlDataPortCountersRsp->PortSelectMask[2],
    		pStlDataPortCountersRsp->PortSelectMask[3]);
    }
    else if (pStlDataPortCountersRsp->PortSelectMask[2]) {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx %016llx\n",
    		indent, "",
    		pStlDataPortCountersRsp->PortSelectMask[2],
    		pStlDataPortCountersRsp->PortSelectMask[3]);
    }
    else {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx\n",
    		indent, "",
    		pStlDataPortCountersRsp->PortSelectMask[3]);
    }
	PrintFunc(dest, "%*s VL Select Mask     0x%08x\n",
		indent, "",
		pStlDataPortCountersRsp->VLSelectMask);

	/* Count the bits in the mask and process the ports in succession */
	/* Assume that even though port numbers may not be contiguous, that entries */
	/*   in the array are */
	port = (struct _port_dpctrs *)&(pStlDataPortCountersRsp->Port[0]);
	for (i = 3; i >= 0; i--) {
		for (j = 0; j < MAX_PM_PORTS; j++) {
			if ((pStlDataPortCountersRsp->PortSelectMask[i] >> j) & (uint64)1) {
				PrintFunc(dest, "%*s    Port Number           %10u\n",
					indent, "",
					port->PortNumber);
				PrintFunc(dest, "%*s    Link Qual. Indicator  %10u (%s)\n",
					indent, "",
					port->lq.s.LinkQualityIndicator,
					StlLinkQualToText(port->lq.s.LinkQualityIndicator));
				PrintFunc(dest, "%*s    Xmit Data   %20"PRIu64" MB (%"PRIu64" Flits)\n",
					indent, "",
					port->PortXmitData/FLITS_PER_MB,
					port->PortXmitData);
				PrintFunc(dest, "%*s    Rcv Data    %20"PRIu64" MB (%"PRIu64" Flits)\n",
					indent, "",
					port->PortRcvData/FLITS_PER_MB,
					port->PortRcvData);
				PrintFunc(dest, "%*s    Xmit Pkts   %20"PRIu64"\n",
					indent, "",
					port->PortXmitPkts);
				PrintFunc(dest, "%*s    Rcv Pkts    %20"PRIu64"\n",
					indent, "",
					port->PortRcvPkts);
				PrintFunc(dest, "%*s    MC Xmt Pkts %20"PRIu64"\n",
					indent, "",
					port->PortMulticastXmitPkts);
				PrintFunc(dest, "%*s    MC Rcv Pkts %20"PRIu64"\n",
					indent, "",
					port->PortMulticastRcvPkts);
				PrintFunc(dest, "%*s    Xmit Wait             %20"PRIu64"\n",
					indent, "",
					port->PortXmitWait);
				PrintFunc(dest, "%*s    Congestion Discards   %20"PRIu64"\n",
					indent, "",
					port->SwPortCongestion);
				PrintFunc(dest, "%*s    Rcv FECN              %20"PRIu64"\n",
					indent, "",
					port->PortRcvFECN);
				PrintFunc(dest, "%*s    Rcv BECN              %20"PRIu64"\n",
					indent, "",
					port->PortRcvBECN);
				PrintFunc(dest, "%*s    Xmit Time Cong        %20"PRIu64"\n",
					indent, "",
					port->PortXmitTimeCong);
				PrintFunc(dest, "%*s    Xmit Wasted BW        %20"PRIu64"\n",
					indent, "",
					port->PortXmitWastedBW);
				PrintFunc(dest, "%*s    Xmit WaIt Data        %20"PRIu64"\n",
					indent, "",
					port->PortXmitWaitData);
				PrintFunc(dest, "%*s    Rcv Bubble            %20"PRIu64"\n",
					indent, "",
					port->PortRcvBubble);
				PrintFunc(dest, "%*s    Mark FECN             %20"PRIu64"\n",
					indent, "",
					port->PortMarkFECN);
				PrintFunc(dest, "%*s    Error Counter Summary %20"PRIu64"\n",
					indent, "",
					port->PortErrorCounterSummary);
				/* Count the bits in the mask and process the VLs in succession */
				/* Assume that even though VL numbers may not be contiguous, that entries */
				/*   in the array are */
				for (ii = 0, jj = 0; ii < MAX_PM_VLS; ii++) {
					if ((pStlDataPortCountersRsp->VLSelectMask >> ii) & (uint64)1) {
						PrintFunc(dest, "%*s    VL Number             %10d\n",
							indent, "",
							ii);
						PrintFunc(dest, "%*s         Xmit Data   %20"PRIu64" MB (%"PRIu64" Flits)\n",
							indent, "",
							port->VLs[jj].PortVLXmitData/FLITS_PER_MB,
							port->VLs[jj].PortVLXmitData);
						PrintFunc(dest, "%*s         Rcv Data    %20"PRIu64" MB (%"PRIu64" Flits)\n",
							indent, "",
							port->VLs[jj].PortVLRcvData/FLITS_PER_MB,
							port->VLs[jj].PortVLRcvData);
						PrintFunc(dest, "%*s         Xmit Pkts   %20"PRIu64"\n",
							indent, "",
							port->VLs[jj].PortVLXmitPkts);
						PrintFunc(dest, "%*s         Rcv Pkts    %20"PRIu64"\n",
							indent, "",
							port->VLs[jj].PortVLRcvPkts);
						PrintFunc(dest, "%*s         Xmit Wait             %20"PRIu64"\n",
							indent, "",
							port->VLs[jj].PortVLXmitWait);
						PrintFunc(dest, "%*s         Congestion Discards   %20"PRIu64"\n",
							indent, "",
							port->VLs[jj].SwPortVLCongestion);
						PrintFunc(dest, "%*s         Rcv FECN              %20"PRIu64"\n",
							indent, "",
							port->VLs[jj].PortVLRcvFECN);
						PrintFunc(dest, "%*s         Rcv BECN              %20"PRIu64"\n",
							indent, "",
							port->VLs[jj].PortVLRcvBECN);
						PrintFunc(dest, "%*s         Xmit Time Cong        %20"PRIu64"\n",
							indent, "",
							port->VLs[jj].PortVLXmitTimeCong);
						PrintFunc(dest, "%*s         Xmit Wasted BW        %20"PRIu64"\n",
							indent, "",
							port->VLs[jj].PortVLXmitWastedBW);
						PrintFunc(dest, "%*s         Xmit Wait Data        %20"PRIu64"\n",
							indent, "",
							port->VLs[jj].PortVLXmitWaitData);
						PrintFunc(dest, "%*s         Rcv Bubble            %20"PRIu64"\n",
							indent, "",
							port->VLs[jj].PortVLRcvBubble);
						PrintFunc(dest, "%*s         Mark FECN             %20"PRIu64"\n",
							indent, "",
							port->VLs[jj].PortVLMarkFECN);
						jj++;
					}
				}
				port = (struct _port_dpctrs *)&(port->VLs[jj]);
			}
		}
	}
}

#define NUMERRORCOUNTERS 9
#define ERRORCTRTEXTSIZE 22

char counterText[NUMERRORCOUNTERS][ERRORCTRTEXTSIZE] =
{
         {"Rcv Constraint Errors"}
        ,{"Rcv Switch Relay     "}
        ,{"Xmit Discards        "}
        ,{"Xmt Constraint Errors"}
        ,{"Rcv Rmt Phys. Errors "}
		,{"Local Link Integrity "}
        ,{"Rcv Errors           "}
        ,{"Exc. Buffer Overrun  "}
		,{"FM Config Errors     "}
};

void PrintStlErrorPortCountersRsp(PrintDest_t *dest, int indent, const STL_ERROR_PORT_COUNTERS_RSP *pStlErrorPortCountersRsp, int counterSizeMode)
{
	int i, j, ii;
	int counterCount1, counterCount2;
	int largeCounterCount;
	int largeVLCounterCount;
	int textIndex;
	uint64 *p64;
	uint32 *p32;
	uint8 *p8;

    if (pStlErrorPortCountersRsp->PortSelectMask[0]) {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx %016llx %016llx %016llx\n",
    		indent, "",
    		pStlErrorPortCountersRsp->PortSelectMask[0],
    		pStlErrorPortCountersRsp->PortSelectMask[1],
    		pStlErrorPortCountersRsp->PortSelectMask[2],
    		pStlErrorPortCountersRsp->PortSelectMask[3]);
    }
    else if (pStlErrorPortCountersRsp->PortSelectMask[1]) {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx %016llx %016llx\n",
    		indent, "",
    		pStlErrorPortCountersRsp->PortSelectMask[1],
    		pStlErrorPortCountersRsp->PortSelectMask[2],
    		pStlErrorPortCountersRsp->PortSelectMask[3]);
    }
    else if (pStlErrorPortCountersRsp->PortSelectMask[2]) {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx %016llx\n",
    		indent, "",
    		pStlErrorPortCountersRsp->PortSelectMask[2],
    		pStlErrorPortCountersRsp->PortSelectMask[3]);
    }
    else {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx\n",
    		indent, "",
    		pStlErrorPortCountersRsp->PortSelectMask[3]);
    }
	PrintFunc(dest, "%*s VL Select Mask     0x%08x\n",
		indent, "",
		pStlErrorPortCountersRsp->VLSelectMask);

	switch (counterSizeMode) {
		case STL_PM_COUNTER_SIZE_MODE_ALL64:
			largeCounterCount = MAX_BIG_ERROR_COUNTERS;
			largeVLCounterCount = MAX_BIG_VL_ERROR_COUNTERS;
			break;
		case STL_PM_COUNTER_SIZE_MODE_ALL32:
			largeCounterCount = 0;
			largeVLCounterCount = 0;
			break;
		case STL_PM_COUNTER_SIZE_MODE_MIXED:
			largeCounterCount = MAX_BIG_ERROR_COUNTERS_MIXED_MODE;
			largeVLCounterCount = 0;
			break;
		default:
			/* error ? */
			largeCounterCount = MAX_BIG_ERROR_COUNTERS;
			largeVLCounterCount = MAX_BIG_VL_ERROR_COUNTERS;
			break;
	}
	//ALL32 Mode does nto have a 32bit Reserved field.
	p8 = (largeCounterCount ? (uint8 *)&pStlErrorPortCountersRsp->Port[0].PortNumber : (uint8 *)&pStlErrorPortCountersRsp->Reserved );

	/* Count the bits in the mask and process the ports in succession */
	/* Assume that even though port numbers may not be contiguous, that entries */
	/*   in the array are */
	for (i = 3; i >= 0; i--) {
		for (j = 0; j < MAX_PM_PORTS; j++) {
			if ((pStlErrorPortCountersRsp->PortSelectMask[i] >> j) & (uint64)1) {
				textIndex = 0;
				PrintFunc(dest, "%*s    Port Number           %10u\n",
					indent, "",
					*p8);
				p8 += (largeCounterCount ? 8 : 4);	/* advance past PortNumber and 7 reserved bytes */
				p32 = (uint32 *)p8;
				p64 = (uint64 *)p32;
				for (counterCount1 = 0; counterCount1 < largeCounterCount; counterCount1++) {
					PrintFunc(dest, "%*s    %s %10llu\n",
						indent, "",
						counterText[textIndex++], *p64++);
				}
				p32 = (uint32 *)p64;
				for (counterCount2 = counterCount1; counterCount2 < MAX_BIG_ERROR_COUNTERS; counterCount2++) {
					PrintFunc(dest, "%*s    %s %10u\n",
						indent, "",
						counterText[textIndex++], *p32++);
				}
				PrintFunc(dest, "%*s    Link Error Recovery   %10u\n",
					indent, "",
					*p32++);
				PrintFunc(dest, "%*s    Link Downed           %10u\n",
					indent, "",
					*p32++);
				p8 = (uint8 *)p32;
				PrintFunc(dest, "%*s    Uncorrectable Errors  %10u\n",
					indent, "",
					*p8);
				/* advance 8 bytes to the VL array */
				p8 += (largeCounterCount ? 8 : 4);
				p64 = (uint64 *)p8;
				p32 = (uint32 *)p8;
				/* Count the bits in the mask and process the VLs in succession */
				/* Assume that even though VL numbers may not be contiguous, that entries */
				/*   in the array are */
				for (ii = 0; ii < MAX_PM_VLS; ii++) {
					if ((pStlErrorPortCountersRsp->VLSelectMask >> ii) & (uint64)1) {
						PrintFunc(dest, "%*s    VL Number             %10d\n",
							indent, "",
							ii);
						if (largeVLCounterCount) {
							PrintFunc(dest, "%*s         Xmit Discards         %10llu\n",
								indent, "",
								*p64);
							p64++;
							p32 = (uint32 *)p64;
						} else {
							PrintFunc(dest, "%*s         Xmit Discards         %10u\n",
								indent, "",
								*p32);
							p32 += (largeCounterCount ? 2 : 1);
							p64 = (uint64 *)p32;
						}
					}
				}
				p8 = (uint8 *)p64;
			}
		}
	}

	return;
}

void PrintStlErrorInfoReq(PrintDest_t *dest, int indent, const STL_ERROR_INFO_REQ *pStlErrorInfoReq)
{
	int i, j, k;
	int ii;
	char bits;
	char *pChar, *pBuf;
	char displayBuf[256];

    if (pStlErrorInfoReq->PortSelectMask[0]) {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx %016llx %016llx %016llx\n",
    		indent, "",
    		pStlErrorInfoReq->PortSelectMask[0],
    		pStlErrorInfoReq->PortSelectMask[1],
    		pStlErrorInfoReq->PortSelectMask[2],
    		pStlErrorInfoReq->PortSelectMask[3]);
    }
    else if (pStlErrorInfoReq->PortSelectMask[1]) {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx %016llx %016llx\n",
    		indent, "",
    		pStlErrorInfoReq->PortSelectMask[1],
    		pStlErrorInfoReq->PortSelectMask[2],
    		pStlErrorInfoReq->PortSelectMask[3]);
    }
    else if (pStlErrorInfoReq->PortSelectMask[2]) {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx %016llx\n",
    		indent, "",
    		pStlErrorInfoReq->PortSelectMask[2],
    		pStlErrorInfoReq->PortSelectMask[3]);
    }
    else {
    	PrintFunc(dest, "%*s Port Select Mask   0x%016llx\n",
    		indent, "",
    		pStlErrorInfoReq->PortSelectMask[3]);
    }
	PrintFunc(dest, "%*s Error Select Mask  0x%08x\n",
		indent, "",
		pStlErrorInfoReq->ErrorInfoSelectMask.AsReg32);

	for (i = 3; i >= 0; i--) {
		for (j = 0, k = 0; j < 64; j++) {
			if ((pStlErrorInfoReq->PortSelectMask[i] >> j) & (uint64)1) {
				PrintFunc(dest, "%*s    Port Number           %10u\n",
					indent, "",
					pStlErrorInfoReq->Port[k].PortNumber);
				if (pStlErrorInfoReq->Port[k].PortRcvErrorInfo.s.Status) {
					PrintFunc(dest, "%*s    Rcv Error Info        %s\n",
						indent, "",
						":");
					PrintFunc(dest, "%*s        Error Code        %10u\n",
						indent, "",
						pStlErrorInfoReq->Port[k].PortRcvErrorInfo.s.ErrorCode);
					if ((pStlErrorInfoReq->Port[k].PortRcvErrorInfo.s.ErrorCode >= 1) &&
					    (pStlErrorInfoReq->Port[k].PortRcvErrorInfo.s.ErrorCode <= 12)) {
						pBuf = displayBuf;
						for (ii = 0, pChar = (char *)pStlErrorInfoReq->Port[k].PortRcvErrorInfo.ErrorInfo.EI1to12.PacketFlit1; ii < 8; ii++) {
							sprintf(pBuf, "0x%02x ", *pChar++);
							pBuf += 5;
						}
						bits = (char)pStlErrorInfoReq->Port[k].PortRcvErrorInfo.ErrorInfo.EI1to12.s.Flit1Bits;
						pChar = &bits;
						sprintf(pBuf, "0x%02x ", *pChar++);
						*pBuf = '\0';
						PrintFunc(dest, "%*s        Flit 1:           %s\n",
							indent, "",
							displayBuf);
						pBuf = displayBuf;
						for (ii = 0, pChar = (char *)pStlErrorInfoReq->Port[k].PortRcvErrorInfo.ErrorInfo.EI1to12.PacketFlit2; ii < 8; ii++) {
							sprintf(pBuf, "0x%02x ", *pChar++);
							pBuf += 5;
						}
						bits = (char)pStlErrorInfoReq->Port[k].PortRcvErrorInfo.ErrorInfo.EI1to12.s.Flit2Bits;
						pChar = &bits;
						sprintf(pBuf, "0x%02x ", *pChar++);
						*pBuf = '\0';
						PrintFunc(dest, "%*s        Flit 2:           %s\n",
							indent, "",
							displayBuf);
					} else if (pStlErrorInfoReq->Port[k].PortRcvErrorInfo.s.ErrorCode == 13) {
						pBuf = displayBuf;
						for (ii = 0, pChar = (char *)pStlErrorInfoReq->Port[k].PortRcvErrorInfo.ErrorInfo.EI13.PacketBytes; ii < 8; ii++) {
							sprintf(pBuf, "0x%02x ", *pChar++);
							pBuf += 5;
						}
						bits = (char)pStlErrorInfoReq->Port[k].PortRcvErrorInfo.ErrorInfo.EI13.s.FlitBits;
						pChar = &bits;
						sprintf(pBuf, "0x%02x ", *pChar++);
						*pBuf = '\0';
						PrintFunc(dest, "%*s        VL Marker Flit:   %s\n",
							indent, "",
							displayBuf);
					} else {
						/* bad error code */
					}
				} else {
					PrintFunc(dest, "%*s    Rcv Error Info        %s\n",
						indent, "",
						"N/A");
				}
				if (pStlErrorInfoReq->Port[k].ExcessiveBufferOverrunInfo.s.Status) {
					PrintFunc(dest, "%*s    Exc Buf Overrun Info  %s\n",
						indent, "",
						":");
					PrintFunc(dest, "%*s        SC                %10u\n",
						indent, "",
						pStlErrorInfoReq->Port[k].ExcessiveBufferOverrunInfo.s.SC);
				} else {
					PrintFunc(dest, "%*s    Exc Buf Overrun Info  %s\n",
						indent, "",
						"N/A");
				}
				if (pStlErrorInfoReq->Port[k].PortXmitConstraintErrorInfo.s.Status) {
					PrintFunc(dest, "%*s    Xmt Constraint Info   %s\n",
						indent, "",
						":");
					PrintFunc(dest, "%*s        P_Key             0x%10x\n",
						indent, "",
						pStlErrorInfoReq->Port[k].PortXmitConstraintErrorInfo.P_Key);
					PrintFunc(dest, "%*s        SLID              0x%10x\n",
						indent, "",
						pStlErrorInfoReq->Port[k].PortXmitConstraintErrorInfo.SLID);
				} else {
					PrintFunc(dest, "%*s    Xmt Constraint Info   %s\n",
						indent, "",
						"N/A");
				}
				if (pStlErrorInfoReq->Port[k].PortRcvConstraintErrorInfo.s.Status) {
					PrintFunc(dest, "%*s    Rcv Constraint Info   %s\n",
						indent, "",
						":");
					PrintFunc(dest, "%*s        P_Key             0x%10x\n",
						indent, "",
						pStlErrorInfoReq->Port[k].PortRcvConstraintErrorInfo.P_Key);
					PrintFunc(dest, "%*s        SLID              0x%10x\n",
						indent, "",
						pStlErrorInfoReq->Port[k].PortRcvConstraintErrorInfo.SLID);
				} else {
					PrintFunc(dest, "%*s    Rcv Constraint Info   %s\n",
						indent, "",
						"N/A");
				}
				if (pStlErrorInfoReq->Port[k].PortRcvSwitchRelayErrorInfo.s.Status) {
					PrintFunc(dest, "%*s    Rcv Sw Rel Info       %s\n",
						indent, "",
						":");
					PrintFunc(dest, "%*s        Error Code        %10u\n",
						indent, "",
						pStlErrorInfoReq->Port[k].PortRcvSwitchRelayErrorInfo.s.ErrorCode);
					switch (pStlErrorInfoReq->Port[k].PortRcvSwitchRelayErrorInfo.s.ErrorCode) {
						case 0:
							PrintFunc(dest, "%*s        DLID:             0x%10x\n",
								indent, "",
								pStlErrorInfoReq->Port[k].PortRcvSwitchRelayErrorInfo.ErrorInfo.EI0.DLID);
							break;
						case 2:
							PrintFunc(dest, "%*s        Egress Port Num:  %10u\n",
								indent, "",
								pStlErrorInfoReq->Port[k].PortRcvSwitchRelayErrorInfo.ErrorInfo.EI2.EgressPortNum);
							break;
						case 3:
							PrintFunc(dest, "%*s        Egress Port Num:  %10u\n",
								indent, "",
								pStlErrorInfoReq->Port[k].PortRcvSwitchRelayErrorInfo.ErrorInfo.EI3.EgressPortNum);
							PrintFunc(dest, "%*s        SC:               %10u\n",
								indent, "",
								pStlErrorInfoReq->Port[k].PortRcvSwitchRelayErrorInfo.ErrorInfo.EI3.SC);
							break;
						default:
							/* bad error code */
							break;
					}
				} else {
					PrintFunc(dest, "%*s    Rcv Sw Rel Info       %s\n",
						indent, "",
						"N/A");
				}
				if (pStlErrorInfoReq->Port[k].UncorrectableErrorInfo.s.Status) {
					PrintFunc(dest, "%*s    Uncorr Error Info     %s\n",
						indent, "",
						":");
					PrintFunc(dest, "%*s        Error Code        %10u\n",
						indent, "",
						pStlErrorInfoReq->Port[k].UncorrectableErrorInfo.s.ErrorCode);
				} else {
					PrintFunc(dest, "%*s    Uncorr Error Info     %s\n",
						indent, "",
						"N/A");
				}
				if (pStlErrorInfoReq->Port[k].FMConfigErrorInfo.s.Status) {
					PrintFunc(dest, "%*s    FM Config Error Info  %s\n",
						indent, "",
						":");
					PrintFunc(dest, "%*s        Error Code        %10u\n",
						indent, "",
						pStlErrorInfoReq->Port[k].FMConfigErrorInfo.s.ErrorCode);
					switch (pStlErrorInfoReq->Port[k].FMConfigErrorInfo.s.ErrorCode) {
						case 0:
						case 1:
						case 2:
							PrintFunc(dest, "%*s        VL:               %10u\n",
								indent, "",
								pStlErrorInfoReq->Port[k].FMConfigErrorInfo.ErrorInfo.EI0to2.VL);
							break;
						case 3:
						case 4:
						case 5:
							PrintFunc(dest, "%*s        Distance:         %10u\n",
								indent, "",
								pStlErrorInfoReq->Port[k].FMConfigErrorInfo.ErrorInfo.EI3to5.Distance);
							break;
						case 6:
							PrintFunc(dest, "%*s        Bad Flit Bits:    0x%10x\n",
								indent, "",
								pStlErrorInfoReq->Port[k].FMConfigErrorInfo.ErrorInfo.EI6.BadFlitBits);
							break;
						case 7:
							PrintFunc(dest, "%*s        SC:               %10u\n",
								indent, "",
								pStlErrorInfoReq->Port[k].FMConfigErrorInfo.ErrorInfo.EI7.SC);
							break;
						default:
							/* bad error code */
							break;
					}
				} else {
					PrintFunc(dest, "%*s    FM Config Error Info  %s\n",
						indent, "",
						"N/A");
				}
				k++;
			}
		}
	}
	return;
}
