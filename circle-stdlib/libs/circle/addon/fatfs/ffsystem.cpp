/*------------------------------------------------------------------------*/
/* Sample code of OS dependent controls for FatFs                         */
/* (C)ChaN, 2014                                                          */
/* Implementation for Circle by R. Stange <rsta2@o2online.de>             */
/*------------------------------------------------------------------------*/


#include "ff.h"
#include <circle/spinlock.h>
#include <circle/alloc.h>
#include <circle/timer.h>
#include <assert.h>


#if FF_FS_REENTRANT
/*------------------------------------------------------------------------*/
/* Create a Synchronization Object                                        */
/*------------------------------------------------------------------------*/
/* This function is called in f_mount() function to create a new
/  synchronization object, such as semaphore and mutex. When a 0 is returned,
/  the f_mount() function fails with FR_INT_ERR.
*/

int ff_cre_syncobj (	/* 1:Function succeeded, 0:Could not create the sync object */
	BYTE vol,			/* Corresponding volume (logical drive number) */
	FF_SYNC_t *sobj		/* Pointer to return the created sync object */
)
{
	int ret;


	*sobj = new CSpinLock (TASK_LEVEL);		/* Circle */
	assert (*sobj != 0);
	ret = 1;

//	*sobj = CreateMutex(NULL, FALSE, NULL);		/* Win32 */
//	ret = (int)(*sobj != INVALID_HANDLE_VALUE);

//	*sobj = SyncObjects[vol];			/* uITRON (give a static sync object) */
//	ret = 1;							/* The initial value of the semaphore must be 1. */

//	*sobj = OSMutexCreate(0, &err);		/* uC/OS-II */
//	ret = (int)(err == OS_NO_ERR);

//	*sobj = xSemaphoreCreateMutex();	/* FreeRTOS */
//	ret = (int)(*sobj != NULL);

	return ret;
}



/*------------------------------------------------------------------------*/
/* Delete a Synchronization Object                                        */
/*------------------------------------------------------------------------*/
/* This function is called in f_mount() function to delete a synchronization
/  object that created with ff_cre_syncobj() function. When a 0 is returned,
/  the f_mount() function fails with FR_INT_ERR.
*/

int ff_del_syncobj (	/* 1:Function succeeded, 0:Could not delete due to any error */
	FF_SYNC_t sobj		/* Sync object tied to the logical drive to be deleted */
)
{
	int ret;


	CSpinLock *pSpinLock = (CSpinLock *) sobj;		/* Circle */
	delete pSpinLock;
	ret = 1;

//	ret = CloseHandle(sobj);	/* Win32 */

//	ret = 1;					/* uITRON (nothing to do) */

//	OSMutexDel(sobj, OS_DEL_ALWAYS, &err);	/* uC/OS-II */
//	ret = (int)(err == OS_NO_ERR);

//  vSemaphoreDelete(sobj);		/* FreeRTOS */
//	ret = 1;

	return ret;
}



/*------------------------------------------------------------------------*/
/* Request Grant to Access the Volume                                     */
/*------------------------------------------------------------------------*/
/* This function is called on entering file functions to lock the volume.
/  When a 0 is returned, the file function fails with FR_TIMEOUT.
*/

int ff_req_grant (	/* 1:Got a grant to access the volume, 0:Could not get a grant */
	FF_SYNC_t sobj	/* Sync object to wait */
)
{
	int ret;

	CSpinLock *pSpinLock = (CSpinLock *) sobj;		/* Circle */
	assert (pSpinLock != 0);
	pSpinLock->Acquire ();
	ret = 1;

//	ret = (int)(WaitForSingleObject(sobj, _FS_TIMEOUT) == WAIT_OBJECT_0);	/* Win32 */

//	ret = (int)(wai_sem(sobj) == E_OK);			/* uITRON */

//	OSMutexPend(sobj, _FS_TIMEOUT, &err));		/* uC/OS-II */
//	ret = (int)(err == OS_NO_ERR);

//	ret = (int)(xSemaphoreTake(sobj, _FS_TIMEOUT) == pdTRUE);	/* FreeRTOS */

	return ret;
}



/*------------------------------------------------------------------------*/
/* Release Grant to Access the Volume                                     */
/*------------------------------------------------------------------------*/
/* This function is called on leaving file functions to unlock the volume.
*/

void ff_rel_grant (
	FF_SYNC_t sobj	/* Sync object to be signaled */
)
{
	CSpinLock *pSpinLock = (CSpinLock *) sobj;	/* Circle */
	assert (pSpinLock != 0);
	pSpinLock->Release ();

//	ReleaseMutex(sobj);		/* Win32 */

//	sig_sem(sobj);			/* uITRON */

//	OSMutexPost(sobj);		/* uC/OS-II */

//	xSemaphoreGive(sobj);	/* FreeRTOS */
}

#endif




#if FF_USE_LFN == 3	/* LFN with a working buffer on the heap */
/*------------------------------------------------------------------------*/
/* Allocate a memory block                                                */
/*------------------------------------------------------------------------*/
/* If a NULL is returned, the file function fails with FR_NOT_ENOUGH_CORE.
*/

void* ff_memalloc (	/* Returns pointer to the allocated memory block */
	UINT msize		/* Number of bytes to allocate */
)
{
	return malloc(msize);	/* Allocate a new memory block with POSIX API */
}


/*------------------------------------------------------------------------*/
/* Free a memory block                                                    */
/*------------------------------------------------------------------------*/

void ff_memfree (
	void* mblock	/* Pointer to the memory block to free */
)
{
	free(mblock);	/* Discard the memory block with POSIX API */
}

#endif




#if !FF_FS_READONLY && !FF_FS_NORTC
/*------------------------------------------------------------------------*/
/* RTC function                                                           */
/*------------------------------------------------------------------------*/

DWORD get_fattime (void)
{
	unsigned nTime = CTimer::Get ()->GetLocalTime ();
	if (nTime == 0)
	{
		return 0;
	}

	unsigned nSecond = nTime % 60;
	nTime /= 60;
	unsigned nMinute = nTime % 60;
	nTime /= 60;
	unsigned nHour = nTime % 24;
	nTime /= 24;

	unsigned nYear = 1970;
	while (1)
	{
		unsigned nDaysOfYear = CTimer::IsLeapYear (nYear) ? 366 : 365;
		if (nTime < nDaysOfYear)
		{
			break;
		}

		nTime -= nDaysOfYear;
		nYear++;
	}

	if (nYear < 1980)
	{
		return 0;
	}

	unsigned nMonth = 0;
	while (1)
	{
		unsigned nDaysOfMonth = CTimer::GetDaysOfMonth (nMonth, nYear);
		if (nTime < nDaysOfMonth)
		{
			break;
		}

		nTime -= nDaysOfMonth;
		nMonth++;
	}

	unsigned nMonthDay = nTime + 1;

	unsigned nFATDate = (nYear-1980) << 9;
	nFATDate |= (nMonth+1) << 5;
	nFATDate |= nMonthDay;

	unsigned nFATTime = nHour << 11;
	nFATTime |= nMinute << 5;
	nFATTime |= nSecond / 2;

	return nFATDate << 16 | nFATTime;
}

#endif
