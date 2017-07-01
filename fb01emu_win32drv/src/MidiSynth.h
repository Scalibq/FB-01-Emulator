/* Copyright (C) 2011-2016 Sergey V. Mikayev
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "stdafx.h"

#ifndef MMSynth_MIDISYNTH_H
#define MMSynth_MIDISYNTH_H

namespace MT32Emu {

class MidiSynth {
public:
	virtual int Init() = 0;
	virtual void Close() = 0;
	virtual int Reset() = 0;
	virtual void RenderAvailableSpace() = 0;
	virtual void Render(Bit16s *bufpos, DWORD totalFrames) = 0;
	virtual Bit32u getMIDIEventTimestamp() = 0;
	virtual void PlayMIDI(DWORD msg) = 0;
	virtual void PlaySysex(const Bit8u *bufpos, DWORD len) = 0;
};

}
#endif
