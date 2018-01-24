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

#ifdef __MINGW32__

#define sscanf_s sscanf

typedef LONG LSTATUS;

#endif // defined(__MINGW32__)

// For the sake of Win9x compatibility
#undef CreateEvent
#define CreateEvent CreateEventA

extern "C"
{
	extern uint8_t SendMidiData(uint8_t* pData, uint32_t length);
}

namespace MT32Emu {

static const char MT32EMU_REGISTRY_PATH[] = "Software\\muntemu.org\\Munt mt32emu-qt";
static const char MT32EMU_REGISTRY_DRIVER_SUBKEY_V1[] = "waveout";
static const char MT32EMU_REGISTRY_DRIVER_SUBKEY_V2[] = "Audio\\waveout";
static const char MT32EMU_REGISTRY_MASTER_SUBKEY[] = "Master";
static const char MT32EMU_REGISTRY_PROFILES_SUBKEY[] = "Profiles";

// Each frame consists of two samples for both the Left and Right channels
static const unsigned int SAMPLES_PER_FRAME = 2;

static MidiSynth &midiSynth = FB01Synth::getInstance();

static class SynthEventWin32 {
private:
	HANDLE hEvent;

public:
	int Init() {
		hEvent = CreateEvent(NULL, false, true, NULL);
		if (hEvent == NULL) {
			MessageBox(NULL, L"Can't create sync object", L"FB01", MB_OK | MB_ICONEXCLAMATION);
			return 1;
		}
		return 0;
	}

	void Close() {
		CloseHandle(hEvent);
	}

	void Wait() {
		WaitForSingleObject(hEvent, INFINITE);
	}

	void Release() {
		SetEvent(hEvent);
	}
} synthEvent;

static class WaveOutWin32 {
private:
	HWAVEOUT	hWaveOut;
	WAVEHDR		*WaveHdr;
	HANDLE		hEvent;
	HANDLE		hThread;
	DWORD		chunks;

	volatile UINT64 prevPlayPosition;
	volatile bool stopProcessing;

public:
	int Init(Bit16s *buffer, unsigned int bufferSize, unsigned int chunkSize, bool useRingBuffer, unsigned int sampleRate) {
		DWORD callbackType = CALLBACK_NULL;
		DWORD_PTR callback = (DWORD_PTR)NULL;
		hEvent = NULL;
		hThread = NULL;
		if (!useRingBuffer) {
			hEvent = CreateEvent(NULL, false, true, NULL);
			callback = (DWORD_PTR)hEvent;
			callbackType = CALLBACK_EVENT;
		}

		PCMWAVEFORMAT wFormat = {WAVE_FORMAT_PCM, 2, sampleRate, sampleRate * 4, 4, 16};

		// Open waveout device
		int wResult = waveOutOpen(&hWaveOut, WAVE_MAPPER, (LPWAVEFORMATEX)&wFormat, callback, (DWORD_PTR)&midiSynth, callbackType);
		if (wResult != MMSYSERR_NOERROR) {
			MessageBox(NULL, L"Failed to open waveform output device", L"FB01", MB_OK | MB_ICONEXCLAMATION);
			return 2;
		}

		// Prepare headers
		chunks = useRingBuffer ? 1 : bufferSize / chunkSize;
		WaveHdr = new WAVEHDR[chunks];
		LPSTR chunkStart = (LPSTR)buffer;
		DWORD chunkBytes = 4 * chunkSize;
		for (UINT i = 0; i < chunks; i++) {
			if (useRingBuffer) {
				WaveHdr[i].dwBufferLength = 4 * bufferSize;
				WaveHdr[i].lpData = chunkStart;
				WaveHdr[i].dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
				WaveHdr[i].dwLoops = -1L;
			} else {
				WaveHdr[i].dwBufferLength = chunkBytes;
				WaveHdr[i].lpData = chunkStart;
				WaveHdr[i].dwFlags = 0L;
				WaveHdr[i].dwLoops = 0L;
				chunkStart += chunkBytes;
			}
			wResult = waveOutPrepareHeader(hWaveOut, &WaveHdr[i], sizeof(WAVEHDR));
			if (wResult != MMSYSERR_NOERROR) {
				MessageBox(NULL, L"Failed to Prepare Header", L"FB01", MB_OK | MB_ICONEXCLAMATION);
				return 3;
			}
		}
		stopProcessing = false;
		return 0;
	}

	int Close() {
		stopProcessing = true;
		SetEvent(hEvent);
		if (hThread != NULL) {
#ifdef ENABLE_DEBUG_OUTPUT
			std::cout << "Waiting for rendering thread to die\n";
#endif
			WaitForSingleObject(hThread, INFINITE);
			hThread = NULL;
		}
		int wResult = waveOutReset(hWaveOut);
		if (wResult != MMSYSERR_NOERROR) {
			MessageBox(NULL, L"Failed to Reset WaveOut", L"FB01", MB_OK | MB_ICONEXCLAMATION);
			return 8;
		}

		for (UINT i = 0; i < chunks; i++) {
			wResult = waveOutUnprepareHeader(hWaveOut, &WaveHdr[i], sizeof(WAVEHDR));
			if (wResult != MMSYSERR_NOERROR) {
				MessageBox(NULL, L"Failed to Unprepare Wave Header", L"FB01", MB_OK | MB_ICONEXCLAMATION);
				return 8;
			}
		}
		delete[] WaveHdr;
		WaveHdr = NULL;

		wResult = waveOutClose(hWaveOut);
		if (wResult != MMSYSERR_NOERROR) {
			MessageBox(NULL, L"Failed to Close WaveOut", L"FB01", MB_OK | MB_ICONEXCLAMATION);
			return 8;
		}
		if (hEvent != NULL) {
			CloseHandle(hEvent);
			hEvent = NULL;
		}
		return 0;
	}

	int Start() {
		prevPlayPosition = 0;
		for (UINT i = 0; i < chunks; i++) {
			if (waveOutWrite(hWaveOut, &WaveHdr[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
				MessageBox(NULL, L"Failed to write block to device", L"FB01", MB_OK | MB_ICONEXCLAMATION);
				return 4;
			}
		}
		hThread = (HANDLE)_beginthread(RenderingThread, 16384, this);
		SetThreadPriority(hThread, THREAD_PRIORITY_TIME_CRITICAL);
		return 0;
	}

	int Pause() {
		if (waveOutPause(hWaveOut) != MMSYSERR_NOERROR) {
			MessageBox(NULL, L"Failed to Pause wave playback", L"FB01", MB_OK | MB_ICONEXCLAMATION);
			return 9;
		}
		return 0;
	}

	int Resume() {
		if (waveOutRestart(hWaveOut) != MMSYSERR_NOERROR) {
			MessageBox(NULL, L"Failed to Resume wave playback", L"FB01", MB_OK | MB_ICONEXCLAMATION);
			return 9;
		}
		return 0;
	}

	UINT64 GetPos() {
		static const DWORD WRAP_BITS = 27;
		static const UINT64 WRAP_MASK = (1 << 27) - 1;
		static const int WRAP_THRESHOLD = 1 << (WRAP_BITS - 1);

		// Taking a snapshot to avoid possible thread interference
		UINT64 playPositionSnapshot = prevPlayPosition;
		DWORD wrapCount = DWORD(playPositionSnapshot >> WRAP_BITS);
		DWORD wrappedPosition = DWORD(playPositionSnapshot & WRAP_MASK);

		MMTIME mmTime;
		mmTime.wType = TIME_SAMPLES;

		if (waveOutGetPosition(hWaveOut, &mmTime, sizeof(MMTIME)) != MMSYSERR_NOERROR) {
			MessageBox(NULL, L"Failed to get current playback position", L"FB01", MB_OK | MB_ICONEXCLAMATION);
			return 10;
		}
		if (mmTime.wType != TIME_SAMPLES) {
			MessageBox(NULL, L"Failed to get # of samples played", L"FB01", MB_OK | MB_ICONEXCLAMATION);
			return 10;
		}
		mmTime.u.sample &= WRAP_MASK;

		// Deal with waveOutGetPosition() wraparound. For 16-bit stereo output, it equals 2^27,
		// presumably caused by the internal 32-bit counter of bits played.
		// The output of that nasty waveOutGetPosition() isn't monotonically increasing
		// even during 2^27 samples playback, so we have to ensure the difference is big enough...
		int delta = mmTime.u.sample - wrappedPosition;
		if (delta < -WRAP_THRESHOLD) {
#ifdef ENABLE_DEBUG_OUTPUT
			std::cout << "FB01: GetPos() wrap: " << delta << "\n";
#endif
			++wrapCount;
		} else if (delta < 0) {
			// This ensures the return is monotonically increased
#ifdef ENABLE_DEBUG_OUTPUT
			std::cout << "FB01: GetPos() went back by " << delta << " samples\n";
#endif
			return playPositionSnapshot;
		}
		prevPlayPosition = playPositionSnapshot = mmTime.u.sample + (wrapCount << WRAP_BITS);
		return playPositionSnapshot;
	}

	static void RenderingThread(void *);
} waveOut;

void WaveOutWin32::RenderingThread(void *) {
	if (waveOut.chunks == 1) {
		// Rendering using single looped ring buffer
		while (!waveOut.stopProcessing) {
			midiSynth.RenderAvailableSpace();
		}
	} else {
		while (!waveOut.stopProcessing) {
			bool allBuffersRendered = true;
			for (UINT i = 0; i < waveOut.chunks; i++) {
				if (waveOut.WaveHdr[i].dwFlags & WHDR_DONE) {
					allBuffersRendered = false;
					midiSynth.Render((Bit16s *)waveOut.WaveHdr[i].lpData, waveOut.WaveHdr[i].dwBufferLength / 4);
					if (waveOutWrite(waveOut.hWaveOut, &waveOut.WaveHdr[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
						MessageBox(NULL, L"Failed to write block to wave device", L"FB01", MB_OK | MB_ICONEXCLAMATION);
					}
				}
			}
			if (allBuffersRendered) {
				// Ensure the playback position is monitored frequently enough in order not to miss a wraparound
				waveOut.GetPos();
				WaitForSingleObject(waveOut.hEvent, INFINITE);
			}
		}
	}
#ifdef ENABLE_DEBUG_OUTPUT
	std::cout << "Rendering thread stopped\n";
#endif
}

static class : public ReportHandler {
protected:
	virtual void onErrorControlROM() {
		MessageBox(NULL, L"Couldn't open Control ROM file", L"FB01", MB_OK | MB_ICONEXCLAMATION);
	}

	virtual void onErrorPCMROM() {
		MessageBox(NULL, L"Couldn't open PCM ROM file", L"FB01", MB_OK | MB_ICONEXCLAMATION);
	}

	virtual void showLCDMessage(const char *message) {
		std::cout << "FB01: LCD-Message: " << message << "\n";
	}

#ifndef ENABLE_DEBUG_OUTPUT
	void printDebug(const char *fmt, va_list list) {}
#endif
} reportHandler;

FB01Synth::FB01Synth() {}

MidiSynth &FB01Synth::getInstance() {
	static FB01Synth instance;
	return instance;
}

// Renders all the available space in the single looped ring buffer
void FB01Synth::RenderAvailableSpace() {
	DWORD playPosition = DWORD(waveOut.GetPos() % bufferSize);
	DWORD renderPosition = DWORD(renderedFramesCount % bufferSize);
	DWORD framesToRender;

	if (playPosition < renderPosition) {
		// Buffer wrap, render 'till the end of the buffer
		framesToRender = bufferSize - renderPosition;
	} else {
		framesToRender = playPosition - renderPosition;
		if (framesToRender < chunkSize) {
			Sleep(1 + (chunkSize - framesToRender) * 1000 / sampleRate);
			return;
		}
	}
	midiSynth.Render(buffer + SAMPLES_PER_FRAME * renderPosition, framesToRender);
}

// Renders totalFrames frames starting from bufpos
// The number of frames rendered is added to the global counter framesRendered
void FB01Synth::Render(Bit16s *bufpos, DWORD framesToRender) {
	// TODO
	synthEvent.Wait();
	synth->render(bufpos, framesToRender);
	synthEvent.Release();
	renderedFramesCount += framesToRender;
}

static bool LoadBoolValue(HKEY hReg, const char *name, const bool nDefault) {
	if (hReg != NULL) {
		char destination[6];
		DWORD nSize = sizeof(destination);
		DWORD type;
		LSTATUS res = RegQueryValueExA(hReg, name, NULL, &type, (LPBYTE)destination, &nSize);
		if (res == ERROR_SUCCESS && type == REG_SZ) {
			return destination[0] != '0' && (destination[0] & 0xDF) != 'F';
		}
	}
	return nDefault;
}

static int LoadIntValue(HKEY hReg, const char *name, const int nDefault) {
	if (hReg != NULL) {
		DWORD destination = 0;
		DWORD nSize = sizeof(DWORD);
		DWORD type;
		LSTATUS res = RegQueryValueExA(hReg, name, NULL, &type, (LPBYTE)&destination, &nSize);
		if (res == ERROR_SUCCESS && type == REG_DWORD) {
			return destination;
		}
	}
	return nDefault;
}

static float LoadFloatValue(HKEY hReg, const char *name, const float nDefault) {
	if (hReg != NULL) {
		float value = nDefault;
		char destination[32];
		DWORD nSize = sizeof(destination);
		DWORD type;
		LSTATUS res = RegQueryValueExA(hReg, name, NULL, &type, (LPBYTE)&destination, &nSize);
		if (res == ERROR_SUCCESS && type == REG_SZ && sscanf_s(destination, "%f", &value) == 1) {
			return value;
		}
	}
	return nDefault;
}

static DWORD LoadStringValue(HKEY hReg, const char *name, const char *nDefault, char *destination, DWORD nSize) {
	if (hReg != NULL) {
		DWORD type;
		LSTATUS res = RegQueryValueExA(hReg, name, NULL, &type, (LPBYTE)destination, &nSize);
		if (res == ERROR_SUCCESS && type == REG_SZ) {
			return nSize - 1;
		}
	}
	lstrcpynA(destination, nDefault, nSize);
	destination[nSize - 1] = 0;
	return lstrlenA(destination);
}

unsigned int FB01Synth::MillisToFrames(unsigned int millis) {
	return UINT(sampleRate * millis / 1000.0f);
}

void FB01Synth::LoadWaveOutSettings() {
	HKEY hReg;
	if (RegOpenKeyA(HKEY_CURRENT_USER, MT32EMU_REGISTRY_PATH, &hReg)) {
		hReg = NULL;
	}
	HKEY hRegDriver;
	const char *driverKey = (settingsVersion == 1) ? MT32EMU_REGISTRY_DRIVER_SUBKEY_V1 : MT32EMU_REGISTRY_DRIVER_SUBKEY_V2;
	if (hReg == NULL || RegOpenKeyA(hReg, driverKey, &hRegDriver)) {
		hRegDriver = NULL;
	}
	RegCloseKey(hReg);
	hReg = NULL;

	// Approx. bufferSize derived from latency
	bufferSize = MillisToFrames(LoadIntValue(hRegDriver, "AudioLatency", 100));
	chunkSize = MillisToFrames(LoadIntValue(hRegDriver, "ChunkLen", 10));
	midiLatency = MillisToFrames(LoadIntValue(hRegDriver, "MidiLatency", 0));
	useRingBuffer = LoadBoolValue(hRegDriver, "UseRingBuffer", false);
	RegCloseKey(hRegDriver);
	if (useRingBuffer) {
		std::cout << "FB01: Using looped ring buffer, buffer size: " << bufferSize << " frames, min. rendering interval: " << chunkSize <<" frames." << std::endl;
	} else {
		// Number of chunks should be ceil(bufferSize / chunkSize)
		DWORD chunks = (bufferSize + chunkSize - 1) / chunkSize;
		// Refine bufferSize as chunkSize * number of chunks, no less then the specified value
		bufferSize = chunks * chunkSize;
		std::cout << "FB01: Using " << chunks << " chunks, chunk size: " << chunkSize << " frames, buffer size: " << bufferSize << " frames." << std::endl;
	}
	// Default MIDI latency equals the audio buffer length
	if ((settingsVersion == 1) || (midiLatency == 0)) midiLatency += bufferSize;
}

void FB01Synth::ReloadSettings() {
	// TODO
	HKEY hReg;
	if (RegOpenKeyA(HKEY_CURRENT_USER, MT32EMU_REGISTRY_PATH, &hReg)) {
		hReg = NULL;
	}
	HKEY hRegMaster;
	if (hReg == NULL || RegOpenKeyA(hReg, MT32EMU_REGISTRY_MASTER_SUBKEY, &hRegMaster)) {
		hRegMaster = NULL;
	}
	settingsVersion = LoadIntValue(hRegMaster, "settingsVersion", 1);
	resetEnabled = !LoadBoolValue(hRegMaster, "startPinnedSynthRoute", false);
	char profile[256];
	LoadStringValue(hRegMaster, "defaultSynthProfile", "default", profile, sizeof(profile));
	RegCloseKey(hRegMaster);
	hRegMaster = NULL;
	HKEY hRegProfiles;
	if (hReg == NULL || RegOpenKeyA(hReg, MT32EMU_REGISTRY_PROFILES_SUBKEY, &hRegProfiles)) {
		hRegProfiles = NULL;
	}
	RegCloseKey(hReg);
	hReg = NULL;
	HKEY hRegProfile;
	if (hRegProfiles == NULL || RegOpenKeyA(hRegProfiles, profile, &hRegProfile)) {
		hRegProfile = NULL;
	}
	RegCloseKey(hRegProfiles);
	hRegProfiles = NULL;

	midiDelayMode = (MIDIDelayMode)LoadIntValue(hRegProfile, "midiDelayMode", MIDIDelayMode_DELAY_SHORT_MESSAGES_ONLY);
}

void FB01Synth::ApplySettings() {
	// TODO
}

int FB01Synth::Init() {
	// Set a test message
	uint8_t data[] = { 0xF0, 0, 1, 2, 3, 0xF7 };
	
	SendMidiData(data, _countof(data));

	// TODO
	synth = NULL;
	ReloadSettings();

	if (synthEvent.Init()) {
		return 1;
	}

	synth = new Synth(&reportHandler);

	// Placeholders
	if (!synth->open()) {
		synth->close();
		MessageBox(NULL, L"Can't open Synth", L"FB01", MB_OK | MB_ICONEXCLAMATION);
		return 1;
	}
	sampleRate = synth->getStereoOutputSampleRate();
	sampleRateRatio = SAMPLE_RATE / (double)sampleRate;
	LoadWaveOutSettings();
	buffer = new Bit16s[SAMPLES_PER_FRAME * bufferSize];

	ApplySettings();

	UINT wResult = waveOut.Init(buffer, bufferSize, chunkSize, useRingBuffer, sampleRate);
	if (wResult) return wResult;

	// Start playing stream
	synth->render(buffer, bufferSize);
	renderedFramesCount = bufferSize;

	wResult = waveOut.Start();
	return wResult;
}

int FB01Synth::Reset() {
	// TODO
	ReloadSettings();
	if (!resetEnabled) {
		synthEvent.Wait();
		ApplySettings();
		synthEvent.Release();
		return 0;
	}

	UINT wResult = waveOut.Pause();
	if (wResult) return wResult;

	synthEvent.Wait();
	synth->close();
	if (!synth->open()) {
		synth->close();
		synthEvent.Release();
		MessageBox(NULL, L"Can't open Synth", L"FB01", MB_OK | MB_ICONEXCLAMATION);
		return 1;
	}
	ApplySettings();
	//FreeROMImages();
	synthEvent.Release();

	wResult = waveOut.Resume();
	return wResult;
}

Bit32u FB01Synth::getMIDIEventTimestamp() {
	// Taking a snapshot to avoid interference with the rendering thread
	UINT64 renderedFramesCountSnapshot = renderedFramesCount;
	Bit32u renderPosition = Bit32u(renderedFramesCountSnapshot % bufferSize);

	// Using relative play position helps to keep correct timing after underruns
	Bit32u playPosition = Bit32u(waveOut.GetPos() % bufferSize);
	Bit32s bufferedFramesCount = renderPosition - playPosition;
	if (bufferedFramesCount <= 0) {
		bufferedFramesCount += bufferSize;
	}
	UINT64 playedFramesCount = renderedFramesCountSnapshot - bufferedFramesCount;
	// Estimated MIDI event timestamp in audio output samples
	UINT64 timestamp = playedFramesCount + midiLatency;
	return Bit32u(timestamp * sampleRateRatio);
}

void FB01Synth::PlayMIDI(DWORD msg) {
	// TODO
	synth->playMsg(msg, getMIDIEventTimestamp());
}

void FB01Synth::PlaySysex(const Bit8u *bufpos, DWORD len) {
	// TODO
	synth->playSysex(bufpos, len, getMIDIEventTimestamp());
}

void FB01Synth::Close() {
	// TODO
	waveOut.Pause();
	waveOut.Close();
	synthEvent.Wait();
	synth->close();

	// Cleanup memory
	delete synth;
	delete buffer;

	synthEvent.Close();
}

}
