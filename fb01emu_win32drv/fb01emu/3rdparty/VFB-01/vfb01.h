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
#define VFB_MAX_CONFIGURATIONS       20

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

typedef enum _VFB_INPUT_CONTROLLER {
	VFB_IC_NOT_ASSIGNED,
	VFB_IC_AFTER_TOUCH,
	VFB_IC_MODULATION_WHEEL,
	VFB_IC_BREATH_CONTROLLER,
	VFB_IC_FOOT_CONTROLLER
} VFB_INPUT_CONTROLLER;

typedef struct _VFB_INSTRUMENT {
	uint8_t note_count;
	uint8_t midi_channel;
	uint8_t key_high_limit;
	uint8_t key_low_limit;
	uint8_t voice_bank;
	uint8_t voice;
	uint8_t detune;
	uint8_t octave_transpose;
	uint8_t output_level;
	uint8_t pan;
	uint8_t LFO_enable;
	uint8_t portamento_time;
	uint8_t pitch_bend_range;
	uint8_t mono_poly;
	VFB_INPUT_CONTROLLER input_controller;
} VFB_INSTRUMENT;

typedef enum _VFB_KEY_RECEIVE_MODE {
	VFB_KM_ALL,
	VFB_KM_EVEN,
	VFB_KM_ODD
} VFB_KEY_RECEIVE_MODE;

typedef struct _VFB_CONFIGURATION {
	uint8_t name[8];
	uint8_t combine;
	uint8_t LFO_speed;
	uint8_t AMD;
	uint8_t PMD;
	uint8_t LFO_waveform;
	VFB_KEY_RECEIVE_MODE key_receive_mode;
	VFB_INSTRUMENT instruments[VFB_MAX_FM_SLOTS];
} VFB_CONFIGURATION;

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

typedef struct _VFB_DATA {
  
	unsigned char version_1[VFB_VERSION_TEXT_SIZE];
	unsigned char version_2[VFB_VERSION_TEXT_SIZE];

	VOICE_DATA voice[VFB_MAX_TONE_NUMBER];
	VFB_CONFIGURATION configuration[VFB_MAX_CONFIGURATIONS];

	char *voice_parameter_file;

	/* playing work area */

	long total_count;       /* total steps */
	long elapsed_time;      /* unit = microsecound */

	VFB_CONFIGURATION active_config;
	MIDI_MAP instrument_map[VFB_MAX_FM_SLOTS];	// These are indexed 1:1 with the instruments in the active configuration
						  
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
