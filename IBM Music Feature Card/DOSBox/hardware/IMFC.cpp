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

void MIDI_RawOutByte(Bit8u data);
void MIDI_RawOutBuffer(Bit8u* pData, Bitu length);
void IMF_MIDIFilter(Bit8u data);
bool MIDI_Available(void);

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
static bool dataMode = true;
static Bitu piu1Data = 0;

static Bitu IMF_ReadPIU0(Bitu port, Bitu iolen)
{
	LOG_MSG("IMF PIU0:Rd %4X", (int)port);

	return 0;
}

static Bitu IMF_ReadPIU1(Bitu port, Bitu iolen)
{
	LOG_MSG("IMF PIU1:Rd %4X", (int)port);

	return piu1Data;
}

static Bitu IMF_ReadPIU2(Bitu port, Bitu iolen)
{
	LOG_MSG("IMF PIU2:Rd %4X", (int)port);

	// Always set TxRDY, RxRDY and some other bits that software might be interested in
	return 0x15;
}

static Bitu IMF_ReadPCR(Bitu port, Bitu iolen)
{
	LOG_MSG("IMF PCR:Rd %4X", (int)port);

	return 0;
}

static Bitu IMF_ReadCNTR0(Bitu port, Bitu iolen)
{
	LOG_MSG("IMF CNTR0:Rd %4X", (int)port);

	return 0;
}

static Bitu IMF_ReadCNTR1(Bitu port, Bitu iolen)
{
	LOG_MSG("IMF CNTR1:Rd %4X", (int)port);

	return 0;
}

static Bitu IMF_ReadCNTR2(Bitu port, Bitu iolen)
{
	LOG_MSG("IMF CNTR0:Rd %4X", (int)port);

	return 0;
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
	LOG_MSG("IMF TSR:Rd %4X", (int)port);

	return 0;
}

static void IMF_WritePIU0(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF PIU0:Wr %4X,%X", (int)port, (int)val);
}

static void IMF_WritePIU1(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF PIU1:Wr %4X,%X", (int)port, (int)val);

	// Buffer last byte, used as detection, apparently
	piu1Data = val;

	if (dataMode)
	{
		// This is a MIDI byte, output it
		IMF_MIDIFilter(val);
	}
}

static void IMF_WritePIU2(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF PIU2:Wr %4X,%X", (int)port, (int)val);
}

static void IMF_WritePCR(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF PCR:Wr %4X,%X", (int)port, (int)val);
}

static void IMF_WriteCNTR0(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF CNTR0:Wr %4X,%X", (int)port, (int)val);
}

static void IMF_WriteCNTR1(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF CNTR1:Wr %4X,%X", (int)port, (int)val);
}

static void IMF_WriteCNTR2(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF CNTR2:Wr %4X,%X", (int)port, (int)val);
}

static void IMF_WriteTCWR(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF TCWR:Wr %4X,%X", (int)port, (int)val);
}

static void IMF_WriteTCR(Bitu port, Bitu val, Bitu iolen)
{
	LOG_MSG("IMF TCR:Wr %4X,%X", (int)port, (int)val);

	if (val == 0x00)
		dataMode = true;
	else if (val == 0x10)
		dataMode = false;
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
	}
	~IMFC() {
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
	for (int i = 0; i < length; i++)
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

void IMF_MIDIFilter(Bit8u data)
{
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