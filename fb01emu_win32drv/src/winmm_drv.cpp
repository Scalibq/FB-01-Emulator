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

namespace winmm_drv {

static bool hrTimerAvailable;
static double mult;
static LARGE_INTEGER counter;
static LARGE_INTEGER nanoCounter = {0L};

void initNanoTimer()
{
	LARGE_INTEGER freq = {0L};
	if (QueryPerformanceFrequency(&freq))
	{
		hrTimerAvailable = true;
		mult = 1E9 / freq.QuadPart;
	}
	else
	{
		hrTimerAvailable = false;
	}
}

void updateNanoCounter()
{
	if (hrTimerAvailable)
	{
		QueryPerformanceCounter(&counter);
		nanoCounter.QuadPart = (long long)(counter.QuadPart * mult);
	}
	else
	{
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

// We can only have one synth, so only one MIDI In device
MIDISource source = { 0 };

static CRITICAL_SECTION midiInLock; /* Critical section for MIDI In */

typedef struct tagPendingData
{
	uint8_t* pData;
	uint8_t length;
	uint8_t sent;
	DWORD timeStamp;
	tagPendingData* pNext;
} PendingData;

PendingData* pPendingData = NULL;

void midCallback(DWORD uDeviceID, DWORD dwMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2);

void ProcessPending()
{
	// Have we started?
	if (source.state == 0)
		return;

	// Do we have any data to send?
	if (pPendingData == NULL)
		return;

	EnterCriticalSection(&midiInLock);
	MIDIHDR* pHdr = source.lpQueueHdr;

	while (pHdr != NULL && pPendingData != NULL)
	{
		uint8_t* pData = &pPendingData->pData[pPendingData->sent];
		uint32_t length = pPendingData->length - pPendingData->sent;
		DWORD timeStamp = pPendingData->timeStamp;

		// Can we just copy into this buffer?
		if (pHdr->dwBufferLength >= length)
		{
			memcpy(pHdr->lpData, pData, length);
			pHdr->dwBytesRecorded = length;

			// Remove this from pending data
			PendingData* pOld = pPendingData;
			pPendingData = pPendingData->pNext;

			delete[] pOld->pData;
			delete pOld;
		}
		else
		{
			// Copy only part of the data
			length = pHdr->dwBufferLength;

			memcpy(pHdr->lpData, pData, length);
			pHdr->dwBytesRecorded = length;

			// Add this to the sent bytes
			pPendingData->sent += length;
		}

		pHdr->dwFlags &= MHDR_INQUEUE;
		pHdr->dwFlags |= MHDR_DONE;

		// Send back to application
		midCallback(source.uDeviceID, MIM_LONGDATA, (DWORD_PTR)pHdr, timeStamp);

		pHdr = pHdr->lpNext;
	}
	LeaveCriticalSection(&midiInLock);
}

struct Driver
{
	bool open;
	int clientCount;
	HDRVR hdrvr;
	struct Client
	{
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
		if (driverCount == MAX_DRIVERS)
		{
			return 0;
		}
		else
		{
			for (driverNum = 0; driverNum < MAX_DRIVERS; driverNum++)
			{
				if (!drivers[driverNum].open)
				{
					break;
				}
				if (driverNum == MAX_DRIVERS)
				{
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
		for (int i = 0; i < MAX_DRIVERS; i++)
		{
			if (drivers[i].open && drivers[i].hdrvr == hdrvr)
			{
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

	if (uDeviceID != source.uDeviceID)
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
	lpMidiHdr->dwFlags &= ~MHDR_DONE;
	lpMidiHdr->dwFlags |= MHDR_INQUEUE;
	lpMidiHdr->dwBytesRecorded = 0;
	lpMidiHdr->lpNext = 0;
	if (source.lpQueueHdr == 0)
	{
		source.lpQueueHdr = lpMidiHdr;
	}
	else
	{
		LPMIDIHDR ptr;
		for (ptr = source.lpQueueHdr;
			ptr->lpNext != 0;
			ptr = ptr->lpNext);
		ptr->lpNext = lpMidiHdr;
	}
	LeaveCriticalSection(&midiInLock);

	ProcessPending();

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
	if (uDeviceID != source.uDeviceID)
	{
		// Wrong device ID!
		return;
	}

	DWORD dwFlags = source.dwFlags;
	DWORD_PTR dwCallback = source.midiDesc.dwCallback;
	HDRVR hDevice = (HDRVR)source.midiDesc.hMidi;
	DWORD_PTR dwUser = source.midiDesc.dwInstance;

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

	if (uDeviceID >= 1)
	{
		//WARN("bad device ID : %d\n", uDeviceID);
		return MMSYSERR_BADDEVICEID;
	}
	
	if (source.midiDesc.hMidi != 0)
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
	source.uDeviceID = uDeviceID;
	source.dwFlags = HIWORD(dwFlags & CALLBACK_TYPEMASK);
	source.lpQueueHdr = NULL;
	source.midiDesc = *lpDesc;
	source.startTime = 0;
	source.state = 0;

	midCallback(uDeviceID, MIM_OPEN, 0, 0);

	return MMSYSERR_NOERROR;
}

static DWORD midClose(DWORD uDeviceID)
{
	if (source.midiDesc.hMidi == 0)
	{
		// Device was already closed!
		return MMSYSERR_ERROR;
	}

	if (uDeviceID != source.uDeviceID)
	{
		//WARN("bad device ID : %d\n", uDeviceID);
		return MMSYSERR_BADDEVICEID;
	}

	if (source.lpQueueHdr != 0)
	{
		return MIDIERR_STILLPLAYING;
	}

	midCallback(uDeviceID, MIM_CLOSE, 0L, 0L);

	// Remove handle to indicate that driver is closed
	source.midiDesc.hMidi = 0;

	return MMSYSERR_NOERROR;
}

static DWORD midStart(DWORD uDeviceID)
{
	if (uDeviceID != source.uDeviceID)
		return MMSYSERR_BADDEVICEID;

	source.state = 1;
	source.startTime = GetTickCount();

	return MMSYSERR_NOERROR;
}

static DWORD midStop(DWORD uDeviceID)
{
	if (uDeviceID != source.uDeviceID)
		return MMSYSERR_BADDEVICEID;

	source.state = 0;

	return MMSYSERR_NOERROR;
}

static DWORD midReset(DWORD uDeviceID)
{
	if (uDeviceID != source.uDeviceID)
		return MMSYSERR_BADDEVICEID;

	DWORD dwTime = GetTickCount();

	// Send all pending buffers to client
	EnterCriticalSection(&midiInLock);
	while (source.lpQueueHdr)
	{
		LPMIDIHDR lpMidiHdr = source.lpQueueHdr;
		source.lpQueueHdr = lpMidiHdr->lpNext;
		lpMidiHdr->dwFlags &= ~MHDR_INQUEUE;
		lpMidiHdr->dwFlags |= MHDR_DONE;
		/* FIXME: when called from 16 bit, lpQueueHdr needs to be a segmented ptr */

		midCallback(uDeviceID, MIM_LONGDATA, (DWORD_PTR)lpMidiHdr, dwTime);
	}
	LeaveCriticalSection(&midiInLock);

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

void modCallback(int driverNum, DWORD_PTR clientNum, DWORD msg, DWORD_PTR param1, DWORD_PTR param2)
{
	Driver::Client *client = &drivers[driverNum].clients[clientNum];

	DriverCallback(client->callback, client->flags, drivers[driverNum].hdrvr, msg, client->instance, param1, param2);
}

LONG OpenDriver(Driver &driver, UINT uDeviceID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	int clientNum;
	if (driver.clientCount == 0)
	{
		clientNum = 0;
	}
	else if (driver.clientCount == MAX_CLIENTS)
	{
		return MMSYSERR_ALLOCATED;
	}
	else
	{
		int i;
		for (i = 0; i < MAX_CLIENTS; i++)
		{
			if (!driver.clients[i].allocated)
			{
				break;
			}
		}
		if (i == MAX_CLIENTS)
		{
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

	modCallback(uDeviceID, clientNum, MOM_OPEN, NULL, NULL);

	return MMSYSERR_NOERROR;
}

LONG CloseDriver(Driver &driver, UINT uDeviceID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (!driver.clients[dwUser].allocated)
	{
		return MMSYSERR_INVALPARAM;
	}
	driver.clients[dwUser].allocated = false;
	driver.clientCount--;

	modCallback(uDeviceID, dwUser, MOM_CLOSE, NULL, NULL);

	return MMSYSERR_NOERROR;
}

class MidiStreamParserImpl : public MidiStreamParser
{
public:
	MidiStreamParserImpl(Driver::Client &useClient) : client(useClient)
	{ }

protected:
	virtual void handleShortMessage(const Bit32u message)
	{
		if (hwnd == NULL)
		{
			midiSynth.PlayMIDI(message);
		}
		else
		{
			updateNanoCounter();
			DWORD msg[] = { 0, 0, nanoCounter.LowPart, nanoCounter.HighPart, message }; // 0, short MIDI message indicator, timestamp, data
			COPYDATASTRUCT cds = { client.synth_instance, sizeof(msg), msg };
			LRESULT res = SendMessage(hwnd, WM_COPYDATA, NULL, (LPARAM)&cds);
			if (res != 1)
			{
				// Synth app was terminated. Fall back to integrated synth
				hwnd = NULL;
				if (midiSynth.Init() == 0)
				{
					synthOpened = true;
					midiSynth.PlayMIDI(message);
				}
			}
		}
	}

	virtual void handleSysex(const Bit8u stream[], const Bit32u length)
	{
		if (hwnd == NULL)
		{
			midiSynth.PlaySysex(stream, length);
		}
		else
		{
			COPYDATASTRUCT cds = { client.synth_instance, length, (PVOID)stream };
			LRESULT res = SendMessage(hwnd, WM_COPYDATA, NULL, (LPARAM)&cds);
			if (res != 1)
			{
				// Synth app was terminated. Fall back to integrated synth
				hwnd = NULL;
				if (midiSynth.Init() == 0)
				{
					synthOpened = true;
					midiSynth.PlaySysex(stream, length);
				}
			}
		}
	}

	virtual void handleSystemRealtimeMessage(const Bit8u realtime)
	{
		// Unsupported by now
	}

	virtual void printDebug(const char *debugMessage)
	{
#ifdef ENABLE_DEBUG_OUTPUT
		std::cout << debugMessage << std::endl;
#endif
	}

private:
	Driver::Client &client;
};

STDAPI_(DWORD) modMessage(DWORD uDeviceID, DWORD uMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	Driver &driver = drivers[uDeviceID];
	switch (uMsg)
	{
	case MODM_OPEN:
	{
		if (hwnd == NULL)
		{
			hwnd = FindWindow(L"mt32emu_class", NULL);
		}
		DWORD instance;
		if (hwnd == NULL)
		{
			// Synth application not found
			if (!synthOpened)
			{
				if (midiSynth.Init() != 0)
					return MMSYSERR_ERROR;
				synthOpened = true;
			}
			instance = NULL;
		}
		else
		{
			if (synthOpened)
			{
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
		if (driver.clients[dwUser].allocated == false)
		{
			return MMSYSERR_ERROR;
		}
		if (hwnd == NULL)
		{
			if (synthOpened) midiSynth.Reset();
		} else
		{
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

	case MODM_DATA:
	{
		if (driver.clients[dwUser].allocated == false) 
		{
			return MMSYSERR_ERROR;
		}
		driver.clients[dwUser].midiStreamParser->processShortMessage((Bit32u)dwParam1);
		if ((hwnd == NULL) && (synthOpened == false))
			return MMSYSERR_ERROR;
		return MMSYSERR_NOERROR;
	}

	case MODM_LONGDATA:
	{
		if (driver.clients[dwUser].allocated == false)
		{
			return MMSYSERR_ERROR;
		}
		MIDIHDR *midiHdr = (MIDIHDR *)dwParam1;
		if ((midiHdr->dwFlags & MHDR_PREPARED) == 0)
		{
			return MIDIERR_UNPREPARED;
		}
		driver.clients[dwUser].midiStreamParser->parseStream((const Bit8u *)midiHdr->lpData, midiHdr->dwBufferLength);
		if ((hwnd == NULL) && (synthOpened == false))
			return MMSYSERR_ERROR;
		midiHdr->dwFlags |= MHDR_DONE;
		midiHdr->dwFlags &= ~MHDR_INQUEUE;

		modCallback(uDeviceID, dwUser, MOM_DONE, dwParam1, NULL);

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
		return midClose(uDeviceID);
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
		return midStart(uDeviceID);
	case MIDM_STOP:
		return midStop(uDeviceID);
	case MIDM_RESET:
		return midReset(uDeviceID);
	default:
		return MMSYSERR_NOTSUPPORTED;
	}
}

} // namespace

extern "C"
{
	uint8_t SendMidiData(uint8_t* pData, uint32_t length);
}

uint8_t SendMidiData(uint8_t* pData, uint32_t length)
{
	// Is device open?
	if (winmm_drv::source.state == 0)
		return 0;

	DWORD dwMsg = 0;

	DWORD timeStamp = GetTickCount() - winmm_drv::source.startTime;

	switch (length)
	{
		// Short messages can be sent right away
	case 3:
		dwMsg |= pData[2] << 16;
	case 2:
		dwMsg |= pData[1] << 8;
	case 1:
		dwMsg |= pData[0];
		winmm_drv::midCallback(winmm_drv::source.uDeviceID, MIM_DATA, dwMsg, timeStamp);
		break;
		// Longer messages need a buffer
	default:
		// Copy data
		winmm_drv::PendingData * pPData = new winmm_drv::PendingData;
		pPData->pData = new uint8_t[length];
		pPData->length = length;
		pPData->sent = 0;
		pPData->timeStamp = timeStamp;
		pPData->pNext = NULL;
		memcpy(pPData->pData, pData, length);

		// Add to list
		EnterCriticalSection(&winmm_drv::midiInLock);
		if (winmm_drv::pPendingData == NULL)
			winmm_drv::pPendingData = pPData;
		else
		{
			winmm_drv::PendingData* pCur = winmm_drv::pPendingData;
			while (pCur->pNext != NULL)
				pCur = pCur->pNext;
			pCur->pNext = pPData;
		}
		LeaveCriticalSection(&winmm_drv::midiInLock);

		winmm_drv::ProcessPending();
		break;
	}

	return 1;
}
