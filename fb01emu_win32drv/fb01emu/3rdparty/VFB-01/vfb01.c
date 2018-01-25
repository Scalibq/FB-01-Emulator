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

extern uint8_t SendMidiData(uint8_t* pData, uint32_t length);

static int is_on_instrument(VFB_INSTRUMENT* instrument, int ch, int note);
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

uint8_t checksum(uint8_t* pData, size_t length)
{
	uint8_t c = 0;

	for (size_t i = 0; i < length; i++)
		c += *pData++;

	return (-c) & 0x7F;
}

// Packet type A has 8-bit data, split up into two 4-bit values (MIDI data can not use the MSB of each byte)
void encode_packet_type_A(uint8_t* pDest, uint8_t* pSrc, size_t length)
{
	uint8_t c = 0;

	// Fill header
	size_t len2 = (length + length);

	*pDest++ = len2 >> 7;
	*pDest++ = len2 & 0x7F;

	// Fill data
	for (size_t i = 0; i < length; i++)
	{
		uint8_t d;

		// Low half
		d = *pSrc & 0xF;
		c += d;
		*pDest++ = d;

		// High half
		d = *pSrc++ >> 4;
		c += d;
		*pDest++ = d;
	}

	// Fill checksum
	*pDest = (-c) & 0x7F;
}

// Packet type B has 7-bit data
void encode_packet_type_B(uint8_t* pDest, uint8_t* pSrc, size_t length)
{
	uint8_t c = 0;

	// Fill header
	*pDest++ = length >> 7;
	*pDest++ = length & 0x7F;

	// Fill data
	for (size_t i = 0; i < length; i++)
	{
		uint8_t d;

		d = *pSrc++ & 0x7F;
		c += d;
		*pDest++ = d;
	}

	// Fill checksum
	*pDest = (-c) & 0x7F;
}

// Packet type A has 8-bit data, split up into two 4-bit values (MIDI data can not use the MSB of each byte)
uint8_t decode_packet_type_A(uint8_t* pDest, uint8_t* pSrc, size_t length, size_t* pLength)
{
	uint8_t c = 0;
	size_t i;

	// Decode length
	*pLength = (*pSrc++ & 0x1F) << 7;
	*pLength |= *pSrc++ & 0x7E;

	if (length == 0)
		length = *pLength;

	// Decode all bytes
	for (i = 0; i < length; i += 2)
	{
		uint8_t d;

		d = *pSrc++;
		c += d;
		*pDest = d;

		d = *pSrc++;
		c += d;
		*pDest++ |= d << 4;
	}

	// Calculate length
	*pLength = length;

	// Compare checksum
	c = (-c) & 0x7F;

	return (c == *pSrc);
}

// Packet type B has 7-bit data
uint8_t decode_packet_type_B(uint8_t* pDest, uint8_t* pSrc, size_t length, size_t* pLength)
{
	uint8_t c = 0;
	size_t i;

	// Decode length
	*pLength = (*pSrc++ & 0x1F) << 7;
	*pLength |= *pSrc++ & 0x7F;

	if (length == 0)
		length = *pLength;

	// Decode all bytes
	for (i = 0; i < length; i++)
	{
		uint8_t d;

		d = *pSrc++;
		c += d;
		*pDest++ = d;
	}

	// Compare checksum
	c = (-c) & 0x7F;

	return (c == *pSrc);
}

/* ------------------------------------------------------------------- */

int vfb01_init( VFB_DATA *vfb, int sample_buffer_size ) {

  int i;

  if ( evfb == NULL && vfb != NULL ) evfb=vfb;
  if ( pcm8_open( vfb, sample_buffer_size ) ) return 1;

  if (setup_configuration(vfb))return 1;
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

void convert_voice(VFB_VOICE_DATA* pSrc, int num )
{
	VOICE_DATA* v;
	int i;

	v = &evfb->voice[num];

	// Convert voice data
	v->voice_number = num;
	v->fl = (pSrc->feedback_level_algorithm >> 2) & 0xF;              /* feed back */
	v->con = (pSrc->feedback_level_algorithm) & 0x3;             /* connection:algorithm */
	v->slot_mask = pSrc->operator_enable >> 3;       /* slot mask */

	for (i = 0; i < 4; i++)
	{
		v->dt1[i] = 0;// pSrc->transpose;          /* detune 1 */
		v->dt2[i] = 0;// (pSrc->operator_block[i].params[2] >> 4) & 0x7;          /* detune 2 */
		v->mul[i] = pSrc->operator_block[i].params[2] & 0xF;          /* multiple */
		v->tl[i] = pSrc->operator_block[i].total_level;           /* total level */
		v->ks[i] = 0;// pSrc->operator_block[i].params[6];           /* key scale */
		v->ar[i] = pSrc->operator_block[i].params[3] & 0x1F;           /* attack rate */
		v->ame[i] = pSrc->operator_block[i].params[4] >> 7;          /* amplitude modulation enable */
		v->d1r[i] = pSrc->operator_block[i].params[4] & 0x1F;          /* decay rate 1 */
		v->d2r[i] = pSrc->operator_block[i].params[5] & 0x1F;          /* decay rate 2 ( sustain rate ) */
		v->rr[i] = pSrc->operator_block[i].params[6] & 0xF;           /* release rate */
		v->sl[i] = pSrc->operator_block[i].params[6] >> 4;           /* sustain level */
	}

	// Initialize voice
	//v->v0 = (v->fl & 0x07 << 3) | (v->con & 0x07);
	v->v0 = pSrc->feedback_level_algorithm;
	for (i = 0; i<4; i++) {
		//v->v1[i] = (v->dt1[i] & 0x07 << 4) | (v->mul[i] & 0x0f);
		v->v1[i] = pSrc->operator_block[i].params[2];
		//v->v2[i] = v->tl[i] & 0x7f;
		v->v2[i] = pSrc->operator_block[i].total_level;
		//v->v3[i] = (v->ks[i] & 0x03 << 6) | (v->ar[i] & 0x1f);
		v->v3[i] = pSrc->operator_block[i].params[3];
		//v->v4[i] = (v->ame[i] & 0x01 << 7) | (v->d1r[i] & 0x1f);
		v->v4[i] = pSrc->operator_block[i].params[4];
		//v->v5[i] = (v->dt2[i] & 0x03 << 6) | (v->d2r[i] & 0x1f);
		v->v5[i] = pSrc->operator_block[i].params[5];
		//v->v6[i] = (v->sl[i] & 0x0f << 4) | (v->rr[i] & 0x0f);
		v->v6[i] = pSrc->operator_block[i].params[6];
	}
}

void vfb01_doMidiEvent( VFB_DATA *vfb, MidiEvent* e ) {

  int i;

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

	// Update all instruments
	for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
		ym2151_set_freq_volume(i);
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

	pcm8_close();

#ifdef _POSIX_PRIORITY_SCHEDULING
  sched_yield();
#endif
#ifdef _POSIX_MEMLOCK
  munlockall();
#endif

  return 0;
}

/* ------------------------------------------------------------------- */

static int is_on_instrument(VFB_INSTRUMENT* instrument, int ch, int note)
{
	if (instrument->midi_channel == ch)
	{
		if (note >= instrument->key_low_limit && note <= instrument->key_high_limit)
			return 1;
	}

	return 0;
}

static int note_off( MidiEvent *ev ) {
	int i;
#ifdef VFB_DEBUG
  fprintf(stdout, "NOTEOFF:  %02x %02x\n", ev->ch, ev->a);
#endif

  for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
  {
	  if (is_on_instrument( &evfb->active_config.instruments[i], ev->ch, ev->a))
		ym2151_note_off(i, ev->a);
  }

  return 0;
}

static int note_on( MidiEvent *ev ) {
	int i;
#ifdef VFB_DEBUG
  fprintf(stdout, "NOTE_ON:  %02x %02x %02x\n", ev->ch, ev->a, ev->b);
#endif

  if (ev->b > 0)
  {
	  for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	  {
		  if (is_on_instrument(&evfb->active_config.instruments[i], ev->ch, ev->a))
			  ym2151_note_on(ev->ch, ev->a, ev->b);
	  }
  }
  else
  {
	  for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	  {
		  if (is_on_instrument(&evfb->active_config.instruments[i], ev->ch, ev->a))
			  ym2151_note_off(i, ev->a);
	  }
  }

  return 0;
}

static int key_pressure( MidiEvent *ev ) {

#ifdef VFB_DEBUG
  fprintf(stdout, "KEYPRES:  %02x %02x %02x\n", ev->ch, ev->a, ev->b);
#endif
  return 0;
}

static int program_change( MidiEvent *ev ) {

	int i;
#ifdef VFB_DEBUG
  fprintf(stdout, "PROGCHG:  %02x %02x\n", ev->ch, ev->a);
#endif

  for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
  {
	  if (evfb->active_config.instruments[i].midi_channel == ev->ch)
		  ym2151_set_voice(i, ev->a);
  }

  return 0;
}

static int channel_pressure( MidiEvent *ev ) {

#ifdef VFB_DEBUG
  fprintf(stdout, "CHPRESS:  %02x %02x\n", ev->ch, ev->a);
#endif
  return 0;
}

static int pitch_wheel( MidiEvent *ev ) {

	int i;

	for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	{
		if (evfb->active_config.instruments[i].midi_channel == ev->ch)
			ym2151_set_bend(i, (ev->b << 7) + ev->a);
	}

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
	  for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	  {
		  if (evfb->active_config.instruments[i].midi_channel == ev->ch)
			  ym2151_set_modulation_depth(i, ev->b);
	  }
	break;

  case SMF_CTRL_BLESS_TYPE:
	break;

  case SMF_CTRL_FOOT_TYPE:
	break;

  case SMF_CTRL_PORTAMENT_TIME:
	  for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	  {
		  if (evfb->active_config.instruments[i].midi_channel == ev->ch)
			  ym2151_set_portament(i, ev->b);
	  }
	break;

  case SMF_CTRL_DATA_ENTRY_M:
	if ( rpn_adr[ev->ch] == 0x0000 ) {
	  /* pitch bend sensitivity */
		for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
		{
			if (evfb->active_config.instruments[i].midi_channel == ev->ch)
				ym2151_set_bend_sensitivity(i, ev->b, -1);
		}
	}
	break;

  case SMF_CTRL_MAIN_VOLUME:
	  for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	  {
		  if (evfb->active_config.instruments[i].midi_channel == ev->ch)
			  ym2151_set_master_volume(i, ev->b);
	  }
	break;

  case SMF_CTRL_BALANCE_CTRL:
	break;

  case SMF_CTRL_PANPOT:
	  // TODO: Move into YM2151 emulation? Was in mixing of multiple YM2151 chips, but now removed
	pcm8_pan( ev->ch, ev->b );
	break;

  case SMF_CTRL_EXPRESSION:
	  for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	  {
		  if (evfb->active_config.instruments[i].midi_channel == ev->ch)
			  ym2151_set_expression(i, ev->b);
	  }
	break;


  case SMF_CTRL_BANK_SELECT_L:
	break;

  case SMF_CTRL_DATA_ENTRY_L:
	if ( rpn_adr[ev->ch] == 0x0000 ) {
	  /* pitch bend sensitivity */
		for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
		{
			if (evfb->active_config.instruments[i].midi_channel == ev->ch)
				ym2151_set_bend_sensitivity(i, -1, ev->b);
		}
	}
	break;


  case SMF_CTRL_HOLD1:
	  for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	  {
		  if (evfb->active_config.instruments[i].midi_channel == ev->ch)
			  ym2151_set_hold(i, ev->b > 64 ? FLAG_TRUE : FLAG_FALSE);
	  }
	break;

  case SMF_CTRL_PORTAMENT:
	  for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	  {
		  if (evfb->active_config.instruments[i].midi_channel == ev->ch)
			  ym2151_set_portament_on(i, ev->b > 64 ? FLAG_TRUE : FLAG_FALSE);
	  }
	break;

  case SMF_CTRL_SUSTENUTE:
	  for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	  {
		  if (evfb->active_config.instruments[i].midi_channel == ev->ch)
			  ym2151_set_hold(i, ev->b > 64 ? FLAG_TRUE : FLAG_FALSE);
	  }
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
	  for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	  {
		  if (evfb->active_config.instruments[i].midi_channel == ev->ch)
			  ym2151_all_note_off(i);
	  }
	reset_ym2151();
	break;

  case SMF_CTRL_RESET_ALL_CTRL:
	reset_ym2151();
	for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	{
		if (evfb->active_config.instruments[i].midi_channel == ev->ch)
		{
			ym2151_set_hold(i, FLAG_FALSE);
			ym2151_set_expression(i, 127);
			ym2151_set_bend(i, 8192);
		}
	}
	for ( i=0 ; i<VFB_MAX_CHANNEL_NUMBER ; i++ ) {
	  rpn_adr[i]  = 0xffff;
	  nrpn_adr[i] = 0xffff;
	}
	break;

  case SMF_CTRL_LOCAL_CONTROL:
	break;

  case SMF_CTRL_ALL_NOTE_OFF:
	  for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	  {
		  if (evfb->active_config.instruments[i].midi_channel == ev->ch)
			  ym2151_all_note_off(i);
	  }
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

void unknown_sysex(MidiEvent* ev)
{
	char buf[1024];

	sprintf(buf, "FB01: Unknown sysex: F0 %02X %02X %02X %02X %02X %02X %02X\n",
		ev->ex_buf[0],
		ev->ex_buf[1],
		ev->ex_buf[2],
		ev->ex_buf[3],
		ev->ex_buf[4],
		ev->ex_buf[5],
		ev->ex_buf[6]
	);

	OutputDebugString(buf);
}

void param_change_instrument(MidiEvent* ev)
{
	// A1: F0 43 75 <0000ssss> <00011iii> <00pppppp> <0ddddddd> F7
	// A2: F0 43 75 <0000ssss> <00011iii> <01pppppp> <0000dddd> <0000dddd> F7
	int s, i, p, d, j;

	char buf[1024];

	s = ev->ex_buf[2] & 0x0F;
	i = ev->ex_buf[3] & 0x07;
	p = ev->ex_buf[4] & 0x7F;
	d = ev->ex_buf[5];

	if (p & 0x40)
		d |= ev->ex_buf[6] << 4;

	sprintf(buf, "FB01: Parameter Change, s: %u, i: %u, p: %02X, d: %02X \n", s, i, p, d);
	OutputDebugString(buf);

	switch (p)
	{
	case 0x00:
		evfb->active_config.instruments[i].note_count = d;
		allocate_base_voices();
		break;
	case 0x01:
		evfb->active_config.instruments[i].midi_channel = d;
		break;
	case 0x02:
		evfb->active_config.instruments[i].key_high_limit = d;
		break;
	case 0x03:
		evfb->active_config.instruments[i].key_low_limit = d;
		break;
	case 0x04:
		evfb->active_config.instruments[i].voice_bank = d;

		// Update the YM2164 registers for this instrument
		update_voice(i);
		break;
	case 0x05:
		evfb->active_config.instruments[i].voice = d;

		// Update the YM2164 registers for this instrument
		update_voice(i);
		break;
	}
}

void voice_bulk_data_dump(MidiEvent* ev)
{
	OutputDebugString("FB01: Voice bulk data dump\n");
}

void voice_data_dump(MidiEvent* ev)
{
	OutputDebugString("FB01: Voice data dump\n");

	// Total size is 8 bytes for SysEx prefix and suffix + 3 bytes packet overhead + 64*2 bytes data == 139 bytes
	uint8_t data[139] = { 0xF0, 0x43, 0x75, 0x00, 0x00, 0x00, 0x00 };
	uint8_t bank = ev->ex_buf[3] & 0x03;
	uint8_t voice = ev->ex_buf[5] & 0x3F;

	// Set source
	data[3] = ev->ex_buf[2];
	data[4] = bank;

	// Set terminator byte
	data[138] = 0xF0;

	// Packets start at offset 7, use type A encoding (8-bit data)
	encode_packet_type_A(&data[7], &evfb->voice_banks[bank].voice_data[voice], sizeof(evfb->voice_banks[bank].voice_data[voice]));

	SendMidiData(data, _countof(data));
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

	// Total size is 8 bytes for SysEx prefix and suffix + 49*3 bytes packet overhead + 32*2 +  48*64*2 bytes data == 6363 bytes
	uint8_t data[6363] = { 0xF0, 0x43, 0x75, 0x00, 0x00, 0x00 };
	uint8_t* pData;
	uint8_t bank = ev->ex_buf[5];
	uint32_t i;

	// Set source
	data[3] = ev->ex_buf[2];

	// Set bank number
	data[6] = bank & 0x03;

	// Set terminator byte
	data[6362] = 0xF0;

	// Export header
	// Packets start at offset 7, use type A encoding (8-bit data)
	pData = data + 7;
	encode_packet_type_A(pData, &evfb->voice_banks[bank], 32);

	pData += (32 * 2) + 3;

	// Export all 48 voices of this bank
	for (i = 0; i < 48; i++)
	{
		// Packets start at offset 7, use type A encoding (8-bit data)
		encode_packet_type_A(pData, &evfb->voice_banks[bank].voice_data[i], sizeof(evfb->voice_banks[bank].voice_data[i]));

		pData += (sizeof(evfb->voice_banks[bank].voice_data[i]) * 2) + 3;
	}

	SendMidiData(data, _countof(data));
}

void current_config_data_dump(MidiEvent* ev)
{
	OutputDebugString("FB01: Current configuration data dump\n");

	// Total size is 8 bytes for SysEx prefix and suffix + 3 bytes packet overhead + 160 bytes data == 171 bytes
	uint8_t data[171] = { 0xF0, 0x43, 0x75, 0x00, 0x00, 0x01, 0x00 };

	// Set source
	data[3] = ev->ex_buf[4];

	// Set terminator byte
	data[170] = 0xF0;

	// Packets start at offset 7, use type B encoding (7-bit data)
	encode_packet_type_B(&data[7], &evfb->active_config, sizeof(evfb->active_config));

	SendMidiData(data, _countof(data));
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
	// D1: F0 43 <0001nnnn> 15 <00pppppp> <0ddddddd> F7
	// D2: F0 43 <0001nnnn> 15 <01pppppp> <0000dddd> <0000dddd> F7

	int i, n, p, d;

	OutputDebugString("FB01: Parameter change by MIDI channel specification\n");

	n = ev->ex_buf[2] & 0x0F;
	p = ev->ex_buf[3] & 0x7F;
	d = ev->ex_buf[4];

	if (p & 0x40)
		d |= ev->ex_buf[5] << 4;

	for (i = 0; i < VFB_MAX_FM_SLOTS; i++)
	{
		if (evfb->active_config.instruments[i].midi_channel == n)
		{
			switch (p)
			{
			case 0x00:
				evfb->active_config.instruments[i].note_count = d;
				allocate_base_voices();
				break;
			case 0x01:
				evfb->active_config.instruments[i].midi_channel = d;
				break;
			case 0x02:
				evfb->active_config.instruments[i].key_high_limit = d;
				break;
			case 0x03:
				evfb->active_config.instruments[i].key_low_limit = d;
				break;
			}
		}
	}	
}

void event_list(MidiEvent* ev)
{
	OutputDebugString("FB01: Event list\n");
}

void node_message(MidiEvent* ev)
{
	// C7: F0 43 75 <0000ssss> 00 00 <0000000b> ... F7
	char buf[1024];
	char name[9] = { 0 };
	int len, i;
	uint8_t format, destination;
	format = ev->ex_buf[4];
	destination = ev->ex_buf[5];
	uint8_t *pSrc, *pData;
	uint8_t data[8192];
	uint8_t checkOk = 1;

	switch (format)
	{
	case 0:
		// Voice data bank
		pSrc = ev->ex_buf + 6;
		pData = data;

		// Header
		// Data encoded in type A messages
		checkOk &= decode_packet_type_A(pData, pSrc, 32, &len);

		pSrc += len + 3;
		pData += len >> 1;

		sprintf(buf, "FB01: Voice bank data, length: %u, destination: %u, checksum: %02X, end: %02X\n",
			len, destination, ev->ex_buf[7 + len], ev->ex_buf[8 + len]);
		OutputDebugString(buf);

		// Parse voice header
		memcpy(name, data, 8);
		sprintf(buf, "Voice bank name: %s\n", name);
		OutputDebugString(buf);

		// Voices only have 7 byte names
		name[7] = 0;

		// Parse all 48 voices
		for (i = 0; i < 48; i++)
		{
			checkOk &= decode_packet_type_A(pData, pSrc, 64, &len);

			pSrc += len + 3;
			pData += len >> 1;
		}

		if (!checkOk)
			OutputDebugString("Checksum error!\n");

		// Store data in the proper voice bank
		memcpy(&evfb->voice_banks[destination], data, sizeof(evfb->voice_banks[destination]));

		// Convert to 'native' VFB format
		// TODO: Use real FB-01 voice data directly
		for (i = 0; i < 48; i++)
		{
			convert_voice(&evfb->voice_banks[destination].voice_data[i], i);
		}
		break;
	case 6:
		// Configuration-2
		OutputDebugString("FB01: Configuration-2\n");
		break;
	default:
		unknown_sysex(ev);
		break;
	}
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

	Only documented for IMFC:
	7) Node message, Voice bank data
		F0 43 75 <0000ssss> 00 00 <0000000b> ... F7
	8) Node message, Configuration-2
		F0 43 75 <0000ssss> 00 06 00 ... F7

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
					//store_in_voice_RAM(ev);
					voice_data_dump(ev);
					break;
				default:
					unknown_sysex(ev);
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
				default:
					unknown_sysex(ev);
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
				case 0x00:
					// C7: F0 43 75 <0000ssss> 00 00 <0000000b> ... F7
					node_message(ev);
					break;
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
				case 0x06:
					// C8
					node_message(ev);
				default:
					unknown_sysex(ev);
					break;
				}
				break;
			case 0x08:
				// C6: F0 43 75 <0000ssss> <00001iii> 00 00 01 00 <0000dddd> <0000dddd> ... <0000dddd> <0000dddd> <0eeeeeee> F7
				single_voice_bulk_data_store(ev);
				break;
			default:
				unknown_sysex(ev);
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
		default:
			unknown_sysex(ev);
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
