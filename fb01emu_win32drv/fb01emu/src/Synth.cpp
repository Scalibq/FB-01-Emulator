/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011-2016 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
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

#include <cstdio>

#include "internals.h"

#include "Synth.h"
#include "MidiEventQueue.h"

#include "Windows.h"

extern "C"
{
#include "pcm8.h"
}

namespace MT32Emu {

// MIDI interface data transfer rate in samples. Used to simulate the transfer delay.
static const double MIDI_DATA_TRANSFER_RATE = double(SAMPLE_RATE) / 31250.0 * 8.0;

Bit32u Synth::getLibraryVersionInt() {
	return (MT32EMU_VERSION_MAJOR << 16) | (MT32EMU_VERSION_MINOR << 8) | (MT32EMU_VERSION_PATCH);
}

const char *Synth::getLibraryVersionString() {
	return MT32EMU_VERSION;
}

Bit8u Synth::calcSysexChecksum(const Bit8u *data, const Bit32u len, const Bit8u initChecksum) {
	unsigned int checksum = -initChecksum;
	for (unsigned int i = 0; i < len; i++) {
		checksum -= data[i];
	}
	return Bit8u(checksum & 0x7f);
}

Synth::Synth(ReportHandler *useReportHandler) {
	opened = false;

	if (useReportHandler == NULL) {
		reportHandler = new ReportHandler;
		isDefaultReportHandler = true;
	} else {
		reportHandler = useReportHandler;
		isDefaultReportHandler = false;
	}

	setMIDIDelayMode(MIDIDelayMode_DELAY_SHORT_MESSAGES_ONLY);
	midiQueue = NULL;
	lastReceivedMIDIEventTimestamp = 0;
	renderedSampleCount = 0;
}

Synth::~Synth() {
	close(); // Make sure we're closed and everything is freed
	if (isDefaultReportHandler) {
		delete reportHandler;
	}
}

void ReportHandler::showLCDMessage(const char *data) {
	printf("WRITE-LCD: %s\n", data);
}

void ReportHandler::printDebug(const char *fmt, va_list list) {
	vprintf(fmt, list);
	printf("\n");
}

void Synth::printDebug(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
#if MT32EMU_DEBUG_SAMPLESTAMPS > 0
	reportHandler->printDebug("[%u]", (va_list)&renderedSampleCount);
#endif
	reportHandler->printDebug(fmt, ap);
	va_end(ap);
}

void Synth::setMIDIDelayMode(MIDIDelayMode mode) {
	midiDelayMode = mode;
}

MIDIDelayMode Synth::getMIDIDelayMode() const {
	return midiDelayMode;
}

bool Synth::open() {
	if (opened) {
		return false;
	}

	// TODO
	vfb = (VFB_DATA *)malloc(sizeof(VFB_DATA));
	if (vfb == NULL) {
		return false;
	}

	verbose = FLAG_FALSE;
	midi_device = MIDI_DEVICE_NAME;
	dsp_device = DSP_DEVICE_NAME;
	voice_parameter_file = VOICE_PARAMETER_NAME;
	is_use_fragment = FLAG_TRUE;
	is_ignore_ch10 = FLAG_FALSE;

	vfb->dsp_device = dsp_device;
	vfb->midi_device = midi_device;
	vfb->is_use_fragment = is_use_fragment;
	vfb->is_normal_exit = FLAG_TRUE;
	vfb->is_ignore_ch10 = is_ignore_ch10;
	vfb->verbose = verbose;

	vfb->voice_parameter_file = voice_parameter_file;
	vfb->master_volume = master_volume;
	vfb->units = units;

	vfb01_init(vfb);
	pcm8_open(vfb);

	midiQueue = new MidiEventQueue();

	opened = true;
	activated = false;

#if MT32EMU_MONITOR_INIT
	printDebug("*** Initialisation complete ***");
#endif
	return true;
}

void Synth::dispose() {
	opened = false;

	delete midiQueue;
	midiQueue = NULL;

	if (vfb != NULL)
	{
		pcm8_close();
		vfb01_close(vfb);
		free(vfb);
		vfb = NULL;
	}
}

void Synth::close() {
	if (opened) {
		dispose();
	}
}

bool Synth::isOpen() const {
	return opened;
}

void Synth::flushMIDIQueue() {
	if (midiQueue != NULL) {
		for (;;) {
			const MidiEvent *midiEvent = midiQueue->peekMidiEvent();
			if (midiEvent == NULL) break;
			if (midiEvent->sysexData == NULL) {
				playMsgNow(midiEvent->shortMessageData);
			} else {
				playSysexNow(midiEvent->sysexData, midiEvent->sysexLength);
			}
			midiQueue->dropMidiEvent();
		}
		lastReceivedMIDIEventTimestamp = renderedSampleCount;
	}
}

Bit32u Synth::setMIDIEventQueueSize(Bit32u useSize) {
	static const Bit32u MAX_QUEUE_SIZE = (1 << 24); // This results in about 256 Mb - much greater than any reasonable value

	if (midiQueue == NULL) return 0;
	flushMIDIQueue();

	// Find a power of 2 that is >= useSize
	Bit32u binarySize = 1;
	if (useSize < MAX_QUEUE_SIZE) {
		// Using simple linear search as this isn't time critical
		while (binarySize < useSize) binarySize <<= 1;
	} else {
		binarySize = MAX_QUEUE_SIZE;
	}
	delete midiQueue;
	midiQueue = new MidiEventQueue(binarySize);
	return binarySize;
}

Bit32u Synth::getShortMessageLength(Bit32u msg) {
	if ((msg & 0xF0) == 0xF0) {
		switch (msg & 0xFF) {
			case 0xF1:
			case 0xF3:
				return 2;
			case 0xF2:
				return 3;
			default:
				return 1;
		}
	}
	// NOTE: This calculation isn't quite correct
	// as it doesn't consider the running status byte
	return ((msg & 0xE0) == 0xC0) ? 2 : 3;
}

Bit32u Synth::addMIDIInterfaceDelay(Bit32u len, Bit32u timestamp) {
	Bit32u transferTime =  Bit32u(double(len) * MIDI_DATA_TRANSFER_RATE);
	// Dealing with wrapping
	if (Bit32s(timestamp - lastReceivedMIDIEventTimestamp) < 0) {
		timestamp = lastReceivedMIDIEventTimestamp;
	}
	timestamp += transferTime;
	lastReceivedMIDIEventTimestamp = timestamp;
	return timestamp;
}

bool Synth::playMsg(Bit32u msg) {
	return playMsg(msg, renderedSampleCount);
}

bool Synth::playMsg(Bit32u msg, Bit32u timestamp) {
	if ((msg & 0xF8) == 0xF8) {
		reportHandler->onMIDISystemRealtime(Bit8u(msg));
		return true;
	}
	if (midiQueue == NULL) return false;
	if (midiDelayMode != MIDIDelayMode_IMMEDIATE) {
		timestamp = addMIDIInterfaceDelay(getShortMessageLength(msg), timestamp);
	}
	if (!activated) activated = true;
	do {
		if (midiQueue->pushShortMessage(msg, timestamp)) return true;
	} while (reportHandler->onMIDIQueueOverflow());
	return false;
}

bool Synth::playSysex(const Bit8u *sysex, Bit32u len) {
	return playSysex(sysex, len, renderedSampleCount);
}

bool Synth::playSysex(const Bit8u *sysex, Bit32u len, Bit32u timestamp) {
	if (midiQueue == NULL) return false;
	if (midiDelayMode == MIDIDelayMode_DELAY_ALL) {
		timestamp = addMIDIInterfaceDelay(len, timestamp);
	}
	if (!activated) activated = true;
	do {
		if (midiQueue->pushSysex(sysex, len, timestamp)) return true;
	} while (reportHandler->onMIDIQueueOverflow());
	return false;
}

void Synth::playMsgNow(Bit32u msg) {
	if (!opened) return;

	// NOTE: Active sense IS implemented in real hardware. However, realtime processing is clearly out of the library scope.
	//       It is assumed that realtime consumers of the library respond to these MIDI events as appropriate.

	Bit8u code = Bit8u((msg & 0x0000F0) >> 4);
	Bit8u chan = Bit8u(msg & 0x00000F);
	Bit8u note = Bit8u((msg & 0x007F00) >> 8);
	Bit8u velocity = Bit8u((msg & 0x7F0000) >> 16);

	//printDebug("Playing chan %d, code 0x%01x note: 0x%02x", chan, code, note);

	/*Bit8u part = chantable[chan];
	if (part > 8) {
#if MT32EMU_MONITOR_MIDI > 0
		printDebug("Play msg on unreg chan %d (%d): code=0x%01x, vel=%d", chan, part, code, velocity);
#endif
		return;
	}*/

	::MidiEvent e;
	e.ch = chan;
	e.type = code << 4;
	e.a = note;
	e.b = velocity;

	if (vfb != NULL)
		vfb01_doMidiEvent( vfb, &e );
	//playMsgOnPart(part, code, note, velocity);
}

void Synth::playSysexNow(const Bit8u *sysex, Bit32u len) {
	if (len < 2) {
		printDebug("playSysex: Message is too short for sysex (%d bytes)", len);
	}
	if (sysex[0] != 0xF0) {
		printDebug("playSysex: Message lacks start-of-sysex (0xF0)");
		return;
	}
	// Due to some programs (e.g. Java) sending buffers with junk at the end, we have to go through and find the end marker rather than relying on len.
	Bit32u endPos;
	for (endPos = 1; endPos < len; endPos++) {
		if (sysex[endPos] == 0xF7) {
			break;
		}
	}
	if (endPos == len) {
		printDebug("playSysex: Message lacks end-of-sysex (0xf7)");
		return;
	}
	::MidiEvent e;
	e.ch = 0;
	e.type = 0xF0;
	for (int i = 1; i < len; i++)
		e.ex_buf[i - 1] = sysex[i];

	if (vfb != NULL)
		vfb01_doMidiEvent(vfb, &e);
}

void Synth::reset() {
	if (!opened) return;
#if MT32EMU_MONITOR_SYSEX > 0
	printDebug("RESET");
#endif
	reportHandler->onDeviceReset();
	isActive();
}

MidiEvent::~MidiEvent() {
	if (sysexData != NULL) {
		delete[] sysexData;
	}
}

void MidiEvent::setShortMessage(Bit32u useShortMessageData, Bit32u useTimestamp) {
	if (sysexData != NULL) {
		delete[] sysexData;
	}
	shortMessageData = useShortMessageData;
	timestamp = useTimestamp;
	sysexData = NULL;
	sysexLength = 0;
}

void MidiEvent::setSysex(const Bit8u *useSysexData, Bit32u useSysexLength, Bit32u useTimestamp) {
	if (sysexData != NULL) {
		delete[] sysexData;
	}
	shortMessageData = 0;
	timestamp = useTimestamp;
	sysexLength = useSysexLength;
	Bit8u *dstSysexData = new Bit8u[sysexLength];
	sysexData = dstSysexData;
	memcpy(dstSysexData, useSysexData, sysexLength);
}

MidiEventQueue::MidiEventQueue(Bit32u useRingBufferSize) : ringBuffer(new MidiEvent[useRingBufferSize]), ringBufferMask(useRingBufferSize - 1) {
	memset(ringBuffer, 0, useRingBufferSize * sizeof(MidiEvent));
	reset();
}

MidiEventQueue::~MidiEventQueue() {
	delete[] ringBuffer;
}

void MidiEventQueue::reset() {
	startPosition = 0;
	endPosition = 0;
}

bool MidiEventQueue::pushShortMessage(Bit32u shortMessageData, Bit32u timestamp) {
	Bit32u newEndPosition = (endPosition + 1) & ringBufferMask;
	// Is ring buffer full?
	if (startPosition == newEndPosition) return false;
	ringBuffer[endPosition].setShortMessage(shortMessageData, timestamp);
	endPosition = newEndPosition;
	return true;
}

bool MidiEventQueue::pushSysex(const Bit8u *sysexData, Bit32u sysexLength, Bit32u timestamp) {
	Bit32u newEndPosition = (endPosition + 1) & ringBufferMask;
	// Is ring buffer full?
	if (startPosition == newEndPosition) return false;
	ringBuffer[endPosition].setSysex(sysexData, sysexLength, timestamp);
	endPosition = newEndPosition;
	return true;
}

const MidiEvent *MidiEventQueue::peekMidiEvent() {
	return isEmpty() ? NULL : &ringBuffer[startPosition];
}

void MidiEventQueue::dropMidiEvent() {
	// Is ring buffer empty?
	if (startPosition != endPosition) {
		startPosition = (startPosition + 1) & ringBufferMask;
	}
}

bool MidiEventQueue::isFull() const {
	return startPosition == ((endPosition + 1) & ringBufferMask);
}

bool MidiEventQueue::isEmpty() const {
	return startPosition == endPosition;
}

Bit32u Synth::getStereoOutputSampleRate() const {
	return PCM8_MASTER_PCM_RATE;//SAMPLE_RATE;
}

void Synth::render(Bit16s *stream, Bit32u len) {
	while (len > 0) {
		// We need to ensure zero-duration notes will play so add minimum 1-sample delay.
		Bit32u thisLen = 1;
		const MidiEvent *nextEvent = midiQueue->peekMidiEvent();
		Bit32s samplesToNextEvent = (nextEvent != NULL) ? Bit32s(nextEvent->timestamp - renderedSampleCount) : MAX_SAMPLES_PER_RUN;
		if (samplesToNextEvent > 0) {
			thisLen = len > MAX_SAMPLES_PER_RUN ? MAX_SAMPLES_PER_RUN : len;
			if (thisLen > Bit32u(samplesToNextEvent)) {
				thisLen = samplesToNextEvent;
			}
		}
		else {
			if (nextEvent->sysexData == NULL) {
				playMsgNow(nextEvent->shortMessageData);
				// If a poly is aborting we don't drop the event from the queue.
				// Instead, we'll return to it again when the abortion is done.
				midiQueue->dropMidiEvent();
			}
			else {
				playSysexNow(nextEvent->sysexData, nextEvent->sysexLength);
				midiQueue->dropMidiEvent();
			}
		}
		pcm8((int8_t*)stream, thisLen);
		stream += thisLen*2;
		len -= thisLen;
		renderedSampleCount += thisLen;
	}
}

bool Synth::isActive() {
	if (!opened) {
		return false;
	}
	if (!midiQueue->isEmpty()) {
		return true;
	}
	activated = false;
	return false;
}

} // namespace MT32Emu
