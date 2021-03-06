/* BEGIN_ICS_COPYRIGHT3 ****************************************

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

** END_ICS_COPYRIGHT3   ****************************************/

/* [ICS VERSION STRING: unknown] */

#include "pm_topology.h"
#include "paAccess.h"
#include "iba/stl_pa.h"
#include "fm_xml.h"
//#include <limits.h>
#include <stdio.h>
#include <time.h>
#ifndef __VXWORKS__
#include <strings.h>
#endif

#undef LOCAL_MOD_ID
#define LOCAL_MOD_ID VIEO_PA_MOD_ID

boolean	isFirstImg = TRUE;
boolean	isUnexpectedClearUserCounters = FALSE;
char* FSTATUS_Strings[] = {
	"FSUCCESS",
	"FERROR",
	"FINVALID_STATE",
	"FINVALID_OPERATION",
	"FINVALID_SETTING",
	"FINVALID_PARAMETER",
	"FINSUFFICIENT_RESOURCES",
	"FINSUFFICIENT_MEMORY",
	"FCOMPLETED",
	"FNOT_DONE",
	"FPENDING",
	"FTIMEOUT",
	"FCANCELED",
	"FREJECT",
	"FOVERRUN",
	"FPROTECTION",
	"FNOT_FOUND",
	"FUNAVAILABLE",
	"FBUSY",
	"FDISCONNECT",
	"FDUPLICATE",
	"FPOLL_NEEDED",
};
// given an Image Index, build an externally usable History ImageId
// this will allow future reference to this image via history[]
static uint64 BuildHistoryImageId(Pm_t *pm, uint32 historyIndex)
{
	ImageId_t id;

	id.AsReg64 = 0;
	id.s.type = IMAGEID_TYPE_HISTORY;
	id.s.sweepNum = pm->Image[pm->history[historyIndex]].sweepNum;
	id.s.index = historyIndex;
	if (pm_config.shortTermHistory.enable) 
		id.s.instanceId = pm->ShortTermHistory.currentInstanceId;
	return id.AsReg64;
}

// given an Image Index, build an externally usable FreezeFrame ImageId
// this will allow future reference to this image via freezeFrames[]
static uint64 BuildFreezeFrameImageId(Pm_t *pm, uint32 freezeIndex,
			  						 uint8 clientId)
{
	ImageId_t id;

	id.AsReg64 = 0;
	id.s.type = IMAGEID_TYPE_FREEZE_FRAME;
	id.s.clientId = clientId;
	id.s.sweepNum = pm->Image[pm->freezeFrames[freezeIndex]].sweepNum;
	id.s.index = freezeIndex;
	if (pm_config.shortTermHistory.enable)
		id.s.instanceId = pm->ShortTermHistory.currentInstanceId;
	//printf("build Freeze Frame ImageId: type=%u, client=%u sweep=%u index=%u Image=0x%llx\n", id.s.type, id.s.clientId, id.s.sweepNum, id.s.index, id.AsReg64);
	return id.AsReg64;
}

// given a index into history[], validate index and figure out
// imageIndex into Image[]
// also build retImageId
static FSTATUS ComputeHistory(Pm_t *pm, uint32 historyIndex, int32 offset,
				uint32 baseSweepNum,
				uint32 *imageIndex, uint64 *retImageId, const char **msg)
{
	uint32 index;

	if (historyIndex >= pm_config.total_images
				|| pm->history[historyIndex] == PM_IMAGE_INDEX_INVALID) {
		*msg = "Invalid Image Id";
		return FINVALID_PARAMETER;
	}
	if (offset < 0) {
		if (-offset >= pm_config.total_images || -offset > pm->NumSweeps) {
			*msg = "negative offset exceeds duration of history";
			return FNOT_FOUND;
		}
	} else {
		if (offset >= pm_config.total_images || offset > pm->NumSweeps) {
			*msg = "positive offset exceeds duration of history";
			return FNOT_FOUND;
		}
	}

	// index into pm->history[]
	index = (historyIndex + pm_config.total_images+offset) % pm_config.total_images;
	//printf("offset=%d historyIndex=%u index=%u\n", offset, historyIndex, index);
	*imageIndex = pm->history[index];
	if (*imageIndex == PM_IMAGE_INDEX_INVALID) {
		*msg = "Invalid Image Id";
		return FINVALID_PARAMETER;
	}
	ASSERT(*imageIndex < pm_config.total_images);

	// validate it's still there
	if (pm->Image[*imageIndex].sweepNum != baseSweepNum+offset
		|| pm->Image[*imageIndex].state != PM_IMAGE_VALID) {
		*msg = "offset exceeds duration of history";
		return FNOT_FOUND;
	}
	*retImageId = BuildHistoryImageId(pm, index);
	return FSUCCESS;
}

// take an ImageId (could be Freeze or history/current) and resolve to a
// imageIndex.  Validate the ImageId, index and the Image index points to are
// all valid.  On error return error code and set message to a string
// describing the failure.
// must be called with pm->stateLock held as read or write lock
// imageid is opaque and should be a value previously returned or
// 0=IMAGEID_LIVE_DATA
// offset is valid for FreezeFrame, LiveData and History images
// offset<0 moves back in time, offset >0 moves forward.
// Positive offsets are only valid for FreezeFrame and History Images
// type:
// ANY - no check
// HISTORY - allow live or history
// FREEZE_FRAME - allow freeze frame only
static FSTATUS GetIndexFromImageId(Pm_t *pm, uint8 type, uint64 imageId, int32 offset,
				uint32 *imageIndex, uint64 *retImageId, const char **msg,
				uint8 *clientId)
{
	ImageId_t id;
	uint32 lastImageIndex = pm->history[pm->lastHistoryIndex];

	id.AsReg64 = imageId;

	if (pm->LastSweepIndex == PM_IMAGE_INDEX_INVALID
		|| lastImageIndex == PM_IMAGE_INDEX_INVALID
		|| pm->Image[lastImageIndex].state != PM_IMAGE_VALID) {
		*msg = "Sweep Engine not ready";
		return FUNAVAILABLE;
	}
	DEBUG_ASSERT(lastImageIndex == pm->LastSweepIndex);

	if ( type != IMAGEID_TYPE_ANY
		 && (id.s.type == IMAGEID_TYPE_FREEZE_FRAME)
					 != (type == IMAGEID_TYPE_FREEZE_FRAME)) {
		*msg = "Invalid Image Id Type";
		return FINVALID_PARAMETER;
	}

	// live data or recent history
	if (IMAGEID_LIVE_DATA == imageId) {
		if (offset > 0) {
			*msg = "Positive offset not allowed for live data";
			return FINVALID_PARAMETER;
		} else if (offset == 0) {
			*imageIndex = pm->LastSweepIndex;
			*retImageId = BuildHistoryImageId(pm, pm->lastHistoryIndex);
			return FSUCCESS;
		} else {
			return ComputeHistory(pm, pm->lastHistoryIndex, offset,
						  pm->Image[lastImageIndex].sweepNum,
						  imageIndex, retImageId, msg);
		}
	} else if (id.s.type == IMAGEID_TYPE_HISTORY) {
		return ComputeHistory(pm, id.s.index, offset, id.s.sweepNum, imageIndex,
					   			retImageId, msg);
	} else if (id.s.type == IMAGEID_TYPE_FREEZE_FRAME) {
		time_t now_time;
		PmImage_t *pmimagep;

		if (id.s.index >= pm_config.freeze_frame_images) {
			*msg = "Invalid Image Id";
			return FINVALID_PARAMETER;
		}
		*imageIndex = pm->freezeFrames[id.s.index];
		if (*imageIndex == PM_IMAGE_INDEX_INVALID) {
			*msg = "Invalid Image Id";
			return FINVALID_PARAMETER;
		}
		ASSERT(*imageIndex < pm_config.total_images);
		// validate it's still there
		pmimagep = &pm->Image[*imageIndex];
		if (pmimagep->sweepNum != id.s.sweepNum
			|| pmimagep->state != PM_IMAGE_VALID) {
			*msg = "Freeze Frame expired or invalid";
			return FNOT_FOUND;
		}
		if (0 == pmimagep->ffRefCount) {
			// cleanup, must have expired
			pm->freezeFrames[id.s.index] = PM_IMAGE_INDEX_INVALID;
			*msg = "Freeze Frame expired or invalid";
			return FNOT_FOUND;
		}
		if (0 == (pmimagep->ffRefCount & (1<<id.s.clientId))) {
			*msg = "Freeze Frame expired or invalid";
			return FNOT_FOUND;
		}
		if (type == IMAGEID_TYPE_FREEZE_FRAME && offset != 0) {
			*msg = "Freeze Frame offset not allowed in this query";
			return FINVALID_PARAMETER;
		}
		// stdtime is 32 bits, so should be atomic write and avoid race
		vs_stdtime_get(&now_time);
		pmimagep->lastUsed= now_time;	// update age due to access
		if (clientId)
			*clientId = id.s.clientId;
		if (offset == 0) {
			*retImageId = imageId;	// no translation, return what given
			return FSUCCESS;
		} else {
			return ComputeHistory(pm, pmimagep->historyIndex, offset,
						   		id.s.sweepNum, imageIndex, retImageId, msg);
		}
	} else {
		*msg = "Invalid Image Id";
		return FINVALID_PARAMETER;
	}
}
#ifndef __VXWORKS__

boolean CheckCachedComposite(Pm_t *pm, uint64 imageId) {
	ImageId_t temp;
	temp.AsReg64 = imageId;
	temp.s.type = IMAGEID_TYPE_HISTORY;
	temp.s.clientId = 0;
	temp.s.index = 0;
	int i;
	for (i = 0; i < PM_HISTORY_MAX_IMAGES_PER_COMPOSITE; i++) {
		if (pm->ShortTermHistory.cachedComposite->header.common.imageIDs[i] == temp.AsReg64) 
			return 1;
	}
	return 0;
}

#endif
/************************************************************************************* 
*   FindImage - given and image ID and an offset, find the corresponding image
*      (in-RAM or Short-Term History)
*  
*   Inputs:
*   	pm - the PM
*       type - (in-RAM)image type: ANY, HISTORY, or FREEZE_FRAME
*       imageId - requested image ID
*       offset - requested offset from image ID
*       imageIndex - if image is found in-RAM, this will be the image index
*       retImageId - the image ID of the image which was found
*       record - if the image was found in Short-Term History, this will be its record
*       msg - error message
*       clientId - client ID
*       frozen - if the found image is the frozen Short-Term History image, this will be true
*       sth - if the found image is in Short-Term History, this will be true
*
*   Returns:
*   	FSUCCESS if success, FNOT_FOUND if not found
*  
*************************************************************************************/
FSTATUS FindImage(
	Pm_t *pm, 
	uint8 type, 
	uint64 imageId, 
	int32 offset, 
	uint32 *imageIndex, 
	uint64 *retImageId, 
	PmHistoryRecord_t **record,
	const char **msg, 
	uint8 *clientId,
	boolean *frozen,
	boolean *sth)
{
	FSTATUS status;
	*sth = 0;
	*frozen = 0;

	// image ID in RAM, offset in RAM
	status = GetIndexFromImageId(pm, type, imageId, offset, imageIndex, retImageId, msg, clientId);
	if (status == FSUCCESS)	return status;

	if (!pm_config.shortTermHistory.enable)
		return status;
	
#ifndef __VXWORKS__
	PmHistoryRecord_t *found;
	ImageId_t histId;
	if (imageId) {
		histId.AsReg64 = imageId;
		histId.s.type = IMAGEID_TYPE_HISTORY;
		histId.s.clientId = 0;
		histId.s.index = 0;
	} else {
		histId.AsReg64 = 0;
	}
	
	int ireq;	// index of the requested imageID in history
	int icurr;	// index of the record currently be built in history
	int oneg;	// maximum negative offset allowed
	int opos;	// maximum positive offset allowed
	int tot;	// total number of records in history

	// image ID in RAM, offset negative into history
	status = GetIndexFromImageId(pm, type, imageId, 0, imageIndex, retImageId, msg, clientId);
	if (status == FSUCCESS && offset < 0) {
		// adjust offset and set up ireq
		int32 ramOff = imageId != IMAGEID_LIVE_DATA ? histId.s.sweepNum - pm->NumSweeps : 0; // this is the offset from live of the image ID in RAM
		// recalculate the offset: RAM offset + offset + total images - (total images / images per composite)
		offset = ramOff + offset + (int32)MIN(pm_config.total_images, pm->NumSweeps) - ((int32)MIN(pm_config.total_images, pm->NumSweeps) / (int32)pm_config.shortTermHistory.imagesPerComposite);
		// when the number of images in the current composite is at its max, the offset needs to adjusted to make up for it, unless total images per composite is 1
		offset = pm->ShortTermHistory.currentComposite->header.common.imagesPerComposite == pm_config.shortTermHistory.imagesPerComposite && pm_config.shortTermHistory.imagesPerComposite != 1? offset:offset + 1;		
		ireq = pm->ShortTermHistory.currentRecordIndex==0?pm->ShortTermHistory.totalHistoryRecords - 1:pm->ShortTermHistory.currentRecordIndex - 1; // now we will be working from the first sth record
	} else {
		// image ID in history
		// check frozen composite
		if (!offset && pm->ShortTermHistory.cachedComposite) {
			*sth = *frozen = CheckCachedComposite(pm, histId.AsReg64);
			if (*frozen) return FSUCCESS;
		}
		// find this image
		cl_map_item_t *mi;
		// look up the image Id in the history images map
		mi = cl_qmap_get(&pm->ShortTermHistory.historyImages, histId.AsReg64);
		if (mi == cl_qmap_end(&pm->ShortTermHistory.historyImages)) {
			*msg = "Invalid image ID";
			return FNOT_FOUND;
		}
		// find the parent entry for the map item
		PmHistoryImageEntry_t *entry = PARENT_STRUCT(mi, PmHistoryImageEntry_t, historyImageEntry);
		if (!entry) {
			*msg = "Error looking up image ID entry";
			return FNOT_FOUND;
		}
		if (entry->inx == INDEX_NOT_IN_USE) {
			*msg = "Error looking up image ID entry not in use";
			return FNOT_FOUND;
		}
		// find the parent record of the entries
		PmHistoryImageEntry_t *entries = entry - (entry->inx);
		found = PARENT_STRUCT(entries, PmHistoryRecord_t, historyImageEntries);
		if (!found || found->index == INDEX_NOT_IN_USE) {
			*msg = "Error looking up image ID no parent entries found";
			return FNOT_FOUND;
		}
		// if offset is 0, then this is the requested record
		if (offset == 0) {
			*record = found;
			*sth = 1;
			return FSUCCESS;
		}
		// otherwise, set ireq
		ireq = found->index;
	}
	icurr = pm->ShortTermHistory.currentRecordIndex;
	tot = pm->ShortTermHistory.totalHistoryRecords;

	// image ID in history, offset positive into RAM
	if (offset > 0) {
		// check: offset goes into RAM but doesn't exceed RAM
		int32 maxRamOff, minRamOff; // the maximum and minimum values for offset that would place the requested image in RAM
		// calculate maxRamOff and minRamOff
		minRamOff = ((ireq < icurr)?(icurr - ireq):(tot - (ireq - icurr))) - ((int32)MIN(pm_config.total_images, pm->NumSweeps)/pm_config.shortTermHistory.imagesPerComposite) + 1;
		maxRamOff = minRamOff + (int32)MIN(pm_config.total_images, pm->NumSweeps) - 2;
		if (offset <= maxRamOff && offset >= minRamOff) {
			// calculate new Offset (offset from the live RAM image)
			int32 newOffset = offset - maxRamOff + 1;
			newOffset = pm->ShortTermHistory.currentComposite->header.common.imagesPerComposite == pm_config.shortTermHistory.imagesPerComposite && pm_config.shortTermHistory.imagesPerComposite != 1? newOffset:newOffset + 1;
			// call GetIndexFromImageId again, with adjusted offset and live image ID
			status = GetIndexFromImageId(pm, type, IMAGEID_LIVE_DATA, newOffset, imageIndex, retImageId, msg, clientId);
			if (status == FSUCCESS) return status;
			// we failed - probably because the image is being overwritten by the next sweep
			// so just look in the short-term history instead
		} else if (offset > maxRamOff) {
			*msg = "Offset exceeds duration of history";
			return FNOT_FOUND;
		}
		// otherwise check the Short-Term History images
	}	
	
	// offset into history
	
	// establish positive and negative bounds for the offset
	if (ireq >= icurr) {
		oneg = ireq - icurr;
		opos = tot - (oneg + 1);
	} else {
		oneg = tot - (icurr - ireq);
		opos = icurr - ireq - 1;
	}

	if (offset < -oneg) {
		*msg = "Negative offset exceeds duration of Short-Term History";
		return FNOT_FOUND;
	} else if (offset > opos) {
		*msg = "Positive offset exceeds duration of Short-Term History";
		return FNOT_FOUND;
	} else {
		// find the record
		int r = (offset < 0)?(ireq + offset):((ireq + offset)%tot);
		if (r < 0) {
			// wrap
			r = tot + r;
		}
		*record = pm->ShortTermHistory.historyRecords[r];
		if (!*record || !(*record)->header.timestamp) {
			*msg = "Image request found empty history record";
			return FNOT_FOUND;
		}
		*imageIndex = 0;
		*sth = 1;
		return FSUCCESS;
	}
#else
	return status;
#endif
}

// locate group by name
static FSTATUS LocateGroup(Pm_t *pm, const char *groupName, PmGroup_t **pmGroupP)
{
	int i;

	if (strcmp(groupName, pm->AllPorts->Name) == 0) {
		*pmGroupP = pm->AllPorts;
		return FSUCCESS;
	}

	for (i = 0; i < pm->NumGroups; i++) {
		if (strcmp(groupName, pm->Groups[i]->Name) == 0) {
			*pmGroupP = pm->Groups[i];
			return FSUCCESS;
		}
	}

	return FNOT_FOUND;
}

/*************************************************************************************
*
* paGetGroupList - return list of group names
*
*  Inputs:
*     pm - pointer to Pm_t (the PM main data type)
*     pmGroupList - pointer to caller-declared data area to return names
*
*  Return:
*     FSTATUS - FSUCCESS if OK
*
*
*************************************************************************************/

FSTATUS paGetGroupList(Pm_t *pm, PmGroupList_t *GroupList)
{
	Status_t			vStatus;
	FSTATUS				fStatus = FSUCCESS;
	int					i;

	// check input parameters
	if (!pm || !GroupList)
		return(FINVALID_PARAMETER);

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		fStatus = FUNAVAILABLE;
		goto done;
	}
	GroupList->NumGroups = pm->NumGroups + 1; // Number of Groups plus ALL Group

	vStatus = vs_pool_alloc(&pm_pool, GroupList->NumGroups * STL_PM_GROUPNAMELEN,
						   (void*)&GroupList->GroupList);
	if (vStatus != VSTATUS_OK) {
		IB_LOG_ERRORRC("Failed to allocate name list buffer for GroupList rc:", vStatus);
		fStatus = FINSUFFICIENT_MEMORY;
		goto done;
	}
	// no lock needed, group names are constant once PM starts
	snprintf(GroupList->GroupList[0].Name, STL_PM_GROUPNAMELEN, "%s", pm->AllPorts->Name);

	for (i = 0; i < pm->NumGroups; i++)
		snprintf(GroupList->GroupList[i+1].Name, STL_PM_GROUPNAMELEN, "%s", pm->Groups[i]->Name);

done:
	AtomicDecrementVoid(&pm->refCount);
	return(fStatus);
}

/*************************************************************************************
*
* paGetGroupInfo - return group information
*
*  Inputs:
*     pm - pointer to Pm_t (the PM main data type)
*     groupName - pointer to name of group
*     pmGroupInfo - pointer to caller-declared data area to return group information
*
*  Return:
*     FSTATUS - FSUCCESS if OK, FERROR
*
*
*************************************************************************************/

FSTATUS paGetGroupInfo(Pm_t *pm, char *groupName, PmGroupInfo_t *pmGroupInfo, uint64 imageId, int32 offset, uint64 *returnImageId,
						boolean *isFailedPort)
{
	uint64				retImageId = 0;
	FSTATUS				status = FSUCCESS;
	PmGroup_t			*pmGroupP = NULL;
	PmGroupImage_t		pmGroupImage;
	PmImage_t			*pmImageP = NULL;
	PmPortImage_t		*pmPortImageP = NULL, *pmPortImageNeighborP = NULL;
	PmPort_t			*pmPortP = NULL;
	uint32				imageIndex;
	const char 			*msg;
	boolean				sth = 0;
	STL_LID_32 			lid;
	uint32				g;
	boolean				isGroupAll = FALSE;
	PmHistoryRecord_t	*record = NULL;
	boolean 			frozen = 0;

	// check input parameters
	if (!pm || !groupName || !pmGroupInfo || !isFailedPort)
		return(FINVALID_PARAMETER);
	if (groupName[0] == '\0') {
		IB_LOG_WARN_FMT(__func__, "Illegal groupName parameter: Empty String\n");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER) ;
	}

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}

	// collect statistics from last sweep and populate pmGroupInfo
	(void)vs_rdlock(&pm->stateLock);
	status = FindImage(pm, IMAGEID_TYPE_ANY, imageId, offset, &imageIndex, &retImageId, &record, &msg, NULL, &frozen, &sth);

	if (FSUCCESS != status) {
		IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
		goto error;
	}

	if (sth && (frozen || (record && !frozen))) {
		PmCompositeImage_t *cimg;
		if (!frozen) {
			// try to load
			status = PmLoadComposite(pm, record, &cimg);
			if (status != FSUCCESS || !cimg) {
				IB_LOG_WARN_FMT(__func__, "Unable to load composite image: %s", FSTATUS_ToString(status));
				goto error;
			}
		} else {
			cimg = pm->ShortTermHistory.cachedComposite;
		}
		// set the return ID
		retImageId = cimg->header.common.imageIDs[0];
		// composite is loaded, reconstitute so we can use it
		status = PmReconstitute(&pm->ShortTermHistory, cimg);
		if (!frozen) PmFreeComposite(cimg);
		if (status != FSUCCESS) {
			IB_LOG_WARN_FMT(__func__, "Unable to reconstitute composite image: %s", FSTATUS_ToString(status));
			goto error;
		}
		pmImageP = pm->ShortTermHistory.LoadedImage.img;
		// look for the group
		if (!strcmp(pm->ShortTermHistory.LoadedImage.AllGroup->Name, groupName)) {
			pmGroupP = pm->ShortTermHistory.LoadedImage.AllGroup;
			isGroupAll = TRUE;
		} else {
			int i;
			for (i = 0; i < PM_MAX_GROUPS; i++) {
				if (!strcmp(pm->ShortTermHistory.LoadedImage.Groups[i]->Name, groupName)) {
					pmGroupP = pm->ShortTermHistory.LoadedImage.Groups[i];
					break;
				}
			}
		}
		if (!pmGroupP) {
			IB_LOG_WARN_FMT(__func__, "Group %.*s not Found", sizeof(groupName), groupName);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_GROUP;
			goto error;
		}
		imageIndex = 0; // STH always uses imageIndex 0
	} else {
		status = LocateGroup(pm, groupName, &pmGroupP);
		if (status != FSUCCESS) {
			IB_LOG_WARN_FMT(__func__, "Group %.*s not Found: %s", sizeof(groupName), groupName, FSTATUS_ToString(status));
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_GROUP;
			goto error;
		}
		if (pmGroupP == pm->AllPorts) isGroupAll = TRUE;

		pmImageP = &pm->Image[imageIndex];
		(void)vs_rdlock(&pmImageP->imageLock);
	}
	(void)vs_rwunlock(&pm->stateLock);
	memset(&pmGroupImage, 0, sizeof(PmGroupImage_t));
	ClearGroupStats(&pmGroupImage);

	*isFailedPort = FALSE;
	for (lid = 1; lid <= pmImageP->maxLid; lid++ ) {
		PmNode_t *pmNodeP = pmImageP->LidMap[lid];
		if (!pmNodeP) continue;
		if (pmNodeP->nodeType == STL_NODE_SW) {
			int p;
			for (p=0; p <= pmNodeP->numPorts; p++) { 	// Includes port 0
				pmPortP = pmNodeP->up.swPorts[p];
				// if this is a sth image, the port may be 'empty' but not null 
				// 'Empty' ports should be excluded from the count, and can be indentified by their having a port num and guid of 0
				if (!pmPortP || (sth && !pmPortP->guid && !pmPortP->portNum)) continue;

				pmPortImageP = &pmPortP->Image[imageIndex];
				if (pmPortImageP->u.s.queryStatus != PM_QUERY_STATUS_OK) {
					// mark a flag to indicate there is at least one failed port.
					*isFailedPort = TRUE;
					continue;
				}
				if (isGroupAll) {
					pmGroupImage.NumIntPorts++;
					UpdateInGroupStats(pm, &pmGroupImage, pmPortImageP); // includes all ports
				} else {
#if PM_COMPRESS_GROUPS
					for (g=0; g < pmPortImageP->u.s.InGroups; g++) {
#else
					for (g=0; g<PM_MAX_GROUPS_PER_PORT; g++) {
#endif
						PmGroup_t *pmPortGroupP = pmPortImageP->Groups[g];
#if PM_COMPRESS_GROUPS
						DEBUG_ASSERT(pmPortGroupP);
#else
						if (! pmPortGroupP)
							continue;	// could be gaps, keep going
#endif
						if (pmGroupP != pmPortGroupP) 
							continue;   // only update group

						if (pmPortImageP->IntLinkFlags & (1<<g)) {
							pmGroupImage.NumIntPorts++;
							UpdateInGroupStats(pm, &pmGroupImage, pmPortImageP);
						} else {
							pmGroupImage.NumExtPorts++;
							pmPortImageNeighborP = &pmPortP->Image[imageIndex].neighbor->Image[imageIndex];
							UpdateExtGroupStats(pm, &pmGroupImage, pmPortImageP, pmPortImageNeighborP);
						}
					}
				}
			}
		} else {
			pmPortP = pmNodeP->up.caPortp;
			if (!pmPortP) continue;
			pmPortImageP = &pmPortP->Image[imageIndex]; 
			if (pmPortImageP->u.s.queryStatus != PM_QUERY_STATUS_OK) {
				// mark a flag to indicate there is at least one failed port.
				*isFailedPort = TRUE;
				continue;
			}
			if (isGroupAll) {
				pmGroupImage.NumIntPorts++;
				UpdateInGroupStats(pm, &pmGroupImage, pmPortImageP); // includes all ports
			} else {
#if PM_COMPRESS_GROUPS
				for (g=0; g < pmPortImageP->u.s.InGroups; g++) {
#else
				for (g=0; g<PM_MAX_GROUPS_PER_PORT; g++) {
#endif
					PmGroup_t *pmPortGroupP = pmPortImageP->Groups[g];
#if PM_COMPRESS_GROUPS
					DEBUG_ASSERT(pmPortGroupP);
#else
					if (! pmPortGroupP)
						continue;	// could be gaps, keep going
#endif
					if (pmGroupP != pmPortGroupP) 
						continue;   // only update group

					if (pmPortImageP->IntLinkFlags & (1<<g)) {
						pmGroupImage.NumIntPorts++;
						UpdateInGroupStats(pm, &pmGroupImage, pmPortImageP);
					} else {
						pmGroupImage.NumExtPorts++;
						pmPortImageNeighborP = &pmPortP->Image[imageIndex].neighbor->Image[imageIndex];
						UpdateExtGroupStats(pm, &pmGroupImage, pmPortImageP, pmPortImageNeighborP);
					}
				}
			}
		}
	}
	FinalizeGroupStats(&pmGroupImage);
	strcpy(pmGroupInfo->groupName, pmGroupP->Name);
	pmGroupInfo->NumIntPorts = pmGroupImage.NumIntPorts;
	pmGroupInfo->NumExtPorts = pmGroupImage.NumExtPorts;
	memcpy(&pmGroupInfo->IntUtil, &pmGroupImage.IntUtil, sizeof(PmUtilStats_t));
	memcpy(&pmGroupInfo->SendUtil, &pmGroupImage.SendUtil, sizeof(PmUtilStats_t));
	memcpy(&pmGroupInfo->RecvUtil, &pmGroupImage.RecvUtil, sizeof(PmUtilStats_t));
	memcpy(&pmGroupInfo->IntErr, &pmGroupImage.IntErr, sizeof(PmErrStats_t));
	memcpy(&pmGroupInfo->ExtErr, &pmGroupImage.ExtErr, sizeof(PmErrStats_t));
	pmGroupInfo->MinIntRate = pmGroupImage.MinIntRate;
	pmGroupInfo->MaxIntRate = pmGroupImage.MaxIntRate;
	pmGroupInfo->MinExtRate = pmGroupImage.MinExtRate;
	pmGroupInfo->MaxExtRate = pmGroupImage.MaxExtRate;

	if (!sth) {
		(void)vs_rwunlock(&pmImageP->imageLock);
	} 

	*returnImageId = retImageId;

done:
	AtomicDecrementVoid(&pm->refCount);
	return(status);
error:
	(void)vs_rwunlock(&pm->stateLock);
	*returnImageId = BAD_IMAGE_ID;
	goto done;
}

#define PORTLISTCHUNK	256

/*************************************************************************************
*
* paGetGroupConfig - return group configuration information
*
*  Inputs:
*     pm - pointer to Pm_t (the PM main data type)
*     groupName - pointer to name of group
*     pmGroupConfig - pointer to caller-declared data area to return group config info
*
*  Return:
*     FSTATUS - FSUCCESS if OK
*
*
*************************************************************************************/

FSTATUS paGetGroupConfig(Pm_t *pm, char *groupName, PmGroupConfig_t *pmGroupConfig, uint64 imageId, int32 offset, uint64 *returnImageId)
{
	uint64				retImageId = 0;
	PmGroup_t			*pmGroupP = NULL;
	STL_LID_32			lid;
	uint32				imageIndex;
	const char 			*msg;
	PmImage_t			*pmimagep = NULL;
	FSTATUS				status = FSUCCESS;
	boolean				sth = 0;
	PmHistoryRecord_t	*record = NULL;
	boolean 			frozen = 0;

	// check input parameters
	if (!pm || !groupName || !pmGroupConfig)
		return(FINVALID_PARAMETER);
	if (groupName[0] == '\0') {
		IB_LOG_WARN_FMT(__func__, "Illegal groupName parameter: Empty String\n");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER) ;
	}

	// initialize group config port list counts
	pmGroupConfig->NumPorts = 0;
	pmGroupConfig->portListSize = 0;
	pmGroupConfig->portList = NULL;

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}

	// pmGroupP points to our group
	// check all ports for membership in our group
	(void)vs_rdlock(&pm->stateLock);
	status = FindImage(pm, IMAGEID_TYPE_ANY, imageId, offset, &imageIndex, &retImageId, &record, &msg, NULL, &frozen, &sth);

	if (FSUCCESS != status) {
		IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
		goto error;
	}
	
	if (sth && (frozen || (record && !frozen))) {
		PmCompositeImage_t *cimg;
		if (!frozen) {
			// found the record, try to load it
			status = PmLoadComposite(pm, record, &cimg);
			if (status != FSUCCESS || !cimg) {
				IB_LOG_WARN_FMT(__func__, "Unable to load composite image: %s", FSTATUS_ToString(status));
				goto error;
			}
		} else {
			cimg = pm->ShortTermHistory.cachedComposite;
		}
		retImageId = cimg->header.common.imageIDs[0];
		// composite is loaded, reconstitute so we can use it
		status = PmReconstitute(&pm->ShortTermHistory, cimg);
		if (!frozen) PmFreeComposite(cimg);
		if (status != FSUCCESS) {
			IB_LOG_WARN_FMT(__func__, "Unable to reconstitute composite image: %s", FSTATUS_ToString(status));
			goto error;
		}
		// look for the group
		if (!strcmp(pm->ShortTermHistory.LoadedImage.AllGroup->Name, groupName)) {
			pmGroupP = pm->ShortTermHistory.LoadedImage.AllGroup;
		} else {
			int i;
			for (i = 0; i < PM_MAX_GROUPS; i++) {
				if (!strcmp(pm->ShortTermHistory.LoadedImage.Groups[i]->Name, groupName)) {
					pmGroupP = pm->ShortTermHistory.LoadedImage.Groups[i];
					break;
				}
			}
		}
		if (!pmGroupP) {
			IB_LOG_WARN_FMT(__func__, "Group %.*s not Found", sizeof(groupName), groupName);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_GROUP;
			goto error;
		}
		imageIndex = 0; // STH always uses imageIndex 0
		pmimagep = pm->ShortTermHistory.LoadedImage.img;
	} else {
		status = LocateGroup(pm, groupName, &pmGroupP);
		if (status != FSUCCESS) {
			IB_LOG_WARN_FMT(__func__, "Group %.*s not Found: %s", sizeof(groupName), groupName, FSTATUS_ToString(status));
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_GROUP;
			goto error;
		}
		pmimagep = &pm->Image[imageIndex];
		(void)vs_rdlock(&pmimagep->imageLock);
	}
	(void)vs_rwunlock(&pm->stateLock);
	for (lid=1; lid<= pmimagep->maxLid; ++lid) {
		uint8 portnum;
		PmNode_t *pmnodep = pmimagep->LidMap[lid];
		if (! pmnodep)
			continue;
		if (pmnodep->nodeType == STL_NODE_SW) {
			for (portnum=0; portnum<=pmnodep->numPorts; ++portnum) {
				PmPort_t *pmportp = pmnodep->up.swPorts[portnum];
				PmPortImage_t *portImage;
				if (! pmportp)
					continue;
				portImage = &pmportp->Image[imageIndex];
				if (PmIsPortInGroup(pm, pmportp, portImage, pmGroupP, sth))
				{
					if (pmGroupConfig->portListSize == pmGroupConfig->NumPorts) {
						pmGroupConfig->portListSize += PORTLISTCHUNK;
					}
					pmGroupConfig->NumPorts++;
				}
			}
		} else {
			PmPort_t *pmportp = pmnodep->up.caPortp;
			PmPortImage_t *portImage = &pmportp->Image[imageIndex];
			if (PmIsPortInGroup(pm, pmportp, portImage, pmGroupP, sth))
			{
				if (pmGroupConfig->portListSize == pmGroupConfig->NumPorts) {
					pmGroupConfig->portListSize += PORTLISTCHUNK;
				}
				pmGroupConfig->NumPorts++;
			}
		}
	}
	// check if there are ports to sort
	if (!pmGroupConfig->NumPorts) {
		IB_LOG_INFO_FMT(__func__, "Group %.*s has no ports", sizeof(groupName), groupName);
		goto norecords;
	}
	// allocate the port list
	Status_t ret = vs_pool_alloc(&pm_pool, pmGroupConfig->portListSize * sizeof(PmPortConfig_t), (void *)&pmGroupConfig->portList);
	if (ret != VSTATUS_OK) {
		if (!sth) (void)vs_rwunlock(&pmimagep->imageLock);
		status = FINSUFFICIENT_MEMORY;
		IB_LOG_ERRORRC("Failed to allocate port list buffer for pmGroupConfig rc:", ret);
		goto done;
	}
	// copy the port list
	int i = 0;
	for (lid=1; lid <= pmimagep->maxLid; ++lid) {
		uint8 portnum;
		PmNode_t *pmnodep = pmimagep->LidMap[lid];
		if (!pmnodep) 
			continue;
		if (pmnodep->nodeType == STL_NODE_SW) {
			for (portnum=0; portnum <= pmnodep->numPorts; ++portnum) {
				PmPort_t *pmportp = pmnodep->up.swPorts[portnum];
				PmPortImage_t *portImage;
				if (!pmportp) 
					continue;
				portImage = &pmportp->Image[imageIndex];
				if (PmIsPortInGroup(pm, pmportp, portImage, pmGroupP, sth)) {
					pmGroupConfig->portList[i].lid = lid;
					pmGroupConfig->portList[i].portNum = pmportp->portNum;
					pmGroupConfig->portList[i].guid = pmnodep->guid;
					memcpy(pmGroupConfig->portList[i].nodeDesc, (char *)pmnodep->nodeDesc.NodeString,
						   sizeof(pmGroupConfig->portList[i].nodeDesc));
					i++;
				}
			}
		} else {
			PmPort_t *pmportp = pmnodep->up.caPortp;
			PmPortImage_t *portImage = &pmportp->Image[imageIndex];
			if (PmIsPortInGroup(pm, pmportp, portImage, pmGroupP, sth)) {
				pmGroupConfig->portList[i].lid = lid;
				pmGroupConfig->portList[i].portNum = pmportp->portNum;
				pmGroupConfig->portList[i].guid = pmnodep->guid;
				memcpy(pmGroupConfig->portList[i].nodeDesc, (char *)pmnodep->nodeDesc.NodeString,
					   sizeof(pmGroupConfig->portList[i].nodeDesc));
				i++;
			}
		}
	}
norecords:
	strcpy(pmGroupConfig->groupName, pmGroupP->Name);
	*returnImageId = retImageId;

	(void)vs_rwunlock(&pmimagep->imageLock);
done:
	if (sth)  {
#ifndef __VXWORKS__
		clearLoadedImage(&pm->ShortTermHistory);
#endif
	}
	AtomicDecrementVoid(&pm->refCount);
	return(status);
error:
	(void)vs_rwunlock(&pm->stateLock);
	*returnImageId = BAD_IMAGE_ID;
	goto done;
}

/*************************************************************************************
*
* paGetPortStats - return port statistics
*			 Get Running totals for a Port.  This simulates a PMA get so
*			 that tools like opareport can work against the Running totals
*			 until we have a history feature.
*
*  Inputs:
*     pm - pointer to Pm_t (the PM main data type)
*     lid, portNum - lid and portNum to select port to get
*     portCounterP - pointer to Consolidated Port Counters data area
*     delta - 1 for delta counters, 0 for running counters
*
*  Return:
*     FSTATUS - FSUCCESS if OK
*
*
*************************************************************************************/

FSTATUS paGetPortStats(Pm_t *pm, STL_LID_32 lid, uint8 portNum, PmCompositePortCounters_t *portCountersP, uint32 delta, uint32 userCntrs, uint64 imageId, int32 offset, uint32 *flagsp, uint64 *returnImageId)
{
	uint64				retImageId = 0, retImageId2 = 0;
	FSTATUS				status = FSUCCESS;
	PmPort_t			*pmPortP, *pmPortPreviousP = NULL;
	PmPortImage_t		*pmPortImageP, *pmPortImagePreviousP = NULL;
	PmPortImage_t		pmPortImage;
	const char 			*msg;
	PmImage_t			*pmImageP, *pmImagePreviousP = NULL;
	uint32				imageIndex = PM_IMAGE_INDEX_INVALID, imageIndexPrevious = PM_IMAGE_INDEX_INVALID;
	boolean				sth = 0, sth2 = 0;
	PmHistoryRecord_t	*record, *record2= NULL;
	boolean 			frozen, frozen2= 0;

	// check input parameters
	if (!pm || !portCountersP || !flagsp)
		return(FINVALID_PARAMETER);
	if (!lid) {
		IB_LOG_WARN_FMT(__func__,  "Illegal LID parameter: must not be zero");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}
	if (userCntrs && (delta || offset)) {
		IB_LOG_WARN_FMT(__func__,  "Illegal combination of parameters: Offset (%d) and delta(%d) must be zero if UserCounters(%d) flag is set",
						offset, delta, userCntrs);
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}

	(void)vs_rdlock(&pm->stateLock);
	if (userCntrs) {
		imageIndex = pm->LastSweepIndex;
		if (pm->LastSweepIndex == PM_IMAGE_INDEX_INVALID) {
			IB_LOG_WARN_FMT(__func__, "Unable to Get PortStats: PM has not completed a sweep.");
			(void)vs_rwunlock(&pm->stateLock);
			status = FUNAVAILABLE | STL_MAD_STATUS_STL_PA_UNAVAILABLE;
			goto done;
		}
		pmImageP = &pm->Image[imageIndex];
		(void)vs_rdlock(&pmImageP->imageLock);

		pmPortP = pm_find_port(pmImageP, lid, portNum);
		if (!pmPortP) {
			IB_LOG_WARN_FMT(__func__, "Port not found: Lid 0x%x Port %u", lid, portNum);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_PORT;
			goto unlock;
		}
		(void)vs_rwunlock(&pm->stateLock);
		// for Running totals, Live Data only, no offset
		(void)vs_rdlock(&pm->totalsLock);
		memcpy(portCountersP, &pmPortP->StlPortCountersTotal, sizeof(PmCompositePortCounters_t));
		(void)vs_rwunlock(&pm->totalsLock);
		(void)vs_rwunlock(&pmImageP->imageLock);
		*flagsp = STL_PA_PC_FLAG_USER_COUNTERS |
			(isUnexpectedClearUserCounters ? STL_PA_PC_FLAG_UNEXPECTED_CLEAR : 0);
		goto done;
	}
	status = FindImage(pm, IMAGEID_TYPE_ANY, imageId, offset, &imageIndex, &retImageId, &record, &msg, NULL, &frozen, &sth);
	if (FSUCCESS != status) {
		IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
		goto error;
	}

	if (sth && (frozen || (record && !frozen))) {
			PmCompositeImage_t *cimg;
			if (!frozen) {
				status = PmLoadComposite(pm, record, &cimg);
				if (status != FSUCCESS || !cimg) {
					IB_LOG_WARN_FMT(__func__, "Unable to load composite image: %s", FSTATUS_ToString(status));
					goto error;
				}
			} else {
				cimg = pm->ShortTermHistory.cachedComposite;
			}
			retImageId = cimg->header.common.imageIDs[0];
			status = PmReconstitute(&pm->ShortTermHistory, cimg);
			if (!frozen) PmFreeComposite(cimg);
			if (status != FSUCCESS) {
				IB_LOG_WARN_FMT(__func__, "Unable to reconstitute composite image: %s", FSTATUS_ToString(status));
				goto error;
			}

			pmImageP = pm->ShortTermHistory.LoadedImage.img;
			imageIndex = 0;
	} else {
		pmImageP = &pm->Image[imageIndex];
		(void)vs_rdlock(&pmImageP->imageLock);
	}
	
	pmPortP = pm_find_port(pmImageP, lid, portNum);
	if (!pmPortP) {
		IB_LOG_WARN_FMT(__func__, "Port not found: Lid 0x%x Port %u", lid, portNum);
		status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_PORT;
		goto unlock;
	}
	pmPortImageP = &pmPortP->Image[imageIndex];
	if (pmPortImageP->u.s.queryStatus != PM_QUERY_STATUS_OK) {
		IB_LOG_WARN_FMT(__func__, "Port Query Status Invalid: %s: Lid 0x%x Port %u",
			(pmPortImageP->u.s.queryStatus == PM_QUERY_STATUS_SKIP ? "Skipped" :
			(pmPortImageP->u.s.queryStatus == PM_QUERY_STATUS_FAIL_QUERY ? "Failed Query" : "Failed Clear")),
			lid, portNum);
		status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_IMAGE;
		goto unlock;
	}
	if (delta) {
		if (sth) {
			memcpy(&pmPortImage, pmPortImageP, sizeof(PmPortImage_t));
			pmPortImageP = &pmPortImage;
		}
		
		status = FindImage(pm, IMAGEID_TYPE_ANY, imageId, offset-1, &imageIndexPrevious, &retImageId2, &record2, &msg, NULL, &frozen2, &sth2);
		if (FSUCCESS != status) {
			IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
			goto unlock;
		}

		if (sth2 && (frozen2 || (record2 && !frozen2))) {
			PmCompositeImage_t *cimg2;
			if (!frozen2) {
				status = PmLoadComposite(pm, record2, &cimg2);
				if (status != FSUCCESS || !cimg2) {
					IB_LOG_WARN_FMT(__func__, "Unable to load composite image: %s", FSTATUS_ToString(status));
					goto unlock;
				}
			} else {
				cimg2 = pm->ShortTermHistory.cachedComposite;
			}

			status = PmReconstitute(&pm->ShortTermHistory, cimg2);
			if (!frozen2) PmFreeComposite(cimg2);
			if (status != FSUCCESS) {
				IB_LOG_WARN_FMT(__func__, "Unable to reconstitute composite image: %s", FSTATUS_ToString(status));
				goto unlock;
			}

			pmImagePreviousP = pm->ShortTermHistory.LoadedImage.img;
			imageIndexPrevious = 0;
		} else {
			pmImagePreviousP = &pm->Image[imageIndexPrevious];
			(void)vs_rdlock(&pmImagePreviousP->imageLock);
		}
		pmPortPreviousP = pm_find_port(pmImagePreviousP, lid, portNum);
	}

	if (delta && !pmPortPreviousP) {
		IB_LOG_WARN_FMT(__func__, "Port not found in previous image: Lid 0x%x Port %u", lid, portNum);
		status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_PORT;
		goto unlock2;
	} 

	if (delta) {
		pmPortImagePreviousP = &pmPortPreviousP->Image[imageIndexPrevious];
		if (pmPortImagePreviousP->u.s.queryStatus != PM_QUERY_STATUS_OK) {
			IB_LOG_WARN_FMT(__func__, "Port Query Status Invalid: %s: Lid 0x%x Port %u",
				(pmPortImageP->u.s.queryStatus == PM_QUERY_STATUS_SKIP ? "Skipped" :
				(pmPortImageP->u.s.queryStatus == PM_QUERY_STATUS_FAIL_QUERY ? "Failed Query" : "Failed Clear")),
				lid, portNum);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_IMAGE;
			goto unlock2;
		}
#define GET_DELTA_PORTCOUNTERS(cntr) \
		portCountersP->cntr = pmPortImageP->StlPortCounters.cntr - \
		(pmPortImagePreviousP->clearSelectMask.s.cntr ? 0 : pmPortImagePreviousP->StlPortCounters.cntr) \

		GET_DELTA_PORTCOUNTERS(PortXmitData);
		GET_DELTA_PORTCOUNTERS(PortRcvData);
		GET_DELTA_PORTCOUNTERS(PortXmitPkts);
		GET_DELTA_PORTCOUNTERS(PortRcvPkts);
		GET_DELTA_PORTCOUNTERS(PortMulticastXmitPkts);
		GET_DELTA_PORTCOUNTERS(PortMulticastRcvPkts);
		GET_DELTA_PORTCOUNTERS(PortXmitWait);
		GET_DELTA_PORTCOUNTERS(SwPortCongestion);
		GET_DELTA_PORTCOUNTERS(PortRcvFECN);
		GET_DELTA_PORTCOUNTERS(PortRcvBECN);
		GET_DELTA_PORTCOUNTERS(PortXmitTimeCong);
		GET_DELTA_PORTCOUNTERS(PortXmitWastedBW);
		GET_DELTA_PORTCOUNTERS(PortXmitWaitData);
		GET_DELTA_PORTCOUNTERS(PortRcvBubble);
		GET_DELTA_PORTCOUNTERS(PortMarkFECN);
		GET_DELTA_PORTCOUNTERS(PortRcvConstraintErrors);
		GET_DELTA_PORTCOUNTERS(PortRcvSwitchRelayErrors);
		GET_DELTA_PORTCOUNTERS(PortXmitDiscards);
		GET_DELTA_PORTCOUNTERS(PortXmitConstraintErrors);
		GET_DELTA_PORTCOUNTERS(PortRcvRemotePhysicalErrors);
		GET_DELTA_PORTCOUNTERS(LocalLinkIntegrityErrors);
		GET_DELTA_PORTCOUNTERS(PortRcvErrors);
		GET_DELTA_PORTCOUNTERS(ExcessiveBufferOverruns);
		GET_DELTA_PORTCOUNTERS(FMConfigErrors);
		GET_DELTA_PORTCOUNTERS(LinkErrorRecovery);
		GET_DELTA_PORTCOUNTERS(LinkDowned);
		GET_DELTA_PORTCOUNTERS(UncorrectableErrors);
#undef GET_DELTA_PORTCOUNTERS
		portCountersP->lq.s.NumLanesDown = pmPortImageP->StlPortCounters.lq.s.NumLanesDown;
		portCountersP->lq.s.LinkQualityIndicator = pmPortImageP->StlPortCounters.lq.s.LinkQualityIndicator;
		*flagsp = STL_PA_PC_FLAG_DELTA|(pmPortImageP->u.s.UnexpectedClear?STL_PA_PC_FLAG_UNEXPECTED_CLEAR:0);
	} else {
		*flagsp = (pmPortImageP->u.s.UnexpectedClear?STL_PA_PC_FLAG_UNEXPECTED_CLEAR:0);
		memcpy(portCountersP, &pmPortImageP->StlPortCounters, sizeof(PmCompositePortCounters_t));
	}

	(void)vs_rwunlock(&pm->stateLock);
	*returnImageId = retImageId;

unlock2:
	if (delta && (!sth2) && imageIndexPrevious != PM_IMAGE_INDEX_INVALID) {
		(void)vs_rwunlock(&pmImagePreviousP->imageLock);
	}
unlock:
	if ((!sth) && imageIndex != PM_IMAGE_INDEX_INVALID) {
		(void)vs_rwunlock(&pmImageP->imageLock);
	}
	if (status != FSUCCESS){
		goto error;
	}
done:
	AtomicDecrementVoid(&pm->refCount);
	return(status);
error:
	(void)vs_rwunlock(&pm->stateLock);
	*returnImageId = BAD_IMAGE_ID;
	goto done;
}

/*************************************************************************************
*
* paClearPortStats - Clear port statistics for a port
*			 Clear Running totals for a port.  This simulates a PMA clear so
*			 that tools like opareport can work against the Running totals
*			 until we have a history feature.
*
*  Inputs:
*     pm - pointer to Pm_t (the PM main data type)
*     lid, portNum - lid and portNum to select port to clear
*     select - selects counters to clear
*
*  Return:
*     FSTATUS - FSUCCESS if OK
*
*
*************************************************************************************/

FSTATUS paClearPortStats(Pm_t *pm, STL_LID_32 lid, uint8 portNum, CounterSelectMask_t select)
{
	FSTATUS				status = FSUCCESS;
	PmImage_t			*pmimagep;
	uint32				imageIndex;

	// check input parameters
	if (!pm)
		return(FINVALID_PARAMETER);
	if (!lid) {
		IB_LOG_WARN_FMT(__func__,  "Illegal LID parameter: must not be zero");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}
	if (!select.AsReg32) {
		IB_LOG_WARN_FMT(__func__, "Illegal select parameter: Must not be zero\n");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}

	(void)vs_rdlock(&pm->stateLock);
	// Live Data only, no offset
	imageIndex = pm->LastSweepIndex;
	if (pm->LastSweepIndex == PM_IMAGE_INDEX_INVALID) {
		IB_LOG_WARN_FMT(__func__, "Unable to Clear PortStats: PM has not completed a sweep.");
		(void)vs_rwunlock(&pm->stateLock);
		status = FUNAVAILABLE | STL_MAD_STATUS_STL_PA_UNAVAILABLE;
		goto done;
	}
	pmimagep = &pm->Image[imageIndex];
	(void)vs_rdlock(&pmimagep->imageLock);
	(void)vs_rwunlock(&pm->stateLock);

	(void)vs_wrlock(&pm->totalsLock);
	if (portNum == PM_ALL_PORT_SELECT) {
		PmNode_t *pmnodep = pm_find_node(pmimagep, lid);
		if (! pmnodep) {
			IB_LOG_WARN_FMT(__func__, "Switch not found: LID: 0x%x", lid);
			status = FNOT_FOUND;
		} else if (pmnodep->nodeType != STL_NODE_SW) {
			IB_LOG_WARN_FMT(__func__, "Illegal port parameter: All Port Select (0xFF) can only be used on switches: LID: 0x%x", lid);
			status = FNOT_FOUND;
		} else {
			status = PmClearNodeRunningCounters(pmnodep, select);
			if (IB_EXPECT_FALSE(status != FSUCCESS)) {
				IB_LOG_WARN_FMT(__func__, "Unable to Clear Counters on LID: 0x%x", lid);
			}
		}
	} else {
		PmPort_t	*pmportp = pm_find_port(pmimagep, lid, portNum);
		if (! pmportp) {
			IB_LOG_WARN_FMT(__func__, "Port not found LID 0x%x Port %u", lid, portNum);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_PORT;
		} else {
			status = PmClearPortRunningCounters(pmportp, select);
			if (IB_EXPECT_FALSE(status != FSUCCESS)) {
				IB_LOG_WARN_FMT(__func__, "Unable to Clear Counters on LID: 0x%x  Port: %u", lid, portNum);
			}
		}
	}
	(void)vs_rwunlock(&pm->totalsLock);

	(void)vs_rwunlock(&pmimagep->imageLock);
done:
	AtomicDecrementVoid(&pm->refCount);
	return(status);
}

/*************************************************************************************
*
* paClearAllPortStats - Clear port statistics for a port
*			 Clear Running totals for all Ports.  This simulates a PMA clear so
*			 that tools like opareport can work against the Running totals
*			 until we have a history feature.
*
*  Inputs:
*     pm - pointer to Pm_t (the PM main data type)
*     lid, portNum - lid and portNum to select port to clear
*     select - selects counters to clear
*
*  Return:
*     FSTATUS - FSUCCESS if OK
*
*
*************************************************************************************/

FSTATUS paClearAllPortStats(Pm_t *pm, CounterSelectMask_t select)
{
	STL_LID_32 lid;
	FSTATUS status = FSUCCESS;
	PmImage_t			*pmimagep;
	uint32				imageIndex;

	// check input parameters
	if (!pm)
		return(FINVALID_PARAMETER);
	if (!select.AsReg32) {
		IB_LOG_WARN_FMT(__func__, "Illegal select parameter: Must not be zero\n");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}
	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}

	(void)vs_rdlock(&pm->stateLock);
	// Live Data only, no offset
	imageIndex = pm->LastSweepIndex;

	if (pm->LastSweepIndex == PM_IMAGE_INDEX_INVALID) {
		IB_LOG_WARN_FMT(__func__, "Unable to Clear PortStats: PM has not completed a sweep.");
		status = FUNAVAILABLE | STL_MAD_STATUS_STL_PA_UNAVAILABLE;
		(void)vs_rwunlock(&pm->stateLock);
		goto done;
	}
	pmimagep = &pm->Image[imageIndex];
	(void)vs_rdlock(&pmimagep->imageLock);
	(void)vs_rwunlock(&pm->stateLock);
	(void)vs_wrlock(&pm->totalsLock);
	for (lid=1; lid<= pmimagep->maxLid; ++lid) {
		PmNode_t *pmnodep = pmimagep->LidMap[lid];
		if (! pmnodep)
			continue;
		if (pmnodep->nodeType == STL_NODE_SW) {
			uint8 i;
			for (i=0; i<= pmnodep->numPorts; ++i) {
				PmPort_t *pmportp = pmnodep->up.swPorts[i];
				if (! pmportp)
					continue;
				status = PmClearPortRunningCounters(pmportp, select);
				if (status != FSUCCESS)
					IB_LOG_WARN_FMT(__func__,"Failed to Clear Counters on LID: 0x%x  Port: %u", lid, i);
			}
		} else {
			status = PmClearPortRunningCounters(pmnodep->up.caPortp, select);
			if (status != FSUCCESS)
				IB_LOG_WARN_FMT(__func__,"Failed to Clear Counters on LID: 0x%x", lid);
		}
	}
	(void)vs_rwunlock(&pm->totalsLock);
	(void)vs_rwunlock(&pmimagep->imageLock);
done:
	AtomicDecrementVoid(&pm->refCount);
	return status;
}

/*************************************************************************************
*
* paFreezeFrameRenew - access FF to renew lease
*
*  Inputs:
*     pm - pointer to Pm_t (the PM main data type)
*     imageId - 64 bit opaque imageId
*
*  Return:
*     FSTATUS - FSUCCESS if OK
*
*************************************************************************************/

FSTATUS paFreezeFrameRenew(Pm_t *pm, uint64 imageId)
{
	FSTATUS				status = FSUCCESS;
	uint32				imageIndex;
	const char 			*msg;
	uint64				retImageId;
	PmHistoryRecord_t	*record = NULL;
	boolean				frozen = 0;
	boolean				sth = 0;

	// check input parameters
	if (!pm)
		return(FINVALID_PARAMETER);

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}

	(void)vs_rdlock(&pm->stateLock);
	// just touching it will renew the image
	status = FindImage(pm, IMAGEID_TYPE_FREEZE_FRAME, imageId, 0, &imageIndex, &retImageId, &record, &msg, NULL, &frozen, &sth);
	if (FSUCCESS != status) {
		IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
	}
	if (sth && !frozen) // not in the cache, never found
			IB_LOG_WARN_FMT(__func__, "Unable to access cached composite image: %s", msg);

	(void)vs_rwunlock(&pm->stateLock);
done:
	AtomicDecrementVoid(&pm->refCount);
	return(status);
}

/*************************************************************************************
*
* paFreezeFrameRelease - release FreezeFrame
*
*  Inputs:
*     pm - pointer to Pm_t (the PM main data type)
*     imageId - 64 bit opaque imageId
*
*  Return:
*     FSTATUS - FSUCCESS if OK
*
*************************************************************************************/

FSTATUS paFreezeFrameRelease(Pm_t *pm, uint64 imageId)
{
	FSTATUS				status = FSUCCESS;
	uint32				imageIndex;
	const char 			*msg;
	uint64				retImageId;
	uint8				clientId;
	PmHistoryRecord_t	*record = NULL;
	boolean				frozen = 0;
	boolean				sth = 0;

	// check input parameters
	if (!pm)
		return(FINVALID_PARAMETER);

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}

	(void)vs_wrlock(&pm->stateLock);
	status = FindImage(pm, IMAGEID_TYPE_FREEZE_FRAME, imageId, 0, &imageIndex, &retImageId, &record, &msg, &clientId, &frozen, &sth);
	if (FSUCCESS != status) {
		IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
		goto unlock;
	}

	if (sth) {
		if (!frozen)  // not in the cache, never found
			IB_LOG_WARN_FMT(__func__, "Unable to access cached composite image: %s", msg);
		else {
			PmFreeComposite(pm->ShortTermHistory.cachedComposite);
			pm->ShortTermHistory.cachedComposite = NULL;
			status = FSUCCESS;
		}
		goto unlock;
	}

	pm->Image[imageIndex].ffRefCount &= ~((uint64)1<<(uint64)clientId);	// release image

unlock:
	(void)vs_rwunlock(&pm->stateLock);
done:
	AtomicDecrementVoid(&pm->refCount);
	return(status);
}

// Find a Freeze Frame Slot
// returns a valid index, on error returns pm_config.freeze_frame_images
static uint32 allocFreezeFrame(Pm_t *pm, uint32 imageIndex)
{
	uint8				freezeIndex;
	if (pm->Image[imageIndex].ffRefCount) {
		// there are other references
		// see if we can find a freezeFrame which already points to this image
		for (freezeIndex = 0; freezeIndex < pm_config.freeze_frame_images; freezeIndex ++) {
			if (pm->freezeFrames[freezeIndex] == imageIndex)
				return freezeIndex;	// points to index we want
		}
	}
	// need to find an empty Freeze Frame Slot
	for (freezeIndex = 0; freezeIndex < pm_config.freeze_frame_images; freezeIndex ++) {
		if (pm->freezeFrames[freezeIndex] == PM_IMAGE_INDEX_INVALID
			|| pm->Image[pm->freezeFrames[freezeIndex]].ffRefCount == 0) {
			// empty or stale FF image
			pm->freezeFrames[freezeIndex] = PM_IMAGE_INDEX_INVALID;
			return freezeIndex;
		}
	}
	return freezeIndex;	// >= pm_config.freeze_frame_images means failure
}

// get clientId which should be used to create a new freezeFrame for imageIndex
// a return >=64 indicates none available
static uint8 getNextClientId(Pm_t *pm, uint32 imageIndex)
{
	uint8				i;
	uint8				clientId;
	// to avoid a unfreeze/freeze against same imageIndex getting same clientId
	// we have a rolling count (eg. starting point) per Image
	for (i = 0; i < 64; i++) {
		clientId = (pm->Image[imageIndex].nextClientId+i)&63;
		if (0 == (pm->Image[imageIndex].ffRefCount & ((uint64)1 << (uint64)clientId))) {
			pm->Image[imageIndex].ffRefCount |= ((uint64)1 << (uint64)clientId);
			return clientId;
		}
	}
	return 255;	// none available
}

/*************************************************************************************
*
* paFreezeFrameCreate - create FreezeFrame
*
*  Inputs:
*     pm - pointer to Pm_t (the PM main data type)
*     imageId - 64 bit opaque imageId
*
*  Return:
*     FSTATUS - FSUCCESS if OK
*
*************************************************************************************/

FSTATUS paFreezeFrameCreate(Pm_t *pm, uint64 imageId, int32 offset, uint64 *retImageId)
{
	FSTATUS				status = FSUCCESS;
	uint32				imageIndex;
	const char 			*msg;
	uint8				clientId;
	uint8				freezeIndex;
	PmHistoryRecord_t	*record = NULL;
	boolean				frozen = 0;
	boolean				sth = 0;

	// check input parameters
	if (!pm)
		return(FINVALID_PARAMETER);

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}
	(void)vs_wrlock(&pm->stateLock);
	status = FindImage(pm, IMAGEID_TYPE_ANY, imageId, offset, &imageIndex, retImageId, &record, &msg, NULL, &frozen, &sth);
	if (FSUCCESS != status) {
		IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
		goto error;
	}
	if (sth && (frozen || (record && !frozen))) {
		*retImageId = imageId;
		if (!frozen) { // if it is already frozen, no need to freeze it again!
			status = PmFreezeComposite(pm, record);
			if (status != FSUCCESS) {
				IB_LOG_WARN_FMT(__func__, "Unable to freeze composite image: %s", FSTATUS_ToString(status));
				goto error;
			}
			*retImageId = record->header.imageIDs[0];
		} else {
			*retImageId = pm->ShortTermHistory.cachedComposite->header.common.imageIDs[0];
		}
		(void)vs_rwunlock(&pm->stateLock);
		goto done;
	}
	
	// Find a Freeze Frame Slot
	freezeIndex = allocFreezeFrame(pm, imageIndex);
	if (freezeIndex >= pm_config.freeze_frame_images) {
		IB_LOG_WARN0( "Out of Freeze Frame Images");
		status = FINSUFFICIENT_MEMORY | STL_MAD_STATUS_STL_PA_NO_IMAGE;
		goto error;
	}
	clientId = getNextClientId(pm, imageIndex);
	if (clientId >= 64) {
		IB_LOG_WARN0( "Too many freezes of 1 image");
		status = FINSUFFICIENT_MEMORY | STL_MAD_STATUS_STL_PA_NO_IMAGE;
		goto error;
	}
	pm->Image[imageIndex].nextClientId = (clientId+1) & 63;
	pm->freezeFrames[freezeIndex] = imageIndex;
	*retImageId = BuildFreezeFrameImageId(pm, freezeIndex, clientId);
	(void)vs_rwunlock(&pm->stateLock);

done:
	AtomicDecrementVoid(&pm->refCount);
	return(status);
error:
	(void)vs_rwunlock(&pm->stateLock);
	*retImageId = BAD_IMAGE_ID;
	goto done;
}

/*************************************************************************************
*
* paFreezeFrameMove - atomically release and create a  FreezeFrame
*
*  Inputs:
*     pm - pointer to Pm_t (the PM main data type)
*     ffImageId - 64 bit opaque imageId for FF to release
*     ImageId - 64 bit opaque imageId for image to FreezeFrame
*     offset - offset relative to ImageId for image to FreezeFrame
*
*  Return:
*     FSTATUS - FSUCCESS if OK
*     on error the ffImageId is not released
*
*************************************************************************************/

FSTATUS paFreezeFrameMove(Pm_t *pm, uint64 ffImageId, uint64 imageId, int32 offset, uint64 *returnImageId)
{
	FSTATUS				status = FSUCCESS;
	uint32				ffImageIndex;
	uint8				ffClientId;
	uint32				imageIndex;
	uint8				clientId;
	const char 			*msg;
	uint8				freezeIndex;
	boolean				oldIsComp = 0, newIsComp = 0;
	PmHistoryRecord_t	*record = NULL;
	boolean				sth = 0;
	boolean				frozen = 0;

	// check input parameters
	if (!pm)
		return(FINVALID_PARAMETER);

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}

	(void)vs_wrlock(&pm->stateLock);
	status = FindImage(pm, IMAGEID_TYPE_ANY, ffImageId, 0, &ffImageIndex, returnImageId, &record, &msg, &ffClientId, &oldIsComp, &sth);
	if (FSUCCESS != status) {
		IB_LOG_WARN_FMT(__func__, "Unable to get freeze frame index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
		goto error;
	}
	status = FindImage(pm, IMAGEID_TYPE_ANY, imageId, offset, &imageIndex, returnImageId, &record, &msg, NULL, &frozen, &sth);
	if (FSUCCESS != status) {
		IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
		goto error;
	}
	if (sth && (frozen || (record && !frozen))) {
		newIsComp = 1;
		if (!frozen) { //composite isn't already cached
			status = PmFreezeComposite(pm, record);
			if (status != FSUCCESS) {
				IB_LOG_WARN_FMT(__func__, "Unable to freeze composite image: %s", FSTATUS_ToString(status));
				goto error;
			}
		}
	}

	if (!newIsComp) {
		if (!oldIsComp && pm->Image[imageIndex].ffRefCount == ((uint64)1 << (uint64)ffClientId)) {
			// we are last/only client using this image
			// so we can simply use the same Freeze Frame Slot as old freeze
			ImageId_t id;
			id.AsReg64 = ffImageId;
			freezeIndex = id.s.index;
		} else {
			// Find a empty Freeze Frame Slot
			freezeIndex = allocFreezeFrame(pm, imageIndex);
		}
		clientId = getNextClientId(pm, imageIndex);
		if (clientId >= 64) {
			IB_LOG_WARN0( "Too many freezes of 1 image");
			status = FINSUFFICIENT_MEMORY | STL_MAD_STATUS_STL_PA_NO_IMAGE;
			goto error;
		}
		// freeze the selected image
		pm->Image[imageIndex].nextClientId = (clientId+1) & 63;
		pm->freezeFrames[freezeIndex] = imageIndex;

		*returnImageId = BuildFreezeFrameImageId(pm, freezeIndex, clientId);
	} else *returnImageId = pm->ShortTermHistory.cachedComposite->header.common.imageIDs[0];
	if (!oldIsComp) pm->Image[ffImageIndex].ffRefCount &= ~((uint64)1 << (uint64)ffClientId); // release old image
	(void)vs_rwunlock(&pm->stateLock);
done:
	AtomicDecrementVoid(&pm->refCount);
	return(status);
error:
	(void)vs_rwunlock(&pm->stateLock);
	*returnImageId = BAD_IMAGE_ID;
	goto done;

}

typedef uint32 (*ComputeFunc_t)(Pm_t *pm, PmPortImage_t *portImage);
typedef uint32 (*CompareFunc_t)(uint64 value1, uint64 value2);

// the following value compute functions return the value in tenths of a percent (thousandths)

// % of wire potential being used
uint32 computeUtilizationValue(Pm_t *pm, PmPortImage_t *portImage)
{
	uint32 rate = PmCalculateRate(portImage->u.s.activeSpeed, portImage->u.s.rxActiveWidth);
	uint32 pct10 = ((uint64)portImage->SendMBps * 1000) / s_StaticRateToMBps[rate];
	// This can be a 1-2% off if the interval and/or sweep time wanders
	// slightly between sweeps.  So limit to 100% to avoid user confusion.
	return pct10<1000?pct10:1000;
}

// packet rate - return actual value
uint32 computePktRate(Pm_t *pm, PmPortImage_t *portImage)
{
	return((uint64)portImage->SendKPps);
}

// return actual value
uint32 computeIntegrityValue(Pm_t *pm, PmPortImage_t *portImage)
{
	return(portImage->Errors.Integrity);
}

// return actual value
uint32 computeCongestionValue(Pm_t *pm, PmPortImage_t *portImage)
{
	return(portImage->Errors.Congestion);
}

// return actual value
uint32 computeSmaCongestionValue(Pm_t *pm, PmPortImage_t *portImage)
{
	return(portImage->Errors.SmaCongestion);
}

// return actual value
uint32 computeBubbleValue(Pm_t *pm, PmPortImage_t *portImage)
{
	return (portImage->Errors.Bubble);
}

// return actual value
uint32 computeSecurityValue(Pm_t *pm, PmPortImage_t *portImage)
{
	return(portImage->Errors.Security);
}

// return actual value
uint32 computeRoutingValue(Pm_t *pm, PmPortImage_t *portImage)
{
	return(portImage->Errors.Routing);
}


uint32 compareGE(uint64 value1, uint64 value2)
{
	return(value1 >= value2);
}

uint32 compareLE(uint64 value1, uint64 value2)
{
	return(value1 <= value2);
}

int neighborInList(STL_LID_32 lid, uint8 portNum, PmPort_t *pmNeighborportp, PmPortImage_t *neighborPortImage, sortInfo_t *sortInfo)
{
	sortedValueEntry_t	*listp;
	STL_LID_32			neighborLid;
	uint8				neighborPort;
	uint32				imageIndex;

    if (pmNeighborportp == NULL || neighborPortImage == NULL)
        return -1;

	imageIndex = ((void *)neighborPortImage - (void *)&pmNeighborportp->Image[0]) / sizeof(PmPortImage_t);
	neighborPort = pmNeighborportp->portNum;
	neighborLid = pmNeighborportp->pmnodep->Image[imageIndex].lid;

	for (listp = sortInfo->sortedValueListHead; listp != NULL; listp = listp->next) {
		if ((lid == listp->neighborPortp->pmnodep->Image[imageIndex].lid) &&
			(portNum == listp->neighborPortp->portNum)) {
			IB_LOG_DEBUG1_FMT(__func__, "Lid 0x%x portNum %d neighborLid 0x%x neighborPort %d already in list\n",
				lid, portNum, neighborLid, neighborPort);
			return(1);
		}
	}

	return(0);
}

FSTATUS processFocusPort(Pm_t *pm, PmPort_t *pmportp, PmPortImage_t *portImage, PmPort_t *pmNeighborportp,
						 PmPortImage_t *neighborPortImage, STL_LID_32 lid, uint8 portNum, ComputeFunc_t computeFunc,
						 CompareFunc_t compareFunc, CompareFunc_t candidateFunc, sortInfo_t *sortInfo)
{
	uint64				computedValue = 0;
	uint64				nbrComputedValue = 0;
	uint64				sortValue;
	PmPort_t			*nbrPt;
	PmPortImage_t		*nbrPI;
	sortedValueEntry_t	*newListEntry = NULL;
	sortedValueEntry_t	*thisListEntry = NULL;
	sortedValueEntry_t	*prevListEntry = NULL;

	if (portNum != 0) {
        int rc;

        if ((rc = neighborInList(lid, portNum, pmNeighborportp, neighborPortImage, sortInfo)) == -1)
    		return(FNOT_DONE);
        else if (rc)
    		return(FSUCCESS);
    }
		
	if (portNum == 0) {
		nbrPt = pmportp;
		nbrPI = portImage;
	} else {
		nbrPt = pmNeighborportp;
		nbrPI = neighborPortImage;
	}
    if (portImage)
        computedValue = computeFunc(pm, portImage);
    if (nbrPI)
    	nbrComputedValue = computeFunc(pm, nbrPI);
	sortValue = MAX(computedValue, nbrComputedValue);

	if (sortInfo->sortedValueListHead == NULL) {
		// list is new - add entry and make it head and tail
		sortInfo->sortedValueListHead = sortInfo->sortedValueListPool;
		sortInfo->sortedValueListTail = sortInfo->sortedValueListPool;
		sortInfo->sortedValueListHead->value = computedValue;
		sortInfo->sortedValueListHead->neighborValue = nbrComputedValue;
		sortInfo->sortedValueListHead->sortValue = sortValue;
		sortInfo->sortedValueListHead->portp = pmportp;
		sortInfo->sortedValueListHead->neighborPortp = nbrPt;
		sortInfo->sortedValueListHead->lid = lid;
		sortInfo->sortedValueListHead->portNum = portNum;
		sortInfo->sortedValueListHead->next = NULL;
		sortInfo->sortedValueListHead->prev = NULL;
		sortInfo->numValueEntries++;
	} else if (sortInfo->numValueEntries < sortInfo->sortedValueListSize) {
		// list is not yet full - use available pool entry and insert sorted
		newListEntry = &sortInfo->sortedValueListPool[sortInfo->numValueEntries++];
		newListEntry->value = computedValue;
		newListEntry->neighborValue = nbrComputedValue;
		newListEntry->sortValue = sortValue;
		newListEntry->portp = pmportp;
		newListEntry->neighborPortp = nbrPt;
		newListEntry->lid = lid;
		newListEntry->portNum = portNum;
		newListEntry->next = NULL;
		thisListEntry = sortInfo->sortedValueListHead;
		while ((thisListEntry != NULL) && compareFunc(sortValue, thisListEntry->sortValue)) {
			prevListEntry = thisListEntry;
			thisListEntry = thisListEntry->next;
		}
		if (prevListEntry == NULL) {
			// put new entry at head
			newListEntry->next = sortInfo->sortedValueListHead;
			sortInfo->sortedValueListHead->prev = newListEntry;
			newListEntry->prev = NULL;
			sortInfo->sortedValueListHead = newListEntry;
		} else {
			// put between prev and its next (even if its next is NULL)
			newListEntry->next = prevListEntry->next;
			newListEntry->prev = prevListEntry;
			if (prevListEntry->next)
				prevListEntry->next->prev = newListEntry;
			prevListEntry->next = newListEntry;
			if (newListEntry->next == NULL)
				sortInfo->sortedValueListTail = newListEntry;
		}
	} else {
		// see if this is a candidate for the list
		if (candidateFunc(sortValue, sortInfo->sortedValueListTail->sortValue)) {
			// if list size is 1, simple replace values
			if (sortInfo->sortedValueListSize == 1) {
				sortInfo->sortedValueListHead->value = computedValue;
				sortInfo->sortedValueListHead->neighborValue = nbrComputedValue;
				sortInfo->sortedValueListHead->sortValue = sortValue;
				sortInfo->sortedValueListHead->portp = pmportp;
				sortInfo->sortedValueListHead->neighborPortp = nbrPt;
				sortInfo->sortedValueListHead->lid = lid;
				sortInfo->sortedValueListHead->portNum = portNum;
			} else {
				// list is full - bump tail and insert sorted
				// first, copy into tail entry and adjust tail
				newListEntry = sortInfo->sortedValueListTail;
				newListEntry->prev->next = NULL;
				newListEntry->value = computedValue;
				newListEntry->neighborValue = nbrComputedValue;
				newListEntry->sortValue = sortValue;
				newListEntry->portp = pmportp;
				newListEntry->neighborPortp = nbrPt;
				newListEntry->lid = lid;
				newListEntry->portNum = portNum;
				sortInfo->sortedValueListTail = sortInfo->sortedValueListTail->prev;
				// now insert sorted entry
				thisListEntry = sortInfo->sortedValueListHead;
				while ((thisListEntry != NULL) && compareFunc(sortValue, thisListEntry->sortValue)) {
					prevListEntry = thisListEntry;
					thisListEntry = thisListEntry->next;
				}
				if (prevListEntry == NULL) {
					// put new entry at head
					newListEntry->next = sortInfo->sortedValueListHead;
					sortInfo->sortedValueListHead->prev = newListEntry;
					newListEntry->prev = NULL;
					sortInfo->sortedValueListHead = newListEntry;
				} else {
					// put between prev and its next (even if its next is NULL)
					newListEntry->next = prevListEntry->next;
					newListEntry->prev = prevListEntry;
					if (prevListEntry->next)
						prevListEntry->next->prev = newListEntry;
					prevListEntry->next = newListEntry;
					if (newListEntry->next == NULL)
						sortInfo->sortedValueListTail = newListEntry;
				}
			}
		}
	}

	return(FSUCCESS);
}

FSTATUS addSortedPorts(PmFocusPorts_t *pmFocusPorts, sortInfo_t *sortInfo, uint32 imageIndex)
{
	Status_t			status;
	sortedValueEntry_t	*listp;
	int					portCount = 0;

	if (sortInfo->sortedValueListHead == NULL) {
		pmFocusPorts->NumPorts = 0;
		return(FSUCCESS);
	}

	status = vs_pool_alloc(&pm_pool, pmFocusPorts->NumPorts * sizeof(PmFocusPortEntry_t),
						   (void*)&pmFocusPorts->portList);
	if (status != VSTATUS_OK) {
		IB_LOG_ERRORRC("Failed to allocate sorted port list buffer for pmFocusPorts rc:", status);
		return(FINSUFFICIENT_MEMORY);
	}
	listp = sortInfo->sortedValueListHead;
	while ((listp != NULL) && (portCount < pmFocusPorts->NumPorts)) {
		pmFocusPorts->portList[portCount].lid = listp->lid;
		pmFocusPorts->portList[portCount].portNum = listp->portNum;
		pmFocusPorts->portList[portCount].rate = PmCalculateRate(listp->portp->Image[imageIndex].u.s.activeSpeed, listp->portp->Image[imageIndex].u.s.rxActiveWidth);
		pmFocusPorts->portList[portCount].mtu = listp->portp->Image[imageIndex].u.s.mtu;
		pmFocusPorts->portList[portCount].value = listp->value;
		pmFocusPorts->portList[portCount].guid = (uint64_t)(listp->portp->pmnodep->guid);
		strncpy(pmFocusPorts->portList[portCount].nodeDesc, (char *)listp->portp->pmnodep->nodeDesc.NodeString,
			sizeof(pmFocusPorts->portList[portCount].nodeDesc)-1);
		if (listp->portNum != 0) {
			pmFocusPorts->portList[portCount].neighborLid = listp->neighborPortp->pmnodep->Image[imageIndex].lid;
			pmFocusPorts->portList[portCount].neighborPortNum = listp->neighborPortp->portNum;
			pmFocusPorts->portList[portCount].neighborValue = listp->neighborValue;
			pmFocusPorts->portList[portCount].neighborGuid = (uint64_t)(listp->neighborPortp->pmnodep->guid);
			strncpy(pmFocusPorts->portList[portCount].neighborNodeDesc, (char *)listp->neighborPortp->pmnodep->nodeDesc.NodeString,
				sizeof(pmFocusPorts->portList[portCount].neighborNodeDesc)-1);
		} else {
			pmFocusPorts->portList[portCount].neighborLid = 0;
			pmFocusPorts->portList[portCount].neighborPortNum = 0;
			pmFocusPorts->portList[portCount].neighborValue = 0;
			pmFocusPorts->portList[portCount].neighborGuid = 0;
			pmFocusPorts->portList[portCount].neighborNodeDesc[0] = 0;
		}
		portCount++;
		listp = listp->next;
	}

	return(FSUCCESS);
}

/*************************************************************************************
*
* paGetFocusPorts - return a set of focus ports
*
*  Inputs:
*     pm - pointer to Pm_t (the PM main data type)
*     groupName - pointer to name of group
*     pmGroupConfig - pointer to caller-declared data area to return group config info
*
*  Return:
*     FSTATUS - FSUCCESS if OK
*
*
*************************************************************************************/

FSTATUS paGetFocusPorts(Pm_t *pm, char *groupName, PmFocusPorts_t *pmFocusPorts, uint64 imageId, int32 offset, uint64 *returnImageId,
					    uint32 select, uint32 start, uint32 range)
{
	uint64				retImageId = 0;
	PmGroup_t			*pmGroupP = NULL;
	STL_LID_32			lid;
	uint32				imageIndex;
	const char 			*msg;
	PmImage_t			*pmimagep;
	FSTATUS				status = FSUCCESS;
	Status_t			allocStatus;
	sortInfo_t			sortInfo;
	ComputeFunc_t		computeFunc = NULL;
	CompareFunc_t		compareFunc = NULL;
	CompareFunc_t		candidateFunc = NULL;
	boolean				sth = 0;
	PmHistoryRecord_t	*record = NULL;
	boolean				frozen = 0;

	// check input parameters
	if (!pm || !groupName || !pmFocusPorts)
		return(FINVALID_PARAMETER);
	if (groupName[0] == '\0') {
		IB_LOG_WARN_FMT(__func__, "Illegal groupName parameter: Empty String\n");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER) ;
	}
	if (start) {
		IB_LOG_WARN_FMT(__func__, "Illegal start parameter: %d: must be zero\n", start);
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}
	if (!range) {
		IB_LOG_WARN_FMT(__func__, "Illegal range parameter: %d: must be greater than zero\n", range);
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}
	switch (select) {
	case STL_PA_SELECT_UTIL_HIGH:
		computeFunc = &computeUtilizationValue;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	case STL_PA_SELECT_UTIL_PKTS_HIGH:
		computeFunc = &computePktRate;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	case STL_PA_SELECT_UTIL_LOW:
		computeFunc = &computeUtilizationValue;
		compareFunc = &compareGE;
		candidateFunc = &compareLE;
		break;
	case STL_PA_SELECT_ERR_INTEG:
		computeFunc = &computeIntegrityValue;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	case STL_PA_SELECT_ERR_CONG:
		computeFunc = &computeCongestionValue;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	case STL_PA_SELECT_ERR_SMA_CONG:
		computeFunc = &computeSmaCongestionValue;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	case STL_PA_SELECT_ERR_BUBBLE:
		computeFunc = &computeBubbleValue;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	case STL_PA_SELECT_ERR_SEC:
		computeFunc = &computeSecurityValue;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	case STL_PA_SELECT_ERR_ROUT:
		computeFunc = &computeRoutingValue;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	default:
		IB_LOG_WARN_FMT(__func__, "Illegal select parameter: 0x%x\n", select);
		return FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER;
		break;
	}

	// initialize group config port list counts
	pmFocusPorts->NumPorts = 0;
	pmFocusPorts->portList = NULL;
	memset(&sortInfo, 0, sizeof(sortInfo));
	allocStatus = vs_pool_alloc(&pm_pool, range * sizeof(sortedValueEntry_t), (void*)&sortInfo.sortedValueListPool);
	if (allocStatus != VSTATUS_OK) {
		IB_LOG_ERRORRC("Failed to allocate list buffer for pmFocusPorts rc:", allocStatus);
		return FINSUFFICIENT_MEMORY;
	}
	sortInfo.sortedValueListHead = NULL;
	sortInfo.sortedValueListTail = NULL;
	sortInfo.sortedValueListSize = range;
	sortInfo.numValueEntries = 0;

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}

	// pmGroupP points to our group
	// check all ports for membership in our group
	(void)vs_rdlock(&pm->stateLock);
	status = FindImage(pm, IMAGEID_TYPE_ANY, imageId, offset, &imageIndex, &retImageId, &record, &msg, NULL, &frozen, &sth);
	if (FSUCCESS != status) {
		IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
		goto error;
	}

	if (sth && (frozen || (record && !frozen))) {
		PmCompositeImage_t *cimg;
		if (!frozen) {
			status = PmLoadComposite(pm, record, &cimg);
			if (status != FSUCCESS || !cimg) {
				IB_LOG_WARN_FMT(__func__, "Unable to load composite image: %s", FSTATUS_ToString(status));
				goto error;
			}
		} else {
			cimg = pm->ShortTermHistory.cachedComposite;
		}
		retImageId = cimg->header.common.imageIDs[0];
		status = PmReconstitute(&pm->ShortTermHistory, cimg);
		if (!frozen) PmFreeComposite(cimg);
		if (status != FSUCCESS) {
			IB_LOG_WARN_FMT(__func__, "Unable to reconstitute composite image: %s", FSTATUS_ToString(status));
			goto error;
		}
		// find the group
		if (!strcmp(pm->ShortTermHistory.LoadedImage.AllGroup->Name, groupName)) {
			pmGroupP = pm->ShortTermHistory.LoadedImage.AllGroup;
		} else {
			int i;
			for (i = 0; i < PM_MAX_GROUPS; i++) {
				if (!strcmp(pm->ShortTermHistory.LoadedImage.Groups[i]->Name, groupName)) {
					pmGroupP = pm->ShortTermHistory.LoadedImage.Groups[i];
					break;
				}
			}
		}
		if (!pmGroupP) {
			IB_LOG_WARN_FMT(__func__, "Group %.*s not Found", sizeof(groupName), groupName);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_GROUP;
			goto error;
		}
		pmimagep = pm->ShortTermHistory.LoadedImage.img;
		imageIndex = 0;
	} else {
		status = LocateGroup(pm, groupName, &pmGroupP);
		if (status != FSUCCESS) {
			IB_LOG_WARN_FMT(__func__, "Group %.*s not Found: %s", sizeof(groupName), groupName, FSTATUS_ToString(status));
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_GROUP;
			goto error;
		}
		pmimagep = &pm->Image[imageIndex];
		(void)vs_rdlock(&pmimagep->imageLock);
	}
	(void)vs_rwunlock(&pm->stateLock);

	for (lid=1; lid<= pmimagep->maxLid; ++lid) {
		uint8 portnum;
		PmNode_t *pmnodep = pmimagep->LidMap[lid];
		if (! pmnodep)
			continue;
		if (pmnodep->nodeType == STL_NODE_SW) {
			for (portnum=0; portnum<=pmnodep->numPorts; ++portnum) {
				PmPort_t *pmportp = pmnodep->up.swPorts[portnum];
				PmPort_t *pmNeighborportp;
				PmPortImage_t *portImage;
				PmPortImage_t *neighborPortImage;
				if (! pmportp)
					continue;
				portImage = &pmportp->Image[imageIndex];
				pmNeighborportp = portImage->neighbor;
				neighborPortImage = &pmNeighborportp->Image[imageIndex];
				if (PmIsPortInGroup(pm, pmportp, portImage, pmGroupP, sth) &&
					portImage->u.s.queryStatus == PM_QUERY_STATUS_OK)
				{
					processFocusPort(pm, pmportp, portImage, pmNeighborportp, neighborPortImage, lid, portnum, computeFunc, compareFunc, candidateFunc, &sortInfo);
				}
			}
		} else {
			PmPort_t *pmportp = pmnodep->up.caPortp;
			PmPortImage_t *portImage = &pmportp->Image[imageIndex];
			PmPort_t *pmNeighborportp = portImage->neighbor;
			PmPortImage_t *neighborPortImage = &pmNeighborportp->Image[imageIndex];
			if (PmIsPortInGroup(pm, pmportp, portImage, pmGroupP, sth) &&
				portImage->u.s.queryStatus == PM_QUERY_STATUS_OK)
			{
				processFocusPort(pm, pmportp, portImage, pmNeighborportp, neighborPortImage, lid, pmportp->portNum, computeFunc, compareFunc, candidateFunc, &sortInfo);
			}
		}
	}

	strcpy(pmFocusPorts->groupName, pmGroupP->Name);
	pmFocusPorts->NumPorts = sortInfo.numValueEntries;
	if (pmFocusPorts->NumPorts)
		status = addSortedPorts(pmFocusPorts, &sortInfo, imageIndex);

	if (!sth) {
		(void)vs_rwunlock(&pmimagep->imageLock);
	}
	if (status != FSUCCESS)
		goto done;
	*returnImageId = retImageId;

done:
	if (sortInfo.sortedValueListPool != NULL)
		vs_pool_free(&pm_pool, sortInfo.sortedValueListPool);
#ifndef __VXWORKS__
	if (sth) {
		clearLoadedImage(&pm->ShortTermHistory);
	}
#endif
	AtomicDecrementVoid(&pm->refCount);
	return(status);
error:
	(void)vs_rwunlock(&pm->stateLock);
	*returnImageId = BAD_IMAGE_ID;
	goto done;
}

FSTATUS paGetImageInfo(Pm_t *pm, uint64 imageId, int32 offset, PmImageInfo_t *imageInfo, uint64 *returnImageId)
{
	FSTATUS				status = FSUCCESS;
	uint64				retImageId = 0;
	uint32				imageIndex;
	int					i;
	const char 			*msg;
	PmImage_t			*pmimagep;
	boolean 			sth = 0;
	PmHistoryRecord_t 	*record = NULL;
	boolean 			frozen = 0;

	// check input parameters
	if (!pm || !imageInfo)
		return FINVALID_PARAMETER;

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}

	(void)vs_rdlock(&pm->stateLock);
	status = FindImage(pm, IMAGEID_TYPE_ANY, imageId, offset, &imageIndex, &retImageId, &record, &msg, NULL, &frozen, &sth);
	if (FSUCCESS != status) {
		IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
		goto error;
	}

	if (sth && (frozen || (record && !frozen))) {
		PmCompositeImage_t *cimg;
		if (!frozen) {
			status = PmLoadComposite(pm, record, &cimg);
			if (status != FSUCCESS || !cimg) {
				IB_LOG_WARN_FMT(__func__, "Unable to load composite image: %s", FSTATUS_ToString(status));
				goto error;
			}
		} else {
			cimg = pm->ShortTermHistory.cachedComposite;
		}
		retImageId = cimg->header.common.imageIDs[0];
		status = PmReconstitute(&pm->ShortTermHistory, cimg);
		if (!frozen) PmFreeComposite(cimg);
		if (status != FSUCCESS) {
			IB_LOG_WARN_FMT(__func__, "Unable to reconstitute composite image: %s", FSTATUS_ToString(status));
			goto error;
		}
		pmimagep = pm->ShortTermHistory.LoadedImage.img;
	}  else {
		pmimagep = &pm->Image[imageIndex];
	}

	imageInfo->sweepStart				= pmimagep->sweepStart;
	imageInfo->sweepDuration			= pmimagep->sweepDuration;
	imageInfo->numHFIPorts	   			= pmimagep->HFIPorts;
	imageInfo->numSwitchNodes			= pmimagep->SwitchNodes;
	imageInfo->numSwitchPorts			= pmimagep->SwitchPorts;
	imageInfo->numLinks					= pmimagep->NumLinks;
	imageInfo->numSMs					= pmimagep->NumSMs;
	imageInfo->numFailedNodes			= pmimagep->FailedNodes;
	imageInfo->numFailedPorts			= pmimagep->FailedPorts;
	imageInfo->numSkippedNodes			= pmimagep->SkippedNodes;
	imageInfo->numSkippedPorts			= pmimagep->SkippedPorts;
	imageInfo->numUnexpectedClearPorts	= pmimagep->UnexpectedClearPorts;
	for (i = 0; i < 2; i++) {
		STL_LID_32 smLid = pmimagep->SMs[i].smLid;
		PmNode_t *pmnodep = pmimagep->LidMap[smLid];
		imageInfo->SMInfo[i].smLid		= smLid;
		imageInfo->SMInfo[i].priority	= pmimagep->SMs[i].priority;
		imageInfo->SMInfo[i].state		= pmimagep->SMs[i].state;
		if (pmnodep != NULL) {
			PmPort_t *pmportp = pm_node_lided_port(pmnodep);
			imageInfo->SMInfo[i].portNumber = pmportp->portNum;
			imageInfo->SMInfo[i].smPortGuid	= pmportp->guid;
			strncpy(imageInfo->SMInfo[i].smNodeDesc, (char *)pmnodep->nodeDesc.NodeString,
				sizeof(imageInfo->SMInfo[i].smNodeDesc)-1);
		} else {
			imageInfo->SMInfo[i].portNumber	= 0;
			imageInfo->SMInfo[i].smPortGuid	= 0;
			imageInfo->SMInfo[i].smNodeDesc[0] = 0;
		}
	}

	*returnImageId = retImageId;
	if (sth) {
		clearLoadedImage(&pm->ShortTermHistory);
	}

	(void)vs_rwunlock(&pm->stateLock);
done:
	AtomicDecrementVoid(&pm->refCount);
	return(status);
error:
	(void)vs_rwunlock(&pm->stateLock);
	*returnImageId = BAD_IMAGE_ID;
	goto done;
}

// this function is used by ESM CLI
static void pm_print_port_running_totals(FILE *out, Pm_t *pm, PmPort_t *pmportp,
				uint32 imageIndex)
{
	PmCompositePortCounters_t *pPortCounters;
	PmPortImage_t *portImage = &pmportp->Image[imageIndex];

	if (! portImage->u.s.active)
		return;
	fprintf(out, "%.*s Guid "FMT_U64" LID 0x%x Port %u\n",
				(int)sizeof(pmportp->pmnodep->nodeDesc.NodeString),
				pmportp->pmnodep->nodeDesc.NodeString, pmportp->pmnodep->guid,
				pmportp->pmnodep->Image[imageIndex].lid, pmportp->portNum);
	if (portImage->neighbor) {
		PmPort_t *neighbor = portImage->neighbor;
		fprintf(out, "    Neighbor: %.*s Guid "FMT_U64" LID 0x%x Port %u\n",
				(int)sizeof(neighbor->pmnodep->nodeDesc.NodeString),
				neighbor->pmnodep->nodeDesc.NodeString, neighbor->pmnodep->guid,
				neighbor->pmnodep->Image[imageIndex].lid,
				neighbor->portNum);
	}
	fprintf(out, "    txRate: %4s rxRate: %4s  MTU: %5s%s\n",
				StlStaticRateToText(PmCalculateRate(portImage->u.s.activeSpeed, portImage->u.s.txActiveWidth)),
				StlStaticRateToText(PmCalculateRate(portImage->u.s.activeSpeed, portImage->u.s.rxActiveWidth)),
				IbMTUToText(portImage->u.s.mtu),
				portImage->u.s.UnexpectedClear?"  Unexpected Clear":"");
	if (pmportp->u.s.PmaAvoid ) {
		fprintf(out, "    PMA Counters Not Available\n");
		return;
	}

	pPortCounters = &pmportp->StlPortCountersTotal;
	fprintf(out, "    Xmit: Data:%-10" PRIu64 " MB (%-10" PRIu64
				" Flits) Pkts:%-10" PRIu64 "\n",
				pPortCounters->PortXmitData / FLITS_PER_MB,
				pPortCounters->PortXmitData, pPortCounters->PortXmitPkts );
	fprintf(out, "    Recv: Data:%-10" PRIu64 " MB (%-10" PRIu64
				" Flits) Pkts:%-10" PRIu64 "\n",
				pPortCounters->PortRcvData / FLITS_PER_MB,
				pPortCounters->PortRcvData, pPortCounters->PortRcvPkts );
	fprintf(out, "    Integrity:                        SmaCongest:\n" );
	fprintf(out, "      Link Recovery:%-10u\n",
				pPortCounters->LinkErrorRecovery);
	fprintf(out, "      Link Downed:%-10u\n", pPortCounters->LinkDowned);
	fprintf(out, "      Rcv Errors:%-10" PRIu64 "           Security:\n",
				pPortCounters->PortRcvErrors );
	fprintf(out, "      Loc Lnk Integrity:%-10" PRIu64 "      Rcv Constrain:%-10" PRIu64 "\n",
				pPortCounters->LocalLinkIntegrityErrors,
				pPortCounters->PortRcvConstraintErrors );
	fprintf(out, "      Excess Bfr Overrun*:%-10" PRIu64 "    Xmt Constrain*:%-10" PRIu64 "\n",
                pPortCounters->ExcessiveBufferOverruns,
				pPortCounters->PortXmitConstraintErrors );
	fprintf(out, "    Congestion:                       Routing:\n" );
	fprintf(out, "      Xmt Discards*:%-10" PRIu64 "     Rcv Sw Relay:%-10" PRIu64 "\n",
				pPortCounters->PortXmitDiscards,
				pPortCounters->PortRcvSwitchRelayErrors );
#if 0
	fprintf(out, "      Xmt Congest*:%-10" PRIu64 "\n",
				pPortCounters->PortXmitCongestion );
	fprintf(out, "    Rcv Rmt Phy:%-10u       Adapt Route:%-10" PRIu64 "\n",
				pPortCounters->PortRcvRemotePhysicalErrors,
				pPortCounters->PortAdaptiveRouting );
#else
	fprintf(out, "    Rcv Rmt Phy:%-10" PRIu64 "                                      \n",
				pPortCounters->PortRcvRemotePhysicalErrors);
#endif
#if 0
	fprintf(out, "    Xmt Congest:%-10" PRIu64 "       Check Rate:0x%4x\n",
				pPortCounters->PortXmitCongestion, pPortCounters->PortCheckRate );
#endif
}

// this function is used by ESM CLI
void pm_print_running_totals_to_stream(FILE * out)
{
	FSTATUS				status;
	const char 			*msg;
	PmImage_t			*pmimagep;
	uint32				imageIndex;
	uint64				retImageId;
	STL_LID_32			lid;
	extern Pm_t g_pmSweepData;
	Pm_t *pm = &g_pmSweepData;

	if (topology_passcount < 1) {
		fprintf(out, "\nSM is currently in the %s state, count = %d\n\n", IbSMStateToText(sm_state), (int)sm_smInfo.ActCount);
		return;
	}

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {
		fprintf(out, "\nPM is currently not running\n\n");
		goto done;
	}

	(void)vs_rdlock(&pm->stateLock);
	status = GetIndexFromImageId(pm, IMAGEID_TYPE_ANY, IMAGEID_LIVE_DATA, 0,
					   				&imageIndex, &retImageId, &msg, NULL);
	if (FSUCCESS != status) {
		fprintf(out, "Unable to access Running Totals: %s\n", msg);
		(void)vs_rwunlock(&pm->stateLock);
		goto done;
	}
	pmimagep = &pm->Image[imageIndex];
	(void)vs_rdlock(&pmimagep->imageLock);
	(void)vs_rwunlock(&pm->stateLock);

	for (lid=1; lid<= pmimagep->maxLid; ++lid) {
		uint8 portnum;
		PmNode_t *pmnodep = pmimagep->LidMap[lid];
		if (! pmnodep)
			continue;
		if (pmnodep->nodeType == STL_NODE_SW) {
			for (portnum=0; portnum<=pmnodep->numPorts; ++portnum) {
				PmPort_t *pmportp = pmnodep->up.swPorts[portnum];
				if (! pmportp)
					continue;
				(void)vs_rdlock(&pm->totalsLock);	// limit in case print slow
				pm_print_port_running_totals(out, pm, pmportp, imageIndex);
				(void)vs_rwunlock(&pm->totalsLock);
			}
		} else {
			PmPort_t *pmportp = pmnodep->up.caPortp;
			(void)vs_rdlock(&pm->totalsLock);	// limit in case print slow
			pm_print_port_running_totals(out, pm, pmportp, imageIndex);
			(void)vs_rwunlock(&pm->totalsLock);
		}
	}
	(void)vs_rwunlock(&pmimagep->imageLock);
done:
	AtomicDecrementVoid(&pm->refCount);
}

// locate vf by name
static FSTATUS LocateVF(Pm_t *pm, const char *vfName, PmVF_t **pmVFP, uint8 activeOnly, uint32 imageIndex)
{
	int i;

	for (i = 0; i < pm->numVFs; i++) {
		if (strcmp(vfName, pm->VFs[i]->Name) == 0) {
			*pmVFP = pm->VFs[i];
			if(*pmVFP && activeOnly <= (pm->VFs[i]->Image[imageIndex].isActive)) {
									// True cases: activeOnly=0 & pm->VFs[i]->Image[imageIndex].isActive = 0 or 1	Locates VF in list of active and standby VFs
				return FSUCCESS;	//             activeOnly=1 & pm->VFs[i]->Image[imageIndex].isActive = 1		Locates VF in list of only active
			}
		}
	}
	return FNOT_FOUND;
}

FSTATUS paGetVFList(Pm_t *pm, PmVFList_t *VFList, uint32 imageIndex)
{
	Status_t			vStatus;
	FSTATUS				fStatus = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_VF;
	int					i;

	// check input parameters
	if (!pm || !VFList)
		return FINVALID_PARAMETER;

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		fStatus = FUNAVAILABLE;
		goto done;
	}
	VFList->NumVFs = pm->numVFsActive;
	vStatus = vs_pool_alloc(&pm_pool, VFList->NumVFs * STL_PM_VFNAMELEN,
						   (void*)&VFList->VfList);
	if (vStatus != VSTATUS_OK) {
		IB_LOG_ERRORRC("Failed to allocate name list buffer for VFList rc:", vStatus);
		fStatus = FINSUFFICIENT_MEMORY;
		goto done;
	}

	(void)vs_rdlock(&pm->stateLock);
	for (i = 0; i < pm->numVFs; i++) {
		if (pm->VFs[i]->Image[imageIndex].isActive) {
			strncpy(VFList->VfList[i].Name, pm->VFs[i]->Name, STL_PM_VFNAMELEN-1);
		}
	}
	if (VFList->NumVFs)
		fStatus = FSUCCESS;

	(void)vs_rwunlock(&pm->stateLock);

done:
	AtomicDecrementVoid(&pm->refCount);
	return(fStatus);
}

FSTATUS paGetVFInfo(Pm_t *pm, char *vfName, PmVFInfo_t *pmVFInfo, uint64 imageId, int32 offset, uint64 *returnImageId,
					boolean *isFailedPort)
{
	uint64				retImageId = 0;
	FSTATUS				status = FSUCCESS;
	PmVF_t				*pmVFP = NULL;
	PmVFImage_t			pmVFImage;
	PmImage_t			*pmImageP = NULL;
	PmPortImage_t		*pmPortImageP = NULL;
	PmPort_t			*pmPortP = NULL;
	uint32				imageIndex;
	const char 			*msg;
	boolean				sth = FALSE;
	int 				lid, v;
	PmHistoryRecord_t	*record = NULL;
	boolean 			frozen = 0;

	if (!pm || !vfName || !pmVFInfo || !isFailedPort)
		return(FINVALID_PARAMETER);
	if (vfName[0] == '\0') {
		IB_LOG_WARN_FMT(__func__, "Illegal vfName parameter: Empty String\n");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}

	(void)vs_rdlock(&pm->stateLock);
	status = FindImage(pm, IMAGEID_TYPE_ANY, imageId, offset, &imageIndex, &retImageId, &record, &msg, NULL, &frozen, &sth);
	if (FSUCCESS != status) {
		IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
		goto error;
	}

	if (sth && (frozen || (record && !frozen))) {
		PmCompositeImage_t *cimg;
		if (!frozen) {
			// load the composite
			status = PmLoadComposite(pm, record, &cimg);
			if (status != FSUCCESS || !cimg) {
				IB_LOG_WARN_FMT(__func__, "Unable to load composite image: %s", FSTATUS_ToString(status));
				goto error;
			}
		} else {
			cimg = pm->ShortTermHistory.cachedComposite;
		}
		retImageId = cimg->header.common.imageIDs[0];
		// composite is loaded, reconstitute so we can use it
		status = PmReconstitute(&pm->ShortTermHistory, cimg);
		if (!frozen) PmFreeComposite(cimg);
		if (status != FSUCCESS) {
			IB_LOG_WARN_FMT(__func__, "Unable to reconstitute composite image: %s", FSTATUS_ToString(status));
			goto error;
		}
		pmImageP = pm->ShortTermHistory.LoadedImage.img;
		int i;
		for (i = 0; i < MAX_VFABRICS; i++) {
			if (!strcmp(pm->ShortTermHistory.LoadedImage.VFs[i]->Name, vfName)) {
				pmVFP = pm->ShortTermHistory.LoadedImage.VFs[i];
				break;
			}
		}
		if (!pmVFP) {
			IB_LOG_WARN_FMT(__func__, "VF %.*s not Found", sizeof(vfName), vfName);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_VF;
			goto error;
		}
		imageIndex = 0; // STH always uses imageIndex 0
		if (!pmVFP->Image[imageIndex].isActive) {
			IB_LOG_WARN_FMT(__func__, "VF %.*s not active", sizeof(pmVFP->Name), pmVFP->Name);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_VF;
			goto error;
		}
	} else {
		status = LocateVF(pm, vfName, &pmVFP, 1, imageIndex);
		if (status != FSUCCESS){
			IB_LOG_WARN_FMT(__func__, "VF %.*s not Found: %s", sizeof(vfName), vfName, FSTATUS_ToString(status));
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_VF;
			goto error;
		}

		pmImageP = &pm->Image[imageIndex];
		(void)vs_rdlock(&pmImageP->imageLock);
	}
	(void)vs_rwunlock(&pm->stateLock);
	memset(&pmVFImage, 0, sizeof(PmVFImage_t));
	ClearVFStats(&pmVFImage);

	*isFailedPort = FALSE;
	for (lid = 1; lid <= pmImageP->maxLid; lid++) {
		PmNode_t *pmNodeP = pmImageP->LidMap[lid];
		if (!pmNodeP) continue;
		if (pmNodeP->nodeType == STL_NODE_SW) {
			int p;
			for (p=0; p <= pmNodeP->numPorts; p++) { 	// Includes port 0
				pmPortP = pmNodeP->up.swPorts[p];
				if (!pmPortP) continue;
				pmPortImageP = &pmPortP->Image[imageIndex];
				if (pmPortImageP->u.s.queryStatus != PM_QUERY_STATUS_OK) {
					// mark a flag to indicate there is at least one failed port.
					*isFailedPort = TRUE;
					continue;
				}
				for (v=0; v < pmPortImageP->numVFs; v++) {
					PmVF_t *pmPortVFP = pmPortImageP->vfvlmap[v].pVF;
					if (pmPortVFP != pmVFP) continue;
					pmVFImage.NumPorts++;
					UpdateVFStats(pm, &pmVFImage, pmPortImageP);
				}
			}
		} else {
			pmPortP = pmNodeP->up.caPortp;
			if (!pmPortP) continue;
			pmPortImageP = &pmPortP->Image[imageIndex];
			if (pmPortImageP->u.s.queryStatus != PM_QUERY_STATUS_OK) {
				// mark a flag to indicate there is at least one failed port.
				*isFailedPort = TRUE;
				continue;
			}
			for (v=0; v < pmPortImageP->numVFs; v++) {
				PmVF_t *pmPortVFP = pmPortImageP->vfvlmap[v].pVF;
				if (pmPortVFP != pmVFP) continue;
				pmVFImage.NumPorts++;
				UpdateVFStats(pm, &pmVFImage, pmPortImageP);
			}
		}
	}
	FinalizeVFStats(&pmVFImage);
	strcpy(pmVFInfo->vfName, vfName);
	pmVFInfo->NumPorts = pmVFImage.NumPorts;
	memcpy(&pmVFInfo->IntUtil, &pmVFImage.IntUtil, sizeof(PmUtilStats_t));
	memcpy(&pmVFInfo->IntErr, &pmVFImage.IntErr, sizeof(PmErrStats_t));
	pmVFInfo->MinIntRate = pmVFImage.MinIntRate;
	pmVFInfo->MaxIntRate = pmVFImage.MaxIntRate;
	if (!sth) {
		(void)vs_rwunlock(&pmImageP->imageLock);
	}

	*returnImageId = retImageId;

done:
	AtomicDecrementVoid(&pm->refCount);
	return(status);
error:
	(void)vs_rwunlock(&pm->stateLock);
	*returnImageId = BAD_IMAGE_ID;
	goto done;
}

FSTATUS paGetVFConfig(Pm_t *pm, char *vfName, uint64 vfSid, PmVFConfig_t *pmVFConfig, uint64 imageId, int32 offset, uint64 *returnImageId)
{
	uint64				retImageId = 0;
	PmVF_t				*pmVFP = NULL;
	STL_LID_32			lid;
	uint32				imageIndex;
	const char 			*msg;
	PmImage_t			*pmImageP = NULL;
	FSTATUS				status = FSUCCESS;
	boolean				sth = 0;
	PmHistoryRecord_t	*record = NULL;
	boolean 			frozen = 0;

	// check input parameters
	if (!pm || !vfName || !pmVFConfig)
		return(FINVALID_PARAMETER);
	if (vfName[0] == '\0') {
		IB_LOG_WARN_FMT(__func__, "Illegal vfName parameter: Empty String\n");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}

	// initialize vf config port list counts
	pmVFConfig->NumPorts = 0;
	pmVFConfig->portListSize = 0;
	pmVFConfig->portList = NULL;

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}

	// pmVFP points to our vf
	// check all ports for membership in our group
	(void)vs_rdlock(&pm->stateLock);
	status = FindImage(pm, IMAGEID_TYPE_ANY, imageId, offset, &imageIndex, &retImageId, &record, &msg, NULL, &frozen, &sth);
	if (FSUCCESS != status) {
		IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
		goto error;
	}

	if (sth && (frozen || (record && !frozen))) {
		int i;
		PmCompositeImage_t *cimg;
		if (!frozen) {
			status = PmLoadComposite(pm, record, &cimg);
			if (status != FSUCCESS || !cimg) {
				IB_LOG_WARN_FMT(__func__, "Unable to load composite image: %s", FSTATUS_ToString(status));
				goto error;
			}
		} else {
			cimg = pm->ShortTermHistory.cachedComposite;
		}
		retImageId = cimg->header.common.imageIDs[0];
		status = PmReconstitute(&pm->ShortTermHistory, cimg);
		if (!frozen) PmFreeComposite(cimg);
		if (status != FSUCCESS) {
			IB_LOG_WARN_FMT(__func__, "Unable to reconstitute composite image: %s", FSTATUS_ToString(status));
			goto error;
		}
		pmImageP = pm->ShortTermHistory.LoadedImage.img;
		for (i = 0; i < MAX_VFABRICS; i++) {
			if (!strcmp(pm->ShortTermHistory.LoadedImage.VFs[i]->Name, vfName)) {
				pmVFP = pm->ShortTermHistory.LoadedImage.VFs[i];
				break;
			}
		}
		if (!pmVFP) {
			IB_LOG_WARN_FMT(__func__, "VF %.*s not Found", sizeof(vfName), vfName);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_VF;
			goto error;
		}
		imageIndex = 0; // STH always uses imageIndex 0
		if (!pmVFP->Image[imageIndex].isActive) {
			IB_LOG_WARN_FMT(__func__, "VF %.*s not active", sizeof(pmVFP->Name), pmVFP->Name);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_VF;
			goto error;
		}
	} else {
		status = LocateVF(pm, vfName, &pmVFP, 1, imageIndex);
		if (status != FSUCCESS){
			IB_LOG_WARN_FMT(__func__, "VF %.*s not Found: %s", sizeof(vfName), vfName, FSTATUS_ToString(status));
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_VF;
			goto error;
		}

		pmImageP = &pm->Image[imageIndex];
		(void)vs_rdlock(&pmImageP->imageLock);
	}

	(void)vs_rwunlock(&pm->stateLock);
	for (lid=1; lid<= pmImageP->maxLid; ++lid) {
		uint8 portnum;
		PmNode_t *pmnodep = pmImageP->LidMap[lid];
		if (! pmnodep)
			continue;
		if (pmnodep->nodeType == STL_NODE_SW) {
			for (portnum=0; portnum<=pmnodep->numPorts; ++portnum) {
				PmPort_t *pmportp = pmnodep->up.swPorts[portnum];
				PmPortImage_t *portImage;
				if (! pmportp)
					continue;
				portImage = &pmportp->Image[imageIndex];
				if ( PmIsPortInVF(pm, pmportp, portImage, pmVFP) ) // Port 0 on switches is in all VF
				{
					if (pmVFConfig->portListSize == pmVFConfig->NumPorts) {
						pmVFConfig->portListSize += PORTLISTCHUNK;
					}
					pmVFConfig->NumPorts++;
				}
			}
		} else {
			PmPort_t *pmportp = pmnodep->up.caPortp;
			PmPortImage_t *portImage = &pmportp->Image[imageIndex];
			if (PmIsPortInVF(pm, pmportp, portImage, pmVFP))
			{
				if (pmVFConfig->portListSize == pmVFConfig->NumPorts) {
					pmVFConfig->portListSize += PORTLISTCHUNK;
				}
				pmVFConfig->NumPorts++;
			}
		}
	}
	// check if there are ports to sort
	if (!pmVFConfig->NumPorts) {
		IB_LOG_INFO_FMT(__func__, "VF %.*s has no ports", sizeof(vfName), vfName);
		goto norecords;
	}
	// allocate the port list
	Status_t ret = vs_pool_alloc(&pm_pool, pmVFConfig->portListSize * sizeof(PmPortConfig_t), (void *)&pmVFConfig->portList);
	if (ret !=  VSTATUS_OK) {
		if (!sth) (void)vs_rwunlock(&pmImageP->imageLock);
		IB_LOG_ERRORRC("Failed to allocate list buffer for pmVFConfig rc:", ret);
		status = FINSUFFICIENT_MEMORY;
		goto done;
	}
	// copy the port list
	int i = 0;
	for (lid=1; lid <= pmImageP->maxLid; ++lid) {
		uint8 portnum;
		PmNode_t *pmnodep = pmImageP->LidMap[lid];
		if (!pmnodep) 
			continue;
		if (pmnodep->nodeType == STL_NODE_SW) {
			for (portnum=0; portnum <= pmnodep->numPorts; ++portnum) {
				PmPort_t *pmportp = pmnodep->up.swPorts[portnum];
				PmPortImage_t *portImage;
				if (!pmportp) 
					continue;
				portImage = &pmportp->Image[imageIndex];
				if (PmIsPortInVF(pm, pmportp, portImage, pmVFP)) {
					pmVFConfig->portList[i].lid = lid;
					pmVFConfig->portList[i].portNum = pmportp->portNum;
					pmVFConfig->portList[i].guid = pmnodep->guid;
					memcpy(pmVFConfig->portList[i].nodeDesc, (char *)pmnodep->nodeDesc.NodeString,
						sizeof(pmVFConfig->portList[i].nodeDesc));
					i++;
				}
			}
		} else {
			PmPort_t *pmportp = pmnodep->up.caPortp;
			PmPortImage_t *portImage = &pmportp->Image[imageIndex];
			if (PmIsPortInVF(pm, pmportp, portImage, pmVFP)) {
				pmVFConfig->portList[i].lid = lid;
				pmVFConfig->portList[i].portNum = pmportp->portNum;
				pmVFConfig->portList[i].guid = pmnodep->guid;
				memcpy(pmVFConfig->portList[i].nodeDesc, (char *)pmnodep->nodeDesc.NodeString,
					sizeof(pmVFConfig->portList[i].nodeDesc));
				i++;
			}
		}
	}

norecords:
	strcpy(pmVFConfig->vfName, pmVFP->Name);
	*returnImageId = retImageId;

	if (!sth) (void)vs_rwunlock(&pmImageP->imageLock);
done:
#ifndef __VXWORKS__
	if (sth) clearLoadedImage(&pm->ShortTermHistory);
#endif
	AtomicDecrementVoid(&pm->refCount);
	return(status);
error:
	(void)vs_rwunlock(&pm->stateLock);
	*returnImageId = BAD_IMAGE_ID;
	goto done;
}

FSTATUS paGetVFPortStats(Pm_t *pm, STL_LID_32 lid, uint8 portNum, char *vfName, PmCompositeVLCounters_t *vfPortCountersP, uint32 delta, uint32 userCntrs, uint64 imageId, int32 offset, uint32 *flagsp, uint64 *returnImageId)
{
	uint64				retImageId = 0, retImageId2 = 0;
	FSTATUS				status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_VF;
	PmPort_t			*pmPortP, *pmPortPreviousP = NULL;
	PmPortImage_t		*pmPortImageP, *pmPortImagePreviousP = NULL;
	PmPortImage_t		pmPortImage;
	const char 			*msg;
	PmImage_t			*pmImageP, *pmImagePreviousP = NULL;
	uint32				imageIndex, imageIndexPrevious;
	boolean				sth = 0, sth2 = 0;
	PmVF_t				*pmVFP = NULL;
	uint32				vl = 0;
	uint16				vlMapsFound = 0;
	int					i;
	boolean				useHiddenVF = !strcmp(HIDDEN_VL15_VF, vfName);
	PmCompositeVLCounters_t vfPortCountersPrevious = { 0 };
	uint32	VFVlSelectMask = 0, VlSelectMask = 0,
			VlSelectMaskShared = 0, SingleVLBit = 0;
	PmHistoryRecord_t	*record, *record2 = NULL;
	boolean				frozen, frozen2 = 0;

	if (!pm || !vfPortCountersP) {
		return(FINVALID_PARAMETER);
	}
	if (userCntrs && (delta || offset)) {
		IB_LOG_WARN_FMT(__func__,  "Illegal combination of parameters: Offset (%d) and delta(%d) must be zero if UserCounters(%d) flag is set",
						offset, delta, userCntrs);
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}
	if (vfName[0] == '\0') {
		IB_LOG_WARN_FMT(__func__, "Illegal vfName parameter: Empty String\n");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}
	if (!lid) {
		IB_LOG_WARN_FMT(__func__,  "Illegal LID parameter: must not be zero");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}

	*flagsp =	(delta ? STL_PA_PC_FLAG_DELTA : 0 ) |
				(userCntrs ? STL_PA_PC_FLAG_USER_COUNTERS : 0 );

	(void)vs_rdlock(&pm->stateLock);
	if (userCntrs) {
		imageIndex = pm->LastSweepIndex;
		if (pm->LastSweepIndex == PM_IMAGE_INDEX_INVALID) {
			IB_LOG_WARN_FMT(__func__, "Unable to Get PortStats: PM has not completed a sweep.");
			(void)vs_rwunlock(&pm->stateLock);
			status = FUNAVAILABLE | STL_MAD_STATUS_STL_PA_UNAVAILABLE;
			goto done;
		}
		pmImageP = &pm->Image[imageIndex];
		(void)vs_rdlock(&pmImageP->imageLock);

		pmPortP = pm_find_port(pmImageP, lid, portNum);
		if (!pmPortP) {
			IB_LOG_WARN_FMT(__func__, "Port not found: Lid 0x%x Port %u", lid, portNum);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_PORT;
			goto unlock;
		}
		pmPortImageP = &pmPortP->Image[imageIndex];
		if (pmPortImageP->u.s.queryStatus != PM_QUERY_STATUS_OK) {
			IB_LOG_WARN_FMT(__func__, "Port Query Status Invalid: %s: Lid 0x%x Port %u",
				(pmPortImageP->u.s.queryStatus == PM_QUERY_STATUS_SKIP ? "Skipped" :
				(pmPortImageP->u.s.queryStatus == PM_QUERY_STATUS_FAIL_QUERY ? "Failed Query" : "Failed Clear")),
				lid, portNum);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_IMAGE;
			goto unlock;
		}
		(void)vs_rwunlock(&pm->stateLock);
		// for Running totals, Live Data only, no offset
		(void)vs_rdlock(&pm->totalsLock);
		for (i = 0; i < MAX_VFABRICS; i++) {
			vl = pmPortImageP->vfvlmap[i].vl;
			SingleVLBit =  1 << vl;
			if ( (useHiddenVF && !i) || (pmPortImageP->vfvlmap[i].pVF && !strcmp(pmPortImageP->vfvlmap[i].pVF->Name, vfName)) ) {
				if (useHiddenVF) {
					vl=15;
					SingleVLBit = 0x8000;
				}
				vlMapsFound++;
				VFVlSelectMask |= SingleVLBit;

				vfPortCountersP->PortVLXmitData     += pmPortP->StlVLPortCountersTotal[vl].PortVLXmitData;
				vfPortCountersP->PortVLRcvData      += pmPortP->StlVLPortCountersTotal[vl].PortVLRcvData;
				vfPortCountersP->PortVLXmitPkts     += pmPortP->StlVLPortCountersTotal[vl].PortVLXmitPkts;
				vfPortCountersP->PortVLRcvPkts      += pmPortP->StlVLPortCountersTotal[vl].PortVLRcvPkts;
				vfPortCountersP->PortVLXmitWait     += pmPortP->StlVLPortCountersTotal[vl].PortVLXmitWait;
				vfPortCountersP->SwPortVLCongestion += pmPortP->StlVLPortCountersTotal[vl].SwPortVLCongestion;
				vfPortCountersP->PortVLRcvFECN      += pmPortP->StlVLPortCountersTotal[vl].PortVLRcvFECN;
				vfPortCountersP->PortVLRcvBECN      += pmPortP->StlVLPortCountersTotal[vl].PortVLRcvBECN;
				vfPortCountersP->PortVLXmitTimeCong += pmPortP->StlVLPortCountersTotal[vl].PortVLXmitTimeCong;
				vfPortCountersP->PortVLXmitWastedBW += pmPortP->StlVLPortCountersTotal[vl].PortVLXmitWastedBW;
				vfPortCountersP->PortVLXmitWaitData += pmPortP->StlVLPortCountersTotal[vl].PortVLXmitWaitData;
				vfPortCountersP->PortVLRcvBubble    += pmPortP->StlVLPortCountersTotal[vl].PortVLRcvBubble;
				vfPortCountersP->PortVLMarkFECN     += pmPortP->StlVLPortCountersTotal[vl].PortVLMarkFECN;
				vfPortCountersP->PortVLXmitDiscards += pmPortP->StlVLPortCountersTotal[vl].PortVLXmitDiscards;

				status = FSUCCESS;
			}
			if (VlSelectMask & SingleVLBit) {
				VlSelectMaskShared |= SingleVLBit;
			}
			else {
				VlSelectMask |= SingleVLBit;
			}
		}
		if (VlSelectMaskShared & VFVlSelectMask) {
			*flagsp |= STL_PA_PC_FLAG_SHARED_VL;
		}
		IB_LOG_DEBUG3_FMT(__func__, "%s: %s=%u %s=0x%x %s=0x%x %s=0x%x", "User",
				  "vlMapsFound", vlMapsFound, "VFVlSelectMask", VFVlSelectMask,
				  "VlSelectMask", VlSelectMask, "VlSelectMaskShared", VlSelectMaskShared);
		(void)vs_rwunlock(&pm->totalsLock);
		(void)vs_rwunlock(&pmImageP->imageLock);
		*flagsp |= (isUnexpectedClearUserCounters ? STL_PA_PC_FLAG_UNEXPECTED_CLEAR : 0);
		goto done;
	}

	status = FindImage(pm, IMAGEID_TYPE_ANY, imageId, offset, &imageIndex, &retImageId, &record, &msg, NULL, &frozen, &sth);
	if (FSUCCESS != status) {
		IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
		goto error;
	}

	if (sth && (frozen || (record && !frozen))) {
		PmCompositeImage_t *cimg;
		if (!frozen) {
			status = PmLoadComposite(pm, record, &cimg);
			if (status != FSUCCESS || !cimg) {
				IB_LOG_WARN_FMT(__func__, "Unable to load composite image: %s", FSTATUS_ToString(status));
				goto error;
			}
		} else {
			cimg = pm->ShortTermHistory.cachedComposite;
		}
		retImageId = cimg->header.common.imageIDs[0];
		status = PmReconstitute(&pm->ShortTermHistory, cimg);
		if (!frozen) PmFreeComposite(cimg);
		if (status != FSUCCESS) {
			IB_LOG_WARN_FMT(__func__, "Unable to reconstitute composite image: %s", FSTATUS_ToString(status));
			goto error;
		}

		if (!useHiddenVF) {
			for (i = 0; i < MAX_VFABRICS; i++) {
				if (!strcmp(pm->ShortTermHistory.LoadedImage.VFs[i]->Name, vfName)) {
					pmVFP = pm->ShortTermHistory.LoadedImage.VFs[i];
					break;
				}
			}
			if (!pmVFP) {
				IB_LOG_WARN_FMT(__func__, "Unable to Get VF Port Stats %s", "No such VF");
				status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_VF;
				goto error;
			}
			if (!pmVFP->Image[imageIndex].isActive) {
				IB_LOG_WARN_FMT(__func__, "VF %.*s not active", sizeof(pmVFP->Name), pmVFP->Name);
				status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_VF;
				goto error;
			}
		}
		pmImageP = pm->ShortTermHistory.LoadedImage.img;
		imageIndex = 0;
	} else {
		if (!useHiddenVF) {
			status = LocateVF(pm, vfName, &pmVFP, 1, imageIndex);
			if (status != FSUCCESS) {
				IB_LOG_WARN_FMT(__func__, "VF %.*s not Found: %s", sizeof(vfName), vfName, FSTATUS_ToString(status));
				status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_VF;
				goto error;
			}
		}

		pmImageP = &pm->Image[imageIndex];
		(void)vs_rdlock(&pmImageP->imageLock);
	}

	pmPortP = pm_find_port(pmImageP, lid, portNum);
	if (! pmPortP) {
		IB_LOG_WARN_FMT(__func__, "Port not found: Lid 0x%x Port %u", lid, portNum);
		status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_PORT;
		goto unlock;
	} 
	pmPortImageP = &pmPortP->Image[imageIndex];
	if (pmPortImageP->u.s.queryStatus != PM_QUERY_STATUS_OK) {
		IB_LOG_WARN_FMT(__func__, "Port Query Status Invalid: %s: Lid 0x%x Port %u",
			(pmPortImageP->u.s.queryStatus == PM_QUERY_STATUS_SKIP ? "Skipped" :
			(pmPortImageP->u.s.queryStatus == PM_QUERY_STATUS_FAIL_QUERY ? "Failed Query" : "Failed Clear")),
			lid, portNum);
		status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_IMAGE;
		goto unlock;
	}
	if (delta) {
		if (sth) {
			memcpy(&pmPortImage, pmPortImageP, sizeof(PmPortImage_t));
			pmPortImageP = &pmPortImage;
		}
		
		status = FindImage(pm, IMAGEID_TYPE_ANY, imageId, offset-1, &imageIndexPrevious, &retImageId2, &record2, &msg, NULL, &frozen2, &sth2);
		if (FSUCCESS != status) {
			IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
			goto unlock;
		}

		if (sth2 && (frozen2 || (record2 && !frozen2))) {
			PmCompositeImage_t *cimg2;
			if (!frozen2) {
				status = PmLoadComposite(pm, record2, &cimg2);
				if (status != FSUCCESS || !cimg2) {
					IB_LOG_WARN_FMT(__func__, "Unable to load composite image: %s", FSTATUS_ToString(status));
					goto unlock;
				}
			} else {
				cimg2 = pm->ShortTermHistory.cachedComposite;
			}

			status = PmReconstitute(&pm->ShortTermHistory, cimg2);
			if (!frozen2) PmFreeComposite(cimg2);
			if (status != FSUCCESS) {
				IB_LOG_WARN_FMT(__func__, "Unable to reconstitute composite image: %s", FSTATUS_ToString(status));
				goto unlock;
			}

			pmImagePreviousP = pm->ShortTermHistory.LoadedImage.img;
			imageIndexPrevious = 0;
		} else {
			pmImagePreviousP = &pm->Image[imageIndexPrevious];
			(void)vs_rdlock(&pmImagePreviousP->imageLock);
		}
		pmPortPreviousP = pm_find_port(pmImagePreviousP, lid, portNum);
	}

	if (delta && !pmPortPreviousP) {
		IB_LOG_WARN_FMT(__func__, "Port not found in previous image: Lid 0x%x Port %u", lid, portNum);
		status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_PORT;
		goto unlock;
	}

	if (delta) {
		pmPortImagePreviousP = &pmPortPreviousP->Image[imageIndexPrevious];
		if (pmPortImagePreviousP->u.s.queryStatus != PM_QUERY_STATUS_OK) {
			IB_LOG_WARN_FMT(__func__, "Port Query Status Invalid: %s: Lid 0x%x Port %u",
				(pmPortImageP->u.s.queryStatus == PM_QUERY_STATUS_SKIP ? "Skipped" :
				(pmPortImageP->u.s.queryStatus == PM_QUERY_STATUS_FAIL_QUERY ? "Failed Query" : "Failed Clear")),
				lid, portNum);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_IMAGE;
			goto unlock;
		}
		memset(&vfPortCountersPrevious, 0, sizeof(PmCompositeVLCounters_t));
		// Calculate previous VF Counters
		for (i = 0; i < MAX_VFABRICS; i++) {
			vl = pmPortImagePreviousP->vfvlmap[i].vl;
			SingleVLBit =  1 << vl;
			if ( (useHiddenVF && !i) || (pmPortImagePreviousP->vfvlmap[i].pVF && !strcmp(pmPortImagePreviousP->vfvlmap[i].pVF->Name, vfName)) ) {
				if (useHiddenVF) {
					vl=15;
					SingleVLBit = 0x8000;
				}
				vlMapsFound++;
				VFVlSelectMask |= SingleVLBit;

				vfPortCountersPrevious.PortVLXmitData     += pmPortImagePreviousP->StlVLPortCounters[vl].PortVLXmitData;
				vfPortCountersPrevious.PortVLRcvData      += pmPortImagePreviousP->StlVLPortCounters[vl].PortVLRcvData;
				vfPortCountersPrevious.PortVLXmitPkts     += pmPortImagePreviousP->StlVLPortCounters[vl].PortVLXmitPkts;
				vfPortCountersPrevious.PortVLRcvPkts      += pmPortImagePreviousP->StlVLPortCounters[vl].PortVLRcvPkts;
				vfPortCountersPrevious.PortVLXmitWait     += pmPortImagePreviousP->StlVLPortCounters[vl].PortVLXmitWait;
				vfPortCountersPrevious.SwPortVLCongestion += pmPortImagePreviousP->StlVLPortCounters[vl].SwPortVLCongestion;
				vfPortCountersPrevious.PortVLRcvFECN      += pmPortImagePreviousP->StlVLPortCounters[vl].PortVLRcvFECN;
				vfPortCountersPrevious.PortVLRcvBECN      += pmPortImagePreviousP->StlVLPortCounters[vl].PortVLRcvBECN;
				vfPortCountersPrevious.PortVLXmitTimeCong += pmPortImagePreviousP->StlVLPortCounters[vl].PortVLXmitTimeCong;
				vfPortCountersPrevious.PortVLXmitWastedBW += pmPortImagePreviousP->StlVLPortCounters[vl].PortVLXmitWastedBW;
				vfPortCountersPrevious.PortVLXmitWaitData += pmPortImagePreviousP->StlVLPortCounters[vl].PortVLXmitWaitData;
				vfPortCountersPrevious.PortVLRcvBubble    += pmPortImagePreviousP->StlVLPortCounters[vl].PortVLRcvBubble;
				vfPortCountersPrevious.PortVLMarkFECN     += pmPortImagePreviousP->StlVLPortCounters[vl].PortVLMarkFECN;
				vfPortCountersPrevious.PortVLXmitDiscards += pmPortImagePreviousP->StlVLPortCounters[vl].PortVLXmitDiscards;

				status = FSUCCESS;
			}
			if (VlSelectMask & SingleVLBit) {
				VlSelectMaskShared |= SingleVLBit;
			}
			else {
				VlSelectMask |= SingleVLBit;
			}
		}
		if (VlSelectMaskShared & VFVlSelectMask) {
			*flagsp |= STL_PA_PC_FLAG_SHARED_VL;
		}
		IB_LOG_DEBUG3_FMT(__func__, "Delta: Offset=%d: %s=%u %s=0x%x %s=0x%x %s=0x%x", offset-1,
						  "vlMapsFound", vlMapsFound, "VFVlSelectMask", VFVlSelectMask,
						  "VlSelectMask", VlSelectMask, "VlSelectMaskShared", VlSelectMaskShared);
	}
	(void)vs_rwunlock(&pm->stateLock);

	vlMapsFound = VFVlSelectMask = VlSelectMask = VlSelectMaskShared = 0;
	// Calculate previous VF Counters
	for (i = 0; i < MAX_VFABRICS; i++) {
		vl = pmPortImageP->vfvlmap[i].vl;
		SingleVLBit =  1 << vl;
		if ( (useHiddenVF && !i) || (pmPortImageP->vfvlmap[i].pVF && !strcmp(pmPortImageP->vfvlmap[i].pVF->Name, vfName)) ) {
			if (useHiddenVF) {
				vl=15;
				SingleVLBit = 0x8000;
			}
			vlMapsFound++;
			VFVlSelectMask |= SingleVLBit;

			vfPortCountersP->PortVLXmitData     += pmPortImageP->StlVLPortCounters[vl].PortVLXmitData;
			vfPortCountersP->PortVLRcvData      += pmPortImageP->StlVLPortCounters[vl].PortVLRcvData;
			vfPortCountersP->PortVLXmitPkts     += pmPortImageP->StlVLPortCounters[vl].PortVLXmitPkts;
			vfPortCountersP->PortVLRcvPkts      += pmPortImageP->StlVLPortCounters[vl].PortVLRcvPkts;
			vfPortCountersP->PortVLXmitWait     += pmPortImageP->StlVLPortCounters[vl].PortVLXmitWait;
			vfPortCountersP->SwPortVLCongestion += pmPortImageP->StlVLPortCounters[vl].SwPortVLCongestion;
			vfPortCountersP->PortVLRcvFECN      += pmPortImageP->StlVLPortCounters[vl].PortVLRcvFECN;
			vfPortCountersP->PortVLRcvBECN      += pmPortImageP->StlVLPortCounters[vl].PortVLRcvBECN;
			vfPortCountersP->PortVLXmitTimeCong += pmPortImageP->StlVLPortCounters[vl].PortVLXmitTimeCong;
			vfPortCountersP->PortVLXmitWastedBW += pmPortImageP->StlVLPortCounters[vl].PortVLXmitWastedBW;
			vfPortCountersP->PortVLXmitWaitData += pmPortImageP->StlVLPortCounters[vl].PortVLXmitWaitData;
			vfPortCountersP->PortVLRcvBubble    += pmPortImageP->StlVLPortCounters[vl].PortVLRcvBubble;
			vfPortCountersP->PortVLMarkFECN     += pmPortImageP->StlVLPortCounters[vl].PortVLMarkFECN;
			vfPortCountersP->PortVLXmitDiscards += pmPortImageP->StlVLPortCounters[vl].PortVLXmitDiscards;

			status = FSUCCESS;
		}
		if (VlSelectMask & SingleVLBit) {
			VlSelectMaskShared |= SingleVLBit;
		}
		else {
			VlSelectMask |= SingleVLBit;
		}
	}
	if (VlSelectMaskShared & VFVlSelectMask) {
		*flagsp |= STL_PA_PC_FLAG_SHARED_VL;
	}
	IB_LOG_DEBUG3_FMT(__func__, "%s: Offset=%d: %s=%u %s=0x%x %s=0x%x %s=0x%x", (delta?"Delta":"Raw"), offset,
					  "vlMapsFound", vlMapsFound, "VFVlSelectMask", VFVlSelectMask,
					  "VlSelectMask", VlSelectMask, "VlSelectMaskShared", VlSelectMaskShared);

	if (delta) {
#define GET_DELTA_VFPORTCOUNTERS(cntr, pcntr) \
		vfPortCountersP->cntr -= (pmPortImagePreviousP->clearSelectMask.s.pcntr ? 0 : vfPortCountersPrevious.cntr)

		GET_DELTA_VFPORTCOUNTERS(PortVLXmitData,     PortXmitData);
		GET_DELTA_VFPORTCOUNTERS(PortVLRcvData,      PortRcvData);
		GET_DELTA_VFPORTCOUNTERS(PortVLXmitPkts,     PortXmitPkts);
		GET_DELTA_VFPORTCOUNTERS(PortVLRcvPkts,      PortRcvPkts);
		GET_DELTA_VFPORTCOUNTERS(PortVLXmitWait,     PortXmitWait);
		GET_DELTA_VFPORTCOUNTERS(SwPortVLCongestion, SwPortCongestion);
		GET_DELTA_VFPORTCOUNTERS(PortVLRcvFECN,      PortRcvFECN);
		GET_DELTA_VFPORTCOUNTERS(PortVLRcvBECN,      PortRcvBECN);
		GET_DELTA_VFPORTCOUNTERS(PortVLXmitTimeCong, PortXmitTimeCong);
		GET_DELTA_VFPORTCOUNTERS(PortVLXmitWastedBW, PortXmitWastedBW);
		GET_DELTA_VFPORTCOUNTERS(PortVLXmitWaitData, PortXmitWaitData);
		GET_DELTA_VFPORTCOUNTERS(PortVLRcvBubble,    PortRcvBubble);
		GET_DELTA_VFPORTCOUNTERS(PortVLMarkFECN,     PortMarkFECN);
		GET_DELTA_VFPORTCOUNTERS(PortVLXmitDiscards, PortXmitDiscards);

#undef GET_DELTA_VFPORTCOUNTERS
	}
	*returnImageId = retImageId;

unlock:
	if (!sth){
		(void)vs_rwunlock(&pmImageP->imageLock);
	}
	if (delta && (!sth2)){
		(void)vs_rwunlock(&pmImagePreviousP->imageLock);
	}
	if (status != FSUCCESS) {
		goto error;
	}
done:
	AtomicDecrementVoid(&pm->refCount);
	return(status);
error:
	(void)vs_rwunlock(&pm->stateLock);
	*returnImageId = BAD_IMAGE_ID;
	goto done;
}

FSTATUS paClearVFPortStats(Pm_t *pm, STL_LID_32 lid, uint8 portNum, STLVlCounterSelectMask select, char *vfName)
{
	FSTATUS				status = FSUCCESS;
	PmImage_t			*pmimagep;
	uint32				imageIndex;

	if (!vfName) {
		return(FINVALID_PARAMETER);
	}
	if(!lid) {
		IB_LOG_WARN_FMT(__func__, "Illegal LID parameter: Must not be zero\n");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}
	if (!select.AsReg32) {
		IB_LOG_WARN_FMT(__func__, "Illegal select parameter: Must not be zero\n");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}
	if (vfName[0] == '\0') {
		IB_LOG_WARN_FMT(__func__, "Illegal vfName parameter: Empty String\n");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}
	(void)vs_rdlock(&pm->stateLock);
	// Live Data only, no offset
	imageIndex = pm->LastSweepIndex;

	if (pm->LastSweepIndex == PM_IMAGE_INDEX_INVALID) {
		IB_LOG_WARN_FMT(__func__, "Unable to Clear PortStats: PM has not completed a sweep.");
		status = FUNAVAILABLE | STL_MAD_STATUS_STL_PA_UNAVAILABLE;
		goto error;
	}
	pmimagep = &pm->Image[imageIndex];
	(void)vs_rdlock(&pmimagep->imageLock);
	(void)vs_rwunlock(&pm->stateLock);

	(void)vs_wrlock(&pm->totalsLock);
	if (portNum == PM_ALL_PORT_SELECT) {
		PmNode_t *pmnodep = pm_find_node(pmimagep, lid);
		if (! pmnodep || pmnodep->nodeType != STL_NODE_SW) {
			IB_LOG_WARN_FMT(__func__, "Switch not found: LID: 0x%x", lid);
			status = FNOT_FOUND;
		} else {
			status = PmClearNodeRunningVFCounters(pmnodep, select, vfName);
		}
	} else {
		PmPort_t	*pmportp = pm_find_port(pmimagep, lid, portNum);
		if (! pmportp) {
			IB_LOG_WARN_FMT(__func__, "Port not found: Lid 0x%x Port %u", lid, portNum);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_PORT;
		} else {
			status = PmClearPortRunningVFCounters(pmportp, select, vfName);
		}
	}
	(void)vs_rwunlock(&pm->totalsLock);

	(void)vs_rwunlock(&pmimagep->imageLock);
done:
	AtomicDecrementVoid(&pm->refCount);
	return(status);
error:
	(void)vs_rwunlock(&pm->stateLock);
	goto done;
}

FSTATUS addVFSortedPorts(PmVFFocusPorts_t *pmVFFocusPorts, sortInfo_t *sortInfo, uint32 imageIndex)
{
	Status_t			status;
	sortedValueEntry_t	*listp;
	int					portCount = 0;

	if (sortInfo->sortedValueListHead == NULL) {
		pmVFFocusPorts->NumPorts = 0;
		return(FSUCCESS);
	}

	status = vs_pool_alloc(&pm_pool, pmVFFocusPorts->NumPorts * sizeof(PmFocusPortEntry_t), (void*)&pmVFFocusPorts->portList);
	if (status != VSTATUS_OK) {
		IB_LOG_ERRORRC("Failed to allocate sorted port list buffer for pmVFFocusPorts rc:", status);
		return(FINSUFFICIENT_MEMORY);
	}
	listp = sortInfo->sortedValueListHead;
	while ((listp != NULL) && (portCount < pmVFFocusPorts->NumPorts)) {
		pmVFFocusPorts->portList[portCount].lid = listp->lid;
		pmVFFocusPorts->portList[portCount].portNum = listp->portNum;
		pmVFFocusPorts->portList[portCount].rate = PmCalculateRate(listp->portp->Image[imageIndex].u.s.activeSpeed, listp->portp->Image[imageIndex].u.s.rxActiveWidth);
		pmVFFocusPorts->portList[portCount].mtu = listp->portp->Image[imageIndex].u.s.mtu;
		pmVFFocusPorts->portList[portCount].value = listp->value;
		pmVFFocusPorts->portList[portCount].guid = (uint64_t)(listp->portp->pmnodep->guid);
		strncpy(pmVFFocusPorts->portList[portCount].nodeDesc, (char *)listp->portp->pmnodep->nodeDesc.NodeString,
			sizeof(pmVFFocusPorts->portList[portCount].nodeDesc)-1);
		if (listp->portNum != 0) {
			pmVFFocusPorts->portList[portCount].neighborLid = listp->neighborPortp->pmnodep->Image[imageIndex].lid;
			pmVFFocusPorts->portList[portCount].neighborPortNum = listp->neighborPortp->portNum;
			pmVFFocusPorts->portList[portCount].neighborValue = listp->neighborValue;
			pmVFFocusPorts->portList[portCount].neighborGuid = (uint64_t)(listp->neighborPortp->pmnodep->guid);
			strncpy(pmVFFocusPorts->portList[portCount].neighborNodeDesc, (char *)listp->neighborPortp->pmnodep->nodeDesc.NodeString,
				sizeof(pmVFFocusPorts->portList[portCount].neighborNodeDesc)-1);
		} else {
			pmVFFocusPorts->portList[portCount].neighborLid = 0;
			pmVFFocusPorts->portList[portCount].neighborPortNum = 0;
			pmVFFocusPorts->portList[portCount].neighborValue = 0;
			pmVFFocusPorts->portList[portCount].neighborGuid = 0;
			pmVFFocusPorts->portList[portCount].neighborNodeDesc[0] = 0;
		}
		portCount++;
		listp = listp->next;
	}

	return(FSUCCESS);
}

FSTATUS paGetVFFocusPorts(Pm_t *pm, char *vfName, PmVFFocusPorts_t *pmVFFocusPorts, uint64 imageId, int32 offset, uint64 *returnImageId,
					    uint32 select, uint32 start, uint32 range)
{
	uint64				retImageId = 0;
	PmVF_t				*pmVFP = NULL;
	STL_LID_32			lid;
	uint32				imageIndex;
	const char 			*msg;
	PmImage_t			*pmimagep;
	FSTATUS				status = FSUCCESS;
	Status_t			allocStatus;
	sortInfo_t			sortInfo;
	ComputeFunc_t		computeFunc = NULL;
	CompareFunc_t		compareFunc = NULL;
	CompareFunc_t		candidateFunc = NULL;
	boolean 			sth = 0;
	PmHistoryRecord_t	*record = NULL;
	boolean				frozen = 0;

	// check input parameters
	if (!pm || !vfName || !pmVFFocusPorts)
		return(FINVALID_PARAMETER);
	if (vfName[0] == '\0') {
		IB_LOG_WARN_FMT(__func__, "Illegal vfName parameter: Empty String\n");
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}
	if (start) {
		IB_LOG_WARN_FMT(__func__, "Illegal start parameter: %d: must be zero\n", start);
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}
	if (!range) {
		IB_LOG_WARN_FMT(__func__, "Illegal range parameter: %d: must be greater than zero\n", range);
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
	}
	switch (select) {
	case STL_PA_SELECT_UTIL_HIGH:
		computeFunc = &computeUtilizationValue;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	case STL_PA_SELECT_UTIL_PKTS_HIGH:
		computeFunc = &computePktRate;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	case STL_PA_SELECT_UTIL_LOW:
		computeFunc = &computeUtilizationValue;
		compareFunc = &compareGE;
		candidateFunc = &compareLE;
		break;
	case STL_PA_SELECT_ERR_INTEG:
		computeFunc = &computeIntegrityValue;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	case STL_PA_SELECT_ERR_CONG:
		computeFunc = &computeCongestionValue;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	case STL_PA_SELECT_ERR_SMA_CONG:
		computeFunc = &computeSmaCongestionValue;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	case STL_PA_SELECT_ERR_BUBBLE:
		computeFunc = &computeBubbleValue;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	case STL_PA_SELECT_ERR_SEC:
		computeFunc = &computeSecurityValue;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	case STL_PA_SELECT_ERR_ROUT:
		computeFunc = &computeRoutingValue;
		compareFunc = &compareLE;
		candidateFunc = &compareGE;
		break;
	default:
		IB_LOG_WARN_FMT(__func__, "Illegal select parameter: 0x%x\n", select);
		return(FINVALID_PARAMETER | STL_MAD_STATUS_STL_PA_INVALID_PARAMETER);
		break;
	}

	// initialize group config port list counts
	pmVFFocusPorts->NumPorts = 0;
	pmVFFocusPorts->portList = NULL;
	memset(&sortInfo, 0, sizeof(sortInfo));
	allocStatus = vs_pool_alloc(&pm_pool, range * sizeof(sortedValueEntry_t), (void*)&sortInfo.sortedValueListPool);
	if (allocStatus != VSTATUS_OK) {
		IB_LOG_ERRORRC("Failed to allocate sort list buffer for pmVFFocusPorts rc:", allocStatus);
		return FINSUFFICIENT_MEMORY;
	}
	sortInfo.sortedValueListHead = NULL;
	sortInfo.sortedValueListTail = NULL;
	sortInfo.sortedValueListSize = range;
	sortInfo.numValueEntries = 0;

	AtomicIncrementVoid(&pm->refCount);	// prevent engine from stopping
	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		status = FUNAVAILABLE;
		goto done;
	}

	(void)vs_rdlock(&pm->stateLock);
	status = FindImage(pm, IMAGEID_TYPE_ANY, imageId, offset, &imageIndex, &retImageId, &record, &msg, NULL, &frozen, &sth);
	if (FSUCCESS != status) {
		IB_LOG_WARN_FMT(__func__, "Unable to get index from ImageId: %s: %s", FSTATUS_ToString(status), msg);
		goto error;
	}
	if (sth && (frozen || (record && !frozen))) {
		int i;
		PmCompositeImage_t *cimg;
		if (!frozen) {
			status = PmLoadComposite(pm, record, &cimg);
			if (status != FSUCCESS || !cimg) {
				IB_LOG_WARN_FMT(__func__, "Unable to load composite image: %s", FSTATUS_ToString(status));
				goto error;
			}
		} else {
			cimg = pm->ShortTermHistory.cachedComposite;
		}
		retImageId = cimg->header.common.imageIDs[0];
		status = PmReconstitute(&pm->ShortTermHistory, cimg);
		if (!frozen) PmFreeComposite(cimg);
		if (status != FSUCCESS) {
			IB_LOG_WARN_FMT(__func__, "Unable to reconstitute composite image: %s", FSTATUS_ToString(status));
			goto error;
		}
		pmimagep = pm->ShortTermHistory.LoadedImage.img;
		for (i = 0; i < MAX_VFABRICS; i++) {
			if (!strcmp(pm->ShortTermHistory.LoadedImage.VFs[i]->Name, vfName)) {
				pmVFP = pm->ShortTermHistory.LoadedImage.VFs[i];
				break;
			}
		}
		if (!pmVFP) {
			IB_LOG_WARN_FMT(__func__, "VF %.*s not Found", sizeof(vfName), vfName);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_VF;
			goto error;
		}
		imageIndex = 0; // STH always uses imageIndex 0
		if (!pmVFP->Image[imageIndex].isActive) {
			IB_LOG_WARN_FMT(__func__, "VF %.*s not active", sizeof(pmVFP->Name), pmVFP->Name);
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_VF;
			goto error;
		}
	} else {
		status = LocateVF(pm, vfName, &pmVFP, 1, imageIndex);
		if (status != FSUCCESS) {
			IB_LOG_WARN_FMT(__func__, "VF %.*s not Found: %s", sizeof(vfName), vfName, FSTATUS_ToString(status));
			status = FNOT_FOUND | STL_MAD_STATUS_STL_PA_NO_VF;
			goto error;
		}
		pmimagep = &pm->Image[imageIndex];
		(void)vs_rdlock(&pmimagep->imageLock);
	}

	(void)vs_rwunlock(&pm->stateLock);
	for (lid=1; lid<= pmimagep->maxLid; ++lid) {
		uint8 portnum;
		PmNode_t *pmnodep = pmimagep->LidMap[lid];
		if (! pmnodep)
			continue;
		if (pmnodep->nodeType == STL_NODE_SW) {
			for (portnum=0; portnum<=pmnodep->numPorts; ++portnum) {
				PmPort_t *pmportp = pmnodep->up.swPorts[portnum];
				PmPort_t *pmNeighborportp;
				PmPortImage_t *portImage;
				PmPortImage_t *neighborPortImage;
				if (! pmportp)
					continue;
				portImage = &pmportp->Image[imageIndex];
				pmNeighborportp = portImage->neighbor;
				neighborPortImage = &pmNeighborportp->Image[imageIndex];
				if ( PmIsPortInVF(pm, pmportp, portImage, pmVFP) && // Port 0 on switches is in all VF
					portImage->u.s.queryStatus == PM_QUERY_STATUS_OK)
				{
					processFocusPort(pm, pmportp, portImage, pmNeighborportp, neighborPortImage, lid, portnum, computeFunc, compareFunc, candidateFunc, &sortInfo);
				}
			}
		} else {
			PmPort_t *pmportp = pmnodep->up.caPortp;
			PmPortImage_t *portImage = &pmportp->Image[imageIndex];
			PmPort_t *pmNeighborportp = portImage->neighbor;
			PmPortImage_t *neighborPortImage = &pmNeighborportp->Image[imageIndex];
			if (PmIsPortInVF(pm, pmportp, portImage, pmVFP) &&
				portImage->u.s.queryStatus == PM_QUERY_STATUS_OK)
			{
				processFocusPort(pm, pmportp, portImage, pmNeighborportp, neighborPortImage, lid, pmportp->portNum, computeFunc, compareFunc, candidateFunc, &sortInfo);
			}
		}
	}

	strcpy(pmVFFocusPorts->vfName, pmVFP->Name);
	pmVFFocusPorts->NumPorts = sortInfo.numValueEntries;
	if (pmVFFocusPorts->NumPorts)
		status = addVFSortedPorts(pmVFFocusPorts, &sortInfo, imageIndex);
	if (!sth) {
		(void)vs_rwunlock(&pmimagep->imageLock);
	}
	if (status != FSUCCESS)
		goto done;
	*returnImageId = retImageId;

done:
	if (sortInfo.sortedValueListPool != NULL)
		vs_pool_free(&pm_pool, sortInfo.sortedValueListPool);
	if (sth) {
#ifndef __VXWORKS__
		clearLoadedImage(&pm->ShortTermHistory);
#endif
	}

	AtomicDecrementVoid(&pm->refCount);
	return(status);
error:
	(void)vs_rwunlock(&pm->stateLock);
	*returnImageId = BAD_IMAGE_ID;
	goto done;
}

// Append details about frozen images into memory pointed to by buffer 
static void appendFreezeFrameDetails(uint8_t *buffer, uint32_t *index)
{
	extern Pm_t 	g_pmSweepData;
	Pm_t			*pm=&g_pmSweepData;
	uint8			freezeIndex;
	uint8			numFreezeImages=0;
	uint64_t		ffImageId=0;

	for (freezeIndex = 0; freezeIndex < pm_config.freeze_frame_images; freezeIndex ++) {
		if (pm->freezeFrames[freezeIndex] != PM_IMAGE_INDEX_INVALID) {
			numFreezeImages++;
		}
	}
	if (pm_config.shortTermHistory.enable && pm->ShortTermHistory.cachedComposite) {
		numFreezeImages++;
	}
	buffer[(*index)++]=numFreezeImages;
	if (numFreezeImages == 0) {
		return;
	}
	for (freezeIndex = 0; freezeIndex < pm_config.freeze_frame_images; freezeIndex ++) {
		if (pm->freezeFrames[freezeIndex] != PM_IMAGE_INDEX_INVALID) {
			ffImageId = BuildFreezeFrameImageId(pm, freezeIndex, 0 /*client id */);
			memcpy(&buffer[*index], &ffImageId, sizeof(uint64_t));
			*index += sizeof(uint64_t);
			IB_LOG_VERBOSE_FMT(__func__, "Appending freeze frame id =0x%lx", ffImageId);
		}
	}
	if (pm_config.shortTermHistory.enable && pm->ShortTermHistory.cachedComposite) {
		ffImageId = pm->ShortTermHistory.cachedComposite->header.common.imageIDs[0];
		memcpy(&buffer[*index], &ffImageId, sizeof(uint64_t));
		*index += sizeof(uint64_t);
		IB_LOG_VERBOSE_FMT(__func__, "Appending Hist freeze frame id =0x%lx", ffImageId);
	}
	return;
}

// return Master PM Sweep History image file data copied into memory pointed to by buffer 
int getPMHistFileData(char *filename, uint32_t histindex, uint8_t *buffer, uint32_t bufflen, uint32_t *filelen)
{
    uint32_t      index=0, nextByte = 0;
    FILE          *file;

    if (!buffer || !filelen) 
        return -1;

    if (filename[0] == '\0') {
        IB_LOG_VERBOSE_FMT(__func__, "Missing hist filename.");
        return -1;
    }
    file = fopen(filename, "r" );
    if (!file) {
        IB_LOG_ERROR("Error opening PA history image file! rc:",0x0020);
        return -1;
    }
    while((nextByte = fgetc(file)) != EOF) {
        buffer[index++] = nextByte;
        if (index >= bufflen) {
            *filelen = 0;
            IB_LOG_ERROR("PM Composite image file overrunning buffer! rc:",0x0020);
            fclose(file);
            return -1;
        }
    }
    *filelen = index;
    fclose(file);

    return 0;

}	// End of getPMHistFileData()

// return latest Master PM Sweep Image Data copied into memory pointed to by buffer 
int getPMSweepImageData(char *filename, uint32_t histindex, uint8_t isCompressed, uint8_t *buffer, uint32_t bufflen, uint32_t *filelen)
{
	uint32_t	index=0;

	if (!buffer || !filelen) 
		return -1;

	if (! PmEngineRunning()) {	// see if is already stopped/stopping
		return -1;
	}

	if (filename[0] != '\0') {
		return getPMHistFileData(filename, histindex, buffer, bufflen, filelen);
	}
	else { /* file name not specified */
		extern Pm_t g_pmSweepData;
		Pm_t	*pm=&g_pmSweepData;

		if (pm->history[histindex] == PM_IMAGE_INDEX_INVALID) {
			return -1;
		}
		if (pm->Image[pm->history[histindex]].state != PM_IMAGE_VALID) {
			IB_LOG_VERBOSE_FMT(__func__,"Invalid history index :0x%x", histindex);
			return -1;
		}
		IB_LOG_VERBOSE_FMT(__func__, "Going to send latest hist imageIndex=0x%x size=0x%x", histindex, computeCompositeSize());
		snprintf(filename, SMDBSYNCFILE_NAME_LEN, "%s/latest_sweep", pm->ShortTermHistory.filepath);
		writeImageToBuffer(pm, histindex, isCompressed, buffer, &index);
		appendFreezeFrameDetails(buffer, &index);
		*filelen = index;
	}
	return 0;
}	// End of getPMSweepImageData()

// copy latest PM Sweep Image Data received from Master PM to Standby PM.
int putPMSweepImageData(char *filename, uint8_t *buffer, uint32_t filelen)
{
	extern Pm_t g_pmSweepData;
	Pm_t		*pm = &g_pmSweepData;

	if (!buffer || !filename) {
		IB_LOG_VERBOSE_FMT(__func__, "Null buffer/file");
		return -1;
	}

	if (!pm_config.shortTermHistory.enable || !PmEngineRunning()) {	// see if is already stopped/stopping
		return -1;
	}

	return injectHistoryFile(pm, filename, buffer, filelen);

}	// End of putPMSweepImageData()

// Temporary declarations
FSTATUS compoundNewImage(Pm_t *pm);
boolean PmCompareHFIPort(PmPort_t *pmportp, char *groupName);
boolean PmCompareTCAPort(PmPort_t *pmportp, char *groupName);
boolean PmCompareSWPort(PmPort_t *pmportp, char *groupName);

extern void release_pmnode(Pm_t *pm, PmNode_t *pmnodep);
extern void free_pmportList(Pm_t *pm, PmNode_t *pmnodep);
extern void free_pmport(Pm_t *pm, PmPort_t *pmportp);
extern PmNode_t *get_pmnodep(Pm_t *pm, Guid_t guid, uint16_t lid);
extern PmPort_t *new_pmport(Pm_t *pm);
extern uint32 connect_neighbor(Pm_t *pm, PmPort_t *pmportp);

// Free Node List
void freeNodeList(Pm_t *pm, PmImage_t *pmimagep) {
	uint32_t	i;
	PmNode_t	*pmnodep;

	if (pmimagep->LidMap) {
		for (i = 0; i <= pmimagep->maxLid; i++) {
			pmnodep = pmimagep->LidMap[i];
			if (pmnodep) {
				release_pmnode(pm, pmnodep);
			}
		}
		vs_pool_free(&pm_pool, pmimagep->LidMap);
		pmimagep->LidMap = NULL;
	}
	return;

}	// End of freeNodeList()

// Find Group by name
static FSTATUS FindPmGroup(Pm_t *pm, PmGroup_t **pmgrouppp, PmGroup_t *sthgroupp) {
    FSTATUS		ret = FNOT_FOUND;
	uint32_t	i;

	if (!pmgrouppp) {
		ret = FINVALID_PARAMETER;
		goto exit;
	}

	*pmgrouppp = NULL;
	if (sthgroupp) {
		if (!pm->AllPorts) {
			ret = FINVALID_PARAMETER;	// Can't reference NULL data
			goto exit;
		}
		if (!strcmp(sthgroupp->Name, pm->AllPorts->Name)) {
			*pmgrouppp = pm->AllPorts;
			ret = FSUCCESS;
			goto exit;
		}
		for (i = 0; i < pm->NumGroups; i++) {
			if (!pm->Groups[i]) {
				ret = FINVALID_PARAMETER;	// Can't reference NULL data
				goto exit;
			}
			if (!strcmp(sthgroupp->Name, pm->Groups[i]->Name)) {
				*pmgrouppp = pm->Groups[i];
				ret = FSUCCESS;
				goto exit;
			}
		}
	}

exit:
	return ret;

}	// End of FindPmGroup()

// Find VF by name
static FSTATUS FindPmVF(Pm_t *pm, PmVF_t **pmvfpp, PmVF_t *sthvfp) {
    FSTATUS		ret = FNOT_FOUND;
	uint32_t	i;

	if (!pmvfpp) {
		ret = FINVALID_PARAMETER;
		goto exit;
	}

	*pmvfpp = NULL;
	if (sthvfp) {
		for (i = 0; i < pm->numVFs; i++) {
			if (!pm->VFs[i]) {
				ret = FINVALID_PARAMETER;	// Can't reference NULL data
				goto exit;
			}
			if (!strcmp(sthvfp->Name, pm->VFs[i]->Name)) {
				*pmvfpp = pm->VFs[i];
				ret = FSUCCESS;
				goto exit;
			}
		}
	}
exit:
	return ret;

}	// End of FindPmVF()

// Copy Short Term History Port to PmImage Port
FSTATUS CopyPortToPmImage(Pm_t *pm, PmNode_t *pmnodep, PmPort_t **pmportpp, PmPort_t *sthportp) {
    FSTATUS			ret = FSUCCESS;
	uint32_t		i, j;
	PmPort_t		*pmportp = NULL;
	PmPortImage_t	*pmportimgp;
	PmPortImage_t	*sthportimgp;

	if (!pmnodep || !pmportpp) {
		return FINVALID_PARAMETER;
	}

	if (!sthportp || (!sthportp->guid && !sthportp->portNum)) {
		// No port to copy
		goto exit;
	}

	pmportp = *pmportpp;

	// Allocate port
	if (!pmportp) {
		pmportp = new_pmport(pm);
		if (!pmportp) goto exit;

		pmportp->guid = sthportp->guid;
		pmportp->portNum = sthportp->portNum;
		pmportp->capmask = sthportp->capmask;
		pmportp->pmnodep = pmnodep;
	}
	pmportp->u.AsReg8 = sthportp->u.AsReg8;
	pmportp->neighbor_lid = sthportp->neighbor_lid;
	pmportp->neighbor_portNum = sthportp->neighbor_portNum;
	pmportp->groupWarnings = sthportp->groupWarnings;
	pmportp->StlPortCountersTotal = sthportp->StlPortCountersTotal;
	for (i = 0; i < MAX_PM_VLS; i++)
		pmportp->StlVLPortCountersTotal[i] = sthportp->StlVLPortCountersTotal[i];

	// Don't Copy port counters. Reconstituted image doesn't contain counters.
	
	// Copy port image
	pmportimgp = &pmportp->Image[pm->SweepIndex];
	sthportimgp = &sthportp->Image[0];
	*pmportimgp = *sthportimgp;

	// Connect neighbors later
	pmportimgp->neighbor = NULL;

	// Copy port image port groups
	memset(&pmportimgp->Groups, 0, sizeof(PmGroup_t *) * PM_MAX_GROUPS_PER_PORT);
#if PM_COMPRESS_GROUPS
	pmportimgp->u.s.InGroups = 0;
	for (j = 0, i=0; i < sthportimgp->u.s.InGroups; i++)
#else
	for (j = 0, i=0; i<PM_MAX_GROUPS_PER_PORT; i++)
#endif
	{
		if (!sthportimgp->Groups[i]) continue;
		ret = FindPmGroup(pm, &pmportimgp->Groups[j], sthportimgp->Groups[i]);
		if (ret == FSUCCESS) {
			j++;
		} else if (ret == FNOT_FOUND) {
			IB_LOG_ERROR_FMT(__func__, "Port group not found: %s", sthportimgp->Groups[i]->Name);
			ret = FSUCCESS;
		} else {
			IB_LOG_ERROR_FMT(__func__, "Error in Port Image Group:%d", ret);
			goto exit_dealloc;
		}
	}
#if PM_COMPRESS_GROUPS
	pmportimgp->u.s.InGroups = j;
#endif

	// Copy port image VF groups
	memset(&pmportimgp->vfvlmap, 0, sizeof(vfmap_t) * MAX_VFABRICS);
	for (j = 0, i = 0; i < sthportimgp->numVFs; i++) {
		if (!sthportimgp->vfvlmap[i].pVF) continue;
		ret = FindPmVF(pm, &pmportimgp->vfvlmap[j].pVF, sthportimgp->vfvlmap[i].pVF);
		if (ret == FSUCCESS) {
			pmportimgp->vfvlmap[j].vl = sthportimgp->vfvlmap[i].vl;
			j++;
		} else if (ret == FNOT_FOUND) {
			IB_LOG_ERROR_FMT(__func__, "Virtual Fabric not found: %s", sthportimgp->vfvlmap[i].pVF->Name);
			ret = FSUCCESS;
		} else {
			IB_LOG_ERROR_FMT(__func__, "Error in Port Image VF:%d", ret);
			goto exit_dealloc;
		}
	}

	goto exit;

exit_dealloc:
	free_pmport(pm, pmportp);
	pmportp = NULL;

exit:
	// Update *pmportpp
	*pmportpp = pmportp;
	return ret;

}	// End of CopyPortToPmImage()

// Copy Short Term History Node to PmImage Node
FSTATUS CopyNodeToPmImage(Pm_t *pm, uint16_t lid, PmNode_t **pmnodepp, PmNode_t *sthnodep) {
    FSTATUS		ret = FSUCCESS;
	Status_t	rc;
	uint32_t	i;
	PmNode_t	*pmnodep;
	Guid_t		guid;

	if (!pmnodepp) {
		ret = FINVALID_PARAMETER;
		goto exit;
	}

	if (!sthnodep) {
		// No node to copy
		goto exit;
	}

	if (sthnodep->nodeType == STL_NODE_SW)
		guid = sthnodep->up.swPorts[0]->guid;
	else
		guid = sthnodep->up.caPortp->guid;

	pmnodep = get_pmnodep(pm, guid, lid);
	if (pmnodep) {
		ASSERT(pmnodep->nodeType == sthnodep->nodeType);
		ASSERT(pmnodep->numPorts == sthnodep->numPorts);
	} else {
		// Allocate node
		rc = vs_pool_alloc(&pm_pool, pm->PmNodeSize, (void *)&pmnodep);
		*pmnodepp = pmnodep;
		if (rc != VSTATUS_OK || !pmnodep) {
			IB_LOG_ERRORRC( "Failed to allocate PM Node rc:", rc);
			ret = FINSUFFICIENT_MEMORY;
			goto exit;
		}
		MemoryClear(pmnodep, pm->PmNodeSize);
		AtomicWrite(&pmnodep->refCount, 1);
		pmnodep->nodeType = sthnodep->nodeType;
		pmnodep->numPorts = sthnodep->numPorts;
		pmnodep->guid = guid;
		cl_map_item_t *mi;
    	mi = cl_qmap_insert(&pm->AllNodes, guid, &pmnodep->AllNodesEntry);
   		if (mi != &pmnodep->AllNodesEntry) {
        		IB_LOG_ERRORLX("duplicate Node for portGuid", guid);
        		goto exit_dealloc;
    	}
	}
	pmnodep->nodeDesc = sthnodep->nodeDesc;
	pmnodep->changed_count = pm->SweepIndex;
	pmnodep->deviceRevision = sthnodep->deviceRevision;
	pmnodep->u = sthnodep->u;
	pmnodep->dlid = sthnodep->dlid;
	pmnodep->pkey = sthnodep->pkey;
	pmnodep->qpn = sthnodep->qpn;
	pmnodep->qkey = sthnodep->qkey;
	pmnodep->Image[pm->SweepIndex] = sthnodep->Image[0];

	if (sthnodep->nodeType == STL_NODE_SW) {
		if (!pmnodep->up.swPorts) {
			rc = vs_pool_alloc(&pm_pool, sizeof(PmPort_t *) * (pmnodep->numPorts + 1), (void *)&pmnodep->up.swPorts);
			if (rc != VSTATUS_OK || !pmnodep->up.swPorts) {
				IB_LOG_ERRORRC( "Failed to allocate Node Port List rc:", rc);
				ret = FINSUFFICIENT_MEMORY;
				goto exit_dealloc;
			}
			MemoryClear(pmnodep->up.swPorts, sizeof(PmPort_t *) * (pmnodep->numPorts + 1));
		}
		for (i = 0; i <= pmnodep->numPorts; i++) {
			ret = CopyPortToPmImage(pm, pmnodep, &pmnodep->up.swPorts[i], sthnodep ? sthnodep->up.swPorts[i] : NULL);
			if (ret != FSUCCESS) {
				IB_LOG_ERROR_FMT(__func__, "Error in Port Copy:%d", ret);
				goto exit_dealloc;
			}
		}
	} else {
		ret = CopyPortToPmImage(pm, pmnodep, &pmnodep->up.caPortp, sthnodep ? sthnodep->up.caPortp : NULL);
		if (ret != FSUCCESS) {
			IB_LOG_ERROR_FMT(__func__, "Error in Port Copy:%d", ret);
			goto exit_dealloc;
		}
	}

	// Update *pmnodepp
	*pmnodepp = pmnodep;

	goto exit;

exit_dealloc:
	if (pmnodep)
		release_pmnode(pm, pmnodep);
	*pmnodepp = NULL;

exit:
	return ret;

}	// End of CopyNodeToPmImage()

void set_neighbor(Pm_t *pm, PmPort_t *pmportp)
{
	PmPort_t *neighbor;
	PmPortImage_t *portImage = &pmportp->Image[pm->SweepIndex];

	if (portImage->u.s.Initialized && pmportp->neighbor_lid) {
		neighbor = pm_find_port(&pm->Image[pm->SweepIndex], pmportp->neighbor_lid, pmportp->neighbor_portNum);
		if (neighbor) {
			portImage->neighbor = neighbor;
		}
	}
}

// Copy Short Term History Image to PmImage
FSTATUS CopyToPmImage(Pm_t *pm, PmImage_t *pmimagep, PmImage_t *sthimagep) {
    FSTATUS		ret = FSUCCESS;
	Status_t	rc;
	uint32_t	i, j;
    Lock_t		orgImageLock;	// Lock image data (except state and imageId).

	if (!pmimagep || !sthimagep) {
		ret = FINVALID_PARAMETER;
		goto exit;
	}

	// retain PmImage lock 
    orgImageLock = pmimagep->imageLock;

	*pmimagep = *sthimagep;
	pmimagep->LidMap = NULL;
	rc = vs_pool_alloc(&pm_pool, sizeof(PmNode_t *) * (pmimagep->maxLid + 1), (void *)&pmimagep->LidMap);
	if (rc != VSTATUS_OK || !pmimagep->LidMap) {
		IB_LOG_ERRORRC( "Failed to allocate PM Lid Map rc:", rc);
		ret = FINSUFFICIENT_MEMORY;
		goto exit_dealloc;
	}
	MemoryClear(pmimagep->LidMap, sizeof(PmNode_t *) * (pmimagep->maxLid + 1));
	for (i = 1; i <= pmimagep->maxLid; i++) {
		ret = CopyNodeToPmImage(pm, i, &pmimagep->LidMap[i], sthimagep->LidMap[i]);
		if (ret != FSUCCESS) {
			IB_LOG_ERROR_FMT(__func__, "Error in Node Copy:%d", ret);
			goto exit_dealloc;
		}
	}
	for (i = 1; i <= pmimagep->maxLid; i++) {
		if (pmimagep->LidMap[i]) {
			if (pmimagep->LidMap[i]->nodeType == STL_NODE_SW) {
				for (j = 0; j <= pmimagep->LidMap[i]->numPorts; j++) {
					if (pmimagep->LidMap[i]->up.swPorts[j]) {
						set_neighbor(pm, pmimagep->LidMap[i]->up.swPorts[j]);
					}
				}
			} else {
				set_neighbor(pm, pmimagep->LidMap[i]->up.caPortp);
			}
		}
	}

	goto exit_unlock;

exit_dealloc:
	freeNodeList(pm, pmimagep);

exit_unlock:
	// restore PmImage lock 
    pmimagep->imageLock = orgImageLock;

exit:
	return ret;

}	// End of CopyToPmImage()

// Integrate ShortTermHistory.LoadedImage into PM RAM-resident image storage
FSTATUS PmReintegrate(Pm_t *pm, PmShortTermHistory_t *sth) {
    FSTATUS		ret = FSUCCESS;
	PmImage_t	*pmimagep;
	PmImage_t	*sthimagep;
	PmGroup_t	*pmgroupp;
	PmVF_t		*pmvfp;
	uint32_t	i;

	pmimagep = &pm->Image[pm->SweepIndex];
	sthimagep = sth->LoadedImage.img;

	// More image processing (from PmSweepAllPortCounters)
	pmimagep->FailedNodes = pmimagep->FailedPorts = 0;
	pmimagep->SkippedNodes = pmimagep->SkippedPorts = 0;
	pmimagep->UnexpectedClearPorts = 0;
	pmimagep->DowngradedPorts = 0;
//	(void)PmClearAllNodes(pm);

	freeNodeList(pm, pmimagep);	// Free old Node List (LidMap) if present

	// Copy *LoadedImage.AllGroup
	pm->AllPorts->Image[pm->SweepIndex] = sth->LoadedImage.AllGroup->Image[0];

	// Copy *LoadedImage.Groups[]
	for (i = 0; i < PM_MAX_GROUPS; i++) {
		if (sth->LoadedImage.Groups[i]) {
			ret = FindPmGroup(pm, &pmgroupp, sth->LoadedImage.Groups[i]);
			if (ret == FSUCCESS) {
				pmgroupp->Image[pm->SweepIndex] = sth->LoadedImage.Groups[i]->Image[0];
			}
		}
	}

	// Copy *LoadedImage.VFs[]
	for (i = 0; i < MAX_VFABRICS; i++) {
		if (sth->LoadedImage.VFs[i]) {
			ret = FindPmVF(pm, &pmvfp, sth->LoadedImage.VFs[i]);
			if (ret == FSUCCESS) {
				pmvfp->Image[pm->SweepIndex] = sth->LoadedImage.VFs[i]->Image[0];
			}
		}
	}

	// Copy *LoadedImage.img
	ret = CopyToPmImage(pm, pmimagep, sthimagep);
	if (ret != FSUCCESS) {
		IB_LOG_ERROR_FMT(__func__, "Error in Image Copy:%d", ret);
		goto exit;
	}

exit:
	return ret;

}	// End of PmReintegrate()

// copy latest PM RAM-Resident Sweep Image Data received from Master PM to Standby PM.
FSTATUS putPMSweepImageDataR(uint8_t *p_img_in, uint32_t len_img_in) {
    FSTATUS		ret = FSUCCESS;
	uint64		size;
	uint32		sweep_num;
	time_t		now_time;
	extern		Pm_t g_pmSweepData;
	Pm_t		*pm = &g_pmSweepData;
	PmShortTermHistory_t *sth = &pm->ShortTermHistory;
	PmImage_t	*pmimagep;
#ifndef __VXWORKS__
	Status_t	status;
#endif
	PmCompositeImage_t	*cimg_in = (PmCompositeImage_t *)p_img_in;
	PmCompositeImage_t	*cimg_out = NULL;
	unsigned char *p_decompress = NULL;
	unsigned char *bf_decompress = NULL;
	static time_t		firstImageSweepStart;
	static uint32		processedSweepNum=0;
	uint32				history_version;
	double				isTdelta;
	boolean				skipCompounding = FALSE;
	uint8		tempInstanceId;

    if (!p_img_in || !len_img_in)
        return FINVALID_PARAMETER;

	if (!PmEngineRunning()) {	// see if is already stopped/stopping
		return -1;
	}

	// check the version
	history_version = cimg_in->header.common.historyVersion;
	BSWAP_PM_HISTORY_VERSION(&history_version);
	if (history_version != PM_HISTORY_VERSION) {
		IB_LOG_INFO0("Received image buffer version does not match current version");
        return FINVALID_PARAMETER;
	}

	BSWAP_PM_FILE_HEADER(&cimg_in->header);
#ifndef __VXWORKS__
	// Decompress image if compressed
	if (cimg_in->header.common.isCompressed) { 
		status = vs_pool_alloc(&pm_pool, cimg_in->header.flatSize, (void *)&bf_decompress);
		if (status != VSTATUS_OK || !bf_decompress) {
			IB_LOG_ERRORRC("Unable to allocate flat buffer rc:", status);
			ret = FINSUFFICIENT_MEMORY;
			goto exit_free;
		}
		MemoryClear(bf_decompress, cimg_in->header.flatSize);
		// copy the header
		memcpy(bf_decompress, p_img_in, sizeof(PmFileHeader_t));
		// decompress the rest
		ret = decompressAndReassemble(p_img_in + sizeof(PmFileHeader_t),
									  len_img_in - sizeof(PmFileHeader_t),
									  cimg_in->header.numDivisions, 
									  cimg_in->header.divisionSizes, 
									  bf_decompress + sizeof(PmFileHeader_t), 
									  cimg_in->header.flatSize - sizeof(PmFileHeader_t));
		if (ret != FSUCCESS) {
			IB_LOG_ERROR0("Unable to decompress image buffer");
			goto exit_free;
		}
		p_decompress = bf_decompress;
	} else {
#endif
		p_decompress = p_img_in;
#ifndef __VXWORKS__
	}
#endif
	BSWAP_PM_COMPOSITE_IMAGE_FLAT((PmCompositeImage_t *)p_decompress, 0);

	// Rebuild composite
	//status = vs_pool_alloc(&pm_pool, sizeof(PmCompositeImage_t), (void *)&cimg_out);
	cimg_out = calloc(1,sizeof(PmCompositeImage_t));
	if (!cimg_out) {
		IB_LOG_ERROR0("Unable to allocate image buffer");
		ret = FINSUFFICIENT_MEMORY;
		goto exit_free;
	}

	MemoryClear(cimg_out, sizeof(PmCompositeImage_t));
	memcpy(cimg_out, p_decompress, sizeof(PmFileHeader_t));
	// Get sweepNum from image ID
	sweep_num = ((ImageId_t)cimg_out->header.common.imageIDs[0]).s.sweepNum;
	// Get the instance ID from the image ID
	tempInstanceId = ((ImageId_t)cimg_out->header.common.imageIDs[0]).s.instanceId;
	ret = rebuildComposite(cimg_out, p_decompress + sizeof(PmFileHeader_t));
	if (ret != FSUCCESS) {
		IB_LOG_ERRORRC("Error rebuilding PM Composite Image rc:", ret);
		goto exit_free;
	}

	(void)vs_wrlock(&pm->stateLock);
	pmimagep = &pm->Image[pm->SweepIndex];
	(void)vs_wrlock(&pmimagep->imageLock);

	// Reconstitute composite
	ret = PmReconstitute(sth, cimg_out);
	if (ret != FSUCCESS) {
		IB_LOG_ERRORRC("Error reconstituting PM Composite Image rc:", ret);
		goto exit_unlock;
	}

	if (isFirstImg) {
		firstImageSweepStart = sth->LoadedImage.img->sweepStart;
		IB_LOG_INFO_FMT(__func__, "First New Sweep Image Received...");
		processedSweepNum = sweep_num;
	}
	else {
		/* compare received image sweepStart time with that of first image. */
		isTdelta = difftime(firstImageSweepStart, sth->LoadedImage.img->sweepStart);
		if (isTdelta > 0) { /* firstImageSweepStart time is greater; i.e. received an older RAM image.*/
			skipCompounding = TRUE;
			IB_LOG_INFO_FMT( __func__, "TDelta = %f, Older Sweep Image Received. Skip processing.", isTdelta);
		}
		else {
			if ((sweep_num > processedSweepNum) || /* wrap around */(sweep_num == (processedSweepNum+1))) {	
				IB_LOG_INFO_FMT( __func__, "TDelta = %f, New Sweep Image Received. Processing..", isTdelta);
				processedSweepNum = sweep_num;
			}
			else {
				skipCompounding = TRUE;
				IB_LOG_INFO_FMT( __func__, "Same/older Sweep Image Received. Skip processing..");
			}
		}
	}

	// Reintegrate image into g_pmSweepData as a sweep image
	if (!skipCompounding) {
		ret = PmReintegrate(pm, sth);
		if (ret != FSUCCESS) {
			IB_LOG_ERRORRC("Error reintegrating PM Composite Image rc:", ret);
			goto exit_unlock;
		}
	}

	vs_stdtime_get(&now_time);

	if (!skipCompounding) {
		// Complete sweep image processing
		pm->LastSweepIndex = pm->SweepIndex;	// last valid sweep
		pm->lastHistoryIndex = (pm->lastHistoryIndex + 1) % pm_config.total_images;
		pm->history[pm->lastHistoryIndex] = pm->LastSweepIndex;
		pm->ShortTermHistory.currentInstanceId = tempInstanceId;
		pmimagep->historyIndex = pm->lastHistoryIndex;
		pmimagep->sweepNum = sweep_num;
		pmimagep->state = PM_IMAGE_VALID;

#ifndef __VXWORKS__
		if (pm_config.shortTermHistory.enable && !isFirstImg) {
			// compound the image that was just created into the current composite image
			IB_LOG_INFO_FMT(__func__, "compoundNewImage LastSweepIndex=%d, lastHistoryIndex=%d",pm->LastSweepIndex,pm->lastHistoryIndex);
			ret = compoundNewImage(pm);
			if (ret != FSUCCESS) {
				IB_LOG_WARNRC("Error while trying to compound new sweep image rc:", ret);
			}
		} else if (isFirstImg) {
			isFirstImg = FALSE;
		}

#endif

		// find next free Image to use, skip FreezeFrame images
		do {
			pm->SweepIndex = (pm->SweepIndex + 1) % pm_config.total_images;
			if (! pm->Image[pm->SweepIndex].ffRefCount)
				break;
			// check lease
			if (now_time > pm->Image[pm->SweepIndex].lastUsed &&
					now_time - pm->Image[pm->SweepIndex].lastUsed > pm_config.freeze_frame_lease) {
				pm->Image[pm->SweepIndex].ffRefCount = 0;
				// paAccess will clean up FreezeFrame on next access or FF Create
			}
			// skip past images which are frozen
		} while (pm->Image[pm->SweepIndex].ffRefCount);

		// Mark current image valid; mark next image in-progress
		pmimagep->state = PM_IMAGE_VALID;
		pm->Image[pm->SweepIndex].state = PM_IMAGE_INPROGRESS;
		// pm->NumSweeps follows image ID sweepNum; +1 anticipates becoming master before next image
		pm->NumSweeps = sweep_num + 1;

	}	// End of if (!skipCompounding)

	// Log sweep image statistics
	vs_pool_size(&pm_pool, &size);
	IB_LOG_INFO_FMT( __func__, "Image Received From Master: Sweep: %u Memory Used: %"PRIu64" MB (%"PRIu64" KB)",
		sweep_num, size / (1024*1024), size/1024 );
	// Additional debug output can be enabled
//	IB_LOG_VERBOSE_FMT( __func__, "... SweepIndex:%u LastSweepIndex:%u LastHistoryIndex:%u IsCompound:%u ImgSweN:%u swe_n:%u",
//		pm->SweepIndex, pm->LastSweepIndex, pm->lastHistoryIndex, !skipCompounding, pmimagep->sweepNum, sweep_num );
//	IB_LOG_VERBOSE_FMT( __func__, "... H[0]:%u H[1]:%u H[2]:%u H[3]:%u H[4]:%u H[5]:%u H[6]:%u H[7]:%u H[8]:%u H[9]:%u",
//		pm->history[0], pm->history[1], pm->history[2], pm->history[3], pm->history[4],
//		pm->history[5], pm->history[6], pm->history[7], pm->history[8], pm->history[9] );

exit_unlock:
	(void)vs_rwunlock(&pmimagep->imageLock);
	(void)vs_rwunlock(&pm->stateLock);
//    IB_LOG_DEBUG1_FMT(__func__, " EXIT-UNLOCK: ret:0x%X", ret);
// Fall-through to exit_free

exit_free:
	if (bf_decompress)
		vs_pool_free(&pm_pool, bf_decompress);
	if (cimg_out)
		PmFreeComposite(cimg_out);

	return ret;

}	// End of putPMSweepImageDataR()

