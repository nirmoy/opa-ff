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

/* [ICS VERSION STRING: unknown] */

#include <stdlib.h>
#include <limits.h>
#include <getopt.h>
#include <stdbool.h>

/* work around conflicting names */
#include "infiniband/umad.h"

#include <oib_utils.h>

#include "iba/ib_types.h"
#include "iba/ib_sm.h"
#include "iba/ib_helper.h"
#include "iba/ib_ibt.h"

#include "iba/ipublic.h"
#include "ibprint.h"
#include "stl_print.h"

PrintDest_t g_dest;
uint64		g_mkey=0;                           // SMA M-Key
uint8       g_verbose = 0;                      // Debug aid: data turn on global verbose => debugging
uint32      debug_level = 0;                    // Debug aid: User passed-in clo; 0 = portdown; 1 = open-ib_utils; 2=debug for mad calls
uint8       g_enableExtLinkSpeedsSupport = 0;   // Enable SMA Extended Link Speeds support for IB mode
struct      oib_port *g_oib_port;
uint16_t    mgmt_pkey = OIB_DEFAULT_PKEY;

#define SEND_WAIT_TIME (100)	// 100 milliseconds for sends
#define FLUSH_WAIT_TIME (100)	// 100 milliseconds for sends/recv during flush
#define RESP_WAIT_TIME (1000)	// 1000 milliseconds for response recv


#define MGT_POOL_TAG MAKE_MEM_TAG('t','g', 'm', 'V')

#define VRBSE_PRINT(format, args...) \
	do { if (g_verbose > 0) { printf(format, ##args); fflush(stdout); } } while (0)

#if defined(DBGPRINT) 
#undef DBGPRINT
#endif

#define DBGPRINT(format, args...) \
	do { if (g_verbose > 1) { fflush(stdout); fprintf(stderr, format, ##args); } } while (0)

// What command are we doing based on argv[0] and sub-command
enum cmd_t {
	portdown=1,
	portconfig=2,
	portenable=3,
	portdisable=4,
	portinfo=5,
	portbounce=6,
	ledon=7,
	ledoff=8
} cmd;
char *cmdname;

// IB mode is only for engineering use on simulators
enum mode_t {
	MODE_IB,
	MODE_STL
} mode = MODE_STL;

/**
 * set the routing portion of the SMP packet
 * 
 * @param dlid
 * @param slid
 * @param port
 * @param smp
 */
void set_ib_route(unsigned dlid, unsigned slid, unsigned port, SMP* smp)
{
	if (dlid) {
		// lid routed to a remote node
		smp->common.MgmtClass = MCLASS_SM_LID_ROUTED;
		smp->common.AttributeModifier = port;
	} else {
		// local port control
		smp->common.MgmtClass = MCLASS_SM_DIRECTED_ROUTE;
		smp->common.AttributeModifier = port;
		smp->SmpExt.DirectedRoute.DrSLID = LID_PERMISSIVE;
		smp->SmpExt.DirectedRoute.DrDLID = LID_PERMISSIVE;
		smp->common.u.DR.HopCount = 0;
		smp->common.u.DR.HopPointer = 0;
		smp->common.u.DR.s.D = 0;
	}

	smp->common.u.DR.s.Status = 0;
}
void set_route(STL_LID_32 dlid, STL_LID_32 slid, uint8_t port, STL_SMP* smp)
{
	if (dlid) {
		// lid routed to a remote node
		smp->common.MgmtClass = MCLASS_SM_LID_ROUTED;
		smp->common.AttributeModifier = 0x01000000 | port;
	} else {
		// local port control
		smp->common.MgmtClass = MCLASS_SM_DIRECTED_ROUTE;
		smp->common.AttributeModifier = 0x01000000 | port;
		smp->SmpExt.DirectedRoute.DrSLID = STL_LID_PERMISSIVE;
		smp->SmpExt.DirectedRoute.DrDLID = STL_LID_PERMISSIVE;
		smp->common.u.DR.HopCount = 0;
		smp->common.u.DR.HopPointer = 0;
		smp->common.u.DR.s.D = 0;
	}

	smp->common.u.DR.s.Status = 0;
}

/**
 * perform a Get(PortInfo) SMA request and wait for response
 * 
 * @param dlid
 * @param slid
 * @param port
 * @param smp
 * 
 * @return int
 */
int get_ib_port_info(unsigned dlid, unsigned slid, unsigned port, SMP* smp)
{
	uint32 attrmod = 0;
	SMP smp2;
	PORT_INFO* pPortInfo = (PORT_INFO *)&(smp->SmpExt.DirectedRoute.SMPData);
	PORT_INFO* pPortInfo2 = (PORT_INFO *)&(smp2.SmpExt.DirectedRoute.SMPData);

	MemoryClear(smp, sizeof(*smp));
	smp->common.BaseVersion = IB_BASE_VERSION;
	smp->common.ClassVersion = IB_SM_CLASS_VERSION;
	set_ib_route(dlid, slid, port, smp);	// MgmtClass and AttributeModifier
	smp->common.mr.AsReg8 = 0;
	smp->common.mr.s.Method = MMTHD_GET;
	smp->common.AttributeID = MCLASS_ATTRIB_ID_PORT_INFO;
	smp->M_Key = g_mkey;
	// rest of fields should be ignored for a Get, zero'ed above

	if (cmd == portdown || cmd == portconfig || g_verbose)
	{
		if (dlid) {
			VRBSE_PRINT("Sending Get(PortInfo) to LID 0x%04x Port %u\n", dlid, port);
		} else {
			VRBSE_PRINT("Sending Get(PortInfo) to Port %u\n", port);
		}
	}

	// Get initial PortInfo and check CapabilityMask.IsExtendedSpeedsSupported
	smp2 = *smp;
	BSWAP_PORT_INFO(pPortInfo2, TRUE);
	BSWAP_SMP_HEADER(&smp2);

	{
		struct oib_mad_addr addr = {
			lid : dlid,
			qpn : 0,
			qkey : QP1_WELL_KNOWN_Q_KEY,
			pkey : mgmt_pkey
		};
        size_t recv_size = sizeof(smp2);
		if (FSUCCESS == oib_send_recv_mad_no_alloc(g_oib_port, (uint8_t *)&smp2, sizeof(smp2), &addr,
											(uint8_t *)&smp2, &recv_size, RESP_WAIT_TIME, 0)) {
			BSWAP_PORT_INFO(pPortInfo2, TRUE);
			if (g_enableExtLinkSpeedsSupport && pPortInfo2->CapabilityMask.s.IsExtendedSpeedsSupported)
				attrmod |= MCLASS_ATTRIB_MOD_SUPPORTS_EXT_SPEEDS;
		}
	}

	// Get PortInfo with adjusted AttributeModifier
	smp->common.AttributeModifier |= attrmod;
	BSWAP_PORT_INFO(pPortInfo, TRUE);
	BSWAP_SMP_HEADER(smp);

	{
		struct oib_mad_addr addr = {
			lid : dlid,
			qpn : 0,
			qkey : QP1_WELL_KNOWN_Q_KEY,
			pkey : mgmt_pkey
		};
        size_t recv_size = sizeof(*smp);
		if (FSUCCESS != oib_send_recv_mad_no_alloc(g_oib_port, (uint8_t *)smp, sizeof(*smp), &addr,
											(uint8_t *)smp, &recv_size, RESP_WAIT_TIME, 0)) {
			fprintf(stderr, "Failed to send MAD or receive response MAD\n");
			return FALSE;
		}
	}

	BSWAP_SMP_HEADER(smp);
	BSWAP_PORT_INFO(pPortInfo, attrmod ? TRUE : FALSE);
	return TRUE;
}
int get_port_info(STL_LID_32 dlid, STL_LID_32 slid, uint8_t port, STL_SMP* smp)
{
	STL_PORT_INFO* pPortInfo;

	MemoryClear(smp, sizeof(*smp));
	smp->common.BaseVersion = STL_BASE_VERSION;
	smp->common.ClassVersion = STL_SM_CLASS_VERSION;
	set_route(dlid, slid, port, smp);	// MgmtClass and AttributeModifier
	smp->common.mr.AsReg8 = 0;
	smp->common.mr.s.Method = MMTHD_GET;
	smp->common.AttributeID = MCLASS_ATTRIB_ID_PORT_INFO;
	smp->M_Key = g_mkey;
	// rest of fields should be ignored for a Get, zero'ed above

	if (cmd == portdown || cmd == portconfig || g_verbose)
	{
		if (dlid) {
			VRBSE_PRINT("Sending Get(STLPortInfo) to LID 0x%08x Port %u\n", dlid, port);
		} else {
			VRBSE_PRINT("Sending Get(STLPortInfo) to Port %u\n", port);
		}
	}

	STL_BSWAP_SMP_HEADER(smp);

	{
		struct oib_mad_addr addr = {
			lid : dlid,
			qpn : 0,
			qkey : QP1_WELL_KNOWN_Q_KEY,
			pkey : mgmt_pkey
		};
        size_t recv_size = sizeof(*smp);
		if (FSUCCESS != oib_send_recv_mad_no_alloc(g_oib_port, (uint8_t *)smp, sizeof(*smp), &addr,
											(uint8_t *)smp, &recv_size, RESP_WAIT_TIME, 0)) {
			fprintf(stderr, "Failed to send MAD or receive response MAD\n");
			return FALSE;
		}
	}

	STL_BSWAP_SMP_HEADER(smp);
	if (smp->common.u.DR.s.Status != MAD_STATUS_SUCCESS) {
		fprintf(stderr, "MAD returned with Bad Status: %s\n",
						iba_mad_status_msg2(smp->common.u.DR.s.Status));
		return FALSE;
	}

	pPortInfo = (STL_PORT_INFO *)stl_get_smp_data(smp);
	BSWAP_STL_PORT_INFO(pPortInfo);

	return TRUE;
}

/**
 * output the contents of a PortInfo SMP packet
 * 
 * @param title
 * @param portGuid
 * @param smp
 */
void show_ib_port_info(const char* title, EUI64 portGuid, SMP* smp)
{
	PORT_INFO* pPortInfo = (PORT_INFO *)&(smp->SmpExt.DirectedRoute.SMPData);

	printf("%s\n", title);
	printf("Port %u Info\n", smp->common.AttributeModifier & 0xFF);
	PrintPortInfo(&g_dest, 3, pPortInfo, portGuid, 1);
}
void show_port_info(const char* title, EUI64 portGuid, STL_SMP* smp)
{
	STL_PORT_INFO* pPortInfo = (STL_PORT_INFO *)stl_get_smp_data(smp);

	printf("%s\n", title);
	printf("Port %u Info\n", smp->common.AttributeModifier & 0xFF);
	PrintStlPortInfo(&g_dest, 3, pPortInfo, portGuid, 0);
}

/**
 * build a Set(PortInfo) SMP packet
 * 
 * @param dlid
 * @param slid
 * @param port
 * @param smp
 * @param state
 * @param physstate
 * @param width
 * @param speed
 * @param portGuid
 * @param getInfo
 */
void initialize_ib_set_port_info(unsigned dlid, unsigned slid, unsigned nlid, unsigned port,
							  SMP *smp, uint state, uint physstate, uint8_t width, uint8_t speed,
							  EUI64 portGuid, const SMP *getInfo)
{
	uint32 attrmod = 0;
	PORT_INFO* pPortInfo = (PORT_INFO *)&(smp->SmpExt.DirectedRoute.SMPData);

	if (! getInfo) {
		// no Get data available
		MemoryClear(smp, sizeof(*smp));
		pPortInfo->LID = 0;
		pPortInfo->MasterSMLID = 0;
		pPortInfo->Subnet.Timeout = 8;
		// hopefully other invalid fields won't stop port state change
	} else {
		PORT_INFO* pPortInfo2 = (PORT_INFO *)&(getInfo->SmpExt.DirectedRoute.SMPData);
		// use attributes in get as starting point
		MemoryCopy(smp, getInfo, sizeof(*smp));
		if (g_enableExtLinkSpeedsSupport && pPortInfo2->CapabilityMask.s.IsExtendedSpeedsSupported)
			attrmod |= MCLASS_ATTRIB_MOD_SUPPORTS_EXT_SPEEDS;
		//pPortInfo->LID = 0;
	}

	smp->common.BaseVersion = IB_BASE_VERSION;
	smp->common.ClassVersion = IB_SM_CLASS_VERSION;
	smp->common.mr.AsReg8 = 0;
	smp->common.mr.s.Method = MMTHD_SET;
	set_ib_route(dlid, slid, port, smp);	// MgmtClass and AttributeModifier
	smp->common.AttributeID = MCLASS_ATTRIB_ID_PORT_INFO;
	smp->common.AttributeModifier |= attrmod;
	smp->M_Key = g_mkey;

	pPortInfo->LID = nlid;
	pPortInfo->Link.PortState = state;
	pPortInfo->Link.PortPhysicalState = physstate;
	pPortInfo->LinkWidth.Enabled = width;
	pPortInfo->Link.DownDefaultState = IB_PORT_PHYS_NOP;
	PortDataSetLinkSpeedEnabledSplit(speed, pPortInfo);
	pPortInfo->s3.OperationalVL = 0;
	if (g_verbose)
		show_ib_port_info("Setting Port State:", dlid?0:portGuid, smp);
	BSWAP_PORT_INFO(pPortInfo, TRUE);
	BSWAP_SMP_HEADER(smp);
}
void initialize_set_port_info(STL_LID_32 dlid, STL_LID_32 slid, int have_nlid, STL_LID_32 nlid, uint8_t port,
							  STL_SMP *smp, uint8_t state, uint8_t physstate, uint16_t width, uint16_t speed, uint8_t crc, 
							  EUI64 portGuid, uint16_t pkey8b, const STL_SMP *getInfo)
{
	STL_PORT_INFO* pPortInfo;

	if (! getInfo) {
		// no Get data available
		MemoryClear(smp, sizeof(*smp));
	} else {
		// use attributes in get as starting point
		MemoryCopy(smp, getInfo, sizeof(*smp));
	}

	smp->common.BaseVersion = STL_BASE_VERSION;
	smp->common.ClassVersion = STL_SM_CLASS_VERSION;
	smp->common.mr.AsReg8 = 0;
	smp->common.mr.s.Method = MMTHD_SET;
	set_route(dlid, slid, port, smp);	// MgmtClass and AttributeModifier
	smp->common.AttributeID = MCLASS_ATTRIB_ID_PORT_INFO;
	smp->M_Key = g_mkey;

	pPortInfo = (STL_PORT_INFO *)stl_get_smp_data(smp);
	if (! getInfo) {
		// no Get data available
		pPortInfo->LID = 0;
		pPortInfo->MasterSMLID = 0;
		pPortInfo->Subnet.Timeout = 8;
		// hopefully other invalid fields won't stop port state change
	}
	if (have_nlid)
		pPortInfo->LID = nlid;
	pPortInfo->PortStates.s.PortState = state;
	pPortInfo->PortStates.s.PortPhysicalState = physstate;
	pPortInfo->LinkWidth.Enabled = width;
	pPortInfo->LinkSpeed.Enabled = speed;
	pPortInfo->PortLTPCRCMode.s.Enabled = crc;
	pPortInfo->s4.OperationalVL = 0;
	if (pkey8b) {
		if (g_verbose)
			printf("Enabling 8B and 10B PKey: 0x%x\n", pkey8b);
		pPortInfo->P_Keys.P_Key_8B = pkey8b;
		pPortInfo->P_Keys.P_Key_10B = pkey8b;
	}
	if (g_verbose)
		show_port_info("Setting Port State:", dlid?0:portGuid, smp);
    BSWAP_STL_PORT_INFO(pPortInfo);
	STL_BSWAP_SMP_HEADER(smp);
}

/**
 * send a pre-built MAD and output what we are doing
 * 
 * @param hfi
 * @param portGuid
 * @param dlid
 * @param slid
 * @param port
 * @param smp
 * 
 * @return int
 */
FSTATUS send_ib_port_info(unsigned hfi, EUI64 portGuid, unsigned dlid, unsigned dport, unsigned slid, unsigned port, SMP* smp)
{
	FSTATUS  fstatus = FSUCCESS;
	const char *str;
	char hfistr[40];
	SMP returnedSmp;

	switch (cmd)
	{
		case portenable:
			str = "Enabling Port";
			break;
		case portdisable:
			str = "Disabling Port";
			break;
		case portbounce:
			str = "Bouncing Port";
			break;
		case portinfo:
			str = "Querying Port";
			break;
		default:
			str = "Sending Set(PortInfo) to Port";
			break;
	}

	if (hfi > 0)
	{
		sprintf(hfistr, " on HFI %u", hfi);
	} else {
		hfistr[0] = '\0';
	}
	if (dlid) {
		printf("%s at LID 0x%04x Port %u via local port %u (0x%016"PRIx64")%s\n",
				str, dlid, dport, port, portGuid, hfistr);
	} else {
		printf("%s %u (0x%016"PRIx64")%s\n", str, port, portGuid, hfistr);
	}

	MemoryClear(&returnedSmp, sizeof(returnedSmp));

	{
		struct oib_mad_addr addr = {
			lid : dlid,
			qpn : 0,
			qkey : QP1_WELL_KNOWN_Q_KEY,
			pkey : mgmt_pkey
		};
        size_t recv_size = sizeof(returnedSmp);
		fstatus = oib_send_recv_mad_no_alloc(g_oib_port, (uint8_t *)smp, sizeof(*smp), &addr,
											(uint8_t *)&returnedSmp, &recv_size, RESP_WAIT_TIME, 0);
	}
	if (FSUCCESS == fstatus)
	{
		PORT_INFO* pPortInfo = (PORT_INFO *)&(returnedSmp.SmpExt.DirectedRoute.SMPData);
		BSWAP_SMP_HEADER(&returnedSmp);
		if (returnedSmp.common.u.DR.s.Status != MAD_STATUS_SUCCESS) {
			fprintf(stderr, "MAD returned with Bad Status: %s\n",
							iba_mad_status_msg2(returnedSmp.common.u.DR.s.Status));
		}
		if (g_verbose)
		{
			BSWAP_PORT_INFO(pPortInfo, TRUE);
			printf("status: 0x%x\n", returnedSmp.common.u.NS.Status.AsReg16);
			show_ib_port_info("New Port State:", dlid?0:portGuid, &returnedSmp);
		}
	}

	return fstatus;
}

FSTATUS send_led_info(bool enabled, unsigned hfi, EUI64 portGuid, STL_LID_32 dlid, uint8_t dport, STL_LID_32 slid, uint8_t lport)
{
	FSTATUS  fstatus = FSUCCESS;
	
	const char *str;
	char hfistr[40];
	STL_SMP returnedSmp;
	STL_SMP smp;

	STL_LED_INFO* pLedInfo;

	MemoryClear(&smp, sizeof(smp));

	smp.common.BaseVersion = STL_BASE_VERSION;
	smp.common.ClassVersion = STL_SM_CLASS_VERSION;
	smp.common.mr.AsReg8 = 0;
	smp.common.mr.s.Method = MMTHD_SET;
	set_route(dlid, slid, dport, &smp);	// MgmtClass and AttributeModifier
	smp.common.AttributeID = MCLASS_ATTRIB_ID_LED_INFO;
	smp.M_Key = g_mkey;

	pLedInfo = (STL_LED_INFO *)stl_get_smp_data(&smp);
	pLedInfo->u.s.LedMask = enabled? 1: 0;

    BSWAP_STL_LED_INFO(pLedInfo);
	STL_BSWAP_SMP_HEADER(&smp);


	str = enabled ? "Enabling LED" : "Disabling Led";
	if (hfi > 0)
	{
		sprintf(hfistr, " on HFI %u", hfi);
	} else {
		hfistr[0] = '\0';
	}
	if (dlid) {
		printf("%s at LID 0x%08x Port %u via local port %u (0x%016"PRIx64")%s\n",
				str, dlid, dport, lport, portGuid, hfistr);
	} else {
		printf("%s %u (0x%016"PRIx64")%s\n", str, lport, portGuid, hfistr);
	}
	
	MemoryClear(&returnedSmp, sizeof(returnedSmp));
	{
		struct oib_mad_addr addr = {
			lid : dlid,
			qpn : 0,
			qkey : QP1_WELL_KNOWN_Q_KEY,
			pkey : mgmt_pkey
		};
        size_t recv_size = sizeof(returnedSmp);
		fstatus = oib_send_recv_mad_no_alloc(g_oib_port, (uint8_t *)&smp, sizeof(smp), &addr,
											(uint8_t *)&returnedSmp, &recv_size, RESP_WAIT_TIME, 0);
	}
	if (FSUCCESS == fstatus)
	{
		STL_BSWAP_SMP_HEADER(&returnedSmp);
		if (returnedSmp.common.u.DR.s.Status != MAD_STATUS_SUCCESS) {
			fprintf(stderr, "MAD returned with Bad Status: %s\n",
							iba_mad_status_msg2(returnedSmp.common.u.DR.s.Status));
		}
		if (g_verbose)
		{
			printf("status: 0x%x\n", returnedSmp.common.u.NS.Status.AsReg16);
		}
	}

	return fstatus;
}




FSTATUS send_port_info(unsigned hfi, EUI64 portGuid, STL_LID_32 dlid, uint8_t dport, STL_LID_32 slid, uint8_t lport, STL_SMP* smp)
{
	FSTATUS  fstatus = FSUCCESS;
	const char *str;
	char hfistr[40];
	STL_SMP returnedSmp;

	switch (cmd)
	{
		case portenable:
			str = "Enabling Port";
			break;
		case portdisable:
			str = "Disabling Port";
			break;
		case portbounce:
			str = "Bouncing Port";
			break;
		case portinfo:
			str = "Querying Port";
			break;
		default:
			str = "Sending Set(STLPortInfo) to Port";
			break;
	}

	if (hfi > 0)
	{
		sprintf(hfistr, " on HFI %u", hfi);
	} else {
		hfistr[0] = '\0';
	}
	if (dlid) {
		printf("%s at LID 0x%08x Port %u via local port %u (0x%016"PRIx64")%s\n",
				str, dlid, dport, lport, portGuid, hfistr);
	} else {
		printf("%s %u (0x%016"PRIx64")%s\n", str, lport, portGuid, hfistr);
	}

	MemoryClear(&returnedSmp, sizeof(returnedSmp));

	{
		struct oib_mad_addr addr = {
			lid : dlid,
			qpn : 0,
			qkey : QP1_WELL_KNOWN_Q_KEY,
			pkey : mgmt_pkey
		};
        size_t recv_size = sizeof(returnedSmp);
		fstatus = oib_send_recv_mad_no_alloc(g_oib_port, (uint8_t *)smp, sizeof(*smp), &addr,
											(uint8_t *)&returnedSmp, &recv_size, RESP_WAIT_TIME, 0);
	}
	if (FSUCCESS == fstatus)
	{
		STL_PORT_INFO* pPortInfo = (STL_PORT_INFO *)stl_get_smp_data(&returnedSmp);
		STL_BSWAP_SMP_HEADER(&returnedSmp);
		if (returnedSmp.common.u.DR.s.Status != MAD_STATUS_SUCCESS) {
			fprintf(stderr, "MAD returned with Bad Status: %s\n",
							iba_mad_status_msg2(returnedSmp.common.u.DR.s.Status));
		}
		if (g_verbose)
		{
			BSWAP_STL_PORT_INFO(pPortInfo);
			printf("status: 0x%x\n", returnedSmp.common.u.NS.Status.AsReg16);
			show_port_info("New Port State:", dlid?0:portGuid, &returnedSmp);
		}
	}

	return fstatus;
}

/** =========================================================================
 * Global param's for the 2 modes
 */
struct {
	STL_LID_32 dlid;
	uint8 dport;
	int have_dport;	// was dport provided (dport can be provided as 0)
	STL_LID_32 slid;
	STL_LID_32 nlid;
	int have_nlid;	// was nlid provided (nlid can be provided as 0)
	uint8 hfi;
	uint32 repeat;
	EUI64 portGuid;
	unsigned get;
	uint8 state;
	uint8 physstate;
	uint16_t width;
	uint16_t speed;
	uint8 crc;
	unsigned cycle;
	int port_in_hfi;
	uint16 pkey8b;
} param = {
	dlid : 0, // if dlid provided by user, use MCLASS_SM_LID_ROUTED else MCLASS_SM_DIRECTED_ROUTE
	dport : 0, // default is the port that the request was issued to
	have_dport : 0,
	slid : 0, // our local LID
	nlid : 0, // Used the change the base lid of the port.
	have_nlid : 0,
	hfi : 0,
	repeat : 0,
	portGuid : 0,
	get : 1,
	state : IB_PORT_NOP,	// default to no link state change
	physstate : IB_PORT_PHYS_NOP,	// default to no physical state change
	width : STL_LINK_WIDTH_NOP,
	speed : STL_LINK_SPEED_NOP,
	crc : 0,
	cycle : 0,	// cycle port to cause speed or link change to activate
	port_in_hfi : 0,
	pkey8b : 0	// value to program in 8B and 10B pkey in PortInfo
};

static void run_ib_mode(void)
{
	SMP setPortInfo;
	SMP getPortInfo;

	/* Verify we have valid IB values */
	if (param.dlid & 0xffff0000) {
		fprintf (stderr,"%s: Error: IB dlid out of range: %x\n", cmdname, param.dlid);
		exit(1);
	}
	if (param.slid & 0xffff0000) {
		fprintf (stderr,"%s: Error: IB slid out of range: %x\n", cmdname, param.slid);
		exit(1);
	}
	if (param.have_nlid && (param.nlid & 0xffff0000)) {
		fprintf (stderr,"%s: Error: IB nlid out of range: %x\n", cmdname, param.nlid);
		exit(1);
	}
	if (param.speed != 0xffff && param.speed & 0xff00) {
		fprintf (stderr,"%s: Error: invalid IB speed: %x\n", cmdname, param.speed);
		exit(1);
	}
	if (param.width != 0xffff && param.width & 0xff00) {
		fprintf (stderr,"%s: Error: invalid IB width: %x\n", cmdname, param.width);
		exit(1);
	}

	memset( &setPortInfo, 0, sizeof(setPortInfo));
	memset( &getPortInfo, 0, sizeof(getPortInfo));

	if (param.cycle && (param.physstate == IB_PORT_PHYS_NOP
				  || param.physstate == IB_PORT_PHYS_POLLING))
	{
		// for param.speed or param.width change to take effect, need to take port down
		// then back up
		param.physstate = IB_PORT_PHYS_DISABLED;
	} else {
		param.cycle = 0;	// explicit param.physstate of sleep or down, do not cycle port
	}

	if (param.get) {
		PORT_INFO* pPortInfo;

		if (get_ib_port_info((IB_LID)param.dlid, (IB_LID)param.slid,
						param.dlid?param.dport:param.port_in_hfi, &getPortInfo)) {
			if (g_verbose || cmd == portinfo)
				show_ib_port_info("Present Port State:", param.dlid?0:param.portGuid, &getPortInfo);
		} else {
			exit(1);
		}
		if (cmd == portinfo)
			return;

		pPortInfo = (PORT_INFO *)&(getPortInfo.SmpExt.DirectedRoute.SMPData);

		if (cmd == portenable
		    && !param.cycle
		    && (pPortInfo->Link.PortPhysicalState == IB_PORT_PHYS_POLLING
			    || pPortInfo->Link.PortPhysicalState == IB_PORT_PHYS_LINKUP)) {
			/* no need to "enable" a port which is already "enabled" */
			return;
		}

		{
			if (param.speed != IB_LINK_SPEED_NOP && param.speed != 255) {
				uint8 supported = PortDataGetLinkSpeedSupportedCombined(pPortInfo);
				uint8 unsupported = (param.speed & ~supported);
				if (unsupported) {
					fprintf(stderr, "%s: ignoring unsupported speed bits: 0x%x (port supports 0x%x)\n",
						cmdname, unsupported, supported);
					fprintf (stderr,"%s: Error: Unsupported speed: %u\n",
						cmdname, param.speed);
					exit(1);
				}
			}
			if (param.width != STL_LINK_WIDTH_NOP && param.width != 255) {
				uint8 unsupported = (param.width & ~pPortInfo->LinkWidth.Supported);
				if (unsupported) {
					fprintf(stderr, "%s: ignoring unsupported width bits: 0x%x (port supports 0x%x)\n",
						cmdname, unsupported, pPortInfo->LinkWidth.Supported);
					fprintf(stderr, "%s: Error: Unsupported width: %u\n",
						cmdname, param.width);
					exit(1);
				}
			}
			if (param.physstate != 0xff) {
				switch (param.physstate) {
				case IB_PORT_PHYS_NOP:
				case IB_PORT_PHYS_DISABLED:
				case IB_PORT_PHYS_POLLING:
					break;
				default:
					fprintf(stderr, "%s: Error: Unsupported Port Physical State: %u\n",
						cmdname, param.physstate);
					exit(1);
				}
			}
		}
		initialize_ib_set_port_info((IB_LID)param.dlid, (IB_LID)param.slid, (IB_LID)param.nlid,
								param.dlid?param.dport:param.port_in_hfi,
								&setPortInfo, param.state, param.physstate,
								(uint8_t)param.width, (uint8_t)param.speed, param.portGuid, &getPortInfo);
	} else {
		initialize_ib_set_port_info((IB_LID)param.dlid, (IB_LID)param.slid, (IB_LID)param.nlid,
								param.dlid?param.dport:param.port_in_hfi,
								&setPortInfo, param.state, param.physstate,
								(uint8_t)param.width, (uint8_t)param.speed, param.portGuid, NULL);
	}

	if (FSUCCESS == send_ib_port_info(param.hfi, param.portGuid, (IB_LID)param.dlid, param.dport, (IB_LID)param.slid,
								param.port_in_hfi, &setPortInfo)) {
		while (param.repeat--) {
			sleep(1);
			if (FSUCCESS != send_ib_port_info(param.hfi, param.portGuid, (IB_LID)param.dlid, param.dport,
								(IB_LID)param.slid, param.port_in_hfi, &setPortInfo)) {
				exit(1);
			}
		}
	} else {
		fprintf(stderr, "%s: Unable to send port information\n", cmdname);
	}
	
	if (param.cycle) {
		param.physstate = IB_PORT_PHYS_POLLING;
		if (param.get) {
			initialize_ib_set_port_info((IB_LID)param.dlid, (IB_LID)param.slid, (IB_LID)param.nlid,
									param.dlid?param.dport:param.port_in_hfi,
									&setPortInfo, param.state, param.physstate,
									(uint8_t)param.width, (uint8_t)param.speed, param.portGuid, &getPortInfo);
		} else {
			initialize_ib_set_port_info((IB_LID)param.dlid, (IB_LID)param.slid, (IB_LID)param.nlid,
									param.dlid?param.dport:param.port_in_hfi,
									&setPortInfo, param.state, param.physstate,
									(uint8_t)param.width, (uint8_t)param.speed, param.portGuid, NULL);
		}
		if (FSUCCESS != send_ib_port_info(param.hfi, param.portGuid, (IB_LID)param.dlid,
									param.dport, (IB_LID)param.slid,
									param.port_in_hfi, &setPortInfo))
			exit(1);
	}
}

static void run_stl_mode(void)
{
	STL_SMP setPortInfo;
	STL_SMP getPortInfo;


	if(cmd == ledon) {
		send_led_info(true, param.hfi, param.portGuid, param.dlid, param.dport, param.slid,
							param.port_in_hfi);
		return;
	};
	if(cmd == ledoff) {
		send_led_info(false, param.hfi, param.portGuid, param.dlid, param.dport, param.slid,
							param.port_in_hfi);
		return;
	}
	

	memset( &setPortInfo, 0, sizeof(setPortInfo));
	memset( &getPortInfo, 0, sizeof(getPortInfo));

	// for explicit param.physstate of sleep or disabled, do not cycle port
	if (param.cycle && param.physstate == IB_PORT_PHYS_NOP)
	{
		if (param.have_dport && param.dport == 0)
			// for Switch Port 0, Set to Down will bounce port
			param.state = IB_PORT_DOWN;
		else {
			// Set to Polling will bounce port
			param.physstate = IB_PORT_PHYS_POLLING;
		}
	}

	if (param.get) {
		STL_PORT_INFO* pPortInfo;

		if (get_port_info(param.dlid, param.slid,
						param.dlid?param.dport:param.port_in_hfi, &getPortInfo)) {
			if (g_verbose || cmd == portinfo)
				show_port_info("Present Port State:", param.dlid?0:param.portGuid, &getPortInfo);
		} else {
			exit(1);
		}
		if (cmd == portinfo)
			return;

		pPortInfo = (STL_PORT_INFO *)stl_get_smp_data(&getPortInfo);

		if (cmd == portenable
		    && !param.cycle
		    && (pPortInfo->PortStates.s.PortPhysicalState == IB_PORT_PHYS_POLLING
			    || pPortInfo->PortStates.s.PortPhysicalState == IB_PORT_PHYS_LINKUP)) {
			/* no need to "enable" a port which is already "enabled" */
			return;
		}

		{
			if (param.speed != IB_LINK_SPEED_NOP && param.speed != 0xFFFF) {
				uint16_t supported = pPortInfo->LinkSpeed.Supported;
				uint16_t unsupported = (param.speed & ~supported);
				if (unsupported) {
					fprintf(stderr, "%s: ignoring unsupported speed bits: 0x%x (port supports 0x%x)\n",
						cmdname, unsupported, supported);
					fprintf (stderr,"%s: Error: Unsupported speed: %u\n",
						cmdname, param.speed);
					exit(1);
				}
			}
			if (param.width != STL_LINK_WIDTH_NOP && param.width != 0xFFFF) {
				uint16_t supported = pPortInfo->LinkWidth.Supported;
				uint16_t unsupported = (param.width & ~supported);
				if (unsupported) {
					fprintf(stderr, "%s: ignoring unsupported width bits: 0x%x (port supports 0x%x)\n",
						cmdname, unsupported, supported);
					fprintf(stderr, "%s: Error: Unsupported width: %u\n",
						cmdname, param.width);
					exit(1);
				}
			}
			if (param.physstate != 0xff) {
				switch (param.physstate) {
				case IB_PORT_PHYS_NOP:
				case IB_PORT_PHYS_DISABLED:
				case IB_PORT_PHYS_POLLING:
				case STL_PORT_PHYS_TEST:
					break;
				default:
					fprintf(stderr, "%s: Error: Unsupported Port Physical State: %u\n",
						cmdname, param.physstate);
					exit(1);
				}
			}
		}
		initialize_set_port_info(param.dlid, param.slid, param.have_nlid, param.nlid,
								param.dlid?param.dport:param.port_in_hfi,
								&setPortInfo, param.state, param.physstate,
								param.width, param.speed, param.crc, param.portGuid, param.pkey8b, &getPortInfo);
	} else {
		initialize_set_port_info(param.dlid, param.slid, param.have_nlid, param.nlid,
								param.dlid?param.dport:param.port_in_hfi,
								&setPortInfo, param.state, param.physstate,
								param.width, param.speed, param.crc, param.portGuid, param.pkey8b, NULL);
	}


	FSTATUS status;
	status = send_port_info(param.hfi, param.portGuid, param.dlid, param.dport, param.slid,
							param.port_in_hfi, &setPortInfo);

	if (FSUCCESS == status) {
		struct timeval nowTime, endTime;
		if(gettimeofday(&nowTime, NULL) < 0) {
			fprintf(stderr, "Unable to get current time, aborting\n");
			exit(1);
		}
		endTime = nowTime;
		endTime.tv_sec+=param.repeat;
		while((timercmp(&nowTime,&endTime,<))) {
			sleep(1);
			if (FSUCCESS != send_port_info(param.hfi, param.portGuid, param.dlid, param.dport,
								param.slid, param.port_in_hfi, &setPortInfo)) {
				exit(1);
			}
			if(gettimeofday(&nowTime, NULL) < 0) {
				fprintf(stderr, "Unable to get current time, aborting\n");
				exit(1);
			}
		}
	} else {
		fprintf(stderr, "%s: Unable to send port information; status=%d\n", cmdname, status);
	}
	
}

/**
 * determine how we've been invoked
 */
static enum cmd_t determine_invocation(const char *argv0, char **command, const char **options,
									struct option **longopts)
{
	enum cmd_t rc;

	*command = strrchr(argv0, '/');		  // Find last '/' in path
	if (*command != NULL) {
		(*command)++;							  // Skip over last '/'
	} else {
		*command = (char *)argv0;
	}

	if ( (strcmp(*command, "opaportconfig") == 0))  {
		static struct option long_options[] = {
			{"ib", 0, 0, 0},
			{"pkey8b", 0, 0, 1}, // undoc opt to override pkey cntrl for testing
			{"help", 0, 0, 2}, // display help text.
			{0,0,0,0}
		};
		*longopts = long_options;
		rc = portconfig;
		*options = "l:m:h:p:r:zS:P:s:w:c:K:vxL:";
	} else if ( (strcmp(*command, "opaportinfo") == 0)) {
		static struct option long_options[] = {
			{"ib", 0, 0, 0},
			// {"", 0, 0, 1}, // unused in portinfo.
			{"help", 0, 0, 2}, // display help text.
			{0,0,0,0}
		};
		*longopts = long_options;
		rc = portinfo;
		*options = "l:m:h:p:K:vx";
	} else {
		fprintf(stderr, "Invalid command name '%s' (must be opaportinfo or opaportconfig\n",
			*command);
		exit(1);
	}
	return (rc);
}

static void initialize_oib(uint8_t hfi, uint8_t port, int *port_in_hfi,
						STL_LID_32 *slid, EUI64 *portGuid)
{
	int status = 0;

	if ((status = oib_open_port_by_num (&g_oib_port, hfi, port)) != 0) {
		fprintf(stderr, "failed to open port hfi %d:%d: %s\n", hfi, port, strerror(status));
		exit(1);
	}

	*port_in_hfi = oib_get_hfi_port_num(g_oib_port);
	*slid = oib_get_port_lid(g_oib_port);
	*portGuid = oib_get_port_guid(g_oib_port);
    if (oib_find_pkey(g_oib_port, mgmt_pkey) < 0) {
        if (param.dlid) {
		    fprintf(stderr, "ERROR: Local port does not have management privileges\n");
            goto error;
        }
        mgmt_pkey = 0x7fff;
        if (oib_find_pkey(g_oib_port, mgmt_pkey) < 0) {
		    fprintf(stderr, "ERROR: Local port does not have management connectivity!!!\n"
                            "neither 0xffff nor 0x7fff is in pkey table???\n");
            goto error;
        }
    }

    return;

error:
    oib_close_port(g_oib_port);
    exit(1);
}

/**
 * display usage
 */
void Usage(void)
{
	if (cmd == portconfig) {
		fprintf(stderr, "%s manually programs a local port.\n\n", cmdname); 
		fprintf(stderr, "Usage: %s [-l lid [-m dest_port]] [-h hfi] [-p port] [-r secs] [-z] [-S state] [-P physstate] [-s speed] [-w width] [-c LTPCRC]  [-K mkey] [-v] [-L lid] [<sub command>]\n", cmdname);
		fprintf(stderr, "     : %s [--help]\n", cmdname);
	} else {
		fprintf(stderr, "%s displays local port info.\n\n", cmdname); 
		fprintf(stderr, "Usage: %s [-l lid [-m dest_port]] [-h hfi] [-p port] [-K mkey] [-v]\n", cmdname);
		fprintf(stderr, "     : %s [--help]\n", cmdname);
	}

	fprintf(stderr, "\n");
	fprintf(stderr, "    -l lid - destination lid, default is local port\n");
	fprintf(stderr, "    -m dest_port - destination port, default is port with given lid\n");
	fprintf(stderr, "                   useful to access switch ports\n");
	fprintf(stderr, "    -h hfi - hfi, numbered 1..n, 0= -p port will be a\n");
	fprintf(stderr, "             system wide port num (default is 0)\n");
	fprintf(stderr, "    -p port - port, numbered 1..n (default is 1)\n");
	fprintf(stderr, "    -K mkey - SM management key to access remote ports\n");

	if (cmd == portconfig)
	{
		fprintf(stderr, "\n");
		fprintf(stderr, "  <sub command> -- one of the following\n");
		fprintf(stderr, "    enable - Enable port\n");
		fprintf(stderr, "    disable - disable port\n");
		fprintf(stderr, "    bounce - bounce port\n");
		fprintf(stderr, "    ledon - port LED on\n");
		fprintf(stderr, "    ledoff - port LED off\n");
		fprintf(stderr, "\n");
		fprintf(stderr, "  Configuration options\n");
		fprintf(stderr, "\n");
		fprintf(stderr, "    -r secs - repeat to keep port down for secs\n");
		fprintf(stderr, "    -S state - new State (default is 0)\n");
		fprintf(stderr, "          0 - no-op\n");
		fprintf(stderr, "          1 - down\n");
		fprintf(stderr, "          2 - init\n");
		fprintf(stderr, "          3 - armed\n");
		fprintf(stderr, "          4 - active\n");
		fprintf(stderr, "    -P physstate - new physical State (default is 0)\n");
		fprintf(stderr, "          0 - no-op\n");
		fprintf(stderr, "          2 - polling\n");
		fprintf(stderr, "          3 - disabled\n");
	if (mode != MODE_IB)	// bad indentation on purpose
		fprintf(stderr, "         11 - Phy-Test\n");
	else
		fprintf(stderr, "         11 - Phy-Test (STL only)\n");
        fprintf(stderr, "    -s speed - new link speeds enabled (default is 0)\n");
        fprintf(stderr, "        To enable multiple speeds, use sum of desired speeds\n"); 
        fprintf(stderr, "          0 - no-op\n"); 
	if (mode == MODE_IB) {	// bad indentation on purpose
        fprintf(stderr, "       IB: (only valid with --ib)\n");
		fprintf(stderr, "          1 - 0x0001 - 2.5 Gb/s\n");
        fprintf(stderr, "          2 - 0x0002 - 5 Gb/s\n"); 
        fprintf(stderr, "          4 - 0x0004 - 10 Gb/s\n"); 
        fprintf(stderr, "         16 - 0x0010 - 14 Gb/s\n"); 
        fprintf(stderr, "         32 - 0x0020 - 25 Gb/s\n"); 
        fprintf(stderr, "         55 - 0x0037 - All IB supported\n");
        fprintf(stderr, "       STL:\n"); 
	}
        fprintf(stderr, "          1 - 0x0001 - 12.5 Gb/s\n"); 
        fprintf(stderr, "          2 - 0x0002 - 25 Gb/s\n"); 
        fprintf(stderr, "    -w width - new link widths enabled (default is 0)\n");
        fprintf(stderr, "        To enable multiple widths, use sum of desired widths\n");
        fprintf(stderr, "          0 - no-op\n");
	if (mode == MODE_IB) {	// bad indentation on purpose
        fprintf(stderr, "       IB:\n"); 
        fprintf(stderr, "          1 - 0x01 - 1x\n"); 
        fprintf(stderr, "          2 - 0x02 - 4x\n");
        fprintf(stderr, "            - 0x03 - IB All Supported\n");
        fprintf(stderr, "       STL:\n"); 
	}
        fprintf(stderr, "          1 - 0x01 - 1x\n"); 
        fprintf(stderr, "          2 - 0x02 - 2x\n"); 
        fprintf(stderr, "          4 - 0x04 - 3x\n"); 
        fprintf(stderr, "          8 - 0x08 - 4x\n"); 
        fprintf(stderr, "    -c CRC - new CRCs enabled (default is 0)\n");
        fprintf(stderr, "        to enable multiple CRCs, use sum of desired CRCs\n");
        fprintf(stderr, "          0 - no-op\n"); 
        fprintf(stderr, "          1 - 0x1 - 14-bit LTP CRC mode\n"); 
        fprintf(stderr, "          2 - 0x2 - 16-bit LTP CRC mode\n"); 
        fprintf(stderr, "          4 - 0x4 - 48-bit LTP CRC mode\n"); 
		fprintf(stderr, "          8 - 0x8 - 12/16 bits per lane LTP CRC mode\n");
		fprintf(stderr, "            - 0xF - enable all supported\n");
	}

	fprintf(stderr, "\n");
    fprintf(stderr, "The -h and -p options permit a variety of selections:\n");
    fprintf(stderr, "    -h 0       1st port in system (this is the default)\n");
    fprintf(stderr, "    -h x       1st port on HFI x\n");
    fprintf(stderr, "    -h 0 -p y  port y within system\n");
    fprintf(stderr, "    -h x -p y  HFI x, port y\n");

	fprintf(stderr, "\n");
	fprintf(stderr, "  DEBUG options\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    -v - verbose output. Additional invocations will turn on debugging,\n"
			        "         openib debugging and libibumad debugging.\n");

	if (cmd == portconfig) {
		fprintf(stderr, "    -z - do not get port info first, clear most port attributes\n");
		fprintf(stderr, "    -L lid - set PortInfo.LID = lid\n");
	}

	fprintf(stderr, "\n");
	if (mode == MODE_IB) {
		fprintf(stderr, "  LEGACY IB options\n");
		fprintf(stderr, "\n");
		fprintf(stderr, "    --ib - issue IB PortInfo MAD's\n");
		fprintf(stderr, "    -x - enable extended link speeds support\n");
	}

	exit(2);
}

/**
 * main
 * 
 * @param argc
 * @param argv
 * 
 * @return int
 */
int main(int argc, char** argv)
{
	uint8 port = 1;
	int c;
	const char *options;
	struct option *longopts;
	int opt_idx;
	int cycleOverride = 0;

	cmd = determine_invocation(argv[0], &cmdname, &options, &longopts);
	VRBSE_PRINT("Debug: Cmd=%s(%d); Options=%s\n", cmdname, cmd, options);

	PrintDestInitFile(&g_dest, stdout);

	while (-1 != (c = getopt_long(argc, argv, options, longopts, &opt_idx))) {
		switch (c) {
			/* Process Long options */
			case 0: /* --ib */
				mode = MODE_IB;
				break;
			case 1: /* --pkey8b */
				param.pkey8b=0x8001;
				break;
			case 2: /* --help */
				Usage();
				break;
			/* End Long options */
        	case 'x': /* modifies --ib */
                g_enableExtLinkSpeedsSupport=1;
                break;
			case 'v':
				g_verbose++;
				if (g_verbose>2) oib_set_dbg(stderr);
				if (g_verbose>3) umad_debug(g_verbose-3);
				break;
			case 'L':
				if (FSUCCESS != StringToUint32(&param.nlid, optarg, NULL, 0, TRUE)) {
					fprintf(stderr, "%s: Invalid LID: %s\n", cmdname, optarg);
					Usage();
				} else	
					param.have_nlid = 1;
				break;
			case 'l':
				if (FSUCCESS != StringToUint32(&param.dlid, optarg, NULL, 0, TRUE)) {
					fprintf(stderr, "%s: Invalid DLID: %s\n", cmdname, optarg);
					Usage();
				}
				break;
			case 'm':
				if (FSUCCESS != StringToUint8(&param.dport, optarg, NULL, 0, TRUE)) {
					fprintf(stderr, "%s: Invalid destination port: %s\n", cmdname, optarg);
					Usage();
				} else
					param.have_dport = 1;
				break;
			case 'h':
				if (FSUCCESS != StringToUint8(&param.hfi, optarg, NULL, 0, TRUE)) {
					fprintf(stderr, "%s: Invalid HFI Number: %s\n", cmdname, optarg);
					Usage();
				}
				break;
			case 'p':
				if (FSUCCESS != StringToUint8(&port, optarg, NULL, 0, TRUE)) {
					fprintf(stderr, "%s: Invalid Port Number: %s\n", cmdname, optarg);
					Usage();
				}
				if (port == 0) {
					fprintf(stderr, "%s: Invalid port number, First Port is 1\n", cmdname);
					exit(1);
				}
				break;
			case 'r':
				if (FSUCCESS != StringToUint32(&param.repeat, optarg, NULL, 0, TRUE)) {
					fprintf(stderr, "%s: Invalid Repeat: %s\n", cmdname, optarg);
					Usage();
				}
				break;
			case 'z':
				param.get=0;
				break;
			case 's':
				if (FSUCCESS != StringToUint16(&param.speed, optarg, NULL, 0, TRUE)) {
					fprintf(stderr, "%s: Invalid Speed: %s\n", cmdname, optarg);
					Usage();
				}
				if (param.speed)
					param.cycle = 1;
				break;
			case 'w':
				if (FSUCCESS != StringToUint16(&param.width, optarg, NULL, 0, TRUE)) {
					fprintf(stderr, "%s: Invalid Width: %s\n", cmdname, optarg);
					Usage();
				}
				if (param.width)
					param.cycle = 1;
				break;
			case 'c':
				if (FSUCCESS != StringToUint8(&param.crc, optarg, NULL, 0, TRUE)) {
					fprintf(stderr, "%s: Invalid CRC: %s\n", cmdname, optarg);
					Usage();
				}
				if (param.crc)
					param.cycle = 1;
				break;
			case 'S':
				if (FSUCCESS != StringToUint8(&param.state, optarg, NULL, 0, TRUE)
					|| param.state > IB_PORT_STATE_MAX) {
					fprintf(stderr, "%s: Invalid State: %s\n", cmdname, optarg);
					Usage();
				}
				break;
			case 'P':
				if (FSUCCESS != StringToUint8(&param.physstate, optarg, NULL, 0, TRUE)) {
					fprintf(stderr, "%s: Invalid PhysState: %s\n", cmdname, optarg);
					Usage();
				}
				cycleOverride = 1;
				break;
			case 'K':
				if (FSUCCESS != StringToUint64(&g_mkey, optarg, NULL, 0, TRUE)) {
					fprintf(stderr, "%s: Invalid mkey: %s\n", cmdname, optarg);
					Usage();
				}
				break;
			default:
				Usage();
				break;
		}
	}

	if(cycleOverride)
		param.cycle = 0;

	/* process sub-command */
	if (argc != optind) {
		char *subcmd = argv[optind];

		if (strcmp(cmdname, "opaportinfo") == 0) {
			Usage();
		}

		if ( (strcmp(subcmd, "enable") == 0))  {
            cmd = portenable;
            param.state = IB_PORT_NOP;
            param.physstate = IB_PORT_PHYS_POLLING;
		} else if ( (strcmp(subcmd, "disable") == 0))  {
            cmd = portdisable;
            param.state = (mode == MODE_IB ? IB_PORT_DOWN : IB_PORT_NOP);
            param.physstate = IB_PORT_PHYS_DISABLED;
		} else if ( (strcmp(subcmd, "bounce") == 0))  {
			cmd = portbounce;
			param.cycle = 1;
		} else if ( (strcmp(subcmd, "ledon") == 0))  {
			cmd = ledon;
		} else if ( (strcmp(subcmd, "ledoff") == 0))  {
			cmd = ledoff;
		} else {
			fprintf(stderr, "Error: sub command '%s' not found\n", subcmd);
			Usage();
		}
	}

	if (param.have_dport && ! param.dlid) {
		fprintf(stderr, "%s: -m option only valid in conjunction with -l\n", cmdname);
		Usage();
	}

	initialize_oib(param.hfi, port, &param.port_in_hfi, &param.slid, &param.portGuid);

	if (mode == MODE_IB) {
		run_ib_mode();
	} else {
		run_stl_mode();
	}

	return 0;
}	// int main(int argc, char** argv)
