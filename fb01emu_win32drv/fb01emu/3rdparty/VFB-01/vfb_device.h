/*
  MDXplayer :  YM2151 emulator access routines

  Made by Daisuke Nagano <breeze.nagano@nifty.ne.jp>
  Apr.16.1999
 */

#ifndef _MDX2151_H_
#define _MDX2151_H_

extern int ym2151_open( VFB_DATA * );

extern void ym2151_all_note_off( int );
extern void ym2151_note_on( int, int, int );
extern void ym2151_note_off( int, int );
extern void ym2151_set_pan( int, int );
extern void ym2151_set_master_volume( int, int );
extern void ym2151_set_expression( int, int );
extern void ym2151_set_detune( int, int );
extern void ym2151_set_portament( int, int );
extern void ym2151_set_portament_on( int, int );
extern void ym2151_set_noise_freq( int, int );
extern void ym2151_set_voice( int, int );
extern void ym2151_set_reg( int, int, int );

extern void ym2151_set_plfo( int, int, int, int, int );
extern void ym2151_set_alfo( int, int, int, int, int );
extern void ym2151_set_lfo_delay( int, int );

extern void ym2151_set_hlfo( int, int, int, int, int, int );
extern void ym2151_set_hlfo_onoff( int, int );

extern void ym2151_set_system_volume(int val);
extern void ym2151_set_freq_volume(int ch);
extern void ym2151_set_bend(int ch, int val);
extern void ym2151_set_modulation_depth(int ch, int val);
extern void ym2151_set_bend_sensitivity(int ch, int msb, int lsb);
extern void ym2151_set_hold(int ch, int sw);

extern int setup_ym2151(VFB_DATA *vfb);
extern int reset_ym2151(void);

extern int setup_voices(VFB_DATA *vfb);
extern int setup_configuration(VFB_DATA *vfb);

#endif /* _MDX2151_H_ */
