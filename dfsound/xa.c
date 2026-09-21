/***************************************************************************
                            xa.c  -  description
                             -------------------
    begin                : Wed May 15 2002
    copyright            : (C) 2002 by Pete Bernert
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

#include "stdafx.h"
#include "spu.h"
#define _IN_XA
#include <stdint.h>
#include "../psxcommon.h"
#include "../Gamecube/DEBUG.h"
#include "../Gamecube/perf_prof.h"
extern char soundXaResampler;

// will be included from spu.c
#ifdef _IN_SPU

////////////////////////////////////////////////////////////////////////
// XA GLOBALS
////////////////////////////////////////////////////////////////////////

static int gauss_ptr = 0;
static int gauss_window[8] = {0, 0, 0, 0, 0, 0, 0, 0};

#define gvall0 gauss_window[gauss_ptr]
#define gvall(x) gauss_window[(gauss_ptr+x)&3]
#define gvalr0 gauss_window[4+gauss_ptr]
#define gvalr(x) gauss_window[4+((gauss_ptr+x)&3)]

#ifdef PERF_PROF
/* Probe state for the XA counters in perf_prof.h. xa_stream_on: a sector was queued and
 * the mixer has not been silent for ~2 s since; xa_gap_open / xa_hold_open: the current
 * silence / repeat episode has been recorded, so only its first call counts as an event. */
static int xa_stream_on, xa_gap_open, xa_hold_open;
static unsigned xa_gap_run, xa_last_cycle;
static unsigned xa_fill(void)
{
 if (spu.XAFeed >= spu.XAPlay) return (unsigned)(spu.XAFeed - spu.XAPlay);
 return (unsigned)((spu.XAEnd - spu.XAPlay) + (spu.XAFeed - spu.XAStart));
}
#endif

////////////////////////////////////////////////////////////////////////
// MIX XA & CDDA
////////////////////////////////////////////////////////////////////////

INLINE void SkipCD(int ns_to, int decode_pos)
{
 int cursor = decode_pos;
 int ns;

 if(spu.XAPlay != spu.XAFeed)
 {
  for(ns = 0; ns < ns_to*2; ns += 2)
   {
    if(spu.XAPlay != spu.XAFeed) spu.XAPlay++;
    if(spu.XAPlay == spu.XAEnd) spu.XAPlay=spu.XAStart;

    spu.spuMem[cursor] = 0;
    spu.spuMem[cursor + 0x400/2] = 0;
    cursor = (cursor + 1) & 0x1ff;
   }
 }
 else if(spu.CDDAPlay != spu.CDDAFeed)
 {
  for(ns = 0; ns < ns_to*2; ns += 2)
   {
    if(spu.CDDAPlay != spu.CDDAFeed) spu.CDDAPlay++;
    if(spu.CDDAPlay == spu.CDDAEnd) spu.CDDAPlay=spu.CDDAStart;

    spu.spuMem[cursor] = 0;
    spu.spuMem[cursor + 0x400/2] = 0;
    cursor = (cursor + 1) & 0x1ff;
   }
 }
 spu.XALastVal = 0;
}

INLINE void MixCD(int *SSumLR, int *RVB, int ns_to, int decode_pos)
{
 int vll = spu.iLeftXAVol * spu.cdv.ll >> 7;
 int vrl = spu.iLeftXAVol * spu.cdv.rl >> 7;
 int vlr = spu.iRightXAVol * spu.cdv.lr >> 7;
 int vrr = spu.iRightXAVol * spu.cdv.rr >> 7;
 int cursor = decode_pos;
 int l1, r1, l, r;
 int ns;
 uint32_t v = spu.XALastVal;

 // note: spu volume doesn't affect cd capture
 if ((spu.cdv.ll | spu.cdv.lr | spu.cdv.rl | spu.cdv.rr) == 0)
 {
  SkipCD(ns_to, decode_pos);
  return;
 }

 if(spu.XAPlay != spu.XAFeed || spu.XARepeat > 0)
 {
  if(spu.XAPlay == spu.XAFeed)
   spu.XARepeat--;
#ifdef PERF_PROF
  if (spu.XAPlay == spu.XAFeed)
   {
    g_perf.xa_hold++;
    if (!xa_hold_open) { xa_hold_open = 1; perf_audio_event('H', spu.cycles_played, ns_to, (int)spu.XARepeat, 0); }
   }
  else { g_perf.xa_mix++; xa_hold_open = 0; }
#endif

  for(ns = 0; ns < ns_to*2; ns += 2)
   {
    if(spu.XAPlay != spu.XAFeed) v=*spu.XAPlay++;
    if(spu.XAPlay == spu.XAEnd) spu.XAPlay=spu.XAStart;

    l1 = (short)v, r1 = (short)(v >> 16);
    l = (l1 * vll + r1 * vrl) >> 15;
    r = (r1 * vrr + l1 * vlr) >> 15;
    ssat32_to_16(l);
    ssat32_to_16(r);
    if (spu.spuCtrl & CTRL_CD)
    {
     SSumLR[ns+0] += l;
     SSumLR[ns+1] += r;
    }
    if (unlikely(spu.spuCtrl & CTRL_CDREVERB))
    {
     RVB[ns+0] += l;
     RVB[ns+1] += r;
    }

    spu.spuMem[cursor] = HTOLE16(v);
    spu.spuMem[cursor + 0x400/2] = HTOLE16(v >> 16);
    cursor = (cursor + 1) & 0x1ff;
   }
  spu.XALastVal = v;
 }
 // occasionally CDDAFeed underflows by a few samples due to poor timing,
 // hence this 'ns_to < 8'
 else if(spu.CDDAPlay != spu.CDDAFeed || ns_to < 8)
 {
  for(ns = 0; ns < ns_to*2; ns += 2)
   {
    if(spu.CDDAPlay != spu.CDDAFeed) v=*spu.CDDAPlay++;
    if(spu.CDDAPlay == spu.CDDAEnd) spu.CDDAPlay=spu.CDDAStart;

    l1 = (short)v, r1 = (short)(v >> 16);
    l = (l1 * vll + r1 * vrl) >> 15;
    r = (r1 * vrr + l1 * vlr) >> 15;
    ssat32_to_16(l);
    ssat32_to_16(r);
    if (spu.spuCtrl & CTRL_CD)
    {
     SSumLR[ns+0] += l;
     SSumLR[ns+1] += r;
    }
    if (unlikely(spu.spuCtrl & CTRL_CDREVERB))
    {
     RVB[ns+0] += l;
     RVB[ns+1] += r;
    }

    spu.spuMem[cursor] = HTOLE16(v);
    spu.spuMem[cursor + 0x400/2] = HTOLE16(v >> 16);
    cursor = (cursor + 1) & 0x1ff;
   }
  spu.XALastVal = v;
 }
 else if (spu.cdClearSamples > 0)
 {
	 for(ns = 0; ns < ns_to; ns++)
	 {
		 spu.spuMem[cursor] = spu.spuMem[cursor + 0x400/2] = 0;
		 cursor = (cursor + 1) & 0x1ff;
	 }
  spu.cdClearSamples -= ns_to;
  spu.XALastVal = 0;
 }
#ifdef PERF_PROF
 /* XA ring empty, repeats used up, no CDDA either: this call added silence. Counted only
  * while a stream is active, i.e. until ~2 s of continuous silence. */
 if (xa_stream_on && spu.XAPlay == spu.XAFeed && spu.XARepeat == 0 && spu.CDDAPlay == spu.CDDAFeed)
  {
   if (!xa_gap_open) { xa_gap_open = 1; g_perf.xa_gaps++; perf_audio_event('G', spu.cycles_played, ns_to, spu.cdClearSamples, 0); }
   g_perf.xa_gap_calls++; g_perf.xa_gap_samples += ns_to;
   xa_gap_run += ns_to;
   if (xa_gap_run > 2 * 44100) xa_stream_on = 0;
  }
#endif
}

////////////////////////////////////////////////////////////////////////
// small linux time helper... only used for watchdog
////////////////////////////////////////////////////////////////////////

#if 0
static unsigned long timeGetTime_spu()
{
#if defined(NO_OS)
 return 0;
#elif defined(_WIN32)
 return GetTickCount();
#else
 struct timeval tv;
 gettimeofday(&tv, 0);                                 // well, maybe there are better ways
 return tv.tv_sec * 1000 + tv.tv_usec/1000;            // to do that, but at least it works
#endif
}
#endif

////////////////////////////////////////////////////////////////////////
// FEED XA
////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////
// HI-FI XA RESAMPLER: real hardware's own 37800 -> 44100 Hz filter
////////////////////////////////////////////////////////////////////////

/* extern char soundXaResampler (Gamecube/wiiSXconfig.h). This is the SPU's own real 37800 Hz
 * XA-ADPCM sample rate converter: a 7-phase, 29-tap "zigzag" FIR run on a 32-entry ring
 * buffer, six input samples in for every seven output samples (37800*7/6 = 44100 exactly,
 * so the ratio is exact with no drift). Coefficients are DuckStation's ResampleXAADPCM
 * (src/core/cdrom.cpp), itself matching the real chip's behaviour; not the same filter as
 * psx-spx's generic description, and not the 18900 Hz stream's own separate table (which
 * needs its own separately-sourced coefficients and is not included here -- 18900 Hz
 * streams always use the legacy path below regardless of this setting). Only handles
 * 37800 Hz; FeedXA falls back to the legacy nearest/Gaussian step otherwise. */
static const short xa_zigzag_tables[7][29] = {
 {      0,  0x0000,  0x0000,  0x0000,  0x0000, -0x0002,  0x000A, -0x0022,  0x0041, -0x0054,
   0x0034,  0x0009, -0x010A,  0x0400, -0x0A78,  0x234C,  0x6794, -0x1780,  0x0BCD, -0x0623,
   0x0350, -0x016D,  0x006B,  0x000A, -0x0010,  0x0011, -0x0008,  0x0003, -0x0001 },
 {      0,  0x0000,  0x0000, -0x0002,  0x0000,  0x0003, -0x0013,  0x003C, -0x004B,  0x00A2,
  -0x00E3,  0x0132, -0x0043, -0x0267,  0x0C9D,  0x74BB, -0x11B4,  0x09B8, -0x05BF,  0x0372,
  -0x01A8,  0x00A6, -0x001B,  0x0005,  0x0006, -0x0008,  0x0003, -0x0001,  0x0000 },
 {      0,  0x0000, -0x0001,  0x0003, -0x0002, -0x0005,  0x001F, -0x004A,  0x00B3, -0x0192,
   0x02B1, -0x039E,  0x04F8, -0x05A6,  0x7939, -0x05A6,  0x04F8, -0x039E,  0x02B1, -0x0192,
   0x00B3, -0x004A,  0x001F, -0x0005, -0x0002,  0x0003, -0x0001,  0x0000,  0x0000 },
 {      0, -0x0001,  0x0003, -0x0008,  0x0006,  0x0005, -0x001B,  0x00A6, -0x01A8,  0x0372,
  -0x05BF,  0x09B8, -0x11B4,  0x74BB,  0x0C9D, -0x0267, -0x0043,  0x0132, -0x00E3,  0x00A2,
  -0x004B,  0x003C, -0x0013,  0x0003,  0x0000, -0x0002,  0x0000,  0x0000,  0x0000 },
 { -0x0001,  0x0003, -0x0008,  0x0011, -0x0010,  0x000A,  0x006B, -0x016D,  0x0350, -0x0623,
   0x0BCD, -0x1780,  0x6794,  0x234C, -0x0A78,  0x0400, -0x010A,  0x0009,  0x0034, -0x0054,
   0x0041, -0x0022,  0x000A, -0x0001,  0x0000,  0x0001,  0x0000,  0x0000,  0x0000 },
 {  0x0002, -0x0008,  0x0010, -0x0023,  0x002B,  0x001A, -0x00EB,  0x027B, -0x0548,  0x0AFA,
  -0x16FA,  0x53E0,  0x3C07, -0x1249,  0x080E, -0x0347,  0x015B, -0x0044, -0x0017,  0x0046,
  -0x0023,  0x0011, -0x0005,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000 },
 { -0x0005,  0x0011, -0x0023,  0x0046, -0x0017, -0x0044,  0x015B, -0x0347,  0x080E, -0x1249,
   0x3C07,  0x53E0, -0x16FA,  0x0AFA, -0x0548,  0x027B, -0x00EB,  0x001A,  0x002B, -0x0023,
   0x0010, -0x0008,  0x0002,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000,  0x0000 },
};

/* Persistent across calls (a continuous filter over the whole stream, not per-sector); left
 * only, duplicated to the right channel for mono. Zero-initialised, like the legacy path's
 * own gauss_window: a stream restart does not explicitly clear this, matching that existing
 * convention rather than introducing a new one. */
static short xa_zz_ring[2][32];
static unsigned int xa_zz_p = 0;
static unsigned int xa_zz_sixstep = 6;

INLINE short xa_zigzag_tap(const short *ring, unsigned int table, unsigned int p)
{
 const short *coef = xa_zigzag_tables[table];
 int32_t sum = 0;
 unsigned int i;
 for (i = 0; i < 29; i++)
  sum += ((int32_t)ring[(p - i) & 31] * (int32_t)coef[i]) >> 15;
 ssat32_to_16(sum);
 return (short)sum;
}

/* Returns 0 on success, -1 if the XA ring filled up mid-burst (caller should stop feeding,
 * same as the legacy path's own "Buffer not enough" break). */
static int FeedXA_zigzag(const xa_decode_t *xap)
{
 unsigned int i;

 for (i = 0; i < (unsigned int)xap->nsamples; i++)
  {
   xa_zz_ring[0][xa_zz_p] = xap->pcm[xap->stereo ? 2 * i + 0 : i];
   xa_zz_ring[1][xa_zz_p] = xap->stereo ? xap->pcm[2 * i + 1] : xa_zz_ring[0][xa_zz_p];
   xa_zz_p = (xa_zz_p + 1) & 31;

   if (--xa_zz_sixstep == 0)
    {
     unsigned int j;
     xa_zz_sixstep = 6;
     for (j = 0; j < 7; j++)
      {
       short l = xa_zigzag_tap(xa_zz_ring[0], j, xa_zz_p);
       short r = xa_zigzag_tap(xa_zz_ring[1], j, xa_zz_p);
       uint32_t packed = ((uint32_t)(unsigned short)l) | ((uint32_t)(unsigned short)r << 16);

       *spu.XAFeed++ = packed;
       if (spu.XAFeed == spu.XAEnd) spu.XAFeed = spu.XAStart;
       if (spu.XAFeed == spu.XAPlay)
        {
         if (spu.XAPlay != spu.XAStart) spu.XAFeed = spu.XAPlay - 1;
         PERF_INC(xa_trunc);
         return -1;
        }
      }
    }
  }
 return 0;
}

void FeedXA(const xa_decode_t *xap)
{
 int sinc,spos,i,iSize,iPlace,vl,vr;

 if(!spu.bSPUIsOpen) return;

 spu.XARepeat  = 3;                                    // set up repeat

#if 0//def XA_HACK
 iSize=((45500*xap->nsamples)/xap->freq);              // get size
#else
 //iSize=((48000*xap->nsamples)/xap->freq);              // get size
 iSize = xap->newSize;

#endif
 /* nsamples == 0 is cdrom.c's flush on CdlPause. iSize is precomputed here (upstream
  * derives it from nsamples and gets 0), so without this test the flush re-queued the
  * previous sector's PCM once more. */
 if(!iSize || !xap->nsamples) return;                  // none? bye

 if(spu.XAFeed<spu.XAPlay) iPlace=spu.XAPlay-spu.XAFeed; // how much space in my buf?
 else              iPlace=(spu.XAEnd-spu.XAFeed) + (spu.XAPlay-spu.XAStart);

 if(iPlace==0) return;                                 // no place at all

 if (soundXaResampler && xap->freq == 37800)
  {
   FeedXA_zigzag(xap);
   return;
  }

 //----------------------------------------------------//
 /*if(spu_config.iXAPitch)                               // pitch change option?
  {
   static DWORD dwLT=0;
   static DWORD dwFPS=0;
   static int   iFPSCnt=0;
   static int   iLastSize=0;
   static DWORD dwL1=0;
   DWORD dw=timeGetTime_spu(),dw1,dw2;
   iPlace=iSize;
   dwFPS+=dw-dwLT;iFPSCnt++;
   dwLT=dw;
   if(iFPSCnt>=10)
    {
     if(!dwFPS) dwFPS=1;
     dw1=1000000/dwFPS;
     if(dw1>=(dwL1-100) && dw1<=(dwL1+100)) dw1=dwL1;
     else dwL1=dw1;
     dw2=(xap->freq*100/xap->nsamples);
     if((!dw1)||((dw2+100)>=dw1)) iLastSize=0;
     else
      {
       iLastSize=iSize*dw2/dw1;
       if(iLastSize>iPlace) iLastSize=iPlace;
       iSize=iLastSize;
      }
     iFPSCnt=0;dwFPS=0;
    }
   else
    {
     if(iLastSize) iSize=iLastSize;
    }
  }*/
 //----------------------------------------------------//

 spos=0x10000L;
 //sinc = (xap->nsamples << 16) / iSize;                 // calc freq by num / size
 sinc = xap->sinc;                 // calc freq by num / size

 if(xap->stereo)
{
   uint32_t * pS=(uint32_t *)xap->pcm;
   uint32_t l=0;

   /*if(spu_config.iXAPitch)
    {
     int32_t l1,l2;short s;
     for(i=0;i<iSize;i++)
      {
       if(spu_config.iUseInterpolation==2)
        {
         while(spos>=0x10000L)
          {
           l = *pS++;
           gauss_window[gauss_ptr] = (short)LOWORD(l);
           gauss_window[4+gauss_ptr] = (short)HIWORD(l);
           gauss_ptr = (gauss_ptr+1) & 3;
           spos -= 0x10000L;
          }
         vl = (spos >> 6) & ~3;
         vr=(gauss[vl]*gvall0) >> 15;
         vr+=(gauss[vl+1]*gvall(1)) >> 15;
         vr+=(gauss[vl+2]*gvall(2)) >> 15;
         vr+=(gauss[vl+3]*gvall(3)) >> 15;
         l= vr & 0xffff;
         vr=(gauss[vl]*gvalr0) >> 15;
         vr+=(gauss[vl+1]*gvalr(1)) >> 15;
         vr+=(gauss[vl+2]*gvalr(2)) >> 15;
         vr+=(gauss[vl+3]*gvalr(3)) >> 15;
         l |= vr << 16;
        }
       else
        {
         while(spos>=0x10000L)
          {
           l = *pS++;
           spos -= 0x10000L;
          }
        }
       s=(short)LOWORD(l);
       l1=s;
       l1=(l1*iPlace)/iSize;
       ssat32_to_16(l1);
       s=(short)HIWORD(l);
       l2=s;
       l2=(l2*iPlace)/iSize;
       ssat32_to_16(l2);
       l=(l1&0xffff)|(l2<<16);
       *spu.XAFeed++=l;
       if(spu.XAFeed==spu.XAEnd) spu.XAFeed=spu.XAStart;
       if(spu.XAFeed==spu.XAPlay)
        {
         if(spu.XAPlay!=spu.XAStart) spu.XAFeed=spu.XAPlay-1;
         break;
        }
       spos += sinc;
      }
    }
   else*/
    {
     for(i=0;i<iSize;i++)
      {
       if(spu_config.iUseInterpolation==2)
        {
         while(spos>=0x10000L)
          {
           l = *pS++;
           gauss_window[gauss_ptr] = (short)LOWORD(l);
           gauss_window[4+gauss_ptr] = (short)HIWORD(l);
           gauss_ptr = (gauss_ptr+1) & 3;
           spos -= 0x10000L;
          }
         vl = (spos >> 6) & ~3;
         vr=(gauss[vl]*gvall0) >> 15;
         vr+=(gauss[vl+1]*gvall(1)) >> 15;
         vr+=(gauss[vl+2]*gvall(2)) >> 15;
         vr+=(gauss[vl+3]*gvall(3)) >> 15;
         l= vr & 0xffff;
         vr=(gauss[vl]*gvalr0) >> 15;
         vr+=(gauss[vl+1]*gvalr(1)) >> 15;
         vr+=(gauss[vl+2]*gvalr(2)) >> 15;
         vr+=(gauss[vl+3]*gvalr(3)) >> 15;
         l |= vr << 16;
        }
       else
        {
         while(spos>=0x10000L)
          {
           l = *pS++;
           spos -= 0x10000L;
          }
        }

       *spu.XAFeed++=l;

       if(spu.XAFeed==spu.XAEnd) spu.XAFeed=spu.XAStart;
       if(spu.XAFeed==spu.XAPlay)
        {
         if(spu.XAPlay!=spu.XAStart) spu.XAFeed=spu.XAPlay-1;
         #ifdef SHOW_DEBUG
         DEBUG_print("FeedXA Buffer not enough", DBG_SPU2);
         #endif // DISP_DEBUG
         PERF_INC(xa_trunc);
         break;
        }

       spos += sinc;
      }
    }
  }
 else
  {
   unsigned short * pS=(unsigned short *)xap->pcm;
   uint32_t l;short s=0;

   /*if(spu_config.iXAPitch)
    {
     int32_t l1;
     for(i=0;i<iSize;i++)
      {
       if(spu_config.iUseInterpolation==2)
        {
         while(spos>=0x10000L)
          {
           gauss_window[gauss_ptr] = (short)*pS++;
           gauss_ptr = (gauss_ptr+1) & 3;
           spos -= 0x10000L;
          }
         vl = (spos >> 6) & ~3;
         vr=(gauss[vl]*gvall0) >> 15;
         vr+=(gauss[vl+1]*gvall(1)) >> 15;
         vr+=(gauss[vl+2]*gvall(2)) >> 15;
         vr+=(gauss[vl+3]*gvall(3)) >> 15;
         l1=s= vr;
         l1 &= 0xffff;
        }
       else
        {
         while(spos>=0x10000L)
          {
           s = *pS++;
           spos -= 0x10000L;
          }
         l1=s;
        }
       l1=(l1*iPlace)/iSize;
       ssat32_to_16(l1);
       l=(l1&0xffff)|(l1<<16);
       *spu.XAFeed++=l;
       if(spu.XAFeed==spu.XAEnd) spu.XAFeed=spu.XAStart;
       if(spu.XAFeed==spu.XAPlay)
        {
         if(spu.XAPlay!=spu.XAStart) spu.XAFeed=spu.XAPlay-1;
         break;
        }
       spos += sinc;
      }
    }
   else*/
    {
     for(i=0;i<iSize;i++)
      {
       if(spu_config.iUseInterpolation==2)
        {
         while(spos>=0x10000L)
          {
           gauss_window[gauss_ptr] = (short)*pS++;
           gauss_ptr = (gauss_ptr+1) & 3;
           spos -= 0x10000L;
          }
         vl = (spos >> 6) & ~3;
         vr=(gauss[vl]*gvall0) >> 15;
         vr+=(gauss[vl+1]*gvall(1)) >> 15;
         vr+=(gauss[vl+2]*gvall(2)) >> 15;
         vr+=(gauss[vl+3]*gvall(3)) >> 15;
         l=s= vr;
        }
       else
        {
         while(spos>=0x10000L)
          {
           s = *pS++;
           spos -= 0x10000L;
          }
         l=s;
        }

       l &= 0xffff;
       *spu.XAFeed++=(l|(l<<16));

       if(spu.XAFeed==spu.XAEnd) spu.XAFeed=spu.XAStart;
       if(spu.XAFeed==spu.XAPlay)
        {
         if(spu.XAPlay!=spu.XAStart) spu.XAFeed=spu.XAPlay-1;
         #ifdef SHOW_DEBUG
         DEBUG_print("FeedXA Buffer not enough", DBG_SPU2);
         #endif // DISP_DEBUG
         PERF_INC(xa_trunc);
         break;
        }

       spos += sinc;
      }
    }
  }
 PERF_ADD(xa_fed, i);
}

////////////////////////////////////////////////////////////////////////
// FEED CDDA
////////////////////////////////////////////////////////////////////////

void FeedCDDA(unsigned char *pcm, int nBytes)
{
 int space;
 space=(spu.CDDAPlay-spu.CDDAFeed-1)*4 & (CDDA_BUFFER_SIZE - 1);
 if (space < nBytes) {
  log_unhandled("FeedCDDA: %d/%d\n", nBytes, space);
  return;
 }

 while(nBytes>0)
  {
   if(spu.CDDAFeed==spu.CDDAEnd) spu.CDDAFeed=spu.CDDAStart;
   space=(spu.CDDAPlay-spu.CDDAFeed-1)*4 & (CDDA_BUFFER_SIZE - 1);
   if(spu.CDDAFeed+space/4>spu.CDDAEnd)
    space=(spu.CDDAEnd-spu.CDDAFeed)*4;
   if(space>nBytes)
    space=nBytes;

   memcpy(spu.CDDAFeed,pcm,space);
   spu.CDDAFeed+=space/4;
   nBytes-=space;
   pcm+=space;
  }
}

#endif
