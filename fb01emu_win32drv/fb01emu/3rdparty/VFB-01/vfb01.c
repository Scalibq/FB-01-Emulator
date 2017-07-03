/*
  VFB-01 : Virtual FB-01 emulator

  Copyright 2000 by Daisuke Nagano <breeze.nagano@nifty.ne.jp>
  Feb.10.2000

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
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>

//#include <signal.h>

//#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
//#include <unistd.h>
#include <sys/stat.h>

#include <Windows.h>

#ifdef _POSIX_PRIORITY_SCHEDULING
# include <sched.h>
#endif
#ifdef _POSIX_MEMLOCK
# include <sys/mman.h>
#endif

#include "vfb01.h"
#include "smf.h"
#include "pcm8.h"
#include "vfb_device.h"

/* ------------------------------------------------------------------- */

#define VFB_DEBUG 1
#undef  VFB_DEBUG

/* ------------------------------------------------------------------- */

static int note_off( MidiEvent * );
static int note_on( MidiEvent * );
static int key_pressure( MidiEvent * );
static int program_change( MidiEvent * );
static int channel_pressure( MidiEvent * );
static int pitch_wheel( MidiEvent * );
static int control_change( MidiEvent * );
static int system_exclusive( MidiEvent * );

static void priority_init( void );

/* ------------------------------------------------------------------- */

static unsigned char sysex_buf[SYSEX_BUF_SIZE];
static VFB_DATA *evfb=NULL;
static int rpn_adr[VFB_MAX_CHANNEL_NUMBER];
static int nrpn_adr[VFB_MAX_CHANNEL_NUMBER];

/* ------------------------------------------------------------------- */

int vfb01_init( VFB_DATA *vfb ) {

  int i;

  if ( evfb == NULL && vfb != NULL ) evfb=vfb;
  if ( pcm8_open( vfb ) ) return 1;

  if ( setup_voices( vfb ) ) return 1;
  if ( setup_ym2151( vfb ) ) return 1;

  //set_signals();
  priority_init();

  pcm8_start();

  vfb->elapsed_time = 0;
  vfb->total_count = 0;


  for ( i=0 ; i<VFB_MAX_CHANNEL_NUMBER ; i++ ) {
	rpn_adr[i]  = 0xffff;
	nrpn_adr[i] = 0xffff;
  }

  return 0;
}

void vfb01_doMidiEvent( VFB_DATA *vfb, MidiEvent* e ) {

  int i;

	if ( e->ch >= vfb->units ) return;

	switch ( e->type ) {
	case MIDI_NOTEOFF:
	  note_off( e );
	  break;

	case MIDI_NOTEON:
	  note_on( e );
	  break;

	case MIDI_PRESSURE:
	  key_pressure( e );
	  break;

	case MIDI_CONTROL:
	  control_change( e );
	  break;

	case MIDI_PROGRAM:
	  program_change( e );
	  break;

	case MIDI_CHANPRES:
	  channel_pressure( e );
	  break;

	case MIDI_PITCHB:
	  pitch_wheel( e );
	  break;

	case MIDI_SYSEX:
	  system_exclusive( e );
	  break;

	default:
	  break;
	}
	for ( i=0 ; i<vfb->units ; i++ ) {
	  ym2151_set_freq_volume(i);
	}
}

#if 0
int vfb01_run( VFB_DATA *vfb ) {

  MidiEvent e;

  /* main loop */

  while (1) {
	if ( !getMidiEvent( &e ) ) {
#ifdef VFB_DEBUG
	  fprintf(stderr,_("Invalid data\n"));
#endif
	  continue;
	}

	vfb01_doMidiEvent( vfb, &e );
  }

  return 0;
}
#endif

int vfb01_close( VFB_DATA *vfb ) {

  /* finalize all resources */

#ifdef _POSIX_PRIORITY_SCHEDULING
  sched_yield();
#endif
#ifdef _POSIX_MEMLOCK
  munlockall();
#endif

  return 0;
}

/* ------------------------------------------------------------------- */

static int note_off( MidiEvent *ev ) {

#ifdef VFB_DEBUG
  fprintf(stdout, "NOTEOFF:  %02x %02x\n", ev->ch, ev->a);
#endif

  ym2151_note_off( ev->ch, ev->a );

  return 0;
}

static int note_on( MidiEvent *ev ) {

#ifdef VFB_DEBUG
  fprintf(stdout, "NOTE_ON:  %02x %02x %02x\n", ev->ch, ev->a, ev->b);
#endif

  if ( ev->b > 0 ) 
	ym2151_note_on( ev->ch, ev->a, ev->b );
  else
	ym2151_note_off( ev->ch, ev->a );

  return 0;
}

static int key_pressure( MidiEvent *ev ) {

#ifdef VFB_DEBUG
  fprintf(stdout, "KEYPRES:  %02x %02x %02x\n", ev->ch, ev->a, ev->b);
#endif
  return 0;
}

static int program_change( MidiEvent *ev ) {

#ifdef VFB_DEBUG
  fprintf(stdout, "PROGCHG:  %02x %02x\n", ev->ch, ev->a);
#endif

  ym2151_set_voice( ev->ch, ev->a );

  return 0;
}

static int channel_pressure( MidiEvent *ev ) {

#ifdef VFB_DEBUG
  fprintf(stdout, "CHPRESS:  %02x %02x\n", ev->ch, ev->a);
#endif
  return 0;
}

static int pitch_wheel( MidiEvent *ev ) {


  ym2151_set_bend( ev->ch, (ev->b<<7)+ev->a );

#ifdef VFB_DEBUG
  fprintf(stdout, "PITCH:    %02x %04d\n", ev->ch, (ev->b<<7)+ev->a);
#endif
  return 0;
}

/* ------------------------------------------------------------------- */

static int control_change( MidiEvent *ev ) {

  int i;

  switch( ev->a ) {
  case SMF_CTRL_BANK_SELECT_M:
	break;

  case SMF_CTRL_MODULATION_DEPTH:
	ym2151_set_modulation_depth( ev->ch, ev->b );
	break;

  case SMF_CTRL_BLESS_TYPE:
	break;

  case SMF_CTRL_FOOT_TYPE:
	break;

  case SMF_CTRL_PORTAMENT_TIME:
	ym2151_set_portament( ev->ch, ev->b );
	break;

  case SMF_CTRL_DATA_ENTRY_M:
	if ( rpn_adr[ev->ch] == 0x0000 ) {
	  /* pitch bend sensitivity */
	  ym2151_set_bend_sensitivity( ev->ch, ev->b, -1 );
	}
	break;

  case SMF_CTRL_MAIN_VOLUME:
	ym2151_set_master_volume( ev->ch, ev->b );
	break;

  case SMF_CTRL_BALANCE_CTRL:
	break;

  case SMF_CTRL_PANPOT:
	pcm8_pan( ev->ch, ev->b );
	break;

  case SMF_CTRL_EXPRESSION:
	ym2151_set_expression( ev->ch, ev->b );
	break;


  case SMF_CTRL_BANK_SELECT_L:
	break;

  case SMF_CTRL_DATA_ENTRY_L:
	if ( rpn_adr[ev->ch] == 0x0000 ) {
	  /* pitch bend sensitivity */
	  ym2151_set_bend_sensitivity( ev->ch, -1, ev->b );
	}
	break;


  case SMF_CTRL_HOLD1:
	ym2151_set_hold( ev->ch,  ev->b>64?FLAG_TRUE:FLAG_FALSE );
	break;

  case SMF_CTRL_PORTAMENT:
	ym2151_set_portament_on( ev->ch, ev->b>64?FLAG_TRUE:FLAG_FALSE );
	break;

  case SMF_CTRL_SUSTENUTE:
	ym2151_set_hold( ev->ch,  ev->b>64?FLAG_TRUE:FLAG_FALSE );
	break;

  case SMF_CTRL_SOFT_PEDAL:
	break;

  case SMF_CTRL_HOLD2:
	break;


  case SMF_CTRL_REVERB:
	break;

  case SMF_CTRL_TREMOLO:
	break;

  case SMF_CTRL_CHORUS:
	break;

  case SMF_CTRL_DELAY:
	break;

  case SMF_CTRL_PHASER:
	break;


  case SMF_CTRL_DATA_INCREMENT:
	break;

  case SMF_CTRL_DATA_DECREMENT:
	break;

  case SMF_CTRL_NRPM_L:
	nrpn_adr[ev->ch] &= 0xff00;
	nrpn_adr[ev->ch] |= ev->b;
	rpn_adr[ev->ch] = 0xffff;
	break;

  case SMF_CTRL_NRPN_M:
	nrpn_adr[ev->ch] &= 0x00ff;
	nrpn_adr[ev->ch] |= (ev->b<<8);
	rpn_adr[ev->ch] = 0xffff;
	break;

  case SMF_CTRL_RPN_L:
	rpn_adr[ev->ch] &= 0xff00;
	rpn_adr[ev->ch] |= ev->b;
	nrpn_adr[ev->ch] = 0xffff;
	break;

  case SMF_CTRL_RPN_M:
	rpn_adr[ev->ch] &= 0x00ff;
	rpn_adr[ev->ch] |= (ev->b<<8);
	nrpn_adr[ev->ch] = 0xffff;
	break;


  case SMF_CTRL_ALL_SOUND_OFF:
	ym2151_all_note_off(ev->ch);
	reset_ym2151();
	break;

  case SMF_CTRL_RESET_ALL_CTRL:
	reset_ym2151();
	ym2151_set_hold( ev->ch,  FLAG_FALSE );
	ym2151_set_expression( ev->ch, 127 );
	ym2151_set_bend( ev->ch, 8192 );
	for ( i=0 ; i<VFB_MAX_CHANNEL_NUMBER ; i++ ) {
	  rpn_adr[i]  = 0xffff;
	  nrpn_adr[i] = 0xffff;
	}
	break;

  case SMF_CTRL_LOCAL_CONTROL:
	break;

  case SMF_CTRL_ALL_NOTE_OFF:
	ym2151_all_note_off(ev->ch);
	break;

  case SMF_CTRL_OMNI_MODE_OFF:
	break;

  case SMF_CTRL_OMNI_MODE_ON:
	break;

  case SMF_CTRL_MONO_MODE_ON:
	break;

  case SMF_CTRL_POLY_MODE_ON:
	break;

	
  default:
#ifdef VFB_DEBUG
	fprintf(stdout, "CTRLCHG:  %02x %02x %02x\n", ev->ch, ev->a, ev->b);
#endif
	break;
  }
  return 0;
}

/* ------------------------------------------------------------------- */

void param_change_instrument(MidiEvent* ev)
{
	OutputDebugString("FB01: Parameter Change\n");
}

void voice_bulk_data_dump(MidiEvent* ev)
{
	OutputDebugString("FB01: Voice bulk data dump\n");
}

void store_in_voice_RAM(MidiEvent* ev)
{
	OutputDebugString("FB01: Store into voice RAM\n");
}

void system_param_change(MidiEvent* ev)
{
	OutputDebugString("FB01: System parameter change\n");
}

void voice_RAM1_bulk_data_dump(MidiEvent* ev)
{
	OutputDebugString("FB01: Voice RAM 1 bulk data dump\n");
}

void each_voice_bulk_data_dump(MidiEvent* ev)
{
	OutputDebugString("FB01: Each voice bank bulk data dump\n");
}

void current_config_data_dump(MidiEvent* ev)
{
	OutputDebugString("FB01: Current configuration data dump\n");
}

void config_data_dump(MidiEvent* ev)
{
	OutputDebugString("FB01: Configuration data dump\n");
}

void each_config_data_dump(MidiEvent* ev)
{
	OutputDebugString("FB01: 16 configuration data dump\n");
}

void unit_ID_number_dump(MidiEvent* ev)
{
	OutputDebugString("FB01: Unit ID number dump\n");
}

void config_data_store(MidiEvent* ev)
{
	OutputDebugString("FB01: Configuration data store\n");
}

void voice_RAM1_bulk_data_store(MidiEvent* ev)
{
	OutputDebugString("FB01: 48 voice bulk data (voice RAM1)\n");
}

void voice_bulk_data_store(MidiEvent* ev)
{
	OutputDebugString("FB01: 48 voices bulk data (to specific bank)\n");
}

void current_config_store(MidiEvent* ev)
{
	OutputDebugString("FB01: Current configuration\n");
}

void config_memory_store(MidiEvent* ev)
{
	OutputDebugString("FB01: Configuration memory\n");
}

void config_16_memory_store(MidiEvent* ev)
{
	OutputDebugString("FB01: 16 configuration memory\n");
}

void single_voice_bulk_data_store(MidiEvent* ev)
{
	OutputDebugString("FB01: 1 voice bulk data\n");
}

void param_change_channel(MidiEvent* ev)
{
	OutputDebugString("FB01: Parameter change by MIDI channel specification\n");
}

void event_list(MidiEvent* ev)
{
	OutputDebugString("FB01: Event list\n");
}

static int fb01_exclusive( MidiEvent* ev )
{
	/*
	SysEx Messages (see Yamaha FB-01 Service Manual page 12):
	A: Instrument message
	1) Parameter Change (1 byte) by System Channel + Instrument Number
		F0 43 75 <0000ssss> <00011iii> <00pppppp> <0ddddddd> F7
		s = system No.
		i = instrument No.
		d = data
	2) Parameter Change (2 byte) by System Channel + Instrument Number
		F0 43 75 <0000ssss> <00011iii> <01pppppp> <0000dddd> <0000dddd> F7
		s = system No.
		i = instrument No.
		d = data
	3) Voice bulk data dump
		F0 43 75 <0000ssss> <00101iii> 40 00 F7
		s = system No.
		i = instrument No.
	4) Store into voice RAM
		F0 43 75 <0000ssss> <00101iii> 00 <00dddddd> F7
		s = system No.
		i = instrument No.
		d = voice No.

	B: System message
	1) System parameter change
		F0 43 75 <0000ssss> 10 <0ppppppp> <0ddddddd> F7
		s = system No.
		p = parameter No.
		d = data
	2) Voice RAM 1 bulk data dump
		F0 43 <0010ssss> 0C F7
	3) Each voice bank bulk data dump
		F0 43 75 <0000ssss> 20 00 <00000xxx> F7
	4) Current configuration data dump
		F0 43 75 <0000ssss> 20 01 00 F7 (NB: 00 and 01 reversed in Service Manual?)
	5) Configuration data dump
		F0 43 75 <0000ssss> 20 02 <000xxxxx> F7
	6) 16 configuration data dump
		F0 43 75 <0000ssss> 20 03 00 F7
	7) Unit ID number dump
		F0 43 75 <0000ssss> 20 04 00 F7
	8) Configuration data store
		F0 43 75 <0000ssss> 20 40 <000ddddd> F7

	Other system messages
	C: Bulk data
	1) 48 voice bulk data (voice RAM 1)
		F0 43 <0000ssss> 0C 20 00 <0000dddd> <0000dddd> ... <0000dddd> <0000dddd> <0eeeeeee> 10 40 <0000dddd> <0000dddd> ... <0000dddd> <0000dddd> <0eeeeeee> F7
	2) 48 voices bulk data (to specific bank)
		F0 43 75 <0000ssss> 0C 00 00 <00000xxx> 20 10 <0000dddd> <0000dddd> ... <0000dddd> <0000dddd> <0eeeeeee> 10 40 <0000dddd> <0000dddd> ... <0000dddd> <0000dddd> <0eeeeeee> F7
	3) Current configuration
		F0 43 75 <0000ssss> 00 01 00 01 20 <0ddddddd> ... <0ddddddd> <0eeeeeee> F7
	4) Configuration memory
		F0 43 75 <0000ssss> 00 02 <000xxxxx> 01 20 <0ddddddd> ... <0ddddddd> <0eeeeeee> F7
	5) 16 configuration memory
		F0 43 75 <0000ssss> 00 03 00 14 00 <0ddddddd> ... <0ddddddd> <0eeeeeee> F7
	6) 1 voice bulk data
		F0 43 75 <0000ssss> <00001iii> 00 00 01 00 <0000dddd> <0000dddd> ... <0000dddd> <0000dddd> <0eeeeeee> F7

	D: Channel message
	1) Parameter Change (1 byte) by MIDI channel specification
		F0 43 <0001nnnn> 15 <00pppppp> <0ddddddd> F7
	2) Parameter Change (2 byte) by MIDI channel specification
		F0 43 <0001nnnn> 15 <01pppppp> <0000dddd> <0000dddd> F7

	E: Event list
	1) Event list
		F0 43 75 70 <0eeeeeee> ... <0eeeeeee> F7

	Event data embedded in event list (not separate SysEx messages):
	1) Key OFF
		<0000nnnn> <0kkkkkkk> <0fffffff>
	2) Key ON/OFF
		<0001nnnn> <0kkkkkkk> <0fffffff> <0vvvvvvv>
	3) Key ON/OFF (with duration)
		<0010nnnn> <0kkkkkkk> <0fffffff> <0vvvvvvv> <0ddddddd> <0ddddddd>
	4) Parameter change (1 byte)
		<0111nnnn> <00pppppp> <0ddddddd>
	5) Parameter change (2 byte)
		<0111nnnn> <01pppppp> <0000dddd> <0000dddd> (NB: mistakenly listed as <00pppppp> in Service and User Manuals?)

	(not in Service Manual, only in user manual)
	6) Control Change
		<0011nnnn> <0ccccccc> <0vvvvvvv>
	7) Program Change
		<0100nnnn> <0ppppppp>
	8) After Touch
		<0101nnnn> <0vvvvvvv>
	9) Pitch Bend
		<0110nnnn> <0yyyyyyy> <0xxxxxxx>
	*/

	if (ev->ex_buf[1] == 0x75) { /* Sub-status */
		// The following SysEx messages can be recognized by Sub-status 75H as third byte in the SysEx message:
		// A1, A2, A3, A4, B1, B3, B4, B5, B6, B7, B8, C2, C3, C4, C5, C6, E1
		if (ev->ex_buf[2] != 0x70)
		{
			switch (ev->ex_buf[3] & 0xF8)
			{
			case 0x18:
				switch (ev->ex_buf[4] & 0x40)
				{
				case 0x00:
					// A1: F0 43 75 <0000ssss> <00011iii> <00pppppp> <0ddddddd> F7
					param_change_instrument(ev);
					break;
				case 0x40:
					// A2: F0 43 75 <0000ssss> <00011iii> <01pppppp> <0000dddd> <0000dddd> F7
					param_change_instrument(ev);
					break;
				}
				break;
			case 0x28:
				switch (ev->ex_buf[4])
				{
				case 0x40:
					// A3: F0 43 75 <0000ssss> <00101iii> 40 00 F7
					voice_bulk_data_dump(ev);
					break;
				case 0x00:
					// A4: F0 43 75 <0000ssss> <00101iii> 00 <00dddddd> F7
					store_in_voice_RAM(ev);
					break;
				}
				break;
			case 0x10:
				// B1: F0 43 75 <0000ssss> 10 <0ppppppp> <0ddddddd> F7
				system_param_change(ev);
				break;
			case 0x20:
				switch (ev->ex_buf[4])
				{
				case 0x00:
					// B3: F0 43 75 <0000ssss> 20 00 <00000xxx> F7
					each_voice_bulk_data_dump(ev);
					break;
				case 0x01:
					// B4: F0 43 75 <0000ssss> 20 01 00 F7 (NB: 00 and 01 reversed in Service Manual?)
					current_config_data_dump(ev);
					break;
				case 0x02:
					// B5: F0 43 75 <0000ssss> 20 02 <000xxxxx> F7
					config_data_dump(ev);
					break;
				case 0x03:
					// B6: F0 43 75 <0000ssss> 20 03 00 F7
					each_config_data_dump(ev);
					break;
				case 0x04:
					// B7: F0 43 75 <0000ssss> 20 04 00 F7
					unit_ID_number_dump(ev);
					break;
				case 0x40:
					// B8: F0 43 75 <0000ssss> 20 40 <000ddddd> F7
					config_data_store(ev);
					break;
				}
				break;
			case 0xC0:
				// C2: F0 43 75 <0000ssss> 0C 00 00 <00000xxx> 20 10 <0000dddd> <0000dddd> ... <0000dddd> <0000dddd> <0eeeeeee> 10 40 <0000dddd> <0000dddd> ... <0000dddd> <0000dddd> <0eeeeeee> F7
				voice_bulk_data_store(ev);
				break;

			case 0x00:
				switch (ev->ex_buf[4])
				{
				case 0x01:
					// C3: F0 43 75 <0000ssss> 00 01 00 01 20 <0ddddddd> ... <0ddddddd> <0eeeeeee> F7
					current_config_store(ev);
					break;
				case 0x02:
					// C4: F0 43 75 <0000ssss> 00 02 <000xxxxx> 01 20 <0ddddddd> ... <0ddddddd> <0eeeeeee> F7
					config_memory_store(ev);
					break;
				case 0x03:
					// C5: F0 43 75 <0000ssss> 00 03 00 14 00 <0ddddddd> ... <0ddddddd> <0eeeeeee> F7
					config_16_memory_store(ev);
					break;
				}
				break;
			case 0x08:
				// C6: F0 43 75 <0000ssss> <00001iii> 00 00 01 00 <0000dddd> <0000dddd> ... <0000dddd> <0000dddd> <0eeeeeee> F7
				single_voice_bulk_data_store(ev);
				break;
			}
		}
		else
		{
			// E1: F0 43 75 70 <0eeeeeee> ... <0eeeeeee> F7
			event_list(ev);
		}
	}
	else
	{
		// The following SysEx messages do not have 75H as third byte:
		// B2, C1, D1, D2
		switch (ev->ex_buf[2] & 0xF0)
		{
		case 0x20:
			// B2: F0 43 <0010ssss> 0C F7
			voice_RAM1_bulk_data_dump(ev);
			break;
		case 0x00:
			// C1: F0 43 <0000ssss> 0C 20 00 <0000dddd> <0000dddd> ... <0000dddd> <0000dddd> <0eeeeeee> 10 40 <0000dddd> <0000dddd> ... <0000dddd> <0000dddd> <0eeeeeee> F7
			voice_RAM1_bulk_data_store(ev);
			break;
		case 0x01:
			switch (ev->ex_buf[3] & 0x40)
			{
			case 0x00:
				// D1: F0 43 <0001nnnn> 15 <00pppppp> <0ddddddd> F7
				param_change_channel(ev);
				break;
			case 0x40:
				// D2: F0 43 <0001nnnn> 15 <01pppppp> <0000dddd> <0000dddd> F7
				param_change_channel(ev);
				break;
			}
			break;
		}
	}

	return 0;
}

static int system_exclusive( MidiEvent *ev ) {

  int i,j;
  int d;

  if ( ev->ex_buf[0] == 0x43 ) { /* Yamaha FB-01 exclusive */
	  return fb01_exclusive( ev );
  }
  else if ( ev->ex_buf[0] == 0x7e ) { /* Universal non-realtime */
	if ( ev->ex_buf[2] == 0x09 && ev->ex_buf[3] == 0x01 ) {
	  /* GM-MODE on */
	  reset_ym2151();
	  ym2151_all_note_off(ev->ch);
	  ym2151_set_hold( ev->ch,  FLAG_FALSE );
	  ym2151_set_expression( ev->ch, 127 );
	  ym2151_set_bend( ev->ch, 8192 );
	  for ( i=0 ; i<VFB_MAX_CHANNEL_NUMBER ; i++ ) {
	rpn_adr[i]  = 0xffff;
	nrpn_adr[i] = 0xffff;
	  }
#ifdef VFB_DEBUG
	  fprintf(stderr,"SYSEX: GM-Mode on\n");
#endif
	}
  }
  else if ( ev->ex_buf[0] == 0x7f ) { /* Universal realtime */
	if ( ev->ex_buf[2] == 0x04 && ev->ex_buf[3] == 0x01 ) {
	  /* master volume */
	  ym2151_set_system_volume( ev->ex_buf[5] );
#ifdef VFB_DEBUG
	  fprintf(stderr,"SYSEX: Master volume: %d\n",ev->ex_buf[5]);
#endif
	}
  }
  else if ( ev->ex_buf[0] == 0x7d ) { /* vfb01 native */
	if ( ev->ex_buf[1] == 0x0a ) {    /* voice setting */
	  int e[36];
	  int sum=0,c;
	  i=0;
	  j=2;
	  while(i<35) {
	c = ev->ex_buf[j++];
	if ( c==0xf7 ) break;
	e[i++] = c;
	sum+=c;
	  }
	  if ( i == 36 && (sum&0x7f) == 0 &&
	   e[0] < VFB_MAX_TONE_NUMBER && evfb != NULL) {
	VOICE_DATA *v = &evfb->voice[e[0]];
	j=0;
	v->voice_number = e[j++];
	for ( i=0 ; i<4 ; i++ ) {
	  v->ar[i]   = e[j++];
	  v->d1r[i]  = e[j++];
	  v->d2r[i]  = e[j++];
	  v->rr[i]   = e[j++];
	  v->sl[i]   = e[j++];
	  v->tl[i]   = e[j++];
	  v->ks[i]   = e[j++];
	  v->mul[i]  = e[j++];
	  v->dt1[i]  = e[j++];
	  v->dt2[i]  = e[j++];
	  v->ame[i]  = e[j++];
	}
	v->con       = e[j++];
	v->fl        = e[j++];
	v->slot_mask = e[j];
#ifdef VFB_DEBUG	
	fprintf(stderr,"SYSEX: Voice setting: %d\n",v->voice_number);
#endif
	  }
	}
  }


#ifdef VFB_DEBUG
  i=0;
  fprintf(stdout, "SYSEX:    ");
  while (i<SYSEX_BUF_SIZE) {
	d = ev->ex_buf[i++];
	fprintf( stdout,"%02x ", d );fflush(stdout);
	if ( d == 0xf7 ) {
	  fprintf( stdout, "\n" );
	  break;
	}
  }
#endif
  return 0;
}

/* ------------------------------------------------------------------- */

static void sigexit( int num ) {

  closeVirtualMidiDevice();

#ifdef _POSIX_PRIORITY_SCHEDULING
  sched_yield();
#endif
#ifdef _POSIX_MEMLOCK
  munlockall();
#endif

  fprintf(stderr,_("Signal caught : %d\n"), num);
  exit(1);
}

static void sigquit( int num ) {
  closeVirtualMidiDevice();

#ifdef _POSIX_PRIORITY_SCHEDULING
  sched_yield();
#endif
#ifdef _POSIX_MEMLOCK
  munlockall();
#endif

  exit(0);
}

static void sigreset( int num ) {
  reset_ym2151();
  return;
}

static void priority_init( void ) {

#ifdef _POSIX_PRIORITY_SCHEDULING
  struct sched_param ptmp, *priority_param;
  int i;

  priority_param=&ptmp;
  i=sched_get_priority_max( SCHED_FIFO );
  priority_param->sched_priority = i/2; /* no means */
  sched_setscheduler( 0, SCHED_FIFO, priority_param );
#endif
#ifdef _POSIX_MEMLOCK
  mlockall(MCL_CURRENT);
#endif

  return;
}
