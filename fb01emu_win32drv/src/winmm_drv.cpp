/* Copyright (C) 2003, 2004, 2005 Dean Beeler, Jerome Fisher
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

#include "stdafx.h"

#define MAX_DRIVERS 8
#define MAX_CLIENTS 8 // Per driver

namespace {

static bool hrTimerAvailable;
static double mult;
static LARGE_INTEGER counter;
static LARGE_INTEGER nanoCounter = {0L};

void initNanoTimer() {
	LARGE_INTEGER freq = {0L};
	if (QueryPerformanceFrequency(&freq)) {
		hrTimerAvailable = true;
		mult = 1E9 / freq.QuadPart;
	} else {
		hrTimerAvailable = false;
	}
}

void updateNanoCounter() {
	if (hrTimerAvailable) {
		QueryPerformanceCounter(&counter);
		nanoCounter.QuadPart = (long long)(counter.QuadPart * mult);
	} else {
		DWORD currentTime = timeGetTime();
		if (currentTime < counter.LowPart) counter.HighPart++;
		counter.LowPart = currentTime;
		nanoCounter.QuadPart = counter.QuadPart * 1000000;
	}
}

using namespace MT32Emu;

static MidiSynth &midiSynth = FB01Synth::getInstance();
static bool synthOpened = false;
static HWND hwnd = NULL;
static int driverCount;

typedef struct tagMIDISource
{
	DWORD uDeviceID;
	int state; /* 0 is no recording started, 1 in recording, bit 2 set if in sys exclusive recording */
	MIDIINCAPSW caps;
	MIDIOPENDESC midiDesc;
	LPMIDIHDR lpQueueHdr;
	DWORD dwFlags;
	DWORD startTime;
} MIDISource;

//MIDISource *sources;
MIDISource sources[MAX_DRIVERS];

static CRITICAL_SECTION midiInLock; /* Critical section for MIDI In */

struct Driver {
	bool open;
	int clientCount;
	HDRVR hdrvr;
	struct Client {
		bool allocated;
		DWORD_PTR instance;
		DWORD flags;
		DWORD_PTR callback;
		DWORD synth_instance;
		MidiStreamParser *midiStreamParser;
	} clients[MAX_CLIENTS];
} drivers[MAX_DRIVERS];

STDAPI_(LRESULT) DriverProc(DWORD_PTR dwDriverID, HDRVR hdrvr, UINT msg, LONG lParam1, LONG lParam2)
{
	switch(msg)
	{
	case DRV_LOAD:
		memset(drivers, 0, sizeof(drivers));
		driverCount = 0;
		InitializeCriticalSection(&midiInLock);
		return DRV_OK;
	case DRV_ENABLE:
		return DRV_OK;
	case DRV_OPEN:
		int driverNum;
		if (driverCount == MAX_DRIVERS) {
			return 0;
		} else {
			for (driverNum = 0; driverNum < MAX_DRIVERS; driverNum++) {
				if (!drivers[driverNum].open) {
					break;
				}
				if (driverNum == MAX_DRIVERS) {
					return 0;
				}
			}
		}
		initNanoTimer();
		drivers[driverNum].open = true;
		drivers[driverNum].clientCount = 0;
		drivers[driverNum].hdrvr = hdrvr;
		driverCount++;
		return DRV_OK;
	case DRV_INSTALL:
	case DRV_PNPINSTALL:
		return DRV_OK;
	case DRV_QUERYCONFIGURE:
		return 0;
	case DRV_CONFIGURE:
		return DRVCNF_OK;
	case DRV_CLOSE:
		for (int i = 0; i < MAX_DRIVERS; i++) {
			if (drivers[i].open && drivers[i].hdrvr == hdrvr) {
				drivers[i].open = false;
				--driverCount;
				return DRV_OK;
			}
		}
		return DRV_CANCEL;
	case DRV_DISABLE:
		return DRV_OK;
	case DRV_FREE:
		DeleteCriticalSection(&midiInLock);
		return DRV_OK;
	case DRV_REMOVE:
		return DRV_OK;
	}
	return DRV_OK;
}

HRESULT modGetCaps(PVOID capsPtr, DWORD capsSize)
{
	MIDIOUTCAPSA * myCapsA;
	MIDIOUTCAPSW * myCapsW;
	MIDIOUTCAPS2A * myCaps2A;
	MIDIOUTCAPS2W * myCaps2W;

	CHAR synthName[] = "FB-01 Synth Emulator Out\0";
	WCHAR synthNameW[] = L"FB-01 Synth Emulator Out\0";

	switch (capsSize)
	{
	case (sizeof(MIDIOUTCAPSA)):
		myCapsA = (MIDIOUTCAPSA *)capsPtr;
		myCapsA->wMid = MM_UNMAPPED;
		myCapsA->wPid = MM_MPU401_MIDIOUT;
		memcpy(myCapsA->szPname, synthName, sizeof(synthName));
		myCapsA->wTechnology = MOD_MIDIPORT;
		myCapsA->vDriverVersion = 0x0090;
		myCapsA->wVoices = 0;
		myCapsA->wNotes = 0;
		myCapsA->wChannelMask = 0xffff;
		myCapsA->dwSupport = 0;
		return MMSYSERR_NOERROR;

	case (sizeof(MIDIOUTCAPSW)):
		myCapsW = (MIDIOUTCAPSW *)capsPtr;
		myCapsW->wMid = MM_UNMAPPED;
		myCapsW->wPid = MM_MPU401_MIDIOUT;
		memcpy(myCapsW->szPname, synthNameW, sizeof(synthNameW));
		myCapsW->wTechnology = MOD_MIDIPORT;
		myCapsW->vDriverVersion = 0x0090;
		myCapsW->wVoices = 0;
		myCapsW->wNotes = 0;
		myCapsW->wChannelMask = 0xffff;
		myCapsW->dwSupport = 0;
		return MMSYSERR_NOERROR;

	case (sizeof(MIDIOUTCAPS2A)):
		myCaps2A = (MIDIOUTCAPS2A *)capsPtr;
		myCaps2A->wMid = MM_UNMAPPED;
		myCaps2A->wPid = MM_MPU401_MIDIOUT;
		memcpy(myCaps2A->szPname, synthName, sizeof(synthName));
		myCaps2A->wTechnology = MOD_MIDIPORT;
		myCaps2A->vDriverVersion = 0x0090;
		myCaps2A->wVoices = 0;
		myCaps2A->wNotes = 0;
		myCaps2A->wChannelMask = 0xffff;
		myCaps2A->dwSupport = 0;
		return MMSYSERR_NOERROR;

	case (sizeof(MIDIOUTCAPS2W)):
		myCaps2W = (MIDIOUTCAPS2W *)capsPtr;
		myCaps2W->wMid = MM_UNMAPPED;
		myCaps2W->wPid = MM_MPU401_MIDIOUT;
		memcpy(myCaps2W->szPname, synthNameW, sizeof(synthNameW));
		myCaps2W->wTechnology = MOD_MIDIPORT;
		myCaps2W->vDriverVersion = 0x0090;
		myCaps2W->wVoices = 0;
		myCaps2W->wNotes = 0;
		myCaps2W->wChannelMask = 0xffff;
		myCaps2W->dwSupport = 0;
		return MMSYSERR_NOERROR;

	default:
		return MMSYSERR_ERROR;
	}
}

static DWORD midAddBuffer(DWORD uDeviceID, LPMIDIHDR lpMidiHdr, DWORD dwSize)
{
	//TRACE("wDevID=%d lpMidiHdr=%p dwSize=%d\n", wDevID, lpMidiHdr, dwSize);

	if (uDeviceID >= MAX_DRIVERS)
	{
		//WARN("bad device ID : %d\n", uDeviceID);
		return MMSYSERR_BADDEVICEID;
	}
	if (lpMidiHdr == NULL)
	{
		//WARN("Invalid Parameter\n");
		return MMSYSERR_INVALPARAM;
	}
	if (dwSize < offsetof(MIDIHDR, dwOffset))
	{
		//WARN("Invalid Parameter\n");
		return MMSYSERR_INVALPARAM;
	}
	if (lpMidiHdr->dwBufferLength == 0)
	{
		//WARN("Invalid Parameter\n");
		return MMSYSERR_INVALPARAM;
	}
	if (lpMidiHdr->dwFlags & MHDR_INQUEUE)
	{
		//WARN("Still playing\n");
		return MIDIERR_STILLPLAYING;
	}
	if (!(lpMidiHdr->dwFlags & MHDR_PREPARED))
	{
		//WARN("Unprepared\n");
		return MIDIERR_UNPREPARED;
	}

	EnterCriticalSection(&midiInLock);
	lpMidiHdr->dwFlags &= ~WHDR_DONE;
	lpMidiHdr->dwFlags |= MHDR_INQUEUE;
	lpMidiHdr->dwBytesRecorded = 0;
	lpMidiHdr->lpNext = 0;
	if (sources[uDeviceID].lpQueueHdr == 0)
	{
		sources[uDeviceID].lpQueueHdr = lpMidiHdr;
	}
	else
	{
		LPMIDIHDR ptr;
		for (ptr = sources[uDeviceID].lpQueueHdr;
			ptr->lpNext != 0;
			ptr = ptr->lpNext);
		ptr->lpNext = lpMidiHdr;
	}
	LeaveCriticalSection(&midiInLock);

	return MMSYSERR_NOERROR;
}

static DWORD midPrepare(DWORD uDeviceID, LPMIDIHDR lpMidiHdr, DWORD dwSize)
{
	//TRACE("wDevID=%d lpMidiHdr=%p dwSize=%d\n", wDevID, lpMidiHdr, dwSize);

	if (dwSize < offsetof(MIDIHDR, dwOffset) || lpMidiHdr == 0 || lpMidiHdr->lpData == 0)
		return MMSYSERR_INVALPARAM;
	if (lpMidiHdr->dwFlags & MHDR_PREPARED)
		return MMSYSERR_NOERROR;

	lpMidiHdr->lpNext = 0;
	lpMidiHdr->dwFlags |= MHDR_PREPARED;
	lpMidiHdr->dwFlags &= ~(MHDR_DONE | MHDR_INQUEUE); /* flags cleared since w2k */

	return MMSYSERR_NOERROR;
}

static DWORD midUnprepare(DWORD uDeviceID, LPMIDIHDR lpMidiHdr, DWORD dwSize)
{
	//TRACE("wDevID=%d lpMidiHdr=%p dwSize=%d\n", wDevID, lpMidiHdr, dwSize);

	if (dwSize < offsetof(MIDIHDR, dwOffset) || lpMidiHdr == 0 || lpMidiHdr->lpData == 0)
		return MMSYSERR_INVALPARAM;
	if (!(lpMidiHdr->dwFlags & MHDR_PREPARED))
		return MMSYSERR_NOERROR;
	if (lpMidiHdr->dwFlags & MHDR_INQUEUE)
		return MIDIERR_STILLPLAYING;

	lpMidiHdr->dwFlags &= ~MHDR_PREPARED;

	return MMSYSERR_NOERROR;
}

void midCallback(DWORD uDeviceID, DWORD dwMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	MIDISource* pSource = &sources[uDeviceID];
	MIDIOPENDESC* pMidiDesc = &pSource->midiDesc;

	DWORD dwFlags = pSource->dwFlags;
	DWORD_PTR dwCallback = pMidiDesc->dwCallback;
	HDRVR hDevice = (HDRVR)pMidiDesc->hMidi;
	DWORD_PTR dwUser = pMidiDesc->dwInstance;

	DriverCallback(dwCallback, dwFlags, hDevice, dwMsg, dwUser, dwParam1, dwParam2);
}

static DWORD midOpen(DWORD uDeviceID, LPMIDIOPENDESC lpDesc, DWORD dwFlags)
{
	//TRACE("wDevID=%d lpDesc=%p dwFlags=%08x\n", uDeviceID, lpDesc, dwFlags);

	if (lpDesc == NULL)
	{
		//WARN("Invalid Parameter\n");
		return MMSYSERR_INVALPARAM;
	}

	if (uDeviceID >= MAX_DRIVERS)
	{
		//WARN("bad device ID : %d\n", uDeviceID);
		return MMSYSERR_BADDEVICEID;
	}
	
	if (sources[uDeviceID].midiDesc.hMidi != 0)
	{
			//WARN("device already open !\n");
			return MMSYSERR_ALLOCATED;
	}

	if ((dwFlags & MIDI_IO_STATUS) != 0)
	{
		//WARN("No support for MIDI_IO_STATUS in dwFlags yet, ignoring it\n");
		dwFlags &= ~MIDI_IO_STATUS;
	}

	if ((dwFlags & ~CALLBACK_TYPEMASK) != 0)
	{
		//FIXME("Bad dwFlags\n");
		return MMSYSERR_INVALFLAG;
	}

	// Save the MIDI info for callback later
	sources[uDeviceID].dwFlags = HIWORD(dwFlags & CALLBACK_TYPEMASK);
	sources[uDeviceID].lpQueueHdr = NULL;
	sources[uDeviceID].midiDesc = *lpDesc;
	sources[uDeviceID].startTime = 0;
	sources[uDeviceID].state = 0;

	midCallback(uDeviceID, MIM_OPEN, 0, 0);

	// Test message
	midCallback(uDeviceID, MIM_DATA, 0xAABBCCDD, 0x1234);

	return MMSYSERR_NOERROR;
}

HRESULT midGetCaps(PVOID capsPtr, DWORD capsSize)
{
	MIDIINCAPSA * myCapsA;
	MIDIINCAPSW * myCapsW;
	MIDIINCAPS2A * myCaps2A;
	MIDIINCAPS2W * myCaps2W;

	CHAR synthName[] = "FB-01 Synth Emulator In\0";
	WCHAR synthNameW[] = L"FB-01 Synth Emulator In\0";

	switch (capsSize) {
	case (sizeof(MIDIINCAPSA)):
		myCapsA = (MIDIINCAPSA *)capsPtr;
		myCapsA->wMid = MM_UNMAPPED;
		myCapsA->wPid = MM_MPU401_MIDIIN;
		memcpy(myCapsA->szPname, synthName, sizeof(synthName));
		myCapsA->vDriverVersion = 0x0090;
		myCapsA->dwSupport = 0;
		return MMSYSERR_NOERROR;

	case (sizeof(MIDIINCAPSW)):
		myCapsW = (MIDIINCAPSW *)capsPtr;
		myCapsW->wMid = MM_UNMAPPED;
		myCapsW->wPid = MM_MPU401_MIDIIN;
		memcpy(myCapsW->szPname, synthNameW, sizeof(synthNameW));
		myCapsW->vDriverVersion = 0x0090;
		myCapsW->dwSupport = 0;
		return MMSYSERR_NOERROR;

	case (sizeof(MIDIINCAPS2A)):
		myCaps2A = (MIDIINCAPS2A *)capsPtr;
		myCaps2A->wMid = MM_UNMAPPED;
		myCaps2A->wPid = MM_MPU401_MIDIIN;
		memcpy(myCaps2A->szPname, synthName, sizeof(synthName));
		myCaps2A->vDriverVersion = 0x0090;
		myCaps2A->dwSupport = 0;
		return MMSYSERR_NOERROR;

	case (sizeof(MIDIINCAPS2W)):
		myCaps2W = (MIDIINCAPS2W *)capsPtr;
		myCaps2W->wMid = MM_UNMAPPED;
		myCaps2W->wPid = MM_MPU401_MIDIIN;
		memcpy(myCaps2W->szPname, synthNameW, sizeof(synthNameW));
		myCaps2W->vDriverVersion = 0x0090;
		myCaps2W->dwSupport = 0;
		return MMSYSERR_NOERROR;

	default:
		return MMSYSERR_ERROR;
	}
}

void DoCallback(int driverNum, DWORD_PTR clientNum, DWORD msg, DWORD_PTR param1, DWORD_PTR param2) {
	Driver::Client *client = &drivers[driverNum].clients[clientNum];
	DriverCallback(client->callback, client->flags, drivers[driverNum].hdrvr, msg, client->instance, param1, param2);
}

LONG OpenDriver(Driver &driver, UINT uDeviceID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	int clientNum;
	if (driver.clientCount == 0) {
		clientNum = 0;
	} else if (driver.clientCount == MAX_CLIENTS) {
		return MMSYSERR_ALLOCATED;
	} else {
		int i;
		for (i = 0; i < MAX_CLIENTS; i++) {
			if (!driver.clients[i].allocated) {
				break;
			}
		}
		if (i == MAX_CLIENTS) {
			return MMSYSERR_ALLOCATED;
		}
		clientNum = i;
	}
	MIDIOPENDESC *desc = (MIDIOPENDESC *)dwParam1;
	driver.clients[clientNum].allocated = true;
	driver.clients[clientNum].flags = HIWORD(dwParam2);
	driver.clients[clientNum].callback = desc->dwCallback;
	driver.clients[clientNum].instance = desc->dwInstance;
	*(LONG *)dwUser = clientNum;
	driver.clientCount++;
	DoCallback(uDeviceID, clientNum, MOM_OPEN, NULL, NULL);
	return MMSYSERR_NOERROR;
}

LONG CloseDriver(Driver &driver, UINT uDeviceID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	if (!driver.clients[dwUser].allocated) {
		return MMSYSERR_INVALPARAM;
	}
	driver.clients[dwUser].allocated = false;
	driver.clientCount--;
	DoCallback(uDeviceID, dwUser, MOM_CLOSE, NULL, NULL);
	return MMSYSERR_NOERROR;
}

class MidiStreamParserImpl : public MidiStreamParser {
public:
	MidiStreamParserImpl(Driver::Client &useClient) : client(useClient) {}

protected:
	virtual void handleShortMessage(const Bit32u message) {
		if (hwnd == NULL) {
			midiSynth.PlayMIDI(message);
		} else {
			updateNanoCounter();
			DWORD msg[] = { 0, 0, nanoCounter.LowPart, nanoCounter.HighPart, message }; // 0, short MIDI message indicator, timestamp, data
			COPYDATASTRUCT cds = { client.synth_instance, sizeof(msg), msg };
			LRESULT res = SendMessage(hwnd, WM_COPYDATA, NULL, (LPARAM)&cds);
			if (res != 1) {
				// Synth app was terminated. Fall back to integrated synth
				hwnd = NULL;
				if (midiSynth.Init() == 0) {
					synthOpened = true;
					midiSynth.PlayMIDI(message);
				}
			}
		}
	}

	virtual void handleSysex(const Bit8u stream[], const Bit32u length) {
		if (hwnd == NULL) {
			midiSynth.PlaySysex(stream, length);
		} else {
			COPYDATASTRUCT cds = { client.synth_instance, length, (PVOID)stream };
			LRESULT res = SendMessage(hwnd, WM_COPYDATA, NULL, (LPARAM)&cds);
			if (res != 1) {
				// Synth app was terminated. Fall back to integrated synth
				hwnd = NULL;
				if (midiSynth.Init() == 0) {
					synthOpened = true;
					midiSynth.PlaySysex(stream, length);
				}
			}
		}
	}

	virtual void handleSystemRealtimeMessage(const Bit8u realtime) {
		// Unsupported by now
	}

	virtual void printDebug(const char *debugMessage) {
#ifdef ENABLE_DEBUG_OUTPUT
		std::cout << debugMessage << std::endl;
#endif
	}

private:
	Driver::Client &client;
};

STDAPI_(DWORD) modMessage(DWORD uDeviceID, DWORD uMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	Driver &driver = drivers[uDeviceID];
	switch (uMsg) {
	case MODM_OPEN: {
		if (hwnd == NULL) {
			hwnd = FindWindow(L"mt32emu_class", NULL);
		}
		DWORD instance;
		if (hwnd == NULL) {
			// Synth application not found
			if (!synthOpened) {
				if (midiSynth.Init() != 0) return MMSYSERR_ERROR;
				synthOpened = true;
			}
			instance = NULL;
		} else {
			if (synthOpened) {
				midiSynth.Close();
				synthOpened = false;
			}
			updateNanoCounter();
			DWORD msg[70] = {0, -1, 1, nanoCounter.LowPart, nanoCounter.HighPart}; // 0, handshake indicator, version, timestamp, .exe filename of calling application
			GetModuleFileNameA(GetModuleHandle(NULL), (char *)&msg[5], 255);
			COPYDATASTRUCT cds = {0, sizeof(msg), msg};
			instance = (DWORD)SendMessage(hwnd, WM_COPYDATA, NULL, (LPARAM)&cds);
		}
		DWORD res = OpenDriver(driver, uDeviceID, uMsg, dwUser, dwParam1, dwParam2);
		Driver::Client &client = driver.clients[*(LONG *)dwUser];
		client.synth_instance = instance;
		client.midiStreamParser = new MidiStreamParserImpl(client);
		return res;
	}

	case MODM_CLOSE:
		if (driver.clients[dwUser].allocated == false) {
			return MMSYSERR_ERROR;
		}
		if (hwnd == NULL) {
			if (synthOpened) midiSynth.Reset();
		} else {
			SendMessage(hwnd, WM_APP, driver.clients[dwUser].synth_instance, NULL); // end of session message
		}
		delete driver.clients[dwUser].midiStreamParser;
		return CloseDriver(driver, uDeviceID, uMsg, dwUser, dwParam1, dwParam2);

	case MODM_PREPARE:
		return MMSYSERR_NOTSUPPORTED;

	case MODM_UNPREPARE:
		return MMSYSERR_NOTSUPPORTED;

	case MODM_GETDEVCAPS:
		return modGetCaps((PVOID)dwParam1, (DWORD)dwParam2);

	case MODM_DATA: {
		if (driver.clients[dwUser].allocated == false) {
			return MMSYSERR_ERROR;
		}
		driver.clients[dwUser].midiStreamParser->processShortMessage((Bit32u)dwParam1);
		if ((hwnd == NULL) && (synthOpened == false))
			return MMSYSERR_ERROR;
		return MMSYSERR_NOERROR;
	}

	case MODM_LONGDATA: {
		if (driver.clients[dwUser].allocated == false) {
			return MMSYSERR_ERROR;
		}
		MIDIHDR *midiHdr = (MIDIHDR *)dwParam1;
		if ((midiHdr->dwFlags & MHDR_PREPARED) == 0) {
			return MIDIERR_UNPREPARED;
		}
		driver.clients[dwUser].midiStreamParser->parseStream((const Bit8u *)midiHdr->lpData, midiHdr->dwBufferLength);
		if ((hwnd == NULL) && (synthOpened == false))
			return MMSYSERR_ERROR;
		midiHdr->dwFlags |= MHDR_DONE;
		midiHdr->dwFlags &= ~MHDR_INQUEUE;
		DoCallback(uDeviceID, dwUser, MOM_DONE, dwParam1, NULL);
 		return MMSYSERR_NOERROR;
	}

	case MODM_GETNUMDEVS:
		return 0x1;

	default:
		return MMSYSERR_NOERROR;
		break;
	}
}

STDAPI_(DWORD) midMessage(DWORD uDeviceID, DWORD uMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	switch (uMsg)
	{
	case MIDM_INIT:
		return MMSYSERR_NOERROR;
	case MIDM_OPEN:
		return midOpen(uDeviceID, (LPMIDIOPENDESC)dwParam1, dwParam2);
	case MIDM_CLOSE:
		return MMSYSERR_NOERROR;
	case MIDM_ADDBUFFER:
		return midAddBuffer(uDeviceID, (LPMIDIHDR)dwParam1, dwParam2);
	case MIDM_PREPARE:
		return midPrepare(uDeviceID, (LPMIDIHDR)dwParam1, dwParam2);
	case MIDM_UNPREPARE:
		return midUnprepare(uDeviceID, (LPMIDIHDR)dwParam1, dwParam2);
	case MIDM_GETDEVCAPS:
		return midGetCaps((PVOID)dwParam1, (DWORD)dwParam2);
	case MIDM_GETNUMDEVS:
		return 0x1;

	case MIDM_START:
		return MMSYSERR_NOERROR;
	case MIDM_STOP:
		return MMSYSERR_NOERROR;
	case MIDM_RESET:
		return MMSYSERR_NOERROR;
	default:
		return MMSYSERR_NOERROR;
	}
}

} // namespace
