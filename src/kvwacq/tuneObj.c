/*
 * Copyright (C) 2015  University of Oregon
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Apache License, as specified in the LICENSE file.
 *
 * For more information, see the LICENSE file.
 */
/* 
 */
#define _POSIX_SOURCE /* defined when source is to be POSIX-compliant */
#include <vxWorks.h>
#include <stdlib.h>
#include <vme.h>
#include <iv.h>
#include <msgQLib.h>
#include <semLib.h>
#include "logMsgLib.h"
#include "vmeIntrp.h"
#include "hardware.h"
#include "timeconst.h"
#include "commondefs.h"
#include "fifoObj.h"
#include "tuneObj.h"
#include "hostAcqStructs.h"
#include "expDoneCodes.h"
#include "errorcodes.h"
#include "AParser.h"
#include "A_interp.h"
#include "config.h"

#ifndef DEBUG_TUNES
#define  DEBUG_TUNES 9
#endif DEBUG_TUNES

/*
modification history
--------------------
5-24-95,  created 
*/

#define MAX_RF_CHAN  6
#define TUNE_MAX_ATTN 79
#define AP_ADDR_UNKNOWN -1		
#define NOT_CONFIGURED	0x1ff		/* unknown board config value	*/
					/* inserted in status block	*/

/*
TUNE Object Routines
*/

/* extern MSG_Q_ID pMsgesToPHandlr; */

extern MSG_Q_ID pMsgesToHost;
extern FIFO_ID pTheFifoObject;
extern TUNE_ID  pTheTuneObject;

/* Configuration info */
extern struct 	_conf_msg {
	long	msg_type;
	struct	_hw_config hw_config;
} conf_msg;

static TUNE_OBJ *tuneObj;


/**************************************************************************
*
*  tuneCreate - create the TUNE Object Data Structure & Semaphore
*
*
* RETURNS:
* OK - if no error, NULL - if mallocing or semaphore creation failed
*
*/ 
TUNE_ID  tuneCreate(void)
{
   register TUNE_OBJ *pTuneObj;

  /* ------- malloc space for FIFO Object --------- */
  if ( (pTuneObj = (TUNE_OBJ *) malloc( sizeof(TUNE_OBJ)) ) == NULL )
  {
    errLogSysRet(LOGIT,debugInfo,"tuneCreate: Could not Allocate Space:");
    return(NULL);
  }

  /* zero out structure so we don't free something by mistake */
  memset(pTuneObj,0,sizeof(TUNE_OBJ));


/*  Create the access-FIFO semaphore full, so the first task that takes it gets it.  */

  pTuneObj->pSemAccessFIFO = semBCreate(SEM_Q_FIFO,SEM_FULL);
  pTuneObj->pSemOK2Tune = semBCreate(SEM_Q_FIFO,SEM_EMPTY);

  return( pTuneObj );
}


/**************************************************************************
*
*  tuneDelete - Deletes TUNE Object and  all resources
*
*
* RETURNS:
*  OK or ERROR
*
*/
int tuneDelete(TUNE_ID pTuneId)
/* TUNE_ID 	pTuneId - fifo Object identifier */
{
   if (pTuneId->pSemAccessFIFO != NULL)
     semDelete(pTuneId->pSemAccessFIFO);
   if (pTuneId->pSemOK2Tune != NULL)
     semDelete(pTuneId->pSemOK2Tune);

   if (pTuneId != NULL)
     free(pTuneId);
}

/*----------------------------------------------------------------------*/
/* tuneShwResrc							*/
/*     Show system resources used by Object (e.g. semaphores,etc.)	*/
/*	Useful to print then related back to WindView Events		*/
/*----------------------------------------------------------------------*/
void tuneShwResrc(TUNE_ID pTuneId, int indent)
{
   int i;
   char spaces[40];

   for (i=0;i<indent;i++) spaces[i] = ' ';
   spaces[i]='\0';

   printf("\n%sTune Obj: 0x%lx\n",spaces,pTuneId);
   printf("%s   Binary Sems: pSemAccessFIFO - 0x%lx\n",spaces,pTuneId->pSemAccessFIFO);
   printf("%s                pSemOK2Tune  --- 0x%lx\n",spaces,pTuneId->pSemOK2Tune);
}


/**************************************************************************
* tuneShow - display the status information on the TUNE Object
*
*  This routine display the status information of the TUNE Object
*
*
*  RETURN
*   VOID
*
*/
void tuneShow(TUNE_ID pTuneId, int level)
/* TUNE_ID pTuneId  - TUNE Object ID */
/* int level 	   - level of information */
{
   int i, j;
   char *pstr;
   unsigned short status;

   if (pTuneId == NULL)
   {
     printf("tuneShow: TUNE Object pointer is NULL.\n");
     return;
   }

   printf("\nTune Object Id: 0x%lx\n",pTuneId);
   printf("Tune FIFO Access Semaphore(0x%lx):\n",pTuneId->pSemAccessFIFO);
   printSemInfo(pTuneId->pSemAccessFIFO,"FIFO Access Semaphore",1);
   printf("Tune OK Semaphore(0x%lx):\n",pTuneId->pSemOK2Tune);
   printSemInfo(pTuneId->pSemOK2Tune,"Tune OK Semaphore",1);

   if (level > 0)
   {
     for (i=0; i<MAX_TUNE_CHAN; i++)
     {
	printf(" Channel: %d	Band: %d\n",i,pTuneId->tuneChannel[i].band);
	for (j=0; j<MAX_FREQ_CODES; j++)
	{
	   printf("	acode(%d): 0x%x\n",j,pTuneId->tuneChannel[i].ptsapb[j]);
	}
     }
     printf("\n-------------------------------------------------------------\n");
   }
   printf("\n");
}

/**************************************************************************
*	Put the system into tune mode.
*	Called from APint for swept tune.
*/
void tuneStart(int channel,	/* Set to the frequency of this channel.
				 * If not a valid channel #, use 1. */
	       int attn		/* Set to this attenuation level. If not a
				 * valid value, get attenuation level from
				 * thumbwheel. */
	       )
{
/* NO MERCURY
/*    if (channel < 1 || channel > MAX_TUNE_CHAN){
/*	channel = 1;
/*    }
/*    if (attn < 0 || attn > TUNE_MAX_ATTN){
/*	attn = ttw_to_attn(getTuneAttn()); /* Map to APbus value */
/*    }else{
/*	attn *= 2;
/*    }
/*    set_ttw_stored(0);		/* Force update at end of experiment */
/* NO MERCURY */
    start_tune(channel, attn);
}

/**************************************************************************
* readapaddr -
*	sends apword to fifo.						
*	Arguments: 							
*		apaddr 	: address of apbus location.			
*		apdelay : delay in 12.5 usec ticks to be sent to apbus	
*			  max is about 650 usecs			
*			  min is 500 ns (total delay 100ns is min) 	
*/
void readapaddr(ushort apaddr, ushort apdelay)
{
 ushort *shortfifoword;
 ulong_t fifoword;
 
 DPRINT1( 9, "readapaddr: 0x%x\n", apaddr);
 fifoStuffCmd(pTheFifoObject,CL_AP_BUS_SLCT,apaddr);
 fifoStuffCmd(pTheFifoObject,CL_AP_BUS_RD,apaddr);
}

/**************************************************************************
* wr_ApbusRegister -  originally created for reading tune switches by 
*                     writing value of 6 to address 0xb4b and reading
*                     back the switch value from the AP_RD_BK_FIFO
*/
wr_ApbusRegister(unsigned int addr, unsigned int value)
{
  unsigned long apval;
  int stat;

    init_fifo(NULL,0,0);
    writeapword(addr, value,AP_MIN_DELAY_CNT);
    fifoStuffCmd(pTheFifoObject,CL_DELAY,CNT1_USEC);	/* 1 usec delay */
    readapaddr(addr,AP_RD_MIN_DELAY_CNT);

    fifoStuffCmd(pTheFifoObject,HALTOP,0);  	/* xhaltop(0); */
    fifoStart(pTheFifoObject); 			/* ff_start(); */
    fifoWait4StopItrp(pTheFifoObject);
    stat=getapread((ushort)(addr),&apval);
    DPRINT3(3,"wr_ApbusRegister: tune reg.(0xb4b): 0x%x, chan: %d, attn: %d\n",
	 apval,(((apval & 0xf0)>>4) & 3), (apval & 0x0f));

    if (stat == AP_ADDR_UNKNOWN)
    {
      DPRINT(0,"wr_ApbusRegister: AP readback failed\n");
      return(stat);
    }
    
    return(apval);	/* return 8 bits (8 data) */
}

/**************************************************************************
* writeApbusRegister -
*
*/
writeApbusRegister(unsigned int addr, unsigned int value)
{
    unsigned int stat;

    DPRINT2(3,"writeApbusRegister: addr: 0x%x, value: %d\n",addr,value);
#ifdef XXX
    writeapwordStandAlone((ushort) addr,(ushort) value, 
		(ushort) AP_MIN_DELAY_CNT);
#endif
    writeapword(addr, value,AP_MIN_DELAY_CNT);
    fifoStuffCmd(pTheFifoObject,HALTOP,0);  	/* xhaltop(0); */
    fifoStart(pTheFifoObject); 			/* ff_start(); */
    fifoWait4StopItrp(pTheFifoObject);
    return(0);
}

/**************************************************************************
* readApbusRegister -
*
*/
readApbusRegister(unsigned int addr)
{
    unsigned long apval;
    int stat; 

    fifoStuffCmd(pTheFifoObject,CL_DELAY,8);	/* 100 nsec delay */
    readapaddr(addr,AP_RD_MIN_DELAY_CNT);
    fifoStuffCmd(pTheFifoObject,HALTOP,0);  	/* xhaltop(0); */
    fifoStart(pTheFifoObject); 			/* ff_start(); */
    fifoWait4StopItrp(pTheFifoObject);
    stat=getapread((ushort)(addr),&apval);
    if (stat == AP_ADDR_UNKNOWN)
    {
      DPRINT(0,"readApbusRegister: AP readback failed\n");
      return(stat);
    }
    return(apval);
}


static unsigned int tuneTaskDelay = 60;

/**************************************************************************
* getMlegTuneStatus -
*
*/
getMlegTuneStatus()
{
  unsigned int stat;
  stat =  wr_ApbusRegister(0xb4b, 6);
  DPRINT1(3,"getMlegTuneStatus: 0x%x\n",stat);
  return(stat & 0x1ff);
}


/**************************************************************************
* getTuneChan -
*
*/
getTuneChan()
{
  unsigned int stat, val;
  stat =  wr_ApbusRegister(0xb4b, 6);
  val = ((stat & 0xf0)>>4) ;
  DPRINT1(2,"getTuneChan():  Tune_Chan = %x\n", val);
  return(val);
}


/**************************************************************************
* getTuneAttn -
*
*/
getTuneAttn()
{
  unsigned int stat, val;
  stat =  wr_ApbusRegister(0xb4b, 6);
  val = stat & 0x0f ;
 /*  printf(" Tune_Attn = %x\n", val); */
  return(val);
}

/**************************************************************************/
/*  Programs for the Nessie Console to turn on/off tune mode              */

#define  OBSERVE        1
extern  int  get_acqstate();                  /*  in /sysvwacq/monitor.c  */
extern  int  update_acqstate(int);

TUNE_ID   pTheTuneObject;

/* Tunefreq tunefrq[MAX_TUNE_CHAN]; */

static int	numRFchan = MAX_RF_CHAN;
static int      stat_before_tune;
static int      coarse_attn[MAX_RF_CHAN];
static int      HSlines[MAX_RF_CHAN];
static int      APgates[MAX_RF_CHAN];
static int      Tx_mix_sel[MAX_RF_CHAN];
static int      fine_attn;
static long     saved_curqui;
static int      ch1_2_Amp_status;
static int      ch3_4_Amp_status;
static int      relay_rout_status;
static int      mag_leg_status;                         /* magnet leg preamp */
static int      preAmp_TR_status;
static int      Dec_mode;
static int      Rx_mixer_byt;
 
static int	saved_ttw_value;
static int      tune_stopped = 0;

/*  APSELECT and APWRITE are defined previously  */

/*
#define  APREAD  (0xf000)
#define  FIFOHALT  (0x7000)
#define  QDELAY  (0x1002)
#define  LDELAY  (0x2002)
*/

#define  AMP_IDLE  (0)



#define  AP_TIMEOUT	(0x1ff)


/**************************************************************************
*
*/
set_ttw_stored( int newvalue )
{
  saved_ttw_value = newvalue;
}

/**************************************************************************
* ttw_saved -
*/
ttw_saved()
{
  return( saved_ttw_value );
}

#define  EBIT_MASK      (0x80)
#define  CHAN_MASK      (0x70)
#define  ATTN_MASK      (0x0f)
#define  CHAN_SHIFT     (4)
#define  TUNE_TTW    1

/**************************************************************************
* ttw_changed - check to see is there any changes in tune thumbwheel
*/
ttw_changed( int  newvalue )
{
  if (newvalue != saved_ttw_value)
    return( 1 );
  else
    return( 0 );
}

/**************************************************************************
* ttw_to_attn - converts attenuation thumbwheel value to AP Bus Reg. Value
*
*/
ttw_to_attn( int  ttw_value )
{
  int   cur_attn;

  cur_attn = (ttw_value & ATTN_MASK);

  if (cur_attn > 7)             /* Thumbwheel to value */
    cur_attn = TUNE_MAX_ATTN;              /* 8, 9  =>  maximum */
  else if (cur_attn < 1)                /* Sanity check... not expected */
         cur_attn = 0;                  /* (unless thumbwheel is 0) */
       else
         cur_attn *= 10;                /* expected action here */

  cur_attn *= 2;                        /* convert to AP Bus Register value */

  return( cur_attn );
}


/**************************************************************************
* check_tune_info - Check to see if the tune infos are made
*                                                available in tune struct
*/
check_tune_info(int channel)
{
        int     n_ap_word_out;
        if (channel < OBSERVE || channel > MAX_TUNE_CHAN)
          return( 0 );
        channel--;            /* convert from TODEV, etc. to C-index */
        n_ap_word_out = pTheTuneObject->tuneChannel[ channel ].ptsapb[ 0 ];
        if (n_ap_word_out > 0 && n_ap_word_out < 100)
          return( 131071 );
        else
          return( 0 );
}


/**************************************************************************
* send_tunefreq - copied from send_tfrq, using std calls.
*
*/
send_tunefreq( int  channel )
{                                /* TODEV, DODEV, etc. (1, 2, etc.)  */
   ushort  *ap_pointer, *temp;
   ushort  *orig_ap_pointer;
   int     band;
 
  channel--;                    /* convert from TODEV, etc. to C-index */
  orig_ap_pointer = ap_pointer;
  ap_pointer = &pTheTuneObject->tuneChannel[channel].ptsapb[0];
 
/* next clear the LO relay so chan1 LO is used, and set mixer relay to hi/lo */
/* since the mixer switches with the pre-amp select (pin 7 on mixer box)     */  
/* *NOT* with mixer select line (pin 6) on mixer box we really switch the    */
/* preamp here */
 
  band = ( Rx_mixer_byt & 0xe ) | (pTheTuneObject->tuneChannel[ channel ].band );
 
  /* Make sure deuterium shimming bit is off */
  band = band & 0xfd;

  temp = apbcout(ap_pointer);     /* apbio(); */
 
  if(pTheTuneObject->tuneChannel[ channel ].band == 0)
            /* set the mixer select bit on the transmitter */
     writeapword(0xb91,  0x80, AP_MIN_DELAY_CNT);
  else   
     writeapword(0xb91,  0x00, AP_MIN_DELAY_CNT);
            /*   To select Tx mixer    1 ==> Hi band
                                       0 ==> Low band      */
 
  writeapword(0xb49, (band & 0xff),AP_MIN_DELAY_CNT);
            /* This signal is the preAmp select 0 ==> Hi band
                                                1 ==> Low band, it is different
               from the Tx mixer select above . The preAmp has nothing to do
               with tunning. But, this signal also acts as mixer_select, because
               in the mixer box there is a junmper (J2) tights preAmp_select and
               mixer_select together  */
 
  fifoStuffCmd(pTheFifoObject,HALTOP,0);        /* xhaltop(0); */
            /* taskDelay( 1 );  */
  fifoStart(pTheFifoObject);                    /* ff_start(); */
  fifoWait4Stop(pTheFifoObject);
 
  tune_stopped = 0;
  return( 0 );
} /* end of send_tunefreq() */


/**************************************************************************
* upd_tune - Check the tune switches status, then decides from there
*        do: -nothing          if channel switch stayed at "0"
*            -start_tune()     if channel switch is different from "0"
*            -send_tunefreq()  if channel switch is changed between 1-7 and 9
*            -stop_tune()      if channel switch is changed back to "0" or "8"
*            -changed in attenuation only   if attn. switch is only the change
*                                                                  during tuning
*/
int
upd_tune( int cur_ttw_value )
{
  int   old_ttw_value;   /* there is a global reg. saved_ttw_value */
  int   cur_tune_chan, old_tune_chan;
  int   cur_tune_attn, old_tune_attn;
  int   attn_value;

  if (cur_ttw_value == 0xff || cur_ttw_value == AP_TIMEOUT)
    return( 0 );
  if (ttw_changed( cur_ttw_value ) == 0)    /*  if changes => go on   */
    return( 0 );                            /*         not => return  */
 
  old_ttw_value = saved_ttw_value;  /* old_ttw_value is from previous upd_tune call */
  saved_ttw_value = cur_ttw_value;  /* save thumbwheel value for next upd_tune call */
                                    /* if there is any changes on tune thumbwheel   */
                                    /* saved_ttw_value is a global parameter        */

  old_tune_chan = (old_ttw_value & CHAN_MASK) >> CHAN_SHIFT;
  cur_tune_chan = (cur_ttw_value & CHAN_MASK) >> CHAN_SHIFT;
 
  old_tune_attn = old_ttw_value & ATTN_MASK;
  cur_tune_attn = cur_ttw_value & ATTN_MASK;
 

  if ((tune_stopped == 1) && (check_tune_info( cur_tune_chan ) == 0))
     return(0);

  if ((cur_tune_chan != 0) && (check_tune_info( cur_tune_chan ) == 0))
  {
      stop_tune();
      printf("No channel %d TUNE INFOS \n", cur_tune_chan);
      return( 0 );              /* monitorTune() will call upd_tune() again */
  }

  if (old_tune_chan != cur_tune_chan)
  {
    if (old_tune_chan == 0)        /* start tuning */
    {
      attn_value = ttw_to_attn( cur_ttw_value );
      start_tune( cur_tune_chan, attn_value );
    }
    else if (cur_tune_chan == 0)   /* done with tuning */
           stop_tune();
         else if (tune_stopped == 1)    /* stop_tune had been called previously */
              {  
                attn_value = ttw_to_attn( cur_ttw_value );
                start_tune( cur_tune_chan, attn_value );
              } 
              else                             /* already on tune-mode, send new freq. only */
                send_tunefreq( cur_tune_chan );
  }
          /* Change in "attenuation only" during tune-mode */
  if ( old_tune_attn != cur_tune_attn )
  {
    int  attn_value;
    if (cur_tune_chan != 0)            /*  Take action only if tune turned on.  */
    {
      attn_value = ttw_to_attn( cur_ttw_value );
      writeApbusRegister(0xb33, attn_value);
    }
  }
  return( cur_tune_chan == 0 );  /* cur_tune_chan=0 ==> return 1 -- no more upd_tune */
                                 /* cur_tune_chan!0 ==> return 0 -- repeating upd_tune */
                                 /*                                 by monitorTune()   */
}/* end of upd_tune */



/**************************************************************************
* start_tune -
*/
start_tune( int channel, int attn_value )
{
  int   ap_reg_addr, cur_arb, cur_mgl, ival;
  int   quickval;

   if ( (channel != 0) && (channel != 1) )
      return;			/* just don't do it */
   if ( attn_value > 160)
      attn_value = 160;

   if (channel == 0)		/* assume high band channel */
   {
      fifoClrHsl (pTheFifoObject, 0);          /* clear High Speed Lines */
      fifoStuffCmd(pTheFifoObject, 0, 0xaa20);
      fifoStuffCmd(pTheFifoObject, 0, 0xba80);
      fifoStuffCmd(pTheFifoObject, 0, 0x9a01); /* set locktune off (2kHz) */
      fifoStuffCmd(pTheFifoObject, 0, 0xaa18);
      fifoStuffCmd(pTheFifoObject, 0, 0xba50);
      fifoStuffCmd(pTheFifoObject, 0, 0x9a00 | attn_value);
      fifoStuffCmd(pTheFifoObject, 0, 0x9a07); /* some safe power level */
      fifoStuffCmd(pTheFifoObject, 0, 0xaa20);
      fifoStuffCmd(pTheFifoObject, 0, 0xba50);
      fifoStuffCmd(pTheFifoObject, 0, 0x9a15); /* hi band into tune */
      fifoMaskHsl (pTheFifoObject, 0, 0x80);   /* set HI-BND XMTR HSL */
      fifoStuffCmd(pTheFifoObject, 0, 0x0018);

   }
   else				/* assume low band channel */
   {
      fifoClrHsl (pTheFifoObject, 0);          /* clear High Speed Lines */
      fifoStuffCmd(pTheFifoObject, 0, 0xaa20);
      fifoStuffCmd(pTheFifoObject, 0, 0xba80);
      fifoStuffCmd(pTheFifoObject, 0, 0x9a01); /* set locktune off (2kHz) */
      fifoStuffCmd(pTheFifoObject, 0, 0xaa00);
      fifoStuffCmd(pTheFifoObject, 0, 0xba50);
      fifoStuffCmd(pTheFifoObject, 0, 0x9a00 | attn_value);
      fifoStuffCmd(pTheFifoObject, 0, 0x9a00); /* some safe power level */
      fifoStuffCmd(pTheFifoObject, 0, 0xaa20);
      fifoStuffCmd(pTheFifoObject, 0, 0xba50);
      fifoStuffCmd(pTheFifoObject, 0, 0x9a12); /* lo band into tune */
      fifoMaskHsl (pTheFifoObject, 0, 0x10);   /* set LOW-BND XMTR HSL */
      fifoStuffCmd(pTheFifoObject, 0, 0x0018);

   }

   stat_before_tune = get_acqstate();
   update_acqstate(ACQ_TUNING);
   tune_stopped = 0;
   return(0);
}/* end of start_tune */
 

 
/**************************************************************************
* stop_tune -
*/
stop_tune()
{
  int   ap_reg_addr, ival;
 
  send_tunefreq( OBSERVE );

  /*--------------------------------------------------------------------*/
  /* ZERO rf power on all channels while restoring registers. 		*/
  /* Init transmitters to default state.				*/
  /*--------------------------------------------------------------------*/
  for (ival=0; ival < numRFchan; ival++)
  {
    if ((ival < 4) || ((conf_msg.hw_config.xmtr_present>>ival) & 0x1))
    {
	if (ival < 4)
	   ap_reg_addr = 0xb33 + (ival * (-1)); /* counts backwards */
	else
	   ap_reg_addr = 0xb3b + ((ival-4) * (-1)); /* counts backwards */
	writeApbusRegister(ap_reg_addr, 0);
	ap_reg_addr = 0xb92 + (ival * 0x10);
	writeApbusRegister(ap_reg_addr, 0);
    }
  }

  if (conf_msg.hw_config.mleg_conf == MLEG_SIS_PIC)
  {
    /* Clear relay for SIS PIC tune */
    Rx_mixer_byt = Rx_mixer_byt & 0xfff7;
  }
 

  /*--------------------------------------------------------------------*/
  /* Restore console registers before returning from tune. 		*/
  /*--------------------------------------------------------------------*/
  for (ival=0; ival < numRFchan; ival++)
  {
    if ((ival < 4) || ((conf_msg.hw_config.xmtr_present>>ival) & 0x1))
    {
	ap_reg_addr = 0xb90 + (ival * 0x10);
	if (HSlines[ival] != NOT_CONFIGURED) 
	   writeApbusRegister(ap_reg_addr, HSlines[ival]);
 
	ap_reg_addr = 0xb92 + (ival * 0x10);
	if (APgates[ival] != NOT_CONFIGURED) 
	   writeApbusRegister(ap_reg_addr, APgates[ival]);

	ap_reg_addr = 0xb91 + (ival * 0x10);
	if (Tx_mix_sel[ival] != NOT_CONFIGURED) 
	   writeApbusRegister(ap_reg_addr, Tx_mix_sel[ival]);
    }
  }
  if (Rx_mixer_byt != NOT_CONFIGURED)
    writeApbusRegister(0xb49, Rx_mixer_byt);

  if (ch1_2_Amp_status != NOT_CONFIGURED)
    writeApbusRegister(0xb34, ch1_2_Amp_status);

  if (ch3_4_Amp_status != NOT_CONFIGURED)
    writeApbusRegister(0xb35, ch3_4_Amp_status);

  if (relay_rout_status != NOT_CONFIGURED)
    writeApbusRegister(0xb36, relay_rout_status);

  if (mag_leg_status != NOT_CONFIGURED)
    writeApbusRegister(0xb4a, mag_leg_status);

  if (preAmp_TR_status != NOT_CONFIGURED)
    writeApbusRegister(0xb48, preAmp_TR_status);

  /* fine_attn not checked since it is a combination of two reads */
    writeApbusRegister(0xb96, (fine_attn & 0xffff) );

  if (Dec_mode != NOT_CONFIGURED)
    writeApbusRegister(0xb9b, Dec_mode);

  /*--------------------------------------------------------------------*/
  /* Return from tune. 							*/
  /*--------------------------------------------------------------------*/
    writeApbusRegister(0xb4c, 0);      /* Set relay to observe */
    Tdelay(10);			/* delay ~ 100 ms */

  /*--------------------------------------------------------------------*/
  /* Restore Power values. 						*/
  /*--------------------------------------------------------------------*/
  for (ival=0; ival < numRFchan; ival++)
  {
    if ((ival < 4) || ((conf_msg.hw_config.xmtr_present>>ival) & 0x1))
    {
	if (ival < 4)
	   ap_reg_addr = 0xb33 + (ival * (-1)); /* counts backwards */
	else
	   ap_reg_addr = 0xb3b + ((ival-4) * (-1)); /* counts backwards */
	if (coarse_attn[ival] != NOT_CONFIGURED) 
	   writeApbusRegister(ap_reg_addr, coarse_attn[ival]);
    }
  }

  if (get_acqstate() == ACQ_TUNING)
      update_acqstate(stat_before_tune);
  tune_stopped = 1;
  return(0);
} /* end of stop_tune() */


#define  TUNE_TIMEOUT	5


/**************************************************************************
*
*/
void
monitorTune()
{
	int	cur_ttw_value, cur_tune_chan, ichan, ival, saved_acqstate, tunes_setup;
	long	tune_update_msge[ 4 ];

	if (pTheTuneObject == NULL)
	  return;

/*  Do not start until a tune channel has been setup.  */

	tunes_setup = 0;
	for (ichan = 1; ichan <= MAX_TUNE_CHAN; ichan++)
	  if (check_tune_info(ichan) != 0) {
		tunes_setup = 131071;
	  }
	if (tunes_setup == 0) {
		return;
	}

	semTake( pTheTuneObject->pSemAccessFIFO, WAIT_FOREVER );
	cur_ttw_value = getMlegTuneStatus();

	DPRINT1(2, "Mag Leg Tune Value: 0x%x\n",cur_ttw_value);
	if (cur_ttw_value == 0xff || cur_ttw_value == AP_TIMEOUT) {
		semGive( pTheTuneObject->pSemAccessFIFO );
		return;
	}
	cur_tune_chan = (cur_ttw_value & CHAN_MASK) >> CHAN_SHIFT;

	if (cur_tune_chan == 0) {
		semGive( pTheTuneObject->pSemAccessFIFO );
		return;
	}

	DPRINT1( DEBUG_TUNES, "Operator wants to tune channel %d\n", cur_tune_chan );
	tune_update_msge[ 0 ] = TUNE_UPDATE;
	tune_update_msge[ 1 ] = 0;
	msgQSend(pMsgesToHost, (char *) &tune_update_msge[ 0 ], sizeof( tune_update_msge ),
			 NO_WAIT, MSG_PRI_NORMAL);

	ival = semTake( pTheTuneObject->pSemOK2Tune, sysClkRateGet() * TUNE_TIMEOUT );
	DPRINT1(1, "taking the OK-to-tune semaphore returned %d\n", ival );
	if (ival != OK) {
		semGive( pTheTuneObject->pSemAccessFIFO );
		return;
	}

	saved_acqstate = get_acqstate( 0 );
	update_acqstate( ACQ_TUNING );
	for (;;) {
		ival = upd_tune( cur_ttw_value );
		DPRINT1( 1, "update tune returned %d\n", ival );
		if (ival != 0)
		  break;
		taskDelay( tuneTaskDelay );
		cur_ttw_value = getMlegTuneStatus();
	}

	update_acqstate( saved_acqstate );
	semGive( pTheTuneObject->pSemAccessFIFO );
}

#define  TUNE_CHECKS_PER_SEC	(1)


/**************************************************************************
*
*/
void
tunemonitor()
{

	tuneTaskDelay = sysClkRateGet() / TUNE_CHECKS_PER_SEC;
	if (tuneTaskDelay < 1)
	  tuneTaskDelay = 1;

	FOREVER {
		taskDelay( tuneTaskDelay );
		monitorTune();
	}
}

/**************************************************************************
*
*/
startTuneMonitor( int taskpriority, int taskoptions, int stacksize )
{
   void tunemonitor();

   if (taskNameToId("tTuneMon") == ERROR)
     taskSpawn("tTuneMon",taskpriority,taskoptions,stacksize,tunemonitor,
		1,2,3,4,5,6,7,8,9,10);
}
/**************************************************************************
*
*/
killTune()
{
   int tid;
   if ((tid = taskNameToId("tTuneMon")) != ERROR)
	taskDelete(tid);
}
