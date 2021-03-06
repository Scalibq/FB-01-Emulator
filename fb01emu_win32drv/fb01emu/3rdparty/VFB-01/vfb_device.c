/*
  VFB-01 : Virtual FB-01 emulator

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

#include "vfb01.h"
#include "vfb_device.h"
#include "ym2151.h"
#include "pcm8.h"

/* ------------------------------------------------------------------- */

// This describes the MIDI state machine for an instrument
typedef struct _MIDI_MAP {
	int base_voice;	// Lowest voice for this instrument

	long portament;
	int portament_on;

	int bend;
	int bend_sense_m;
	int bend_sense_l;

	int note[VFB_MAX_FM_SLOTS];

	/* state of note_on:
	2:  key pressed
	1:  key on
	0:  key released
	-1: none pronouncing
	*/

	int note_on[VFB_MAX_FM_SLOTS];
	int velocity[VFB_MAX_FM_SLOTS];
	int hold;

	int total_level[4];
	int algorithm;
	int slot_mask;

	int step[VFB_MAX_FM_SLOTS];

	int master_volume;
	int expression;
} MIDI_MAP;

/* ------------------------------------------------------------------- */

static VFB_DATA *evfb=NULL;
MIDI_MAP instrument_map[VFB_MAX_FM_SLOTS];	// These are indexed 1:1 with the instruments in the active configuration
int ym2151_register_map[256];

static int system_volume = 127;

void* YMPSG;

/* ------------------------------------------------------------------- */

static void freq_write( int, int );
static void volume_write( int, int );

static void reg_write( int, int );
static int reg_read( int );

static const int is_vol_set[8][4]={
  {0,0,0,1},
  {0,0,0,1},
  {0,0,0,1},
  {0,0,0,1},
  {0,0,1,1},
  {0,1,1,1},
  {0,1,1,1},
  {1,1,1,1}
};

/* ------------------------------------------------------------------- */

extern int allocate_base_voices(void)
{
	int i;
	int base_voice = 0;

	// TODO: sanity check if we aren't allocating too many voices
	for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	{
		instrument_map[i].base_voice = base_voice;
		base_voice += evfb->active_config.instruments[i].note_count;
	}

	return 0;
}

extern int update_voice(int v)
{
	int i, j;

	j = instrument_map[v].base_voice + evfb->active_config.instruments[v].note_count;

	for (i = instrument_map[v].base_voice; i < j; i++)
	{
		// TODO: use proper bank and voice
		ym2151_set_voice(i, evfb->active_config.instruments[v].voice);
	}
}

static int is_already_opm_allocated = FLAG_FALSE;
static int ym2151_reg_init( VFB_DATA *vfb ) {
  int i,j;

  /* Init OPM Emulator */
  /*
	clock         = 4MHz
	sample rate   = PCM8_MASTER_PCM_RATE ( typically 44.1 kHz )
	sample bits   = 16 bit
	*/

	if ( is_already_opm_allocated == FLAG_FALSE ) {
		YMPSG = ym2151_init( NULL, 4000*1000,
			vfb->dsp_speed );

		if ( YMPSG == NULL  ) return 1;
		ym2151_reset_chip(YMPSG);

		is_already_opm_allocated = FLAG_TRUE;
	}

	for ( i=0 ; i<VFB_MAX_FM_SLOTS; i++ ) {
	  reg_write( 0x08, 0*8 + i );    /* KON */
	}
	reg_write( 0x0f, 0 );            /* NE, NFREQ */
	reg_write( 0x18, 0 );            /* LFRQ */
	reg_write( 0x19, 0*128 + 0 );    /* AMD */
	reg_write( 0x19, 1*128 + 0 );    /* AMD */
	reg_write( 0x1b, 0*64  + 0 );    /* CT, W */
	
	for ( i=0 ; i<VFB_MAX_FM_SLOTS; i++ ) {
	  reg_write( 0x20+i, 3*64 + 0*8 +0 ); /* LR, FL, CON */
	  reg_write( 0x28+i, 0*16 + 0 );      /* OCT, NOTE */
	  reg_write( 0x30+i, 0 );             /* KF */
	  reg_write( 0x38+i, 0*16 + 0 );      /* PMS, AMS */
	}
	for ( i=0 ; i<0x20 ; i++ ) {
	  reg_write( 0x40+i, 0*16 + 0 );      /* DT1, MUL */
	  reg_write( 0x60+i, 0 );             /* TL */
	  reg_write( 0x80+i, 0*64 + 0 );      /* KS, AR */
	  reg_write( 0xa0+i, 0*128 + 0 );     /* AMS, D1R */
	  reg_write( 0xc0+i, 0*64 + 0 );      /* DT2, D2R */
	  reg_write( 0xe0+i, 0*16 + 0 );      /* D1L, RR */
	}
	
	reg_write( 0x1b, 2 );                 /* wave form: triangle */
	reg_write( 0x18, 196 );               /* frequency */

	for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	{
		instrument_map[i].base_voice = i;
		for (j = 0; j < VFB_MAX_FM_SLOTS; j++) {
			instrument_map[i].note[j] = 0;
			instrument_map[i].note_on[j] = -1;
			instrument_map[i].step[j] = 0;
			instrument_map[i].velocity[j] = 0;
		}

		for (j = 0; j < 3; j++) {
			instrument_map[i].total_level[j] = 0;
		}
		instrument_map[i].total_level[3] = 127;

		instrument_map[i].portament = 0;
		instrument_map[i].portament_on = 0;
		instrument_map[i].slot_mask = 0;
		instrument_map[i].algorithm = 0;
		instrument_map[i].bend = 0;
		instrument_map[i].bend_sense_m = 2;
		instrument_map[i].bend_sense_l = 0;
		instrument_map[i].hold = FLAG_FALSE;

		instrument_map[i].master_volume = 127;
		instrument_map[i].expression = 127;

		ym2151_set_voice(i, VFB_INITIAL_VOICE_NUMBER);
	}
  
  system_volume = 127;

  return 0;
}

int setup_ym2151( VFB_DATA *vfb ) {

  evfb = vfb;
  if ( ym2151_reg_init(vfb) ) return 1;

  return 0;
}

int reset_ym2151( void ) {

  if ( ym2151_reg_init(evfb) ) return 1;

  return 0;
}

/* ------------------------------------------------------------------- */

void ym2151_all_note_off( int instrument ) {

  int i,j;

  for ( i=0; i < evfb->active_config.instruments[instrument].note_count; i++ )
  {
	instrument_map[instrument].note_on[i]=0;

	reg_write( 0x08, 0+i + instrument_map[instrument].base_voice);          /* KON */
  }

  return;
}

void ym2151_note_on( int instrument, int note, int vel ) {

  int slot;
  int longest_step=0, longest_slot=0;

  for ( slot=0 ; slot < evfb->active_config.instruments[instrument].note_count; slot++ ) {
	if (instrument_map[instrument].note_on[slot] <= 0 ) break;        /* not playing */
	if (instrument_map[instrument].note[slot] == note ) break;        /* same note */

	if ( longest_step < instrument_map[instrument].step[slot] ) {      /* longest tone */
	  longest_step = instrument_map[instrument].step[slot];
	  longest_slot = slot;
	}
  }
  if ( slot == evfb->active_config.instruments[instrument].note_count)
	slot = longest_slot;

  instrument_map[instrument].step[slot]=0;
  instrument_map[instrument].note[slot]=note;
  instrument_map[instrument].note_on[slot]=2;
  instrument_map[instrument].velocity[slot]=vel;

  reg_write( 0x01, 0x02 ); /* LFO SYNC */
  reg_write( 0x01, 0x00 );

  return;
}

void ym2151_note_off( int instrument, int note ) {

  int slot;

  for ( slot=0 ; slot < evfb->active_config.instruments[instrument].note_count; slot++ ) {
	if (instrument_map[instrument].note[slot] == note ) {
		instrument_map[instrument].note_on[slot]=0;
	}
  }

  return;
}

void ym2151_set_system_volume( int val ) {

  if ( val > 127 ) val = 127;
  if ( val <   0 ) val = 0;

  system_volume = val;

  return;
}

void ym2151_set_master_volume( int instrument, int val ) {

  if ( val < 0 ) val = 0;
  if ( val > 127 ) val = 127;
  instrument_map[instrument].master_volume = val;

  return;
}

void ym2151_set_expression( int instrument, int val ) {

  if ( val < 0 ) val = 0;
  if ( val > 127 ) val = 127;
  instrument_map[instrument].expression = val;

  return;
}

void ym2151_set_bend_sensitivity( int instrument, int msb, int lsb ) {

  if ( msb >= 0 ) {
	if (msb>127) msb=127;
	instrument_map[instrument].bend_sense_m = msb;
  }
  if ( lsb >= 0 ) {
	if (lsb>127) lsb=127;
	instrument_map[instrument].bend_sense_l = lsb;
  }

  return;
}

void ym2151_set_bend( int instrument, int val ) {

  val -= 8192;
  if ( val < -8192 ) val = -8192;
  if ( val > 8191 ) val = 8191;
  
  instrument_map[instrument].bend = val;
  return;
}

void ym2151_set_portament( int instrument, int val ) {

	instrument_map[instrument].portament=val;
  return;
}

void ym2151_set_portament_on( int instrument, int val ) {

	instrument_map[instrument].portament_on=val;
  return;
}

void ym2151_set_hold( int instrument, int sw ) {

	instrument_map[instrument].hold = sw;
  return;
}

void ym2151_set_modulation_depth( int instrument, int val ) {

  int i;

  if ( val > 127 ) val = 127;
  if ( val < 0 )   val = 0;

  reg_write( 0x1b, 66&0x03 );
  reg_write( 0x18, 212 );
  reg_write( 0x19, val|0x80 ); /* PMD */
  reg_write( 0x19, 9 ); /* AMD */
  for ( i=0 ; i<evfb->active_config.instruments[instrument].note_count; i++ ) {
	reg_write( 0x38+i + instrument_map[instrument].base_voice, 112 );
  }

  return;
}

void ym2151_set_voice( int instrument, int tone ) {

  int i,j,r;
  int slot;
  VOICE_DATA *v;

  if ( tone > VFB_MAX_TONE_NUMBER ) tone=0;
  v = &evfb->voice[tone];

  for ( slot=0 ; slot < evfb->active_config.instruments[instrument].note_count; slot++ ) {

	j = reg_read(  0x20+slot + instrument_map[instrument].base_voice );     /* LR, FL, CON */
	reg_write( 0x20+slot + instrument_map[instrument].base_voice, (j&0xc0) + v->v0 );
	instrument_map[instrument].algorithm = v->con;
	instrument_map[instrument].slot_mask = v->slot_mask;
	
	for ( i=0 ; i<4 ; i++ ) {
	  r = slot + i*8 + instrument_map[instrument].base_voice;
	  
	  reg_write( 0x40+r, v->v1[i] );    /* DT1, MUL */
	  reg_write( 0x80+r, v->v3[i] );    /* KS, AR */
	  reg_write( 0xa0+r, v->v4[i] );    /* AME, D1R */
	  reg_write( 0xc0+r, v->v5[i] );    /* DT2, D2R */
	  reg_write( 0xe0+r, v->v6[i] );    /* SL, RR */
	  
	  instrument_map[instrument].total_level[i] = 127 - v->v2[i];
	  if ( is_vol_set[instrument_map[instrument].algorithm][i] == 0 )
		reg_write( 0x60+r, v->v2[i]&0x7f );   /* TL */
	  else
		reg_write( 0x60+r, 127 );             /* TL */
	}
  }

  return;
}

/* ------------------------------------------------------------------- */

static const int ym2151_note[] ={
  0,1,2,4,5,6,8,9,10,12,13,14
};

void ym2151_set_freq_volume( int instrument ) {

  int slot;

  for ( slot=0 ; slot < evfb->active_config.instruments[instrument].note_count ; slot++ ) {
	freq_write(instrument, slot );
	volume_write(instrument, slot );
  }

  return;
}

static void freq_write( int instrument, int slot ) {

  int oct, scale, kf;
  int ofs_o, ofs_s, ofs_f;
  long long f;
  int c,d;
  int key;
  int f1,f2,f3;

  ofs_o = 0; /* octave offset */
  ofs_s = 0; /* scale offset */
  ofs_f = 0; /* detune offset */

  instrument_map[instrument].step[slot]++;

  /* detune jobs */

  c = instrument_map[instrument].bend_sense_m * 64 * instrument_map[instrument].bend / 8192;
  ofs_f += c%64;                      /* detune */
  ofs_s += (c/64)%12;                 /* scale */
  ofs_o += c/(64*12);                 /* octave */
	
  /* portament jobs */
	
  if (instrument_map[instrument].portament != 0 ) {
	f = instrument_map[instrument].portament*instrument_map[instrument].step[slot] / 256;
	
	ofs_f += f%64;          /* detune */
	if ( ofs_f>63 ) {ofs_f-=64;c=1;}
	else if (ofs_f<0 ) { ofs_f+=64;c=-1;}
	else c=0;

	ofs_s += c+(f/64)%12;   /* scale */
	if ( ofs_s>11 ) {ofs_s-=12;c=1;}
	else if (ofs_s<0 ) {ofs_s+=12;c=-1;}
	else c=0;

	ofs_o += c+f/(64*12);   /* octave */
  }
	
  /* generate f-number */

  kf = ofs_f;
  c=0;
  while ( kf > 63 ) { kf -= 64; c++; }
  while ( kf <  0 ) { kf += 64; c--; }

  d = instrument_map[instrument].note[slot]-15;
  if ( d<0 ) d=0;
  scale = c + (d%12) + ofs_s;
  c=0;
  while ( scale > 11 ) { scale-=12; c++; }
  while ( scale <  0 ) { scale+=12; c--; }

  scale = ym2151_note[scale];

  oct = c + (d/12) + ofs_o;
  if ( oct>7 ) oct = 7;
  else if ( oct<0 ) oct = 0;


  /* key on/off */

  if (instrument_map[instrument].note_on[slot]==2 ) {
	//if (instrument_map[instrument].hold==FLAG_TRUE ) {
	  reg_write( 0x08, slot + instrument_map[instrument].base_voice);
	//}
	instrument_map[instrument].note_on[slot] = 1;
  }
  if (instrument_map[instrument].note_on[slot] > 0 ||
	   (instrument_map[instrument].note_on[slot]==0 ) ) { // && instrument_map[instrument].hold==FLAG_TRUE ) ) {
	key= instrument_map[instrument].slot_mask<<3;
  }
  else {
	  instrument_map[instrument].note_on[slot]=-1;
	key=0;
  }


  /* write to register */

  f1 = oct*16 + scale;
  f2 = kf*4;
  f3 = key + slot;

  reg_write( 0x28 + slot + instrument_map[instrument].base_voice, f1 );  /* OCT, NOTE */
  reg_write( 0x30 + slot + instrument_map[instrument].base_voice, f2 );  /* KF */
  reg_write( 0x08,        f3 + instrument_map[instrument].base_voice);  /* KEY ON */

  /*reg_write( ch, 0x38 + slot + instrument_map[instrument].base_voice, 0x50 );   /* PMS:5, AMS:0 */

  return;
}

/*
  MIDI-VOL: m (0..127)
  OPM-TL  : TL

  TL = -40*log(m)/0.75

 */

static int vol_table[128] = {
  127,109, 99, 91, 85, 81, 76, 73, 70, 67, 65, 62, 60, 58, 57, 55,
   53, 52, 50, 49, 48, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 37,
   36, 35, 34, 34, 33, 32, 31, 31, 30, 30, 29, 28, 28, 27, 27, 26,
   25, 25, 24, 24, 23, 23, 22, 22, 22, 21, 21, 20, 20, 19, 19, 18,
   18, 18, 17, 17, 16, 16, 16, 15, 15, 15, 14, 14, 14, 13, 13, 13,
   12, 12, 12, 11, 11, 11, 10, 10, 10, 10,  9,  9,  9,  8,  8,  8,
	8,  7,  7,  7,  7,  6,  6,  6,  5,  5,  5,  5,  5,  4,  4,  4,
	4,  3,  3,  3,  3,  2,  2,  2,  2,  2,  1,  1,  1,  1,  0,  0
};

static void volume_write( int instrument, int slot ) {
  int i,r,v;
  int vol;
  int velocity = 0;

  /* set volume */

  if (instrument_map[instrument].velocity[slot] > 0 )
	velocity = instrument_map[instrument].velocity[slot] / 2 + 63;
  for ( i=0 ; i<4 ; i++ ) {
	r = slot + i*8 + instrument_map[instrument].base_voice;
	if ( is_vol_set[instrument_map[instrument].algorithm][i]==0 ) continue;
	
	vol = (int)((long)instrument_map[instrument].master_volume * instrument_map[instrument].expression * velocity * instrument_map[instrument].total_level[i]/127/127/127);
	vol *= system_volume / 127;

	if ( vol > 127 ) vol = 127;
	if ( vol < 0 )   vol = 0;
	v = vol_table[vol];
	
	reg_write( 0x60+r, v );                      /* TL */
  }

  return;
}

/* ------------------------------------------------------------------- */

/* register actions */

static void reg_write( int adr, int val ) {

  if ( adr > 0x0ff ) return;
  if ( adr < 0 ) return;

  ym2151_register_map[adr] = val;

  ym2151_write_reg( YMPSG, adr, val );

  return;
}

static int reg_read( int adr ) {

  if ( adr > 0xff ) return 0;
  if ( adr < 0 ) return 0;

  return ym2151_register_map[adr];
}

int setup_configuration(VFB_DATA *vfb) {
	int i;

	vfb->active_config.key_receive_mode = VFB_KM_ALL;
	vfb->active_config.combine = 1;

	for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	{
		vfb->active_config.instruments[i].detune = 0;
		vfb->active_config.instruments[i].input_controller = 0;
		vfb->active_config.instruments[i].key_low_limit = 0;
		vfb->active_config.instruments[i].key_high_limit = 127;
		vfb->active_config.instruments[i].LFO_enable = 1;
		vfb->active_config.instruments[i].midi_channel = i;
		vfb->active_config.instruments[i].mono_poly = 1;
		vfb->active_config.instruments[i].note_count = 1;
		vfb->active_config.instruments[i].octave_transpose = 0;
		vfb->active_config.instruments[i].output_level = 127;
		vfb->active_config.instruments[i].pan = 64;
		vfb->active_config.instruments[i].pitch_bend_range = 2;
		vfb->active_config.instruments[i].portamento_time = 0;
		vfb->active_config.instruments[i].voice = i;
		vfb->active_config.instruments[i].voice_bank = 0;
	}

	// TODO
	vfb->active_config.AMD = 0;
	vfb->active_config.PMD = 0;
	vfb->active_config.LFO_speed = 0;
	vfb->active_config.LFO_waveform = 0;

	memset(vfb->active_config.name, 0, sizeof(vfb->active_config.name));
	memcpy(vfb->active_config.name, "single", sizeof("single" - 1));
	
	return 0;
}
