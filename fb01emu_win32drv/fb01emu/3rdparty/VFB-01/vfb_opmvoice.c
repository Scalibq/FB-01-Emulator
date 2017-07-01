/*
  VFB-01*16 / Virtual FB-01 module

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

#include "vfb01.h"

#ifndef BUFSIZ
# define BUFSIZ 1024
#endif

/*#define VFB_VOICE_DEBUG*/

/* ------------------------------------------------------------------- */

/*
( @000,
#        AR  D1R  D2R   RR   SL   TL   KS  MUL  DT1  DT2  AME
		 31,   0,   7,   0,   4,  32,   3,   2,   2,   0,   0,
		 31,   0,   7,   0,   4,  28,   3,   6,   7,   0,   0,
		 22,   7,   3,   7,   3,   0,   2,   2,   4,   0,   0,
		 22,   7,   3,   7,   3,   0,   2,   2,   1,   0,   0,
#       CON   FL   SM
		  4,   7,  15 )
 */

VOICE_DATA sample = {
  0,                   /* voice_number */
  7,                   /* feed back */
  4,                   /* connection:algorithm */
  15,                  /* slot mask */
  { 2, 7, 4, 1},       /* detune 1 */
  { 0, 0, 0, 0},       /* detune 2 */
  { 1, 3, 1, 1},       /* multiple */
  {32,28,10,10},       /* total level */
  { 3, 3, 2, 2},       /* key scale */
  {31,31,22,22},       /* attack rate */
  { 0, 0, 0, 0},       /* amplitude modulation enable */
  { 0, 0, 7, 7},       /* decay rate 1 */
  { 7, 7, 3, 3},       /* decay rate 2 ( sustain rate ) */
  { 0, 0, 7, 7},       /* release rate */
  { 4, 4, 3, 3},       /* sustain level */

  0,
  { 0,0,0,0,},
  { 0,0,0,0,},
  { 0,0,0,0,},
  { 0,0,0,0,},
  { 0,0,0,0,},
  { 0,0,0,0 },
};

/* ------------------------------------------------------------------- */

static int iserror = FLAG_FALSE;

/* ------------------------------------------------------------------- */

static int skipLine( FILE *fp ) {
  int c;

  while (1) {
	c = fgetc(fp);
	if ( c==EOF ) {
	  ungetc( c, fp );
	  break;
	}
	if ( c=='\n' ) break;
	if ( c=='\r' ) break;
  }

  return;
}

static int getUInt( FILE *fp ) {

  int c;
  int i;
  unsigned char buf[BUFSIZ+1];

  while (1) {
	c = fgetc(fp);
	if ( c==EOF ) return -1;
	if ( c=='#' ) skipLine(fp);
	if ( c==')' ) return -1;
	if ( isdigit((unsigned char)c) ) break;
  }
  i=0;
  buf[i++] = (unsigned char)c;

  while(1) {
	c=fgetc(fp);
	if ( c==EOF ) return -1;
	if ( c=='#' ) skipLine(fp);
	if ( i>BUFSIZ ) return -1;
	if ( !isdigit((unsigned char)c) ) break;
	buf[i++] = (unsigned char)c;
  }
  ungetc( c, fp );
  buf[i] = '\0';

  c = atoi(buf);
  return c;
}

static int getUIntm( FILE *fp, int max ) {
  int ret;

  if ( iserror == FLAG_TRUE ) return 0;

  ret = getUInt(fp);
  if ( ret > max ) ret = max;
  if ( ret < 0 ) {
	iserror = FLAG_TRUE;
	ret = 0;
  }

  return ret;
}

static int getVoiceNum( FILE *fp ) {

  int c;

  while(1) {
	c = fgetc(fp);
	if ( c==EOF ) return -1;
	if ( c=='#' ) skipLine(fp);
	if ( c=='(' ) break;
  }

  while(1) {
	c = fgetc(fp);
	if ( c==EOF ) return -1;
	if ( c=='#' ) skipLine(fp);
	if ( c=='@' ) break;
  }

  c = getUInt(fp);
  if ( c<0 ) return -1;

  return c;
}

static int skiptoNextObject( FILE *fp ) {

  int c;

  while (1) {
	c = fgetc(fp);
	if ( c==EOF ) return 1;
	if ( c=='#' ) skipLine(fp);
	if ( c==')' ) break;
  }

  while (1) {
	c = fgetc(fp);
	if ( c==EOF ) return 1;
	if ( c=='#' ) skipLine(fp);
	if ( c=='(' ) break;
  }
  ungetc( c, fp );

  return 0;
}

/* ------------------------------------------------------------------- */

static int initVoice( VFB_DATA *vfb, int num ) {

  int i;

  VOICE_DATA *v = &vfb->voice[num];
	
  v->voice_number = num;

  v->con       = sample.con;
  v->fl        = sample.fl;
  v->slot_mask = sample.slot_mask;
  v->v0        = (sample.fl&0x07<<3) | (sample.con&0x07);

  for ( i=0 ; i<4 ; i++ ) {
	v->mul[i]  = sample.mul[i];
	v->dt1[i]  = sample.dt1[i];
	
	v->tl[i]   = sample.tl[i];
	
	v->ar[i]   = sample.ar[i];
	v->ks[i]   = sample.ks[i];
	
	v->d1r[i]  = sample.d1r[i];
	v->ame[i]  = sample.ame[i];
	
	v->d2r[i]  = sample.d2r[i];
	v->dt2[i]  = sample.dt2[i];
	
	v->rr[i]   = sample.rr[i];
	v->sl[i]   = sample.sl[i];
	
	v->v1[i]   = (sample.dt1[i]&0x07<<4) | (sample.mul[i]&0x0f);
	v->v2[i]   =  sample.tl[i]&0x7f;
	v->v3[i]   = (sample.ks[i] &0x03<<6) | (sample.ar[i] &0x1f);
	v->v4[i]   = (sample.ame[i]&0x01<<7) | (sample.d1r[i]&0x1f);
	v->v5[i]   = (sample.dt2[i]&0x03<<6) | (sample.d2r[i]&0x1f);
	v->v6[i]   = (sample.sl[i] &0x0f<<4) | (sample.rr[i] &0x0f);
  }

  return 0;
}

/* ------------------------------------------------------------------- */

int setup_voices( VFB_DATA *vfb ) {

  FILE *fp;
  int num;
  VOICE_DATA *v;
  int i;

  for ( num=0 ; num<VFB_MAX_TONE_NUMBER ; num++ ) {
	initVoice( vfb, num );
  }

  if ( vfb->voice_parameter_file == NULL ) return 0;

  fp = fopen( vfb->voice_parameter_file, "r" );
  if ( fp == NULL ) return 0;
  iserror = FLAG_FALSE;

  while (1) {
	num = getVoiceNum(fp);
	if ( num < 0 ) break;

	v = &vfb->voice[num];
	v->voice_number = num;

	for ( i=0 ; i<4 ; i++ ) {
	  v->ar[i]  = getUIntm( fp, 31 );
	  v->d1r[i] = getUIntm( fp, 31 );
	  v->d2r[i] = getUIntm( fp, 31 );
	  v->rr[i]  = getUIntm( fp, 15 );
	  v->sl[i]  = getUIntm( fp, 15 );
	  v->tl[i]  = getUIntm( fp, 127);
	  v->ks[i]  = getUIntm( fp, 1  );
	  v->mul[i] = getUIntm( fp, 15 );
	  v->dt1[i] = getUIntm( fp, 7 );
	  v->dt2[i] = getUIntm( fp, 3 );
	  v->ame[i] = getUIntm( fp, 1 );
	  if ( iserror == FLAG_TRUE ) goto parse_failed;
	}
	v->con = getUIntm( fp, 7 );
	v->fl  = getUIntm( fp, 7 );
	v->slot_mask = getUIntm( fp, 15 );
	if ( iserror == FLAG_TRUE ) goto parse_failed;

	v->v0      = (v->fl&0x07<<3) | (v->con&0x07);
	for ( i=0 ; i<4 ; i++ ) {
	  v->v1[i]   = (v->dt1[i]&0x07<<4) | (v->mul[i]&0x0f);
	  v->v2[i]   =  v->tl[i] &0x7f;
	  v->v3[i]   = (v->ks[i] &0x03<<6) | (v->ar[i] &0x1f);
	  v->v4[i]   = (v->ame[i]&0x01<<7) | (v->d1r[i]&0x1f);
	  v->v5[i]   = (v->dt2[i]&0x03<<6) | (v->d2r[i]&0x1f);
	  v->v6[i]   = (v->sl[i] &0x0f<<4) | (v->rr[i] &0x0f);
	}

#ifdef VFB_VOICE_DEBUG
	fprintf(stderr, "( @%03d, \n", num);
	fprintf(stderr, "#\t AR  D1R  D2R   RR   SL   TL   KS  MUL  DT1  DT2  AME\n");
	for ( i=0 ; i<4 ; i++ ) {
	  fprintf(stderr, "\t%3d, %3d, %3d, %3d, %3d, %3d, %3d, %3d, %3d, %3d, %3d,\n",
		  v->ar[i],
		  v->d1r[i],
		  v->d2r[i],
		  v->rr[i],
		  v->sl[i],
		  v->tl[i],
		  v->ks[i],
		  v->mul[i],
		  v->dt1[i],
		  v->dt2[i],
		  v->ame[i] );
	}
	fprintf(stderr, "#\tCON   FL   SM\n");
	fprintf(stderr, "\t%3d, %3d, %3d )\n",
		v->con,
		v->fl,
		v->slot_mask );
#endif
	if ( skiptoNextObject( fp ) ) break;
	continue;

  parse_failed:
	initVoice( vfb, num );
	if ( skiptoNextObject( fp ) ) break;
	continue;
  }

  fclose(fp);
  return 0;
}

