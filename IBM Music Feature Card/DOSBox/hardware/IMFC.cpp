/*
*  Copyright (C) 2002-2012  The DOSBox Team
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


#include <string.h>
#include "dosbox.h"
#include "inout.h"
#include "pic.h"
#include "setup.h"
#include "cpu.h"
#include "support.h"
#include "PIT_8254.h"
#include <Windows.h>

void MIDI_RawOutByte(Bit8u data);
void MIDI_RawOutBuffer(Bit8u* pData, Bitu length);
void IMF_MIDIFilter(Bit8u data, bool reset);
void IMF_CommandFilter(Bit8u data, bool reset);
bool MIDI_Available(void);

static void IMFC_TimerAEvent(Bitu val);
static void IMFC_TimerBEvent(Bitu val);
static void IMFC_RxRDYEvent(Bitu val);
static void IMFC_TxRDYEvent(Bitu val);

static void TimerAInt(bool enabled);
static void TimerBInt(bool enabled);
static void RxRDYInt(bool enabled);
static void TxRDYInt(bool enabled);

bool AddToFifo(Bit16u* buf, Bitu count);

// Midi channel status byte
typedef enum
{
	NOTE_OFF = 0x80,
	NOTE_ON = 0x90,
	KEY_AFTERTOUCH = 0xA0,
	CC = 0xB0,
	PC = 0xC0,
	CHANNEL_AFTERTOUCH = 0xD0,
	PITCH_WHEEL = 0xE0,
	SYSTEM = 0xF0
} MIDI_MSG;

HMIDIIN hMidiIn = NULL;
DWORD devID = 0;	// Device id for MIDI IN
MIDIHDR midiHdr;
CHAR midiBuffer[65536];

void CALLBACK MidiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD dwParam1, DWORD dwParam2)
{
	// Translate to 9-bit values for IMFC
	Bit16u temp[65536];

	// Filter out timer messages
	if ((wMsg == MIM_DATA) && ((dwParam1 & 0xFF) == 0xFE))
		return;

	switch (wMsg)
	{
	case MIM_DATA:
	case MIM_MOREDATA:
		{
			temp[0] = dwParam1 & 0xFF;
			temp[1] = (dwParam1 >> 8) & 0xFF;
			temp[2] = (dwParam1 >> 16) & 0xFF;
			DWORD dwTimeStamp = dwParam2;
			Bitu length;

			switch (temp[0])
			{
			case 0:
				// Running status message?
				break;
			// System message?
			case SYSTEM:
				length = 0;
				break;
			// Single-byte message?
			case PC:
			case CHANNEL_AFTERTOUCH:
				length = 1;
				break;
			// Double-byte message
			default:
				length = 2;
				break;
			}

			AddToFifo(temp, length+1);
		}
		break;
	case MIM_LONGDATA:
		{
			// System-exclusive data sent in a MIDIHDR struct
			LPMIDIHDR pMidiHdr = (LPMIDIHDR)dwParam1;
			DWORD dwTimeStamp = dwParam2;

			// Queue up data
			int j = pMidiHdr->dwOffset;

			for (int i = 0; i < pMidiHdr->dwBytesRecorded; i++, j++)
				temp[i] = pMidiHdr->lpData[j] & 0xFF;

			AddToFifo(temp, pMidiHdr->dwBytesRecorded);

			// Add new buffer
			if (pMidiHdr->dwBytesRecorded > 0)
				midiInAddBuffer(hMidiIn, &midiHdr, sizeof(midiHdr));
			else
				midiInUnprepareHeader(hMidiIn, pMidiHdr, sizeof(midiHdr));
		}
		break;
	}	
}

class IMFC_PIT : public PIT_8254
{
public:
	IMFC_PIT(Section* configuration):PIT_8254(configuration)
	{
		pit[0].tick_rate = 2000000/4;
		pit[1].tick_rate = 2000000/65536;
		pit[2].tick_rate = 2000000;

		/* Setup Timer 0 */
		pit[0].cntr=0x10000;
		pit[0].write_state = 3;
		pit[0].read_state = 3;
		pit[0].read_latch=0;
		pit[0].write_latch=0;
		pit[0].mode=2;
		pit[0].bcd = false;
		pit[0].go_read_latch = true;
		pit[0].counterstatus_set = false;
		pit[0].update_count = false;
	
		pit[1].cntr=0x10000;
		pit[1].bcd = false;
		pit[1].write_state = 1;
		pit[1].read_state = 1;
		pit[1].go_read_latch = true;
		pit[1].mode = 2;
		pit[1].write_state = 3;
		pit[1].counterstatus_set = false;
	
		pit[2].cntr=0x10000;
		pit[2].read_latch=0;	/* MadTv1 */
		pit[2].write_state = 3; /* Chuck Yeager */
		pit[2].read_state = 3;
		pit[2].mode=3;
		pit[2].bcd=false;   
		pit[2].go_read_latch=true;
		pit[2].counterstatus_set = false;
		pit[2].counting = false;
	
		pit[0].delay=(1000.0f/((float)pit[0].tick_rate/(float)pit[0].cntr));
		pit[1].delay=(1000.0f/((float)pit[1].tick_rate/(float)pit[1].cntr));
		pit[2].delay=(1000.0f/((float)pit[2].tick_rate/(float)pit[2].cntr));

		pit[0].gate = false;
		pit[1].gate = false;
		pit[2].gate = false;

		latched_timerstatus_locked=false;

		PIC_AddEvent(IMFC_TimerAEvent,pit[0].delay);
		PIC_AddEvent(IMFC_TimerBEvent,pit[1].delay);
	}

	~IMFC_PIT()
	{
	}

	void UpdateMode(Bitu counter, Bit8u mode, bool old_output)
	{
		/* If the line goes from low to up => generate irq. 
			*      ( BUT needs to stay up until acknowlegded by the cpu!!! therefore: )
			* If the line goes to low => disable irq.
			* Mode 0 starts with a low line. (so always disable irq)
			* Mode 2,3 start with a high line.
			* counter_output tells if the current counter is high or low 
			* So actually a mode 3 timer enables and disables irq al the time. (not handled) */

		if (counter == 0) {
			PIC_RemoveEvents(::IMFC_TimerAEvent);
			if((mode != 0)&& !old_output) {
				IMFC_TimerAEvent(0);
			} else {
				TimerAInt(false);
			}
		} else if (counter == 1) {
			PIC_RemoveEvents(::IMFC_TimerBEvent);
			if((mode != 0)&& !old_output) {
				IMFC_TimerBEvent(0);
			} else {
				TimerBInt(false);
			}
		}
	}

	void UpdateLatch(Bitu counter)
	{
		PIT_Block* p = &pit[counter];
		
		switch (counter) {
		case 0x00:			/* Timer A */
			if (p->new_mode || p->mode == 0 ) {
				if(p->mode==0) PIC_RemoveEvents(::IMFC_TimerAEvent);
				PIC_AddEvent(::IMFC_TimerAEvent,p->delay);
			} else LOG(LOG_PIT,LOG_NORMAL)("IMFC Timer A set without new control word");
			LOG(LOG_PIT,LOG_NORMAL)("PIT 0 Timer at %.4f Hz mode %d",1000.0/p->delay,p->mode);
			break;
		case 0x01:			/* Timer B */
			if (p->new_mode || p->mode == 0 ) {
				if(p->mode==0) PIC_RemoveEvents(::IMFC_TimerBEvent);
				PIC_AddEvent(::IMFC_TimerBEvent,p->delay);
			} else LOG(LOG_PIT,LOG_NORMAL)("IMFC Timer B set without new control word");
			LOG(LOG_PIT,LOG_NORMAL)("PIT 0 Timer at %.4f Hz mode %d",1000.0/p->delay,p->mode);
			break;
		case 0x02:
			// Update tickrate for Timer B
			pit[1].tick_rate = pit[2].tick_rate/pit[2].cntr;
			pit[1].delay=(1000.0f/((float)pit[1].tick_rate/(float)pit[1].cntr));
			PIC_RemoveEvents(::IMFC_TimerBEvent);
			PIC_AddEvent(::IMFC_TimerBEvent,p->delay);
			break;
		default:
			LOG(LOG_PIT,LOG_ERROR)("PIT:Illegal timer selected for writing");
			break;
		}
	}

	void RestartIRQ(Bitu counter, PIC_EventHandler handler)
	{
		if (pit[counter].mode != 0) {
			pit[counter].start += pit[counter].delay;

			if (GCC_UNLIKELY(pit[counter].update_count)) {
				pit[counter].delay=(1000.0f/((float)pit[counter].tick_rate/(float)pit[counter].cntr));
				pit[counter].update_count=false;
			}
			PIC_AddEvent(handler,pit[counter].delay);
		}
	}
};

// IMF registers:
// Offsets for IBM Music Feature Card registers
#define	PIU0	0x0	// Used for reading data from card
#define	PIU1	0x1	// Used for writing data to card
#define	PIU2	0x2	// Controls read and write interrupts
#define	PCR		0x3	// Control register for PIU
#define	CNTR0	0x4	// 8253 PIT channel 0
#define	CNTR1	0x5	// 8253 PIT channel 1
#define	CNTR2	0x6	// 8253 PIT channel 2
#define	TCWR	0x7	// 8253 PIT control register
#define	TCR		0x8	// Total Control Register
#define	TSR		0xC	// Total Status Register

// Simple IMFC state, very incomplete atm
static bool timerAInt = false;
static bool timerBInt = false;
static bool txRDYInt = false;
static bool rxRDYInt = false;

static bool EXT8 = true;
static bool IBE = true;
static bool TMSK = true;
static bool TBE = false;
static bool TAE = false;
static bool RIE = false;
static bool WIE = false;
static bool EXR8 = false;
static bool RxRDY = false;
static bool TxRDY = true;

// Latches for read/write ports
//static Bitu piu0 = 0;	// Parallel interface unit port 0
static Bitu piu1 = 0;	// Parallel interface unit port 1
//static Bitu piu2 = 0;	// Parallel interface unit port 2
//static Bitu pcr = 0;	// Parallel interface unit command register
//static Bitu tcr = 0;	// Total control register

static IMFC_PIT* imfcPIT;

static Bitu IMFCIrq = 9;

// Use 16-bit elements, because IMFC transfers 9-bit values
static Bit16u outputFIFO[65536];
static Bitu outReadPos = 0;
static Bitu outWritePos = 0;

bool IsFifoEmpty()
{
	bool ret = (outReadPos == outWritePos);

	if (!ret)
	{
		// Fetch next data
		EXR8 = outputFIFO[outReadPos] >> 8;
	}

	return ret;
}

Bit16u GetFromFifo()
{
	Bit16u data = outputFIFO[outReadPos];

	if (outReadPos == outWritePos)
		RxRDY = false;
	else
	{
		if (++outReadPos >= _countof(outputFIFO))
			outReadPos = 0;
	}

	// Hack to generate IRQs
	if (RxRDY)
		IMFC_RxRDYEvent(0);

	return data;
}

bool AddToFifo(Bit16u* buf, Bitu count)
{
	RxRDY = true;
	IMFC_RxRDYEvent(0);

	for (Bitu i = 0; i < count; i++)
	{
		outputFIFO[outWritePos] = buf[i];

		if (++outWritePos >= _countof(outputFIFO))
			outWritePos = 0;
	}

	return true;
}

static Bitu IMF_ReadPIU0(Bitu port, Bitu iolen)
{
	Bitu piu0;

	LOG_MSG("IMF PIU0:Rd %4X", (int)port);

	piu0 = GetFromFifo();

	return piu0;
}

static Bitu IMF_ReadPIU1(Bitu port, Bitu iolen)
{
	LOG_MSG("IMF PIU1:Rd %4X", (int)port);

	return piu1;
}

static Bitu IMF_ReadPIU2(Bitu port, Bitu iolen)
{
	Bitu piu2 = 0;

	//LOG_MSG("IMF PIU2:Rd %4X", (int)port);

	// Always set TxRDY, RxRDY and some other bits that software might be interested in
	//return 0x15;

	// Poll FIFO to get EXR8 and piu0 set for next read
	IsFifoEmpty();

	if (EXR8)
		piu2 |= 0x80;

	if (RIE)
		piu2 |= 0x10;

	if (RxRDY)
		piu2 |= 0x08;

	if (WIE)
		piu2 |= 0x04;

	if (TxRDY)
		piu2 |= 0x01;

	return piu2;
}

static Bitu IMF_ReadPCR(Bitu port, Bitu iolen)
{
	LOG_MSG("IMF PCR:Rd %4X", (int)port);

	return 0;
}

static Bitu IMF_ReadCNTR0(Bitu port, Bitu iolen)
{
	LOG_MSG("IMF CNTR0:Rd %4X", (int)port);

	return imfcPIT->read_latch(0);
}

static Bitu IMF_ReadCNTR1(Bitu port, Bitu iolen)
{
	LOG_MSG("IMF CNTR1:Rd %4X", (int)port);

	return imfcPIT->read_latch(1);
}

static Bitu IMF_ReadCNTR2(Bitu port, Bitu iolen)
{
	LOG_MSG("IMF CNTR0:Rd %4X", (int)port);

	return imfcPIT->read_latch(2);
}

static Bitu IMF_ReadTCWR(Bitu port, Bitu iolen)
{
	LOG_MSG("IMF TCWR:Rd %4X", (int)port);

	return 0;
}

static Bitu IMF_ReadTCR(Bitu port, Bitu iolen)
{
	LOG_MSG("IMF TCR:Rd %4X", (int)port);

	return 0;
}

static Bitu IMF_ReadTSR(Bitu port, Bitu iolen)
{
	Bitu tsr = 0;

	LOG_MSG("IMF TSR:Rd %4X", (int)port);

	if (TAE && timerAInt)
		tsr |= 0x81;

	if (TBE && timerBInt)
		tsr |= 0x82;

	if (RIE && rxRDYInt)
		tsr |= 0x80;

	if (WIE && txRDYInt)
		tsr |= 0x80;

	return tsr;
}

static void IMF_WritePIU0(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF PIU0:Wr %4X,%X", (int)port, (int)val);
}

static void IMF_WritePIU1(Bitu port, Bitu val, Bitu iolen)
{
	static bool lastEXT8 = EXT8;
	bool reset;

	LOG_MSG("IMF PIU1:Wr %4X,%X", (int)port, (int)val);

	// Buffer last byte, used as detection, apparently
	piu1 = val;

	// Did we switch between command and data?
	reset = false;//(lastEXT8 == EXT8);
	lastEXT8 = EXT8;

	if (EXT8)
	{
		// This is a command byte
		IMF_CommandFilter(val, reset);
	}
	else
	{
		// This is a MIDI byte, output it
		IMF_MIDIFilter(val, reset);
	}

	//IMFC_TxRDYEvent(0);
}

static void IMF_WritePIU2(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF PIU2:Wr %4X,%X", (int)port, (int)val);

	// Read interrupt enable (RIE)
	//RIE = (val & 0x10);

	// Write interrupt enable (WIE)
	//WIE = (val & 0x02);
}

static void IMF_WritePCR(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF PCR:Wr %4X,%X", (int)port, (int)val);

	switch (val)
	{
	case 0xBC:
		// Initialize
		EXT8 = true;
		IBE = true;
		TMSK = true;
		TBE = false;
		TAE = false;
		RIE = false;
		WIE = false;
		EXR8 = false;
		RxRDY = false;
		TxRDY = true;
		break;
	case 0x05:
		// Set bit 2 (Write Interrupt Enable) in PIU2
		if (!WIE && TxRDY)
			IMFC_TxRDYEvent(0);

		WIE = true;
		break;
	case 0x04:
		// Reset bit 2 (Write Interrupt Enable) in PIU2
		WIE = false;
		break;
	case 0x09:
		// Set bit 4 (Read Interrupt Enable) in PIU2
		if (!RIE && RxRDY)
			IMFC_RxRDYEvent(0);

		RIE = true;
		break;
	case 0x08:
		// Reset bit 4 (Read Interrupt Enable) in PIU2
		RIE = false;
		break;
	}
}

static void IMF_WriteCNTR0(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF CNTR0:Wr %4X,%X", (int)port, (int)val);

	imfcPIT->write_latch(0, val);
}

static void IMF_WriteCNTR1(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF CNTR1:Wr %4X,%X", (int)port, (int)val);

	imfcPIT->write_latch(1, val);
}

static void IMF_WriteCNTR2(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF CNTR2:Wr %4X,%X", (int)port, (int)val);

	imfcPIT->write_latch(2, val);
}

static void IMF_WriteTCWR(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF TCWR:Wr %4X,%X", (int)port, (int)val);

	imfcPIT->write_ctrl(val);
}

static void IMF_WriteTCR(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF TCR:Wr %4X,%X", (int)port, (int)val);

	// Transmit data bit 8 (EXT8)
	EXT8 = (val & 0x10);

	// IRQ enable (IBE)
	//IBE = (val & 0x80);

	// Total IRQ mask (-TMSK)
	//TMSK = (val & 0x40);

	// Timer B enable (TBE)
	TBE = (val & 0x08);

	// Timer A enable (TAE)
	TAE = (val & 0x04);

	if (val & 0x02)
	{
		// Timer B clear (-TBC)
		timerBInt = false;
	}

	if (val & 0x01)
	{
		// Timer A clear (-TAC)
		timerAInt = false;
	}
}

static void IMF_WriteTSR(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF TSR:Wr %4X,%X", (int)port, (int)val);
}

Bit8u disableMemProtect[] =	{ 0xF0, 0x43, 0x75, 0x00, 0x10, 0x21, 0x00, 0xF7 };	// -Disable memory - protect
Bit8u setSysChannel1[] =	{ 0xF0, 0x43, 0x75, 0x00, 0x10, 0x20, 0x00, 0xF7 };	// -Set the system channel to 1

class IMFC :public Module_base {
public:
	IMFC(Section* configuration) :Module_base(configuration) {
		IO_RegisterReadHandler(0x2A20, &IMF_ReadPIU0, IO_MB);
		IO_RegisterReadHandler(0x2A21, &IMF_ReadPIU1, IO_MB);
		IO_RegisterReadHandler(0x2A22, &IMF_ReadPIU2, IO_MB);
		IO_RegisterReadHandler(0x2A23, &IMF_ReadPCR, IO_MB);
		IO_RegisterReadHandler(0x2A24, &IMF_ReadCNTR0, IO_MB);
		IO_RegisterReadHandler(0x2A25, &IMF_ReadCNTR1, IO_MB);
		IO_RegisterReadHandler(0x2A26, &IMF_ReadCNTR2, IO_MB);
		IO_RegisterReadHandler(0x2A27, &IMF_ReadTCWR, IO_MB);
		IO_RegisterReadHandler(0x2A28, &IMF_ReadTCR, IO_MB);
		IO_RegisterReadHandler(0x2A29, &IMF_ReadTCR, IO_MB);
		IO_RegisterReadHandler(0x2A2A, &IMF_ReadTCR, IO_MB);
		IO_RegisterReadHandler(0x2A2B, &IMF_ReadTCR, IO_MB);
		IO_RegisterReadHandler(0x2A2C, &IMF_ReadTSR, IO_MB);
		IO_RegisterReadHandler(0x2A2D, &IMF_ReadTSR, IO_MB);
		IO_RegisterReadHandler(0x2A2E, &IMF_ReadTSR, IO_MB);
		IO_RegisterReadHandler(0x2A2F, &IMF_ReadTSR, IO_MB);

		IO_RegisterWriteHandler(0x2A20, &IMF_WritePIU0, IO_MB);
		IO_RegisterWriteHandler(0x2A21, &IMF_WritePIU1, IO_MB);
		IO_RegisterWriteHandler(0x2A22, &IMF_WritePIU2, IO_MB);
		IO_RegisterWriteHandler(0x2A23, &IMF_WritePCR, IO_MB);
		IO_RegisterWriteHandler(0x2A24, &IMF_WriteCNTR0, IO_MB);
		IO_RegisterWriteHandler(0x2A25, &IMF_WriteCNTR1, IO_MB);
		IO_RegisterWriteHandler(0x2A26, &IMF_WriteCNTR2, IO_MB);
		IO_RegisterWriteHandler(0x2A27, &IMF_WriteTCWR, IO_MB);
		IO_RegisterWriteHandler(0x2A28, &IMF_WriteTCR, IO_MB);
		IO_RegisterWriteHandler(0x2A29, &IMF_WriteTCR, IO_MB);
		IO_RegisterWriteHandler(0x2A2A, &IMF_WriteTCR, IO_MB);
		IO_RegisterWriteHandler(0x2A2B, &IMF_WriteTCR, IO_MB);
		IO_RegisterWriteHandler(0x2A2C, &IMF_WriteTSR, IO_MB);
		IO_RegisterWriteHandler(0x2A2D, &IMF_WriteTSR, IO_MB);
		IO_RegisterWriteHandler(0x2A2E, &IMF_WriteTSR, IO_MB);
		IO_RegisterWriteHandler(0x2A2F, &IMF_WriteTSR, IO_MB);

		// Send configuration commands to put FB-01 into IMFC-like state
		MIDI_RawOutBuffer(disableMemProtect, sizeof(disableMemProtect));
		MIDI_RawOutBuffer(setSysChannel1, sizeof(setSysChannel1));

		imfcPIT = new IMFC_PIT(configuration);

		// Start MIDI input
		// Open the device with the given ID
		if (MMSYSERR_NOERROR == midiInOpen( &hMidiIn, devID, (DWORD_PTR)MidiInProc, NULL, CALLBACK_FUNCTION ))
		{
			midiHdr.lpData = midiBuffer;
			midiHdr.dwBufferLength = sizeof(midiBuffer);
			midiHdr.dwFlags = 0;

			// Prepare a buffer so we can receive SysEx data
			if (MMSYSERR_NOERROR == midiInPrepareHeader(hMidiIn, &midiHdr, sizeof(midiHdr)))
				midiInAddBuffer(hMidiIn, &midiHdr, sizeof(midiHdr));
			else
				LOG_MSG("Error preparing MIDI In header!");

			midiInStart(hMidiIn);
		}
	}
	~IMFC() {
		if (hMidiIn != NULL)
		{
			midiInStop(hMidiIn);
			midiInClose(hMidiIn);
			hMidiIn = NULL;
		}

		delete imfcPIT;
	}
};

static IMFC* test;

void IMFC_Destroy(Section* sec) {
	delete test;
}

void IMFC_Init(Section* sec) {
	test = new IMFC(sec);
	sec->AddDestroyFunction(&IMFC_Destroy, true);
}

void MIDI_RawOutBuffer(Bit8u* pData, Bitu length)
{
	for (Bitu i = 0; i < length; i++)
		MIDI_RawOutByte(pData[i]);
}

// State for MIDI translations
bool inSysEx = false;
bool translate = false;
Bitu sIdx = 0;
Bitu pIdx = 0;

Bit8u parameterList[] = { 0xF0, 0x43, 0x75, 0x71 };
Bit8u parameterChange[] = { 0xF0, 0x43, 0x75, 0x00, 0x00, 0x00, 0x00, 0xF7, 0xF7 };	// Placeholder for SysEx message
Bit8u paramBuffer[4];

void IMF_MIDIFilter(Bit8u data, bool reset)
{
	if (reset)
	{
		sIdx = 0;
		pIdx = 0;
		inSysEx = false;
		translate = false;
	}

	if (data == 0xF0)
	{
		inSysEx = true;
		sIdx = 0;
	}

	if (!inSysEx)
	{
		// Just pass through the data
		MIDI_RawOutByte(data);
		return;
	}

	// Detect the Parameter List SysEx command
	if (sIdx < sizeof(parameterList))
	{
		if (parameterList[sIdx] == data)
		{
			sIdx++;

			if (sIdx >= sizeof(parameterList))
			{
				translate = true;
				return;
			}
		}
		else
		{
			// Not this particular SysEx command
			inSysEx = false;
			translate = false;

			// Pass through
			MIDI_RawOutBuffer(parameterList, sIdx);
			MIDI_RawOutByte(data);
			return;
		}
	}

	if (translate)
	{
		// We are in a Parameter List command
		if (data == 0xF7)
		{
			// Not anymore
			inSysEx = false;
			translate = false;
			return;
		}

		// Buffer data
		paramBuffer[pIdx++] = data;

		if (pIdx > 2)
		{
			// Is this a 3-byte command?
			if ((paramBuffer[1] & 0x40) == 0)
			{
				// Send SysEx
				Bit8u instrument = (paramBuffer[0] >> 4) & 0x7;
				Bit8u node = paramBuffer[0] & 0xF;
				Bit8u param = paramBuffer[1] & 0x3F;

				parameterChange[3] = node;
				parameterChange[4] = instrument | 0x18;
				parameterChange[5] = param;
				parameterChange[6] = paramBuffer[2];
				parameterChange[7] = 0xF7;

				MIDI_RawOutBuffer(parameterChange, 8);

				pIdx = 0;
				return;
			}
			
			// Is this a 4-byte command?
			if (pIdx > 3)
			{
				// Send SysEx
				Bit8u instrument = (paramBuffer[0] >> 4) & 0x7;
				Bit8u node = paramBuffer[0] & 0xF;
				Bit8u param = paramBuffer[1] & 0x3F;

				parameterChange[3] = node;
				parameterChange[4] = instrument | 0x18;
				parameterChange[5] = param | 0x40;
				parameterChange[6] = paramBuffer[2];
				parameterChange[7] = paramBuffer[3];
				parameterChange[8] = 0xF7;

				MIDI_RawOutBuffer(parameterChange, 9);

				pIdx = 0;
				return;
			}
		}
	}
}

// State for commands
Bitu cIdx = 0;
Bitu cLength = 0;

Bit8u commandBuffer[9];

// Supported commands:
// 1E0 Select Music Card Mode
// 1E1 Select Error Report Mode
// 1E2 Set Path
// 1E5 Reboot
// 1E3 Set Node Parameters

void IMF_CommandFilter(Bit8u data, bool reset)
{
	if (reset)
		cIdx = 0;

	// Are we in a command?
	if (cIdx == 0)
	{
		// Determine command length
		switch (data)
		{
		case 0xE0:	// Select Music Card Mode
			cLength = 2;
			break;
		case 0xE1:	// Select Error Report Mode
			cLength = 2;
			break;
		case 0xE2:	// Set Path
			cLength = 6;
			break;
		case 0xE5:	// Reboot
			cLength = 1;
			break;
		case 0xE3:	// Set Node Parameters
			cLength = 9;
			break;
		default:
			LOG_MSG("Unknown command: 1%02X", data);
			return;
		}
	}

	// Store next byte
	commandBuffer[cIdx++] = data;

	// Is command complete?
	if (cIdx >= cLength)
	{
		char buf[40];
		char* pBuf = buf;

		// Perform command
		for (Bitu i = 0; i < cLength; i++)
		{
			pBuf += sprintf(pBuf, "1%02X ", commandBuffer[i]);
		}

		LOG_MSG("IMF command: %s", buf);

		// Reset command
		cIdx = 0;
	}
}

static void IMFC_TimerAEvent(Bitu val)
{
	TimerAInt(true);

	if (IBE && TMSK && TAE)
		PIC_ActivateIRQ(IMFCIrq);

	// Hack to generate IRQs
	if (TxRDY)
		IMFC_TxRDYEvent(0);

	imfcPIT->RestartIRQ(0, IMFC_TimerAEvent);
}

static void IMFC_TimerBEvent(Bitu val)
{
	TimerBInt(true);

	if (IBE && TMSK && TBE)
		PIC_ActivateIRQ(IMFCIrq);

	imfcPIT->RestartIRQ(1, IMFC_TimerBEvent);
}

static void IMFC_RxRDYEvent(Bitu val)
{
	RxRDYInt(true);

	if (IBE && TMSK && RIE)
		PIC_ActivateIRQ(IMFCIrq);
}

static void IMFC_TxRDYEvent(Bitu val)
{
	TxRDYInt(true);

	if (IBE && TMSK && WIE)
		PIC_ActivateIRQ(IMFCIrq);
}

static void TimerAInt(bool enabled)
{
	timerAInt = enabled;
}

static void TimerBInt(bool enabled)
{
	timerBInt = enabled;
}

static void RxRDYInt(bool enabled)
{
	rxRDYInt = enabled;
}

static void TxRDYInt(bool enabled)
{
	txRDYInt = enabled;
}

