/***************************************************************************
                          fps.c  -  description
                             -------------------
    begin                : Sun Oct 28 2001
    copyright            : (C) 2001 by Pete Bernert
    email                : BlackDove@addcom.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version. See also the license.txt file for *
 *   additional informations.                                              *
 *                                                                         *
 ***************************************************************************/

//*************************************************************************//
// History of changes:
//
// 2007/10/27 - Pete
// - Added Nagisa's changes for SSSPSX as a special gpu config option
//
// 2005/04/15 - Pete
// - Changed user frame limit to floating point value
//
// 2003/07/30 - Pete
// - fixed frame limitation if "old skipping method" is used
//
// 2002/12/14 - Pete
// - improved skipping and added some skipping security code
//
// 2002/11/24 - Pete
// - added new frameskip func
//
// 2001/10/28 - Pete
// - generic cleanup for the Peops release
//
//*************************************************************************//

#include "../gpulib/stdafx.h"

#define _IN_FPS

#include "externals.h"
#include "oldGpuFps.h"
#include "gpu.h"

#include <stdbool.h>
#include "../Gamecube/DEBUG.h"
#include "../Gamecube/wiiSXconfig.h"
#include "../Gamecube/perf_prof.h"
double psxGetFps(void);         /* psxcounters.c: the vblank rate the core actually emulates (its header pulls in gctypes.h, which collides with stdafx.h BOOL) */

////////////////////////////////////////////////////////////////////////
// FPS stuff
////////////////////////////////////////////////////////////////////////

#include <unistd.h>

float          fFrameRateHz=0;
DWORD          dwFrameRateTicks=16;
float          fFrameRate;
int            iFrameLimit;
int            UseFrameLimit=0;
int            UseFrameSkip=0;

////////////////////////////////////////////////////////////////////////
// FPS skipping / limit
////////////////////////////////////////////////////////////////////////

static DWORD  dwLaceCnt = 0;
BOOL   bInitCap = TRUE;
static float  fps_skip = 0;
float  fps_cur  = 0;
extern char fpsInfo[32];

////////////////////////////////////////////////////////////////////////

#define MAXLACE 16

void OldGpuCheckFrameRate(void)
{
#ifdef PROFILE
 start_section(IDLE_SECTION);
#endif
 if(UseFrameSkip)                                      // skipping mode?
  {
    {
     dwLaceCnt++;                                      // -> store cnt of vsync between frames
     if(dwLaceCnt>=MAXLACE && UseFrameLimit)           // -> if there are many laces without screen toggling,
      {                                                //    do std frame limitation
       if(dwLaceCnt==MAXLACE) bInitCap=TRUE;

       FrameCap();
      }
    }
   calcfps();                                          // -> calc fps display in skipping mode
  }
 else                                                  // non-skipping mode:
  {
   if(UseFrameLimit) FrameCap();                       // -> do it
   if(showFPSonScreen == FPS_SHOW) calcfps();                // -> and calc fps display
  }
#ifdef PROFILE
	end_section(IDLE_SECTION);
#endif
}

////////////////////////////////////////////////////////////////////////

#define TIMEBASE 100000

// prototypes
 long long gettime(void);
 unsigned int diff_usec(long long start,long long end);

unsigned long timeGetTime()
{
 long long nowTick = gettime();
 return diff_usec(0,nowTick)/10;
}

extern int newDwFrameRateTicks;

/* The limiter keeps a schedule: each frame is due dwFrameRateTicks after the previous one
 * was DUE, not after it actually ended, so a frame that ran long leaves a debt and the
 * following frames run unthrottled until it is paid. The old code carried at most one
 * period of debt and dropped the rest, so after a disc-read stall of a few frames the
 * emulated clock stayed that far behind the wall clock for good; the sound output, which
 * is mixed at emulated rate, then stayed that much lower in its queue and the SPU had to
 * stretch time to refill it (the pull-back that broke CD-XA, see dfsound/ratectl.c).
 * The debt is bounded: the sound drivers keep about 125 ms in hand and paying back more
 * than that would only overfill them, and a long load stall should not turn into seconds
 * of fast-forward. A stall beyond the bound is dropped as before, minus the bound. */
#define FRAMECAP_MAX_DEBT_TICKS 1250    /* 100 us ticks: 125 ms (BUSY_TARGET_SAMPLES, CUBE_BUSY_BUFFERS) */

/* The period is the one the core emulates (psxcounters.c: 60 or 50 Hz, or the per-game
 * fractional rate), kept in 1/256 tick so it does not truncate. Until 2026-09-20 the auto
 * limiter used the PEOPS table's 59.8275 Hz rounded down to whole ticks (1671 = 59.84 Hz)
 * against a core that emulates 60.00 Hz vblanks: emulated time ran 0.26 % slower than the
 * wall clock by construction, and the sound output had to absorb that as a permanent
 * rate offset, more than half of the +-0.5 % the rate control has. The user-set limit
 * (iFrameLimit == 1) keeps its own rate. */
static unsigned long framecap_period256(void)
{
 double hz = fFrameRateHz;
 if (iFrameLimit == 2)
  {
   double emu = psxGetFps();
   if (emu > 1.0) hz = emu;
  }
 if (hz < 1.0) hz = 60.0;
 return (unsigned long)(TIMEBASE * 256.0 / hz + 0.5);
}

void FrameCap (void)
{
 static unsigned long due256 = 0;       /* the tick this frame may end at, in 1/256 tick */
 static int have_due = 0;
 unsigned long now = timeGetTime();
 unsigned long period256 = framecap_period256();
 unsigned long due;
 long late;

 if (!have_due) { due256 = (unsigned long)now << 8; have_due = 1; }
 due  = due256 >> 8;                    /* modulo 2^24 ticks: see the wrap note below */
 late = (long)((now - due) << 8) >> 8;  /* sign-extend a 24-bit tick difference */

 if (late > FRAMECAP_MAX_DEBT_TICKS)
  {
   /* a long stall, or a wrap of the tick counter: keep only the bounded debt */
   due256 = (unsigned long)(now - FRAMECAP_MAX_DEBT_TICKS) << 8;
   late = FRAMECAP_MAX_DEBT_TICKS;
   PERF_INC(limit_debt_drops);
  }
 else if (late < -(long)(2 * (period256 >> 8) + FRAMECAP_MAX_DEBT_TICKS))
  {
   /* far ahead of a schedule that cannot be right (clock went backwards): resync */
   due256 = (unsigned long)now << 8;
   late = 0;
  }
 /* due256 holds 24 bits of tick; the 100 us tick wraps them every 28 minutes, which the
  * masked, sign-extended differences above absorb like any other wrap. */
 due = due256 >> 8;
#ifdef PERF_PROF
 if (late > 0 && (unsigned long)late > g_perf.limit_debt_max) g_perf.limit_debt_max = late;
#endif

 if (late < 0)
    {
     /* This spin serves both the Old Soft and OpenGX plugins (via
      * OldGpuCheckFrameRate) and runs from the VBlank rcnt callback, i.e.
      * inside a CPU slice -- timed so the profile can tell "capped" from
      * "CPU-bound", which otherwise look identical. */
     unsigned long long limit_t0 = perf_now_ticks();
     /* A spin, not a sleep: usleep() for all but the last millisecond ran FF7 at 0.54x on the
      * bench Wii (2026-09-29, each wait about 56 ms instead of a few), so threads below this
      * one -- the Homebrew Channel's agent among them -- get no time while a game runs. */
     do { now = timeGetTime(); } while (((long)((now - due) << 8) >> 8) < 0);
     PERF_ADD(limit_ticks, perf_now_ticks() - limit_t0);
     PERF_INC(limit_calls);
#ifdef PERF_PROF
     g_perf.limit_target = period256 >> 8;
#endif
    }
 due256 += period256;                   /* the next frame is due one period after this one was */
}

////////////////////////////////////////////////////////////////////////

#define MAXSKIP 120

void FrameSkip(void)
{
 static int   iNumSkips=0;
 static DWORD dwLastLace=0;                            // helper var for frame limitation

 if(!dwLaceCnt) return;                                // important: if no updatelace happened, we ignore it completely

#ifdef PROFILE
 start_section(IDLE_SECTION);
#endif

 if(iNumSkips)                                         // we are in skipping mode?
  {
   dwLastLace+=dwLaceCnt;                              // -> calc frame limit helper (number of laces)
   bSkipNextFrame = TRUE;                              // -> we skip next frame
   iNumSkips--;                                        // -> ok, one done
  }
 else                                                  // ok, no additional skipping has to be done...
  {                                                    // we check now, if some limitation is needed, or a new skipping has to get started
   DWORD dwWaitTime;
   static DWORD curticks, lastticks, _ticks_since_last_update;

   if(bInitCap || bSkipNextFrame)                      // first time or we skipped before?
    {
     static int iAdditionalSkip=0;                     // number of additional frames to skip

     if(UseFrameLimit && !bInitCap)                    // frame limit wanted and not first time called?
      {
       DWORD dwT=_ticks_since_last_update;             // -> that's the time of the last drawn frame
       dwLastLace+=dwLaceCnt;                          // -> and that's the number of updatelace since the start of the last drawn frame

       curticks = timeGetTime();                       // -> now we calc the time of the last drawn frame + the time we spent skipping
       _ticks_since_last_update= dwT+curticks - lastticks;

       dwWaitTime=dwLastLace*dwFrameRateTicks;         // -> and now we calc the time the real psx would have needed

       if(_ticks_since_last_update<dwWaitTime)         // -> we were too fast?
        {
         if((dwWaitTime-_ticks_since_last_update)>     // -> some more security, to prevent
            (60*dwFrameRateTicks))                     //    wrong waiting times
          _ticks_since_last_update=dwWaitTime;

         while(_ticks_since_last_update<dwWaitTime)    // -> loop until we have reached the real psx time
          {                                            //    (that's the additional limitation, yup)
           curticks = timeGetTime();
           _ticks_since_last_update = dwT+curticks - lastticks;
          }
        }
       else                                            // we were still too slow ?!!?
        {
         if(iAdditionalSkip<MAXSKIP)                   // -> well, somewhen we really have to stop skipping on very slow systems
          {
           iAdditionalSkip++;                          // -> inc our watchdog var
           dwLaceCnt=0;                                // -> reset lace count
           lastticks = timeGetTime();
#ifdef PROFILE
	end_section(IDLE_SECTION);
#endif
           return;                                     // -> done, we will skip next frame to get more speed
          }
        }
      }

     bInitCap=FALSE;                                   // -> ok, we have inited the frameskip func
     iAdditionalSkip=0;                                // -> init additional skip
     bSkipNextFrame=FALSE;                             // -> we don't skip the next frame
     lastticks = timeGetTime();                        // -> we store the start time of the next frame
     dwLaceCnt=0;                                      // -> and we start to count the laces
     dwLastLace=0;
     _ticks_since_last_update=0;
#ifdef PROFILE
	end_section(IDLE_SECTION);
#endif
     return;                                           // -> done, the next frame will get drawn
    }

   bSkipNextFrame=FALSE;                               // init the frame skip signal to 'no skipping' first

   curticks = timeGetTime();                           // get the current time (we are now at the end of one drawn frame)
   _ticks_since_last_update = curticks - lastticks;

   dwLastLace=dwLaceCnt;                               // store curr count (frame limitation helper)
   dwWaitTime=dwLaceCnt*dwFrameRateTicks;              // calc the 'real psx lace time'

   if(_ticks_since_last_update>dwWaitTime)             // hey, we needed way too long for that frame...
    {
     if(UseFrameLimit)                                 // if limitation, we skip just next frame,
      {                                                // and decide after, if we need to do more
       iNumSkips=0;
      }
     else
      {
       iNumSkips=_ticks_since_last_update/dwWaitTime;  // -> calc number of frames to skip to catch up
       iNumSkips--;                                    // -> since we already skip next frame, one down
       if(iNumSkips>MAXSKIP) iNumSkips=MAXSKIP;        // -> well, somewhere we have to draw a line
      }
     bSkipNextFrame = TRUE;                            // -> signal for skipping the next frame
    }
   else                                                // we were faster than real psx? fine :)
   if(UseFrameLimit)                                   // frame limit used? so we wait til the 'real psx time' has been reached
    {
     if(dwLaceCnt>MAXLACE)                             // -> security check
      _ticks_since_last_update=dwWaitTime;

     while(_ticks_since_last_update<dwWaitTime)        // -> just do a waiting loop...
      {
       curticks = timeGetTime();
       _ticks_since_last_update = curticks - lastticks;
      }
    }

   lastticks = timeGetTime();                          // ok, start time of the next frame
  }

 dwLaceCnt=0;                                          // init lace counter
#ifdef PROFILE
	end_section(IDLE_SECTION);
#endif
}

////////////////////////////////////////////////////////////////////////

void calcfps(void)
{
 static unsigned long _ticks_since_last_update;
 static unsigned long fps_cnt = 0;
 static unsigned long fps_tck = 1;
  {
   static unsigned long lastticks;
   static unsigned long curticks;

   curticks= timeGetTime();
   _ticks_since_last_update=curticks-lastticks;

   if(UseFrameSkip && !UseFrameLimit && _ticks_since_last_update)
    fps_skip=min(fps_skip,((float)TIMEBASE/(float)_ticks_since_last_update+1.0f));

   lastticks = curticks;
  }

 if(UseFrameSkip && UseFrameLimit)
  {
   static unsigned long fpsskip_cnt = 0;
   static unsigned long fpsskip_tck = 1;

   fpsskip_tck += _ticks_since_last_update;

   if(++fpsskip_cnt==2)
    {
     fps_skip = (float)2000/(float)fpsskip_tck;
     fps_skip +=6.0f;
     fpsskip_cnt = 0;
     fpsskip_tck = 1;
    }
  }

 fps_tck += _ticks_since_last_update;

 if(++fps_cnt==10)
  {
   fps_cur = (float)(TIMEBASE*10)/(float)fps_tck;

   fps_cnt = 0;
   fps_tck = 1;

   if(UseFrameLimit && fps_cur>fFrameRateHz)           // optical adjust ;) avoids flickering fps display
    fps_cur=fFrameRateHz;
  }

  sprintf(fpsInfo, "FPS %.2f", fps_cur);

}

////////////////////////////////////////////////////////////////////////

void SetAutoFrameCap(void)
{
 if(iFrameLimit==1)
  {
   fFrameRateHz = fFrameRate;
   dwFrameRateTicks=(TIMEBASE / (unsigned long)fFrameRateHz);
   return;
  }

  {
   //fFrameRateHz = PSXDisplay.PAL?50.0f:59.94f;
   if(PSXDisplay.PAL)
    {
     if (lGPUstatusRet&GPUSTATUS_INTERLACED)
           fFrameRateHz=33868800.0f/677343.75f;        // 50.00238
      else fFrameRateHz=33868800.0f/680595.00f;        // 49.76351
    }
   else
    {
     if (lGPUstatusRet&GPUSTATUS_INTERLACED)
           fFrameRateHz=33868800.0f/565031.25f;        // 59.94146
      else fFrameRateHz=33868800.0f/566107.50f;        // 59.82750
    }
//   dwFrameRateTicks=(TIMEBASE / (unsigned long)fFrameRateHz);
   dwFrameRateTicks=(unsigned long) (TIMEBASE / fFrameRateHz);
  }
}

////////////////////////////////////////////////////////////////////////

void InitFPS(void)
{
 if(!fFrameRate) fFrameRate=200.0f;

 if(fFrameRateHz==0)
  {
   if(iFrameLimit==2) fFrameRateHz=59.94f;           // auto framerate? set some init val (no pal/ntsc known yet)
   else               fFrameRateHz=fFrameRate;       // else set user framerate
  }

 dwFrameRateTicks=(TIMEBASE / (unsigned long)fFrameRateHz);
}

