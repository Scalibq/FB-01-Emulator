/*
  MDXplayer : PCM8 emulater

  Made by Daisuke Nagano <breeze.nagano@nifty.ne.jp>
  Jan.21.1999
 */

#ifndef _PCM8_H_
#define _PCM8_H_

#include <stdint.h>

/* ------------------------------------------------------------------ */

#define PCM8_MAX_NOTE        16
#define PCM8_MAX_VOLUME      127
#define PCM8_MASTER_PCM_RATE 44100      /* Hz */
#define PCM8_NUM_OF_BUFFERS  14         /* for SNDCTL_DSP_SETFRAGMENT */
#define PCM8_SYSTEM_RATE     10         /* milli-second */

/* ------------------------------------------------------------------ */

/* functions */

extern int  pcm8_open( VFB_DATA * );
extern int  pcm8_close( void );
extern void pcm8_init( void );
extern void pcm8_start( void );
extern void pcm8_stop( void );

extern void do_pcm8( void );
extern void pcm8(int8_t* sample_buffer, int sample_buffer_size);
extern int pcm8_pan(int ch, int val);

#endif /* _PCM8_H_ */
