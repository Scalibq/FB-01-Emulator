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

#ifndef DOSBOX_PIT_8254_H
#define DOSBOX_PIT_8254_H

#include <math.h>
#include "dosbox.h"
#include "inout.h"
#include "pic.h"
#include "mem.h"
#include "mixer.h"
#include "setup.h"

struct PIT_Block {
	Bit32u tick_rate;

	Bitu cntr;
	float delay;
	double start;

	Bit16u read_latch;
	Bit16u write_latch;

	Bit8u mode;
	Bit8u latch_mode;
	Bit8u read_state;
	Bit8u write_state;

	bool bcd;
	bool go_read_latch;
	bool new_mode;
	bool counterstatus_set;
	bool counting;
	bool update_count;
	bool gate;
};

class PIT_8254 : public Module_base
{
protected:
	PIT_Block pit[3];
	Bit8u latched_timerstatus;
	// the timer status can not be overwritten until it is read or the timer was 
	// reprogrammed.
	bool latched_timerstatus_locked;

public:
	PIT_8254(Section* configuration):Module_base(configuration)
	{
		latched_timerstatus_locked = false;
	}

	virtual void UpdateMode(Bitu counter, Bit8u mode, bool old_output) = 0;
	virtual void UpdateLatch(Bitu counter) = 0;

	bool counter_output(Bitu counter);
	void status_latch(Bitu counter);
	void counter_latch(Bitu counter);
	void write_latch(Bitu counter,Bitu val);
	Bitu read_latch(Bitu counter);
	void write_ctrl(Bitu val);
	void SetGate(Bitu counter, bool in);
};

#endif /* DOSBOX_PIT_8254_H */
