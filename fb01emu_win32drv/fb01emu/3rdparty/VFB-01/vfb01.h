/*
  VFB-01 : Virtual FB-01 emulator

  Copyright 2000 by Daisuke Nagano <breeze.nagano@nifty.ne.jp>
  Feb.10.1999

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

#ifndef _VFB01_H_
#define _VFB01_H_

#include <stdint.h>

/* ------------------------------------------------------------------- */

#define VFB_MAX_CHANNEL_NUMBER        16 /* up to 16 ( <= heavy load ) */
#define VFB_INITIAL_VOICE_NUMBER      0

#ifndef VOICE_PARAMETER_NAME
# define VOICE_PARAMETER_NAME "tone_sample.cfg"
#endif

/* ------------------------------------------------------------------- */

#define VFB_MAX_FM_SLOTS              8
#define VFB_MAX_TONE_NUMBER         256

#define FLAG_TRUE                     1
#define FLAG_FALSE                    0

#define VFB_PAN_N                     0
#define VFB_PAN_L                     1
#define VFB_PAN_R                     2
#define VFB_PAN_C                     3

#define SYSEX_BUF_SIZE             8192
#define VFB_VERSION_TEXT_SIZE       256

/* ------------------------------------------------------------------- */

typedef struct _MidiEvent {
  uint8_t type;
  uint8_t ch;
  uint8_t a,b;
  uint8_t ex_buf[SYSEX_BUF_SIZE];
} MidiEvent;

typedef struct _VOICE_DATA {
  int voice_number;
  int fl;              /* feed back */
  int con;             /* connection:algorithm */
  int slot_mask;       /* slot mask */
  int dt1[4];          /* detune 1 */
  int dt2[4];          /* detune 2 */
  int mul[4];          /* multiple */
  int tl[4];           /* total level */
  int ks[4];           /* key scale */
  int ar[4];           /* attack rate */
  int ame[4];          /* amplitude modulation enable */
  int d1r[4];          /* decay rate 1 */
  int d2r[4];          /* decay rate 2 ( sustain rate ) */
  int rr[4];           /* release rate */
  int sl[4];           /* sustain level */

  int v0;
  int v1[4];
  int v2[4];
  int v3[4];
  int v4[4];
  int v5[4];
  int v6[4];

} VOICE_DATA;

typedef struct _VFB_DATA {
  
  unsigned char version_1[VFB_VERSION_TEXT_SIZE];
  unsigned char version_2[VFB_VERSION_TEXT_SIZE];

  VOICE_DATA voice[VFB_MAX_TONE_NUMBER];

  char *voice_parameter_file;

  /* playing work area */

  long total_count;       /* total steps */
  long elapsed_time;      /* unit = microsecound */

  /* user configuration */

  int master_volume;

  /* work parameter */

  int is_normal_exit;

  int verbose;

  int  dsp_speed;

} VFB_DATA;

/* ------------------------------------------------------------------- */

extern int vfb01_init( VFB_DATA *, int );
extern int vfb01_run( VFB_DATA * );
extern int vfb01_close( VFB_DATA * );
extern void vfb01_doMidiEvent(VFB_DATA *vfb, MidiEvent* e);

extern int getMidiEvent( MidiEvent * );

/* ------------------------------------------------------------------- */

extern VOICE_DATA **voices;
extern void* YMPSG;

/* ------------------------------------------------------------------- */

#endif /* _VFB01_H_ */
