/*
 *  Copyright (C) 2002-2017  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */


#include <math.h>
#include "dosbox.h"
#include "inout.h"
#include "pic.h"
#include "mem.h"
#include "mixer.h"
#include "PIT_8254.h"
#include "setup.h"

static INLINE void BIN2BCD(Bit16u& val) {
	Bit16u temp=val%10 + (((val/10)%10)<<4)+ (((val/100)%10)<<8) + (((val/1000)%10)<<12);
	val=temp;
}

static INLINE void BCD2BIN(Bit16u& val) {
	Bit16u temp= (val&0x0f) +((val>>4)&0x0f) *10 +((val>>8)&0x0f) *100 +((val>>12)&0x0f) *1000;
	val=temp;
}

bool PIT_8254::counter_output(Bitu counter) {
	PIT_Block * p=&pit[counter];
	double index=PIC_FullIndex()-p->start;
	switch (p->mode) {
	case 0:
		if (p->new_mode) return false;
		if (index>p->delay) return true;
		else return false;
		break;
	case 2:
		if (p->new_mode) return true;
		index=fmod(index,(double)p->delay);
		return index>0;
	case 3:
		if (p->new_mode) return true;
		index=fmod(index,(double)p->delay);
		return index*2<p->delay;
	case 4:
		//Only low on terminal count
		// if(fmod(index,(double)p->delay) == 0) return false; //Maybe take one rate tick in consideration
		//Easiest solution is to report always high (Space marines uses this mode)
		return true;
	default:
		LOG(LOG_PIT,LOG_ERROR)("Illegal Mode %d for reading output",p->mode);
		return true;
	}
}

void PIT_8254::status_latch(Bitu counter) {
	// the timer status can not be overwritten until it is read or the timer was 
	// reprogrammed.
	if(!latched_timerstatus_locked)	{
		PIT_Block * p=&pit[counter];
		latched_timerstatus=0;
		// Timer Status Word
		// 0: BCD 
		// 1-3: Timer mode
		// 4-5: read/load mode
		// 6: "NULL" - this is 0 if "the counter value is in the counter" ;)
		// should rarely be 1 (i.e. on exotic modes)
		// 7: OUT - the logic level on the Timer output pin
		if(p->bcd)latched_timerstatus|=0x1;
		latched_timerstatus|=((p->mode&7)<<1);
		if((p->read_state==0)||(p->read_state==3)) latched_timerstatus|=0x30;
		else if(p->read_state==1) latched_timerstatus|=0x10;
		else if(p->read_state==2) latched_timerstatus|=0x20;
		if(counter_output(counter)) latched_timerstatus|=0x80;
		if(p->new_mode) latched_timerstatus|=0x40;
		// The first thing that is being read from this counter now is the
		// counter status.
		p->counterstatus_set=true;
		latched_timerstatus_locked=true;
	}
}

void PIT_8254::counter_latch(Bitu counter) {
	/* Fill the read_latch of the selected counter with current count */
	PIT_Block * p=&pit[counter];
	p->go_read_latch=false;

	//If gate is disabled don't update the read_latch
	if (!p->gate && p->mode !=1) return;
	if (GCC_UNLIKELY(p->new_mode)) {
		double passed_time = PIC_FullIndex() - p->start;
		Bitu ticks_since_then = (Bitu)(passed_time / (1000.0/p->tick_rate));
		//if (p->mode==3) ticks_since_then /= 2; // TODO figure this out on real hardware
		p->read_latch -= ticks_since_then;
		return;
	}
	double index=PIC_FullIndex()-p->start;
	switch (p->mode) {
	case 4:		/* Software Triggered Strobe */
	case 0:		/* Interrupt on Terminal Count */
		/* Counter keeps on counting after passing terminal count */
		if (index>p->delay) {
			index-=p->delay;
			if(p->bcd) {
				index = fmod(index,(1000.0/p->tick_rate)*10000.0);
				p->read_latch = (Bit16u)(9999-index*(p->tick_rate/1000.0));
			} else {
				index = fmod(index,(1000.0/p->tick_rate)*(double)0x10000);
				p->read_latch = (Bit16u)(0xffff-index*(p->tick_rate/1000.0));
			}
		} else {
			p->read_latch=(Bit16u)(p->cntr-index*(p->tick_rate/1000.0));
		}
		break;
	case 1: // countdown
		if(p->counting) {
			if (index>p->delay) { // has timed out
				p->read_latch = 0xffff; //unconfirmed
			} else {
				p->read_latch=(Bit16u)(p->cntr-index*(p->tick_rate/1000.0));
			}
		}
		break;
	case 2:		/* Rate Generator */
		index=fmod(index,(double)p->delay);
		p->read_latch=(Bit16u)(p->cntr - (index/p->delay)*p->cntr);
		break;
	case 3:		/* Square Wave Rate Generator */
		index=fmod(index,(double)p->delay);
		index*=2;
		if (index>p->delay) index-=p->delay;
		p->read_latch=(Bit16u)(p->cntr - (index/p->delay)*p->cntr);
		// In mode 3 it never returns odd numbers LSB (if odd number is written 1 will be
		// subtracted on first clock and then always 2)
		// fixes "Corncob 3D"
		p->read_latch&=0xfffe;
		break;
	default:
		LOG(LOG_PIT,LOG_ERROR)("Illegal Mode %d for reading counter %d",p->mode,counter);
		p->read_latch=0xffff;
		break;
	}
}

void PIT_8254::write_latch(Bitu counter,Bitu val) {
	PIT_Block * p=&pit[counter];
	if(p->bcd == true) BIN2BCD(p->write_latch);
   
	switch (p->write_state) {
		case 0:
			p->write_latch = p->write_latch | ((val & 0xff) << 8);
			p->write_state = 3;
			break;
		case 3:
			p->write_latch = val & 0xff;
			p->write_state = 0;
			break;
		case 1:
			p->write_latch = val & 0xff;
			break;
		case 2:
			p->write_latch = (val & 0xff) << 8;
		break;
	}
	if (p->bcd==true) BCD2BIN(p->write_latch);
   	if (p->write_state != 0) {
		if (p->write_latch == 0) {
			if (p->bcd == false) p->cntr = 0x10000;
			else p->cntr=9999;
		} else p->cntr = p->write_latch;

		if ((!p->new_mode) && (p->mode == 2) && (counter == 0)) {
			// In mode 2 writing another value has no direct effect on the count
			// until the old one has run out. This might apply to other modes too.
			// This is not fixed for PIT2 yet!!
			p->update_count=true;
			return;
		}
		p->start=PIC_FullIndex();
		p->delay=(1000.0f/((float)p->tick_rate/(float)p->cntr));

		UpdateLatch(counter);

		p->new_mode=false;
	}
}

Bitu PIT_8254::read_latch(Bitu counter) {
	Bit8u ret=0;
	if(GCC_UNLIKELY(pit[counter].counterstatus_set)){
		pit[counter].counterstatus_set = false;
		latched_timerstatus_locked = false;
		ret = latched_timerstatus;
	} else {
		if (pit[counter].go_read_latch == true) 
			counter_latch(counter);

		if( pit[counter].bcd == true) BIN2BCD(pit[counter].read_latch);

		switch (pit[counter].read_state) {
		case 0: /* read MSB & return to state 3 */
			ret=(pit[counter].read_latch >> 8) & 0xff;
			pit[counter].read_state = 3;
			pit[counter].go_read_latch = true;
			break;
		case 3: /* read LSB followed by MSB */
			ret = pit[counter].read_latch & 0xff;
			pit[counter].read_state = 0;
			break;
		case 1: /* read LSB */
			ret = pit[counter].read_latch & 0xff;
			pit[counter].go_read_latch = true;
			break;
		case 2: /* read MSB */
			ret = (pit[counter].read_latch >> 8) & 0xff;
			pit[counter].go_read_latch = true;
			break;
		default:
			E_Exit("Timer.cpp: error in readlatch");
			break;
		}
		if( pit[counter].bcd == true) BCD2BIN(pit[counter].read_latch);
	}
	return ret;
}

void PIT_8254::write_ctrl(Bitu val) {
	Bitu counter=(val >> 6) & 0x03;
	switch (counter) {
	case 0:
	case 1:
	case 2:
		if ((val & 0x30) == 0) {
			/* Counter latch command */
			counter_latch(counter);
		} else {
			// save output status to be used with timer 0 irq
			bool old_output = counter_output(0);
			// save the current count value to be re-used in undocumented newmode
			counter_latch(counter);
			pit[counter].bcd = (val&1)>0;   
			if (val & 1) {
				if(pit[counter].cntr>=9999) pit[counter].cntr=9999;
			}

			// Timer is being reprogrammed, unlock the status
			if(pit[counter].counterstatus_set) {
				pit[counter].counterstatus_set=false;
				latched_timerstatus_locked=false;
			}
			pit[counter].start = PIC_FullIndex(); // for undocumented newmode
			pit[counter].go_read_latch = true;
			pit[counter].update_count = false;
			pit[counter].counting = false;
			pit[counter].read_state  = (val >> 4) & 0x03;
			pit[counter].write_state = (val >> 4) & 0x03;
			Bit8u mode             = (val >> 1) & 0x07;
			if (mode > 5)
				mode -= 4; //6,7 become 2 and 3

			pit[counter].mode = mode;

			UpdateMode(counter, mode, old_output);

			pit[counter].new_mode = true;
		}
		break;
	case 3:
		if ((val & 0x20)==0) {	/* Latch multiple pit counters */
			if (val & 0x02) counter_latch(0);
			if (val & 0x04) counter_latch(1);
			if (val & 0x08) counter_latch(2);
		}
		// status and values can be latched simultaneously
		if ((val & 0x10)==0) {	/* Latch status words */
			// but only 1 status can be latched simultaneously
			if (val & 0x02) status_latch(0);
			else if (val & 0x04) status_latch(1);
			else if (val & 0x08) status_latch(2);
		}
		break;
	}
}

void PIT_8254::SetGate(Bitu counter, bool in) {
	//No changes if gate doesn't change
	if(pit[counter].gate == in) return;
	Bit8u & mode=pit[counter].mode;
	switch(mode) {
	case 0:
		if(in) pit[counter].start = PIC_FullIndex();
		else {
			//Fill readlatch and store it.
			counter_latch(counter);
			pit[counter].cntr = pit[counter].read_latch;
		}
		break;
	case 1:
		// gate 1 on: reload counter; off: nothing
		if(in) {
			pit[counter].counting = true;
			pit[counter].start = PIC_FullIndex();
		}
		break;
	case 2:
	case 3:
		//If gate is enabled restart counting. If disable store the current read_latch
		if(in) pit[counter].start = PIC_FullIndex();
		else counter_latch(counter);
		break;
	case 4:
	case 5:
		LOG(LOG_MISC,LOG_WARN)("unsupported gate mode %x",mode);
		break;
	}
	pit[counter].gate = in; //Set it here so the counter_latch above works
}
