/*
  SMF data structure

  Copyright 1999 by Daisuke Nagano <breeze.nagano@nifty.ne.jp>
  Mar.12.1999


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

#ifndef _SMF_H_
#define _SMF_H_

#define SMF_MTHD_HEADER_SIZE 14

#define SMF_HEADER_STRING    "MThd"
#define SMF_TRACK_STRING     "MTrk"

#define SMF_TERM  -1            /* terminator */

/*
  These definitions are introduced from midiplay/midi.h
  by Takanari HAYAMA 
   */

/* MIDI COMMANDS */
#define MIDI_NOTEOFF    0x80    /* Note off */
#define MIDI_NOTEON     0x90    /* Note on */
#define MIDI_PRESSURE   0xa0    /* Polyphonic key pressure */
#define MIDI_CONTROL    0xb0    /* Control change */
#define MIDI_PROGRAM    0xc0    /* Program change */
#define MIDI_CHANPRES   0xd0    /* Channel pressure */
#define MIDI_PITCHB     0xe0    /* Pitch wheel change */
#define MIDI_SYSEX      0xf0    /* System exclusive data */
#define MIDI_META       0xff    /* Meta event header */

/* META-EVENT MESSAGE TYPES */
#define META_SEQNUM             0x00    /* Sequence number */
#define META_TEXT               0x01    /* Text event */
#define META_COPYRIGHT          0x02    /* Copyright notice */
#define META_SEQNAME            0x03    /* Sequence/track name */
#define META_INSTNAME           0x04    /* Instrument name */
#define META_LYRIC              0x05    /* Lyric */
#define META_MARKER             0x06    /* Marker */
#define META_CUEPT              0x07    /* Cue point */
#define META_EOT                0x2f    /* End of track */
#define META_TEMPO              0x51    /* Set tempo */
#define META_SMPTE              0x54    /* SMPTE offset */
#define META_TIMESIG            0x58    /* Time signature */
#define META_KEYSIG             0x59    /* Key signature */
#define META_SEQSPEC            0x7f    /* Sequencer-specific event */

#define META_PORT               0x21    /* Port change (unauthorized) */

/* CONTROL CHANGE FUNCTIONS */
#define SMF_CTRL_BANK_SELECT_M      0x00
#define SMF_CTRL_MODULATION_DEPTH   0x01
#define SMF_CTRL_BLESS_TYPE         0x02
#define SMF_CTRL_FOOT_TYPE          0x04
#define SMF_CTRL_PORTAMENT_TIME     0x05
#define SMF_CTRL_DATA_ENTRY_M       0x06
#define SMF_CTRL_MAIN_VOLUME        0x07
#define SMF_CTRL_BALANCE_CTRL       0x08
#define SMF_CTRL_PANPOT             0x0a
#define SMF_CTRL_EXPRESSION         0x0b

#define SMF_CTRL_BANK_SELECT_L      0x20
#define SMF_CTRL_DATA_ENTRY_L       0x26

#define SMF_CTRL_HOLD1              0x40
#define SMF_CTRL_PORTAMENT          0x41
#define SMF_CTRL_SUSTENUTE          0x42
#define SMF_CTRL_SOFT_PEDAL         0x43
#define SMF_CTRL_HOLD2              0x45

#define SMF_CTRL_REVERB             0x5b
#define SMF_CTRL_TREMOLO            0x5c
#define SMF_CTRL_CHORUS             0x5d
#define SMF_CTRL_DELAY              0x5e
#define SMF_CTRL_PHASER             0x5f

#define SMF_CTRL_DATA_INCREMENT     0x60
#define SMF_CTRL_DATA_DECREMENT     0x61
#define SMF_CTRL_NRPM_L             0x62
#define SMF_CTRL_NRPN_M             0x63
#define SMF_CTRL_RPN_L              0x64
#define SMF_CTRL_RPN_M              0x65

#define SMF_CTRL_ALL_SOUND_OFF      0x78
#define SMF_CTRL_RESET_ALL_CTRL     0x79
#define SMF_CTRL_LOCAL_CONTROL      0x7a
#define SMF_CTRL_ALL_NOTE_OFF       0x7b
#define SMF_CTRL_OMNI_MODE_OFF      0x7c
#define SMF_CTRL_OMNI_MODE_ON       0x7d
#define SMF_CTRL_MONO_MODE_ON       0x7e
#define SMF_CTRL_POLY_MODE_ON       0x7f

/* functions */

int *smf_number_conversion( long num );

#endif /* _SMF_H_ */
