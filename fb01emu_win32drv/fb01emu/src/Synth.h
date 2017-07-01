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

#ifndef MT32EMU_SYNTH_H
#define MT32EMU_SYNTH_H

#include <cstdarg>
#include <cstddef>
#include <cstring>

#include "globals.h"
#include "Types.h"
#include "Enumerations.h"
extern "C"
{
#include "vfb01.h"
}

namespace MT32Emu {

class MidiEventQueue;
struct PCMWaveEntry;

static char *command_name = NULL;
static int   verbose = FLAG_FALSE;
static char *dsp_device = NULL;
static char *midi_device = NULL;
static int   master_volume = 127;
static int   units = VFB_MAX_CHANNEL_NUMBER;
static char *voice_parameter_file;
static int   is_use_fragment = FLAG_TRUE;
static int   is_ignore_ch10 = FLAG_FALSE;

static VFB_DATA *vfb = NULL;

// Class for the client to supply callbacks for reporting various errors and information
class MT32EMU_EXPORT ReportHandler {
public:
	virtual ~ReportHandler() {}

	// Callback for debug messages, in vprintf() format
	virtual void printDebug(const char *fmt, va_list list);
	// Callbacks for reporting errors
	virtual void onErrorControlROM() {}
	virtual void onErrorPCMROM() {}
	// Callback for reporting about displaying a new custom message on LCD
	virtual void showLCDMessage(const char *message);
	// Callback for reporting actual processing of a MIDI message
	virtual void onMIDIMessagePlayed() {}
	// Callback for reporting an overflow of the input MIDI queue.
	// Returns true if a recovery action was taken and yet another attempt to enqueue the MIDI event is desired.
	virtual bool onMIDIQueueOverflow() { return false; }
	// Callback invoked when a System Realtime MIDI message is detected at the input.
	virtual void onMIDISystemRealtime(Bit8u /* systemRealtime */) {}
	// Callbacks for reporting system events
	virtual void onDeviceReset() {}
	virtual void onDeviceReconfig() {}
	// Callbacks for reporting changes of reverb settings
	virtual void onNewReverbMode(Bit8u /* mode */) {}
	virtual void onNewReverbTime(Bit8u /* time */) {}
	virtual void onNewReverbLevel(Bit8u /* level */) {}
	// Callbacks for reporting various information
	virtual void onPolyStateChanged(Bit8u /* partNum */) {}
	virtual void onProgramChanged(Bit8u /* partNum */, const char * /* soundGroupName */, const char * /* patchName */) {}
};

class Synth {
friend class DefaultMidiStreamParser;

private:
	MidiEventQueue *midiQueue;
	volatile Bit32u lastReceivedMIDIEventTimestamp;
	volatile Bit32u renderedSampleCount;

	MIDIDelayMode midiDelayMode;

	bool opened;
	bool activated;

	bool isDefaultReportHandler;
	ReportHandler *reportHandler;

	// **************************** Implementation methods **************************

	Bit32u addMIDIInterfaceDelay(Bit32u len, Bit32u timestamp);

	void reset();
	void dispose();

	void printDebug(const char *fmt, ...);

public:
	// Returns library version as an integer in format: 0x00MMmmpp, where:
	// MM - major version number
	// mm - minor version number
	// pp - patch number
	MT32EMU_EXPORT static Bit32u getLibraryVersionInt();
	// Returns library version as a C-string in format: "MAJOR.MINOR.PATCH"
	MT32EMU_EXPORT static const char *getLibraryVersionString();

	MT32EMU_EXPORT static Bit32u getShortMessageLength(Bit32u msg);
	MT32EMU_EXPORT static Bit8u calcSysexChecksum(const Bit8u *data, const Bit32u len, const Bit8u initChecksum = 0);

	// Optionally sets callbacks for reporting various errors, information and debug messages
	MT32EMU_EXPORT explicit Synth(ReportHandler *useReportHandler = NULL);
	MT32EMU_EXPORT ~Synth();

	// Used to initialise the MT-32. Must be called before any other function.
	// Returns true if initialization was sucessful, otherwise returns false.
	MT32EMU_EXPORT bool open();

	// Closes the MT-32 and deallocates any memory used by the synthesizer
	MT32EMU_EXPORT void close();

	// Returns true if the synth is in completely initialized state, otherwise returns false.
	MT32EMU_EXPORT bool isOpen() const;

	// All the enqueued events are processed by the synth immediately.
	MT32EMU_EXPORT void flushMIDIQueue();

	// Sets size of the internal MIDI event queue. The queue size is set to the minimum power of 2 that is greater or equal to the size specified.
	// The queue is flushed before reallocation.
	// Returns the actual queue size being used.
	MT32EMU_EXPORT Bit32u setMIDIEventQueueSize(Bit32u);

	// Enqueues a MIDI event for subsequent playback.
	// The MIDI event will be processed not before the specified timestamp.
	// The timestamp is measured as the global rendered sample count since the synth was created (at the native sample rate 32000 Hz).
	// The minimum delay involves emulation of the delay introduced while the event is transferred via MIDI interface
	// and emulation of the MCU busy-loop while it frees partials for use by a new Poly.
	// Calls from multiple threads must be synchronised, although, no synchronisation is required with the rendering thread.
	// The methods return false if the MIDI event queue is full and the message cannot be enqueued.

	// Enqueues a single short MIDI message to play at specified time. The message must contain a status byte.
	MT32EMU_EXPORT bool playMsg(Bit32u msg, Bit32u timestamp);
	// Enqueues a single well formed System Exclusive MIDI message to play at specified time.
	MT32EMU_EXPORT bool playSysex(const Bit8u *sysex, Bit32u len, Bit32u timestamp);

	// Enqueues a single short MIDI message to be processed ASAP. The message must contain a status byte.
	MT32EMU_EXPORT bool playMsg(Bit32u msg);
	// Enqueues a single well formed System Exclusive MIDI message to be processed ASAP.
	MT32EMU_EXPORT bool playSysex(const Bit8u *sysex, Bit32u len);

	// WARNING:
	// The methods below don't ensure minimum 1-sample delay between sequential MIDI events,
	// and a sequence of NoteOn and immediately succeeding NoteOff messages is always silent.
	// A thread that invokes these methods must be explicitly synchronised with the thread performing sample rendering.

	// Sends a short MIDI message to the synth for immediate playback. The message must contain a status byte.
	// See the WARNING above.
	MT32EMU_EXPORT void playMsgNow(Bit32u msg);

	// Sends a single well formed System Exclusive MIDI message for immediate processing. The length is in bytes.
	// See the WARNING above.
	MT32EMU_EXPORT void playSysexNow(const Bit8u *sysex, Bit32u len);
	// Sends inner body of a System Exclusive MIDI message for direct processing. The length is in bytes.
	// See the WARNING above.

	// Sets new MIDI delay mode. See MIDIDelayMode for details.
	MT32EMU_EXPORT void setMIDIDelayMode(MIDIDelayMode mode);
	// Returns current MIDI delay mode. See MIDIDelayMode for details.
	MT32EMU_EXPORT MIDIDelayMode getMIDIDelayMode() const;

	// Returns actual sample rate used in emulation of stereo analog circuitry of hardware units.
	// See comment for render() below.
	MT32EMU_EXPORT Bit32u getStereoOutputSampleRate() const;

	// Renders samples to the specified output stream as if they were sampled at the analog stereo output.
	// When AnalogOutputMode is set to ACCURATE (OVERSAMPLED), the output signal is upsampled to 48 (96) kHz in order
	// to retain emulation accuracy in whole audible frequency spectra. Otherwise, native digital signal sample rate is retained.
	// getStereoOutputSampleRate() can be used to query actual sample rate of the output signal.
	// The length is in frames, not bytes (in 16-bit stereo, one frame is 4 bytes). Uses NATIVE byte ordering.
	MT32EMU_EXPORT void render(Bit16s *stream, Bit32u len);

	// Returns true if the synth is active and subsequent calls to render() may result in non-trivial output (i.e. silence).
	// The synth is considered active when either there are pending MIDI events in the queue, there is at least one active partial,
	// or the reverb is (somewhat unreliably) detected as being active.
	MT32EMU_EXPORT bool isActive();
}; // class Synth

} // namespace MT32Emu

#endif // #ifndef MT32EMU_SYNTH_H
