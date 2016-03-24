/*
 * Copyright (C) 2015  University of Oregon
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Apache License, as specified in the LICENSE file.
 *
 * For more information, see the LICENSE file.
 */
/* fifoObj.c  11.1 07/09/07 - Fifo Object Source Modules */
/* 
 */


#define _POSIX_SOURCE /* defined when source is to be POSIX-compliant */
#include <vxWorks.h>
#include <stdlib.h>
#include <vme.h>
#include <iv.h>
#include <semLib.h>
#include <rngLib.h>
#include "logMsgLib.h"
#include "rngXBlkLib.h"
#include "vmeIntrp.h"
#include "hardware.h"
#include "taskPrior.h"
#include "commondefs.h"
#include "fifoObj.h"


/*
modification history
--------------------
7-23-93,gmb  created 
*/

/*
DESCRIPTION

 MV147S-1 VMEbus short I/O Address: FFFF0000 - FFFFFFFF

 FIFO Short I/O Memory Map:

  FFFF0000 - FIFO base address

      0815 - Dram Contoller Enable 8-bit Control Register 
	     --------------------------------------------
	     Write anything to this register to enable 
	      hardwired information into DRAM controller. 
	      Must be done at power-up and allow 60ms for
              setup time prior to use.

      0811 - FIFO/STM 8-bit Control Register 
	     -------------------------------
             0 - State Machine reset (OPCLR~)
	     1 - FIFO reset (FFCLR~)
	     2 - APBus & HIgh Speed Lines reset (APHSCLR~)
	     3 - STM reset (STMCLR~)
	     4 - Start FIFO running (START)
	     5 - Start FIFO on Sync Signel (SYNCSTART)
	     6 - Sync Signal Select (SYNCSEL) 
	         { 0-lockgate, 1- external }
	     7 - UNDEFINED

      0813 - VME Interrupt 8-bit Enable Register
	     -------------------------------
             0 - FIFO started on Empty or Haltop
	     1 - FIFO Stopped
	     2 - Pre-Fifo Almost Full
	     3 - Pre-Fifo Empty 
	     4 - Software Programable 
	     5 - Data Error, The Number of points acquired 
		  does not match that requested.
	     6 - Max Transients Acquired
	     7 - Data Acquired, Ready for transfer to Host

      0817 - VME Interrupt 8-bit Mask Register (1 = masked)
	     -------------------------------
	      See above for bit to interrupt map
	     
      081A - FIFO 16-bit Status register 
	     ---------------------------
             0 - Stopped on Haltop (HALTSTOP~)
	     1 - FIFO Empty (FFMT~)
	     2 - FIFO started on Haltop (INITHALT~)
	     3 - FIFO started on Empty (RUN_MT~)
	     4 - Pre-FIFO Almost empty (PFF_AE~)
	     5 - Pre-FIFO Almost full (PFF_AF~)
	     6 - FIFO with a hardware loop (LOOPING~)
	     7 - FIFO running (RUNNING~)
	     8 - APbus Timed-Out (APTO~)
	     9 - Subsystem Failure {Power,Fan,OvrTemp} 
		 (ALARM)
	    10 - Sample is Locked (LKSENS) 
	    11 - Acquisition over-run has occurred 
		 (MISSME_LA)
		 See below for more complete discription.

	    12 - BD_REV0 -> Board 4-bit Revision Number
	    13 - BD_REV1 ---^^^
	    14 - BD_REV2 ----||
	    15 - BD_REV3 -----|

      081C - APBus 16-bit Status register 
	     ---------------------------
	     0-7 APbus Read Back 8-bit Value (APD[0:7])
	     8-11 APbus 4-bit Address (APA[0:3])
	    12 - APSI~
	    13 - APMN~
	    14 - APR/W~
	    15 - FF_APOP

      0820 - Pre-FIFO Write 32-bit Data Register 
	     ------------------------------------
             0-13 Time Base Count (max 0x00003FFF) (14-bits)
	    14-15 Time Base (3-bits)
	    16-23 High Speed Lines (8-bits)
	       24 CTC
	       25 Start Loop   25&26 = Loop Count
	       26 End Loop

Fifo Object Routines

   Interrupt Service Routines (ISR)

   There are 5 VME interrupts generated by the FIFO 
   In Order of priority, 1 being the highest priority
 
   1. FIFO started on Empty or Haltop: read status register 
      to determine which.
   2. FIFO Stopped : read status to determine if stopped on 
	             haltop (OK) or FIFO Underflow if not. 
   3. Pre-Fifo Almost Full: Time to stop stuffing the FIFO
   4. Pre-Fifo Empty: Time to start stuffing the FIFO
   5. Software Programable Interrupt

*/

static FIFO_OBJ *fifoObj;
static SEM_ID pSemFifoAFull;  /* cnting semphore given by ISR to trigger IST */
static SEM_ID pSemFifoEmpty;
static SEM_ID pSemFifoStop;
static SEM_ID pSemFifoStartErr;
static SEM_ID pSemProgrmItr;
static long  progItrpCnt;

static char *IDStr ="Fifo Object";
static int  IdCnt = 0;

static short timeToBlock;	/* Flag to indicate when to Block Stuffing FIFO */


/*-----------------------------------------------------------
|
|  Internal Functions
|
+---------------------------------------------------------*/
/***********************************************************
*
*  blockStuffing - Takes the FIFO blocking Semiphore, resulting in Blocking
*		   the Task
*
*  When its time to stop stuffing the fifo the pointer to the stuffing routine
*  is redirected (pointed) to this routine which take the stuffing semiphore
*  resulting in the task blocking.
*/

static int blockStuffing(FIFO_ID pFifoId,long code)
{
   /* Take semi thus blocking the stuffing of the FIFO */
   semTake(pFifoId->pSyncOk2Stuff,WAIT_FOREVER);
   fifoStuffIt(pFifoId, code);
   return( OK );
}

/*-------------------------------------------------------------
| Interrupt Service Tasks (IST) 
+--------------------------------------------------------------*/
/***********************************************************
*
* fifoAFull - Interrupt Service Task 
*
*  Action to take:
*   1. Unmask Almost Empty Interrupt 
*   2. Block stuffing any more into FIFO
*   3. Update fifoState, flush semaphore
*
*/
static VOID fifoAFull(FIFO_ID pFifoId)
{

   FOREVER
   {
     semTake(pSemFifoAFull,WAIT_FOREVER); /* wait here for interrupt */

     /* re-enable Almost Empty interrupt */
     *FF_REG(ITRMASK_CR,pFifoId->fifoBaseAddr) =  (pFifoId->vmeItrMask & ~PFF_AMEMPTY) & 0xff;


     /* LOGMSG(ALL_PORTS,OK,"PreFIFO IST:  Almost Full"); */

     semTake(pFifoId->pFifoMutex,WAIT_FOREVER); /*Mutual Exclusion Semiphore */

     pFifoId->fifoState = FIFO_ALMOST_FULL;
     pFifoId->vmeItrMask = (pFifoId->vmeItrMask & ~PFF_AMEMPTY) & 0xff;

     /* ptr to blocking stuffing function */
     /* pFifoId->pStuffIt = blockStuffing; */
     timeToBlock = TRUE;

     semGive(pFifoId->pFifoMutex);	/* give Mutex back */

     semFlush(pFifoId->pSemFifoStateChg);/* release any task Block on statchg for FIFO */
   }
}

/***********************************************************
*
* fifoEmptyIST - Interrupt Service Task 
*
* The Pre - FIFO is Empty,
*  Action to take:
*   1. Mask Almost Empty Interrupt, Unmasked in Full,Stop or Error Interrupts
*       This prevents the unecessary interrupts that would occur if the
*	high water mark of fifo word continued to rise above and drop below
*	the Almost Empty state while stuffing the fifo. 
*   2. Unblock stuffing, i.e. time to start putting words into FIFO 
*   3. Update fifoState, flush semaphore
*
*/
static VOID fifoEmptyIST(FIFO_ID pFifoId)
{

   void fifoStuffIt(FIFO_ID pFifoId, long code);

   FOREVER
   {
     semTake(pSemFifoEmpty,WAIT_FOREVER); /* wait here for interrupt */
     /* disable - Almost Empty interrupt */
     *FF_REG(ITRMASK_CR,pFifoId->fifoBaseAddr) = (pFifoId->vmeItrMask | PFF_AMEMPTY) & 0xff;


     /* LOGMSG(ALL_PORTS,OK,"PreFIFO IST:  Almost Empty"); */

     semTake(pFifoId->pFifoMutex,WAIT_FOREVER); /*Mutual Exclusion Semiphore */

     pFifoId->fifoState = FIFO_EMPTY;
     pFifoId->vmeItrMask = (pFifoId->vmeItrMask | PFF_AMEMPTY) & 0xff;
     /* pFifoId->pStuffIt = (PFI) fifoStuffIt; */
     timeToBlock = FALSE;

     semGive(pFifoId->pFifoMutex);	/* give Mutex back */

     /* give semi thus unblocking stuffing */
     semGive(pFifoId->pSyncOk2Stuff);	

     /* release any task Block on statchg for FIFO */
     semFlush(pFifoId->pSemFifoStateChg); 
   }
}

/***********************************************************
*
* fifoStopped - Interrupt Service Task 
*
* Actions to be taken:
* 1. Read FIFO status to determine condition of stop 
* 2. If FIFO Stopped on Haltop then:
*    A. read apbus register, incase this was an apbus read 
*	back operation
*    B. update fifoStatus, flush status semaphore.
* 2b.If FIFO Underflow occurred then:
*     A. Update fifoStatus
*     B. Update lastError  (FIFO_UNDERFLOW)
*     C. Block anymore going into FIFO
*     D. Reset FIFO 
*/
static VOID fifoStopped(FIFO_ID pFifoId)
{
   void fifo_reset(FIFO_ID pFifoId, int options);
   int fifostatus;
   int tmpApVal, tmpStat, tmpError;

   FOREVER
   {
      semTake(pSemFifoStop,WAIT_FOREVER); /* wait here for interrupt */

      LOGMSG(ALL_PORTS,OK,"FIFO IST:  Stopped");

      fifostatus = *FF_REGW(FF_SR, pFifoId->fifoBaseAddr); /* read status register */
      if (fifostatus & HALTSTOP )  /* stop on haltop */
      {
         tmpApVal = *FF_REGW(APB_SR, pFifoId->fifoBaseAddr);/* 16-bits msb=apvalue */
         tmpStat = FIFO_STOPPED;
      }
      else if (fifostatus & FFMT ) /* fifo empty and not halt stop */
      {					     /* i.e. stopped on empty */
         tmpApVal = NO_VALUE;
         tmpStat = FIFO_ERROR;
         tmpError = FIFO_UNDERFLOW;

         fifo_reset(pFifoId, FF_STATE | FF_CONTENTS | HSLnAPBUS );
      }

      semTake(pFifoId->pFifoMutex,WAIT_FOREVER); /*Mutual Exclusion Semiphore */

      pFifoId->fifoState = tmpStat;
      pFifoId->lastError = tmpError;
      pFifoId->apValue = tmpApVal;
      pFifoId->vmeItrMask = (pFifoId->vmeItrMask & ~PFF_AMEMPTY) & 0xff;
      /* pFifoId->pStuffIt = (PFI) fifoStuffIt; */
      timeToBlock = FALSE;

      if (tmpError == FIFO_UNDERFLOW)
      {
        /* pFifoId->pStuffIt = blockStuffing; /* block stuffing of fifo */
         timeToBlock = TRUE;
      }

      semGive(pFifoId->pFifoMutex);	/* give Mutex back */

     /* re-enable Almost Empty interrupt */
     *FF_REG(ITRMASK_CR,pFifoId->fifoBaseAddr) =  (pFifoId->vmeItrMask & ~PFF_AMEMPTY) & 0xff;

      semFlush(pFifoId->pSemFifoStateChg); /* release any task Block on statchg for FIFO */
   }
}

/***********************************************************
*
* fifoStartErr - Interrupt Service Task 
*
* Actions to be taken:
* 1. Read FIFO status, determine start on empty or halt
* 2. Update fifoStatus
* 3. Update lastError  (FIFO_START_ON_HALT, FIFO_START_ON_EMPTY)
* 4. Block anymore going into FIFO
* 5. Reset FIFO (ON_HALT - reset mode that does not empty FIFO)
*/

static VOID fifoStartErr(FIFO_ID pFifoId)
{

#define shows_start_on_halt 1
#define shows_start_on_empty 2


   int fifostatus;
   int tmpStat, tmpError;
   void fifo_reset(FIFO_ID pFifoId, int options);

   FOREVER
   {
      semTake(pSemFifoStartErr,WAIT_FOREVER); /* wait here for interrupt */

      LOGMSG(ALL_PORTS,OK,"FIFO IST:  Start Error");

      fifostatus = *FF_REGW(FF_SR, pFifoId->fifoBaseAddr); /* read status register */
      tmpStat = FIFO_ERROR;

      if (! (fifostatus & RUN_MT) )	/* Started on Empty */
      {
         tmpError = FIFO_START_ON_EMPTY;
         fifo_reset(pFifoId, FF_STATE | FF_CONTENTS | HSLnAPBUS );
         LOGMSG(ALL_PORTS,OK,"        :  Started on Empty, Reset FIFO");
      }
      else				/* started on Haltop */
      {
         tmpError = FIFO_START_ON_HALT;
         fifo_reset(pFifoId, FF_STATE | HSLnAPBUS );
         LOGMSG(ALL_PORTS,OK,"        :  Started on Haltop, Reset FIFO");
      }

      semTake(pFifoId->pFifoMutex,WAIT_FOREVER); /*Mutual Exclusion Semiphore */

      pFifoId->fifoState = tmpStat;
      pFifoId->lastError = tmpError;
      pFifoId->vmeItrMask = (pFifoId->vmeItrMask & ~PFF_AMEMPTY) & 0xff;

      if (tmpError == FIFO_START_ON_EMPTY)
      {
         /* pFifoId->pStuffIt = blockStuffing; /* block stuffing of fifo */
         timeToBlock = TRUE;
      }

      semGive(pFifoId->pFifoMutex);	/* give Mutex back */

     /* re-enable Almost Empty interrupt */
     *FF_REG(ITRMASK_CR,pFifoId->fifoBaseAddr) =  (pFifoId->vmeItrMask & ~PFF_AMEMPTY) & 0xff;

      semFlush(pFifoId->pSemFifoStateChg);/* release any task Block on statchg for FIFO */
   }
}


/***********************************************************
*
* fifoPgrmItr - Interrupt Service Task 
*
* Actions to be taken:
* 1. Increment counter 
* 2. Call function if none Zero Address
*/

static VOID fifoPgrmItr(FIFO_ID pFifoId)
{
   FOREVER
   {
      semTake(pSemProgrmItr,WAIT_FOREVER); /* wait here for interrupt */
     LOGMSG(ALL_PORTS,OK,"FLAG1 IST");
      progItrpCnt++;
      if (pFifoId->pUserFunc != NULL)
         (*(pFifoId->pUserFunc))(pFifoId->pUserArg);
   }
}

/***********************************************************
*
* fifoBufStuffer - To be a called once by a task (never to return)
*
* Actions to be taken:
* 1. Reads fifo blocking ring buffer 
* 2. Write fifio word into FIFO 
*/

fifoBufStuffer(FIFO_ID pFifoId)
/* FIFO_ID pFifoId;  pointer to fifo object */
{
   register RINGXBLK_ID pFifoWrdBuf;
   long rCode;
   pFifoWrdBuf = pFifoId->pFifoWordBuf;
   FOREVER
   {
    /* get Code, block if none */
    rngXBlkGet(pFifoWrdBuf, &rCode, 1 ); 
    if (timeToBlock)  /* if time to block take semiphore & block */
      semTake(pFifoId->pSyncOk2Stuff,WAIT_FOREVER);
    *FF_REGL(PF_WRITE, PFIFO_BASE_ADR) = (unsigned long) rCode;
   }
}

/*-------------------------------------------------------------
| FIFO Object Public Interfaces
+-------------------------------------------------------------*/

/**************************************************************
*
*  fifoCreate - create the Fifo Object Data Structure & Semiphores
*
*
* RETURNS:
* OK - if no error, NULL - if mallocing or semiphore creation failed
*
*/ 

FIFO_ID  fifoCreate(char* baseAddr, int vector, int level, char* idstr)
/* char* baseAddr - base address of FIFO */
/* int   vector  - VME Interrupt vector number */
/* int   level   - VME Interrupt level */
/* char* idstr - user indentifier string */
{
   static VOID fifoAFull();
   static VOID fifoEmptyIST();
   static VOID fifoStopped();
   static VOID fifoStartErr();
   static VOID fifoPgrmItr();

   void fifoStuffIt(FIFO_ID pFifoId, long code);
   void fifo_reset(FIFO_ID pFifoId, int options);

   char tmpstr[80];
   register FIFO_OBJ *pFifoObj;
   int tFAFid, tFMTid, tFSPid, tFSTid, tFPIid;
   short sr;


  /* ------- malloc space for FIFO Object --------- */
  if ( (pFifoObj = (FIFO_OBJ *) malloc( sizeof(FIFO_OBJ)) ) == NULL )
  {
    LOGMSG(ALL_PORTS,ERROR,"fifoCreate: Could not Allocate Space:");
    return(NULL);
  }

  /* zero out structure so we don't free something by mistake */
  memset(pFifoObj,0,sizeof(FIFO_OBJ));

  /* ------ Translate Bus address to CPU Board local address ----- */
  if (sysBusToLocalAdrs(FIFO_VME_ACCESS_TYPE,
                         baseAddr,&(pFifoObj->fifoBaseAddr)) == -1)
  {
    LOGMSG1(ALL_PORTS,OK,"fifoCreate: Can't Obtain Bus(0x%lx) to Local Address.",
		baseAddr);
    fifoDelete(pFifoObj);
    return(NULL);
  }

  /* ------ Test for Boards Presents ---------- */
  if ( vxMemProbe((char*) (pFifoObj->fifoBaseAddr + FF_SR), 
		     VX_READ, WORD, &sr) == ERROR)
  { 
    LOGMSG(ALL_PORTS,OK,"fifoCreate: Could not read STM's Status register\n");
    fifoDelete(pFifoObj);
    return(NULL);
  }
  else
  {
     pFifoObj->fifoBrdVersion = (sr >> 12) & 0x000f;
     LOGMSG1(ALL_PORTS,OK,"fifoCreate: FIFO Board Version %d present.\n",
		pFifoObj->fifoBrdVersion);
  }

  if ( (vector < MIN_VME_ITRP_VEC) || (vector > MAX_VME_ITRP_VEC) )
  {
     LOGMSG3(ALL_PORTS,OK,"fifoCreate: vector: 0x%x out of bounds (0x%x-0x%x)\n",
		vector,MIN_VME_ITRP_VEC,MAX_VME_ITRP_VEC);
     fifoDelete(pFifoObj);
     return(NULL);
  }
  else
  {
     pFifoObj->vmeItrVector = vector;
  }

  if ( (level >= MIN_VME_ITRP_LEVEL) && (level <= MAX_VME_ITRP_LEVEL) )
  {
    pFifoObj->vmeItrLevel = level;
  }
  else
  {
     LOGMSG3(ALL_PORTS,OK,"fifoCreate: vme level: %d out of bounds (%d-%d)\n",
	level,MIN_VME_ITRP_LEVEL,MAX_VME_ITRP_LEVEL);
     fifoDelete(pFifoObj);
     return(NULL);
  }


  /* ------ Create Id String ---------- */
  if (idstr == NULL) 
  {
     sprintf(tmpstr,"%s %d\n",IDStr,++IdCnt);
     pFifoObj->pIdStr = (char *) malloc(strlen(tmpstr)+2);
  }
  else
  {
     pFifoObj->pIdStr = (char *) malloc(strlen(idstr)+2);
  }

  if (pFifoObj->pIdStr == NULL)
  {
     fifoDelete(pFifoObj);
     LOGMSG(ALL_PORTS,ERROR,"fifoCreate: IdStr - Could not Allocate Space:");
     return(NULL);
  }

  if (idstr == NULL) 
  {
     strcpy(pFifoObj->pIdStr,tmpstr);
  }
  else
  {
     strcpy(pFifoObj->pIdStr,idstr);
  }


  progItrpCnt = 0L;
  timeToBlock = FALSE;

  pFifoObj->fifoState = OK;
  pFifoObj->lastError = -1;
  pFifoObj->apValue = 0;
  pFifoObj->pUserFunc = NULL;
  pFifoObj->pUserArg = 0;
  pFifoObj->pStuffIt = (PFI) fifoStuffIt;		
  pFifoObj->fifoState = sr;

  /* ----- reset board and get status register */
  fifo_reset(pFifoObj, BOARD);  /* reset board */
  pFifoObj->fifoState = *FF_REGW(FF_SR, pFifoObj->fifoBaseAddr); /* read status reg*/

  /*------ DIsable all interrupts on board -----------*/
  fifoItrpDisable(pFifoObj,FF_ALLITRPS);
  pFifoObj->vmeItrMask = FF_ALLITRPS & 0xff;

  /* ------- Create the Resource needed by the FIFO -------- */

  pFifoObj->pFifoWordBuf = rngXBlkCreate(FIFO_WORD_BUF_SIZE,"Fifo Code Buffer");

  pFifoObj->pSemFifoStateChg = semBCreate(SEM_Q_FIFO,SEM_EMPTY);

  pFifoObj->pSyncOk2Stuff = semBCreate(SEM_Q_FIFO,SEM_EMPTY);

  pFifoObj->pFifoMutex =  semMCreate(SEM_Q_PRIORITY | SEM_INVERSION_SAFE |
                                        SEM_DELETE_SAFE);

  pSemFifoAFull = semCCreate(SEM_Q_FIFO,SEM_EMPTY);
  pSemFifoEmpty = semCCreate(SEM_Q_FIFO,SEM_EMPTY);
  pSemFifoStop = semCCreate(SEM_Q_FIFO,SEM_EMPTY);
  pSemFifoStartErr = semCCreate(SEM_Q_FIFO,SEM_EMPTY);
  pSemProgrmItr = semCCreate(SEM_Q_FIFO,SEM_EMPTY);

  if ( (pFifoObj->pFifoWordBuf == NULL) || 
          (pFifoObj->pSemFifoStateChg == NULL) || 
          (pFifoObj->pSyncOk2Stuff == NULL) ||
          (pFifoObj->pFifoMutex == NULL) ||
          (pSemFifoAFull == NULL) ||
          (pSemFifoEmpty == NULL) ||
          (pSemFifoStop == NULL) ||
          (pSemFifoStartErr == NULL) ||
          (pSemProgrmItr == NULL) )
     {
        fifoDelete(pFifoObj);
	LOGMSG(ALL_PORTS,ERROR,"fifoCreate: Failed to allocate some resource:");
        return(NULL);
     }


  /* ------- Connect VME interrupt vector to proper Semiphore to Give ----- */

   if ( intConnect( INUM_TO_IVEC( FIFO_ALMOST_FULL_ITRP_VEC ),  
		     semGive, pSemFifoAFull) == ERROR)
   {
     LOGMSG(ALL_PORTS,ERROR,"fifoCreate: Could not connect FIFO FULL itrp vector: ");
     fifoDelete(pFifoObj);
     return(NULL);
   }

   if ( intConnect( INUM_TO_IVEC( FIFO_ALMOST_EMPTY_ITRP_VEC ), 
		    semGive, pSemFifoEmpty) == ERROR)
   {
     LOGMSG(ALL_PORTS,ERROR,"fifoCreate: Could not connect FIFO EMPTY itrp vector: ");
     fifoDelete(pFifoObj);
     return(NULL);
   }

   if ( intConnect( INUM_TO_IVEC( FIFO_STOP_ITRP_VEC ), 
		    semGive, pSemFifoStop) == ERROR)
   {
     LOGMSG(ALL_PORTS,ERROR,"fifoCreate: Could not connect FIFO STOP itrp vector: ");
     fifoDelete(pFifoObj);
     return(NULL);
   }

   if ( intConnect( INUM_TO_IVEC( FIFO_START_ERR_ITRP_VEC ),
		    semGive, pSemFifoStartErr) == ERROR)
   {
     LOGMSG(ALL_PORTS,ERROR,
	    "fifoCreate: Could not connect FIFO START ERROR itrp vector: ");
     fifoDelete(pFifoObj);
     return(NULL);
   }
   
   if ( intConnect( INUM_TO_IVEC( FIFO_PROG_ITRP ),
		    semGive, pSemProgrmItr) == ERROR)
   {
     LOGMSG(ALL_PORTS,ERROR,
	    "fifoCreate: Could not connect FIFO PGRM ITRP itrp vector: ");
     fifoDelete(pFifoObj);
     return(NULL);
   }

   /* ------- Spawn the Interrupt Service Tasks -------- */

     tFAFid = taskSpawn("tPFfull7", FIFO_FULL_IST_PRIORTY, FIFO_IST_TASK_OPTIONS,
		FIFO_IST_STACK_SIZE, fifoAFull, pFifoObj, ARG2,
		ARG3,ARG4,ARG5,ARG6,ARG7,ARG8,ARG9,ARG10);
     if ( tFAFid == ERROR)
     {
        LOGMSG(ALL_PORTS,ERROR,"fifoCreate: could not spawn tFFfull3:");
        fifoDelete(pFifoObj);
        return(NULL);
     }

     tFMTid = taskSpawn("tPFMT6", FIFO_MT_IST_PRIORTY, FIFO_IST_TASK_OPTIONS,
		FIFO_IST_STACK_SIZE, fifoEmptyIST, pFifoObj,ARG2,
		ARG3,ARG4,ARG5,ARG6,ARG7,ARG8,ARG9,ARG10);
     if ( tFMTid == ERROR)
     {
        LOGMSG(ALL_PORTS,ERROR,"fifoCreate: could not spawn tPFMT4:");
        fifoDelete(pFifoObj);
        return(NULL);
     }

     tFSPid = taskSpawn("tFFStp5", FIFO_STOP_IST_PRIORTY, FIFO_IST_TASK_OPTIONS,
		FIFO_IST_STACK_SIZE, fifoStopped, pFifoObj,ARG2,
		ARG3,ARG4,ARG5,ARG6,ARG7,ARG8,ARG9,ARG10);
     if ( tFSPid == ERROR)
     {
        LOGMSG(ALL_PORTS,ERROR,"fifoCreate: could not spawn tFFStp5:");
        fifoDelete(pFifoObj);
        return(NULL);
     }

     tFSTid = taskSpawn("tFFStE4", FIFO_STRT_ERR_IST_PRIORTY, FIFO_IST_TASK_OPTIONS,
		FIFO_IST_STACK_SIZE, fifoStartErr, pFifoObj,ARG2,
		ARG3,ARG4,ARG5,ARG6,ARG7,ARG8,ARG9,ARG10);
     if ( tFSTid == ERROR)
     {
        LOGMSG(ALL_PORTS,ERROR,"fifoCreate: could not spawn tFFStE6:");
        fifoDelete(pFifoObj);
        return(NULL);
     }

     tFPIid = taskSpawn("tFFPrgI3", FIFO_PRGITR_IST_PRIORTY, FIFO_IST_TASK_OPTIONS,
		FIFO_IST_STACK_SIZE, fifoPgrmItr, pFifoObj,ARG2,
		ARG3,ARG4,ARG5,ARG6,ARG7,ARG8,ARG9,ARG10);
     if ( tFPIid == ERROR)
     {
        LOGMSG(ALL_PORTS,ERROR,"fifoCreate: could not spawn tFFPrgI7:");
        fifoDelete(pFifoObj);
        return(NULL);
     }

     return( pFifoObj );
}


/**************************************************************
*
*  fifoDelete - Deletes FIFO Object and  all resources
*
*
* RETURNS:
*  OK or ERROR
*
*	Author Greg Brissey 10/1/93
*/
int fifoDelete(FIFO_ID pFifoId)
/* FIFO_ID 	pFifoId - fifo Object identifier */
{
   if (pFifoId != NULL)
   {

      if (pFifoId->pIdStr != NULL)
	 free(pFifoId->pIdStr);
      if (pFifoId->pFifoWordBuf != NULL)
	 rngXBlkDelete(pFifoId->pFifoWordBuf);
      if (pFifoId->pSemFifoStateChg != NULL)
         semDelete(pFifoId->pSemFifoStateChg);
      if (pFifoId->pSyncOk2Stuff != NULL)
         semDelete(pFifoId->pSyncOk2Stuff);
      if (pFifoId->pFifoMutex != NULL)
         semDelete(pFifoId->pFifoMutex);

      if (pSemFifoAFull != NULL)
         semDelete(pSemFifoAFull);
      if (pSemFifoEmpty != NULL)
         semDelete(pSemFifoEmpty);
      if (pSemFifoStop != NULL)
         semDelete(pSemFifoStop);
      if (pSemFifoStartErr != NULL)
         semDelete(pSemFifoStartErr);
      if (pSemProgrmItr != NULL)
         semDelete(pSemProgrmItr);

      free(pFifoId);
   }
}


/*
#define PFF_AMFULL 	0x80
#define PFF_AMEMPTY 	0x40
#define FF_STOPPED 	0x20
#define FF_START_ERR 	0x10
#define FF_PRGITRP 	0x8
#define STM_DATA_ERR	0x4
#define STM_MAX_TRANS	0x2
#define STM_DATA_RDY	0x1
*/

/**************************************************************
*
*  fifoItrpEnable - Set the Fifo Interrupt Mask
*
*  This routines set the VME interrupt mask of the FIFO. 
*
* RETURNS:
* void 
*
*/ 
void fifoItrpEnable(FIFO_ID pFifoId, int mask)
/* FIFO_ID 	pFifoId - fifo Object identifier */
/* int mask;	 mask of interrupts to enable */
{
   *FF_REG(ITRMASK_CR,pFifoId->fifoBaseAddr) =  (pFifoId->vmeItrMask & ~mask) & 0xff;
   pFifoId->vmeItrMask = (pFifoId->vmeItrMask & ~mask) & 0xff;
}

/**************************************************************
*
*  fifoItrpDisable - Set the Fifo Interrupt Mask
*
*  This routines set the VME interrupt mask of the FIFO. 
*
* RETURNS:
* void 
*
*/ 
void fifoItrpDisable(FIFO_ID pFifoId, int mask)
/* FIFO_ID 	pFifoId - fifo Object identifier */
/* int mask;	 mask of interrupts to disable */
{
   *FF_REG(ITRMASK_CR,pFifoId->fifoBaseAddr) = (pFifoId->vmeItrMask | mask) & 0xff;
   pFifoId->vmeItrMask = (pFifoId->vmeItrMask | mask) & 0xff;
}

/**************************************************************
*
*  getFifoState - Obtains the current Fifo Status
*
*  This routines Obtains the status of the FIFO via 3 different modes.
*
*   NO_WAIT - return the present value immediately.
*   WAIT_FOREVER - waits till the FIFO Status has changed 
*			and and returns this new value.
*   TIME_OUT - waits till the FIFO Status has changed or 
*		    the number of <secounds> has elasped 
*		    (timed out) before returning.
*
*   DIRECT_READ - read the status register directly
*	          Used in test software.
*
*  NOTE: The Task that calls this routine with 
*	 WAIT_FOREVER or TIME_OUT will block !!
*     
*
*
* RETURNS:
* Fifo state - if no error, TIME_OUT - if in TIME_OUT mode call timed out
*
*/ 

int getFifoState(FIFO_ID pFifoId, int mode, int secounds)
/* FIFO_ID 	pFifoId - fifo Object identifier */
/* int mode;	 mode of call, see above */
/* int secounds;    number of secounds to wait before timing out */
{
   int state;
   switch(mode)
   {
     case NO_WAIT:
          state = pFifoId->fifoState;
	  break;

     case WAIT_FOREVER: /* block if state has not changed */
	  semTake(pFifoId->pSemFifoStateChg, WAIT_FOREVER);  
          state = pFifoId->fifoState;
	  break;

     case TIME_OUT:     /* block if state has not changed, until timeout */
          if ( semTake(pFifoId->pSemFifoStateChg, (sysClkRateGet() * secounds) ) != OK )
	         state = TIME_OUT;
          else 
             state = pFifoId->fifoState;
          break;

     case DIRECT_READ:     /* Read the status register directly */
          state = *FF_REGW(FF_SR, pFifoId->fifoBaseAddr); /* read status register */
          break;
   }
   return(state & 0xff);
}

/**************************************************************
*
*  fifoGetApValue - Obtains the current Apbus Valus 
*
*
*  This routines Obtains the Apbus read back Vaule via 3 different modes.
*
*   NO_WAIT - return the present value immediately.
*   WAIT_FOREVER - waits till the FIFO Status has changed 
*		    and and returns this new value.
*   TIME_OUT - waits till the FIFO Status has changed or 
*		the number of <secounds> has elasped 
*		(timed out) before returning.
*
*   DIRECT_READ - read the status register directly
*	          Used in test software.
*
*  NOTE: The Task that calls this routine with 
*	 WAIT_FOREVER or TIME_OUT will block !!
*     
*
*     Modes:  NO_WAIT, WAIT_FOREVER, TIME_OUT
*
*
* RETURNS:
* apbus value - if no error, TIME_OUT - if in FIFO_TIME_OUT mode call timed out
*/ 
int fifoGetApValue(FIFO_ID pFifoId, int mode, int secounds)
/* FIFO_ID 	pFifoId - fifo Object identifier */
/* int mode;	 mode of call, see above */
/* int secounds; number of secounds to wait before timing out */
{
   int state;
   switch(mode)
   {
     case NO_WAIT:
          state = pFifoId->apValue;
	  break;

     case WAIT_FOREVER: /* block if state has not changed */
	      semTake(pFifoId->pSemFifoStateChg, WAIT_FOREVER);  
          state = pFifoId->apValue;
	  break;

     case TIME_OUT:     /* block if state has not changed, until timeout */
          if ( semTake(pFifoId->pSemFifoStateChg, (sysClkRateGet() * secounds) ) != OK )
	         state = TIME_OUT;
          else 
             state = pFifoId->apValue;
          break;

     case DIRECT_READ:     /* Read the APbus register directly */
          state = *FF_REGW(APB_SR, pFifoId->fifoBaseAddr) >> 8;/* 16-bits msb=apvalue */
          break;
   }
   return(state);
}

/**************************************************************
*
*  fifoStart - Start FIFO if not running 
*
*
* RETURNS:
*  OK - If FIFO was Started, ARRUNNING - If already running
* 
*   Question: if fifoState semiphore needs to flushed when 
*         fifo starts ?
*/

int fifoStart(FIFO_ID pFifoId)
/* FIFO_ID 	pFifoId - fifo Object identifier */
{
   int state;
   /* if not running start it, one should fifoGetStatus(WAIT_FOREVER) */
   if ( ((*FF_REGW(FF_SR, pFifoId->fifoBaseAddr)) & STOPPED) )
   {
       *FF_REG(FF_CR, pFifoId->fifoBaseAddr) = FFSTART;
   }

   /* started fifo, update status, should the semiphore be flushed ? */
   state = *FF_REGW(APB_SR, pFifoId->fifoBaseAddr);/* msb=apvalue */

   semTake(pFifoId->pFifoMutex,WAIT_FOREVER); /*Mutual Exclusion Semiphore */
    pFifoId->fifoState = state;
   semGive(pFifoId->pFifoMutex);	/* give Mutex back */
}

/**************************************************************
*
*  fifoStartSync - Start FIFO synchronize with signal selected
*
*
* RETURNS:
*  OK - If FIFO was Started, ARRUNNING - If already running
* 
*   Question: if fifoState semiphore needs to flushed when 
*         fifo starts ?
*/

int fifoStartSync(FIFO_ID pFifoId, int select)
/* pFifoId - fifo Object identifier */
/* select - select lockgate {0} or external signal to sync on */
{
   unsigned char selectopt;
   int state;
   if (select == 0)
   {
     selectopt = 0;
   }
   else
   {
     selectopt = SYNCSELECT;
   }

   /* if not running start it */
   if ( ((*FF_REG(FF_SR, pFifoId->fifoBaseAddr)) & STOPPED) )
   {
       *FF_REG(FF_CR, pFifoId->fifoBaseAddr) = FFSTART | selectopt;
   }

   /* started fifo, update status, should the semiphore be flushed ? */
   state = *FF_REGW(APB_SR, pFifoId->fifoBaseAddr);/* msb=apvalue */
   semTake(pFifoId->pFifoMutex,WAIT_FOREVER); /*Mutual Exclusion Semiphore */
    pFifoId->fifoState = state;
   semGive(pFifoId->pFifoMutex);	/* give Mutex back */
}

/**************************************************************
*
*  fifoReset - Resets combinations of FIFO functions 
*
*  Functions resetable - state machine, FIFO, APbus & High
*  			 Speed Lines
*
*	     Updates fifo Status, gives status change semiphore if state
*	     has changed.
* RETURNS:
* 
*/
void fifoReset(FIFO_ID pFifoId, int options)
/* pFifoId - fifo Object identifier */
/* options - those FIFO functions to be reset */
{
   int state;
   void fifo_reset(FIFO_ID pFifoId, int options);

   fifo_reset(pFifoId, options);

   state = *FF_REGW(APB_SR, pFifoId->fifoBaseAddr);/* msb=apvalue */
   if ( (pFifoId->fifoState & 0xff) != (state & 0xff) )
   {
      semTake(pFifoId->pFifoMutex,WAIT_FOREVER); /*Mutual Exclusion Semiphore */
      pFifoId->fifoState = state;
      /* pFifoId->pStuffIt = (PFI) fifoStuffIt; /* Is this the right place for this ? */
      timeToBlock = FALSE;	/* is this the right place for this ? */
      semGive(pFifoId->pFifoMutex);	/* give Mutex back */
      semFlush(pFifoId->pSemFifoStateChg);/* release any task Block on statchg for FIFO */
   }
}
/**************************************************************
*
*  fifo_reset - Resets combinations of FIFO functions 
*
*  Functions resetable - state machine, FIFO, APbus & High
*  			 Speed Lines
*
*   Note: Used by internal routines only, does not effect fifo Status
*	  and controlling semipores.
* RETURNS:
* 
*/
void fifo_reset(FIFO_ID pFifoId, int options)
/* pFifoId - fifo Object identifier */
/* options - those FIFO functions to be reset */
{
   *FF_REG(FF_CR, pFifoId->fifoBaseAddr) = (options);
}
/* see commondef.h for orred bits for reset conditions */


/**************************************************************
*
*  fifoStuffIt - Puts FIFO Code into FIFO Ring Buffer
*
*
* RETURNS:
* 
*/
void fifoStuffIt(FIFO_ID pFifoId, long code)
/* pFifoId - fifo Object identifier */
/* code - fifo code to be stuffed into FIFO */
{
/*
    *FF_REGL(PF_WRITE, pFifoId->fifoBaseAddr) = (unsigned long) code;
*/
/*    *FF_REGL(PF_WRITE, PFIFO_BASE_ADR) = (unsigned long) code; */
    rngXBlkPut(pFifoId->pFifoWordBuf, &code, 1 );
}

/* ------------------- Non Buffered Routines, Direct access ------------- */


/**************************************************************
*
*  fifoHaltop - Puts HALTOP into FIFO 
*
*
* RETURNS:
* 
*/
void fifoHaltop(FIFO_ID pFifoId)
/* pFifoId - fifo Object identifier */
{
/*
    *FF_REGL(PF_WRITE, pFifoId->fifoBaseAddr) = (long) HALT_OPCODE;
*/
    *FF_REGL(PF_WRITE, PFIFO_BASE_ADR) = (unsigned long) HALT_OPCODE;
}

/**************************************************************
*
*  fifoStuffCode - Puts Code into FIFO 
*
*
* RETURNS:
* 
*/
void fifoStuffCode(FIFO_ID pFifoId, long code)
/* pFifoId - fifo Object identifier */
/* code - fifo code to be stuffed into FIFO */
{
/*
    *FF_REGL(PF_WRITE, pFifoId->fifoBaseAddr) = (long) code;
    *FF_REGL(PF_WRITE, PFIFO_BASE_ADR) = (long) code;
*/
  /* (*pFifoId->pStuffIt)(pFifoId, code); */
  if (timeToBlock)  /* if time to block take semiphore & block */
     semTake(pFifoId->pSyncOk2Stuff,WAIT_FOREVER);
  *FF_REGL(PF_WRITE, PFIFO_BASE_ADR) = (unsigned long) code;
}

/**************************************************************
*
*  fifoStatReg - Gets FIFO status register value
*
*
* RETURNS:
*  16-bit FIFO/STM Status Register Value
*/
int fifoStatReg(FIFO_ID pFifoId)
/* pFifoId - fifo Object identifier */
{
    return (*FF_REGW(FF_SR, pFifoId->fifoBaseAddr) );
}

/**************************************************************
*
*  fifoRunning - Returns True if FIFO is running 
*
*
* RETURNS:
*  TRUE or FALSE
*/
int fifoRunning(FIFO_ID pFifoId)
/* pFifoId - fifo Object identifier */
{
   return (
    ( *FF_REGW(FF_SR, pFifoId->fifoBaseAddr) & STOPPED > 0 )
	  );
}

/**************************************************************
*
*  fifoEmpty - Returns True if FIFO is empty 
*
*
* RETURNS:
*  TRUE or FALSE
*/
int fifoEmpty(FIFO_ID pFifoId)
/* pFifoId - fifo Object identifier */
{
   return (
    ( *FF_REGW(FF_SR, pFifoId->fifoBaseAddr) & FFMT > 0 )
	  );
}

/**************************************************************
*
*  fifoPreAEmpty - Returns True if Pre-FIFO is almost empty 
*
*
* RETURNS:
*  TRUE or FALSE
*/
int fifoPreAEmpty(FIFO_ID pFifoId)
/* pFifoId - fifo Object identifier */
{
   return (
    ( *FF_REGW(FF_SR, pFifoId->fifoBaseAddr) & PFF_AE > 0 )
	  );
}

/**************************************************************
*
*  fifoPreAFull - Returns True if Pre-FIFO is almost full
*
*
* RETURNS:
*  TRUE or FALSE
*/
int fifoPreAFull(FIFO_ID pFifoId)
/* pFifoId - fifo Object identifier */
{
   return (
    ( *FF_REGW(FF_SR, pFifoId->fifoBaseAddr) & PFF_AF > 0 )
	  );
}

/**************************************************************
*
*  fifoWait4Stop - Returns when FIFO is not running
*
*
* RETURNS:
* 
*/
void fifoWait4Stop(FIFO_ID pFifoId)
/* pFifoId - fifo Object identifier */
{
   while (
    ( ! (*FF_REGW(FF_SR, pFifoId->fifoBaseAddr) & STOPPED) > 0 )
	  );
}


/* -----------------------------------------------------------------*/

/**************************************************************
*
*  fifoBlockStuff - Block Stuffing of FIFO 
*
*
* RETURNS:
* 
*/
void fifoBlockStuff(void) 
{
   /* DUMMY CODE */
}

/**************************************************************
*
*  fifoUnblockStuff - Unblocks Stuffing FIFO 
*
*
* RETURNS:
* 
*/
void fifoUnblockStuff(void) 
{
   /* DUMMY CODE */
}

/**************************************************************
*
*  initFifoLog - Initializes FIFO Logging facility 
*
*
* RETURNS:
* 
*/
int initFifoLog(int bufsize) 
/* bufsize - logging buffer size */
{
   /* DUMMY CODE */
}

/**************************************************************
*
*  fifoLogStart - Starts FIFO logging 
*
*
* RETURNS:
* 
*/
void fifoLogStart(void) 
{
   /* DUMMY CODE */
}

/**************************************************************
*
*  fifoLogStop - Stop FIFO logging 
*
*
* RETURNS:
* 
*/
void fifoLogStop(void) 
{
   /* DUMMY CODE */
}

/**************************************************************
*
*  fifoLogReset - Reset FIFO logging 
*
*
* RETURNS:
* 
*/
void fifoLogReset(void) 
{
   /* DUMMY CODE */
}



/********************************************************************
* fifoShow - display the status information on the FIFO Object
*
*  This routine display the status information of the FIFO Object
*
*
*  RETURN
*   VOID
*
*/
VOID fifoShow(FIFO_ID pFifoId, int level)
/* FIFO_ID pStmId  - FIFO Object ID */
/* int level 	   - level of information */
{
   int i;

   if (pFifoId == NULL)
   {
     printf("fifoShow: FIFO Object pointer is NULL.\n");
     return;
   }
   printf("\n\n-------------------------------------------------------------\n\n");
   printf("FIFO Object (0x%lx): %s\n",pFifoId->fifoBaseAddr, pFifoId->pIdStr);

   printf("Board Ver: %d,  VME: vector 0x%x, level %d\n",pFifoId->fifoBrdVersion,
		pFifoId->vmeItrVector, pFifoId->vmeItrLevel);

   printf("FIFO State: 0x%x, Last Error: %d, ApValue: %d(0x%x)\n",
		pFifoId->fifoState & 0xffff, pFifoId->lastError, pFifoId->apValue,
		pFifoId->apValue);

   printf("\nFIFO Code Buffer:\n");
   rngXBlkShow(pFifoId->pFifoWordBuf, 0);

   printf("\nFIFO State Semiphore: \n");
   semShow(pFifoId->pSemFifoStateChg,level);

   printf("\nFIFO Mutex Semiphore: \n");
   semShow(pFifoId->pFifoMutex,level);

   printf("\nFIFO Block Stuffing Semiphore: \n");
   semShow(pFifoId->pSyncOk2Stuff,level);
   return;
}