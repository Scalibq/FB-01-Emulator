/*
  VFB-01 / Virtual FB-01 module

  Made by Daisuke Nagano <breeze.nagano@nifty.ne.jp>
  Jun.27.2000

  Copyright 2000 by Daisuke Nagano <breeze.nagano@nifty.ne.jp>
  Jun.27.2000

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* ------------------------------------------------------------------- */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
//#include <signal.h>
#include <io.h>

#ifdef STDC_HEADERS
# include <string.h>
#endif
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif

#ifdef HAVE_OSS_AUDIO
#  ifdef HAVE_MACHINE_SOUNDCARD_H
#   include <machine/soundcard.h>
#  else
#   ifdef HAVE_SOUNDCARD_H
#    include <soundcard.h>
#   else
#    include <linux/soundcard.h>
#   endif
#  endif
#endif

#ifdef HAVE_ESD_AUDIO
# include <esd.h>
#endif

//#include "waveOut.h"

#include "vfb01.h"
#include "pcm8.h"
#include "ym2151.h"

/* ------------------------------------------------------------------ */

#define ENABLE_BUFFERED_PCM 1

/* ------------------------------------------------------------------ */
/* local valiables */

static VFB_DATA *evfb=NULL;

static int pcm8_opened           = FLAG_FALSE;
static int pcm8_interrupt_active = FLAG_FALSE;

static int master_volume         = 0;

static int is_encoding_16bit     = FLAG_TRUE;
static int is_encoding_stereo    = FLAG_TRUE; 
static int dsp_speed             = PCM8_MASTER_PCM_RATE;

static SAMP *ym2151_voice[2];
static int   ym2151_pan[VFB_MAX_CHANNEL_NUMBER];

static unsigned char riff[]={
  'R','I','F','F',
  0xff,0xff,0xff,0xff,
  'W','A','V','E','f','m','t',' ',
  16,0,0,0, 1,0, 2,0,
  PCM8_MASTER_PCM_RATE%256,PCM8_MASTER_PCM_RATE/256,0,0,
  (PCM8_MASTER_PCM_RATE*4)%256,((PCM8_MASTER_PCM_RATE*4)/256)%256,
  ((PCM8_MASTER_PCM_RATE*4)/65536)%256,0,
  4,0, 16,0,
  'd','a','t','a',
  0,0,0,0
};

/* ------------------------------------------------------------------ */

int pcm8_open( VFB_DATA *vfb, int sample_buffer_size ) {
  int i,j;
  void *buf;

  if ( pcm8_opened == FLAG_TRUE ) return 0;
  evfb = vfb;

  buf = (SAMP *)malloc( sizeof(SAMP) * sample_buffer_size * 2 );
  if ( buf == NULL ) return 1;

	 for ( i=0 ; i<2 ; i++ ) {
		ym2151_voice[i] = (SAMP *)((uintptr_t)buf + sizeof(SAMP)*sample_buffer_size*i);
	}

	// TODO
	for (j = 0; j<1; j++) {
		ym2151_pan[j] = 64;
	}

  master_volume = evfb->master_volume;
  if ( master_volume < 0 ) master_volume = 0;

  pcm8_opened   = FLAG_TRUE;
  evfb->dsp_speed = dsp_speed;

  return 0;
}

int pcm8_close( void ) {
	int i;

	pcm8_opened = FLAG_FALSE;

	pcm8_stop();

	free(ym2151_voice[0]);

	ym2151_voice[0] = NULL;
	ym2151_voice[1] = NULL;

	return 0;
}

int pcm8_pan( int ch, int val ) {

  if ( val < 0 ) val = 0;
  if ( val > 127 ) val = 127;

  ym2151_pan[ch] = val;

  return 0;
}

/* ------------------------------------------------------------------ */

/* PCM8 main function: mixes all of PCM sound and OPM emulator */

void pcm8( int8_t* sample_buffer, int sample_buffer_size ) {
  int i, j;
  int buffer_size=0;

  /* must I pronounce? */

  if ( pcm8_opened == FLAG_FALSE || sample_buffer == NULL ) return;

  /* Execute YM2151 emulator */

	ym2151_update_one( YMPSG, ym2151_voice, sample_buffer_size );

  /* now pronouncing ! */


	// TODO: This was a mixing routine for multiple YM2151 instances
	// Make it work correctly for one YM2151 with panning and master volume.
	// Some things might not have to be here, but part of the YM2151 parameters itself.
  for ( i=0 ; i<sample_buffer_size ; i++ ) {
	int v, sv;
	long l,r, sl,sr;
	unsigned char l1, l2, r1, r2, v1,v2;

	l = (long)(ym2151_voice[0][i]);// *(127 - ym2151_pan[j]) / 128);
	  
	r = (long)(ym2151_voice[1][i]);// * ym2151_pan[j]/128);

	l = l * master_volume / PCM8_MAX_VOLUME;
	r = r * master_volume / PCM8_MAX_VOLUME;

	if ( l<-32768 ) l=-32768;
	if ( l> 32767 ) l= 32767;
	if ( r<-32768 ) r=-32768;
	if ( r> 32767 ) r= 32767;

	v  = (l+r)/2;  /*   signed short MONO */
	sl = l+32768;  /* unsigned short L */
	sr = r+32768;  /* unsigned short R */
	sv = v+32768;  /* unsigned short MONO */
	
	if ( l<0 ) l=0x10000+l;
	l1 = (unsigned char)(l/256);
	l2 = (unsigned char)(l%256);

	if ( r<0 ) r=0x10000+r;
	r1 = (unsigned char)(r/256);
	r2 = (unsigned char)(r%256);

	if ( v<0 ) v=0x10000+v;
	v1 = (unsigned char)(v/256);
	v2 = (unsigned char)(v%256);

	if ( is_encoding_16bit  == FLAG_TRUE &&
	 is_encoding_stereo == FLAG_TRUE ) {
	  sample_buffer[i*4+0] = l2;
	  sample_buffer[i*4+1] = l1;
	  sample_buffer[i*4+2] = r2;
	  sample_buffer[i*4+3] = r1;
	}
	else if ( is_encoding_16bit  == FLAG_TRUE &&
		  is_encoding_stereo == FLAG_FALSE ) {
	  sample_buffer[i*2+0] = v2;
	  sample_buffer[i*2+1] = v1;
	}
	else if ( is_encoding_16bit  == FLAG_FALSE &&
		  is_encoding_stereo == FLAG_TRUE ) {
	  sample_buffer[i*2+0] = sl/256;
	  sample_buffer[i*2+1] = sr/256;
	}
	else {
	  sample_buffer[i] = sv/256;
	}
  }

  return;
}

/* ------------------------------------------------------------------ */

void pcm8_start( void ) {
	pcm8_interrupt_active = FLAG_TRUE;
	return;
}

void pcm8_stop( void ) {
	pcm8_interrupt_active=FLAG_FALSE;
	return;
}

