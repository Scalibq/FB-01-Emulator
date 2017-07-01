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
#include "MidiSynth.h"

#ifndef MT32EMU_FB01SYNTH_H
#define MT32EMU_FB01SYNTH_H

namespace MT32Emu {

class FB01Synth : public MidiSynth {
private:
	unsigned int sampleRate;
	unsigned int midiLatency;
	unsigned int bufferSize;
	unsigned int chunkSize;
	unsigned int settingsVersion;
	bool useRingBuffer;
	bool resetEnabled;

	MIDIDelayMode midiDelayMode;
	double sampleRateRatio;

	Bit16s *buffer;
	volatile UINT64 renderedFramesCount;

	Synth *synth;
	unsigned int MillisToFrames(unsigned int millis);
	void LoadWaveOutSettings();
	void ReloadSettings();
	void ApplySettings();

	FB01Synth();

public:
	static MidiSynth &getInstance();

	virtual int Init();
	virtual void Close();
	virtual int Reset();
	virtual void RenderAvailableSpace();
	virtual void Render(Bit16s *bufpos, DWORD totalFrames);
	virtual Bit32u getMIDIEventTimestamp();
	virtual void PlayMIDI(DWORD msg);
	virtual void PlaySysex(const Bit8u *bufpos, DWORD len);
};

}
#endif
