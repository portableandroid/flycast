/*
	Basic gdrom syscall emulation
	Adapted from some (very) old pre-nulldc hle code
	Bits and pieces from redream (https://github.com/inolen/redream)
*/

#include <cstdio>
#include "types.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/sh4/sh4_core.h"
#undef r

#include "gdrom_hle.h"
#include "hw/gdrom/gdromv3.h"
#include "hw/holly/holly_intc.h"
#include "reios.h"
#include "imgread/common.h"
#include "hw/sh4/modules/mmu.h"

#include <algorithm>

#define SWAP32(a) ((((a) & 0xff) << 24)  | (((a) & 0xff00) << 8) | (((a) >> 8) & 0xff00) | (((a) >> 24) & 0xff))

#define debugf(...) DEBUG_LOG(REIOS, __VA_ARGS__)

gdrom_hle_state_t gd_hle_state;

static void GDROM_HLE_ReadSES()
{
	auto [s, b, ba, bb] = gd_hle_state.params;
	INFO_LOG(REIOS, "GDROM_HLE_ReadSES: doing nothing w/ %d, %d, %d, %d", s, b, ba, bb);
}

static void GDROM_HLE_ReadTOC()
{
	u32 area = gd_hle_state.params[0];
	u32 dest = gd_hle_state.params[1];

	debugf("GDROM READ TOC : %X %X", area, dest);
	if (area == DoubleDensity && libGDR_GetDiscType() != GdRom)
	{
		// Only GD-ROM has a high-density area but no error is reported
		gd_hle_state.status = GDC_OK;
		return;
	}

	u32 toc[102];
	libGDR_GetToc(toc, (DiskArea)area);

	// Swap results to LE
	for (std::size_t i = 0; i < std::size(toc); i++) {
		toc[i] = SWAP32(toc[i]);
	}
	if (!mmu_enabled())
	{
		u32* pDst = (u32*)GetMemPtr(dest, sizeof(toc));
		if (pDst != NULL)
		{
			memcpy(pDst, toc, sizeof(toc));
			return;
		}
	}
	for (std::size_t i = 0; i < std::size(toc); i++, dest += 4)
		WriteMem32(dest, toc[i]);
}

template<bool virtual_addr>
static void read_sectors_to(u32 addr, u32 sector, u32 count)
{
	gd_hle_state.cur_sector = sector + count - 1;
	if (virtual_addr)
		gd_hle_state.xfer_end_time = 0;
	else if (count > 5 && !config::FastGDRomLoad)
		// Large Transfers: GD-ROM rate (approx. 1.8 MB/s)
		gd_hle_state.xfer_end_time = sh4_sched_now64() + (u64)count * 2048 * 1000000L / 10240;
	else
		// Small transfers: Max G1 bus rate: 50 MHz x 16 bits
		gd_hle_state.xfer_end_time = sh4_sched_now64() + 5 * 2048 * 2;
	if (!virtual_addr || !mmu_enabled())
	{
		u8 * pDst = GetMemPtr(addr, 0);

		if (pDst != NULL)
		{
			libGDR_ReadSector(pDst, sector, count, 2048);
			return;
		}
	}
	u32 temp[2048 / 4];

	while (count > 0)
	{
		libGDR_ReadSector((u8 *)temp, sector, 1, sizeof(temp));

		for (std::size_t i = 0; i < std::size(temp); i++)
		{
			if (virtual_addr)
				WriteMem32(addr, temp[i]);
			else
				WriteMem32_nommu(addr, temp[i]);
			addr += 4;
		}

		sector++;
		count--;
	}
}

static void GDROM_HLE_ReadDMA()
{
	u32 fad = gd_hle_state.params[0] & 0xffffff;
	u32 nsect = gd_hle_state.params[1];
	u32 buffer = gd_hle_state.params[2];
	// params[3] 0

	debugf("GDROM: DMA READ Sector=%d, Num=%d, Buffer=%08x, zero=%x", fad, nsect, buffer, gd_hle_state.params[3]);

	read_sectors_to<false>(buffer, fad, nsect);
	gd_hle_state.result[2] = 0;
	gd_hle_state.result[3] = 0;
}

static void GDROM_HLE_ReadPIO()
{
	u32 fad = gd_hle_state.params[0] & 0xffffff;
	u32 nsect = gd_hle_state.params[1];
	u32 buffer = gd_hle_state.params[2];
	// params[3] seekAhead (wince) or 0

	debugf("GDROM: PIO READ Sector=%d, Num=%d, Buffer=%08x, SeekAhead=%x", fad, nsect, buffer, gd_hle_state.params[3]);

	read_sectors_to<true>(buffer, fad, nsect);
	gd_hle_state.result[2] = nsect * 2048;
	gd_hle_state.result[3] = 0;
}

static void GDCC_HLE_GETSCD()
{
	u32 format = gd_hle_state.params[0];
	u32 size = gd_hle_state.params[1];
	u32 dest = gd_hle_state.params[2];
	// params[3] 0

	DEBUG_LOG(REIOS, "GDROM: GETSCD format %x size %x dest %08x", format, size, dest);
	if (sns_asc != 0)
	{
		// Helps D2 detect the disk change
		gd_hle_state.status = GDC_ERR;
		gd_hle_state.result[0] = sns_key;
		gd_hle_state.result[1] = sns_asc | (sns_ascq << 8);
		gd_hle_state.result[2] = 0x18; // ?
		gd_hle_state.result[3] = 0;
		sns_key = 0;
		sns_asc = 0;
		sns_ascq = 0;
		return;
	}
	if (SecNumber.Status != GD_BUSY && (libGDR_GetDiscType() == Open || libGDR_GetDiscType() == NoDisk))
	{
		gd_hle_state.status = GDC_ERR;
		gd_hle_state.result[0] = 2;
		gd_hle_state.result[1] = 0x3a;	// Media operation command was received but no media is inserted.
		gd_hle_state.result[2] = 0;
		gd_hle_state.result[3] = 0;
		return;
	}
	if (cdda.status == cdda_t::Playing)
		gd_hle_state.cur_sector = cdda.CurrAddr.FAD;
	u8 scd[100];
	gd_get_subcode(format, gd_hle_state.cur_sector, scd);
	verify(scd[3] == size);

	if (!mmu_enabled() && GetMemPtr(dest, size) != NULL)
		memcpy(GetMemPtr(dest, size), scd, size);
	else
	{
		for (u32 i = 0; i < size; i++)
			WriteMem8(dest++, scd[i]);
	}

	// record size of pio transfer to gdrom
	gd_hle_state.result[2] = size;
}

template<bool dma>
static void multi_xfer()
{
	u32 dest = gd_hle_state.params[0];
	u32 size = gd_hle_state.params[1];

	size = std::min(size, gd_hle_state.multi_read_count);
	while (size > 0)
	{
		u8 buf[2048];
		libGDR_ReadSector(buf, gd_hle_state.multi_read_sector, 1, 2048);
		while (size > 0)
		{
			int remaining = 2048 - gd_hle_state.multi_read_offset;
			if (size >= 4 && remaining >= 4 && (dest & 3) == 0)
			{
				if constexpr (dma)
					WriteMem32_nommu(dest, *(u32*)&buf[gd_hle_state.multi_read_offset]);
				else
					WriteMem32(dest, *(u32*)&buf[gd_hle_state.multi_read_offset]);
				dest += 4;
				gd_hle_state.multi_read_offset += 4;
				gd_hle_state.multi_read_count -= 4;
				size -= 4;
			}
			else if (size >= 2 && remaining >= 2 && (dest & 1) == 0)
			{
				if constexpr (dma)
					WriteMem16_nommu(dest, *(u16*)&buf[gd_hle_state.multi_read_offset]);
				else
					WriteMem16(dest, *(u16*)&buf[gd_hle_state.multi_read_offset]);
				dest += 2;
				gd_hle_state.multi_read_offset += 2;
				gd_hle_state.multi_read_count -= 2;
				size -= 2;
			}
			else
			{
				if constexpr (dma)
					WriteMem8_nommu(dest, buf[gd_hle_state.multi_read_offset]);
				else
					WriteMem8(dest, buf[gd_hle_state.multi_read_offset]);
				dest++;
				gd_hle_state.multi_read_offset++;
				gd_hle_state.multi_read_count--;
				size--;
			}
			if (gd_hle_state.multi_read_offset >= 2048)
			{
				verify(gd_hle_state.multi_read_offset == 2048);
				gd_hle_state.multi_read_sector++;
				gd_hle_state.multi_read_offset = 0;
				break;
			}
		}
	}
	if (!dma)
	{
		gd_hle_state.result[2] = gd_hle_state.multi_read_total - gd_hle_state.multi_read_count;
		gd_hle_state.result[3] = GDC_WAIT_INTERNAL;
		if (gd_hle_state.multi_callback != 0)
		{
			Sh4cntx.r[4] = gd_hle_state.multi_callback_arg;
			Sh4cntx.pc = gd_hle_state.multi_callback;
		}
	}
	else
	{
		gd_hle_state.result[2] = 2048;
		gd_hle_state.result[3] = gd_hle_state.multi_read_count > 0 ? GDC_WAIT_IRQ : GDC_WAIT_INTERNAL;
		gd_hle_state.dma_trans_ended = true;
		if (gd_hle_state.multi_read_count == 0)
			gd_hle_state.status = GDC_COMPLETE;
		asic_RaiseInterrupt(holly_GDROM_DMA);
	}
}

u32 SecMode[4];

static void GD_HLE_Command(gd_command cc)
{
	switch(cc)
	{
	case GDCC_GETTOC:
		WARN_LOG(REIOS, "GDROM: *FIXME* CMD GETTOC");
		break;

	case GDCC_GETTOC2:
		GDROM_HLE_ReadTOC();
		break;

	case GDCC_REQ_SES:
		GDROM_HLE_ReadSES();
		break;

	case GDCC_INIT:
		DEBUG_LOG(REIOS, "GDROM: CMD INIT");
		gd_hle_state.multi_callback = 0;
		gd_hle_state.multi_read_count = 0;
		cdda.status = cdda_t::NoInfo;
		break;

	case GDCC_PIOREAD:
		GDROM_HLE_ReadPIO();
		SecNumber.Status = GD_PAUSE;
		cdda.status = cdda_t::NoInfo;
		break;

	case GDCC_DMAREAD:
		cdda.status = cdda_t::NoInfo;
		if (gd_hle_state.xfer_end_time == 0)
			GDROM_HLE_ReadDMA();
		if (gd_hle_state.xfer_end_time > 0)
		{
			if (gd_hle_state.xfer_end_time > sh4_sched_now64())
				return;
			gd_hle_state.xfer_end_time = 0;
		}
		gd_hle_state.result[2] = gd_hle_state.params[1] * 2048;
		gd_hle_state.result[3] = 0;
		SecNumber.Status = GD_PAUSE;
		break;


	case GDCC_PLAY2:
		{
			cdda.StartAddr.FAD = gd_hle_state.params[0] & 0xffffff;
			cdda.EndAddr.FAD = gd_hle_state.params[1] & 0xffffff;
			cdda.repeats = gd_hle_state.params[2];
			// params[3] debug (0)
			DEBUG_LOG(REIOS, "GDROM: CMD PLAYSEC from %d to %d repeats %d", cdda.StartAddr.FAD, cdda.EndAddr.FAD, cdda.repeats);
			cdda.status = cdda_t::Playing;
			cdda.CurrAddr.FAD = cdda.StartAddr.FAD;
			SecNumber.Status = GD_PLAY;
		}
		break;

	case GDCC_RELEASE:
		DEBUG_LOG(REIOS, "GDROM: CMD RELEASE");
		// params[0] reptime (ignored)
		// params[1] debug (0)
		if (cdda.status == cdda_t::Paused)
			cdda.status = cdda_t::Playing;
		SecNumber.Status = GD_PLAY;
		break;

	case GDCC_STOP:
		DEBUG_LOG(REIOS, "GDROM: CMD STOP");
		cdda.status = cdda_t::NoInfo;
		SecNumber.Status = GD_STANDBY;
		break;

	case GDCC_SEEK:
		DEBUG_LOG(REIOS, "GDROM: CMD SEEK");
		cdda.CurrAddr.FAD = cdda.StartAddr.FAD = gd_hle_state.params[0] & 0xffffff;
		// params[1] debug (0)
		cdda.status = cdda_t::Paused;
		SecNumber.Status = GD_PAUSE;
		break;

	case GDCC_PLAY:
		if (libGDR_GetDiscType() != Open && libGDR_GetDiscType() != NoDisk)
		{
			u32 first_track = gd_hle_state.params[0];
			u32 last_track = gd_hle_state.params[1];
			cdda.repeats = gd_hle_state.params[2];
			// params[3] debug (0)
			if (first_track == last_track) {
				libGDR_GetTrack(first_track, cdda.StartAddr.FAD, cdda.EndAddr.FAD);
			}
			else
			{
				u32 dummy;
				libGDR_GetTrack(first_track, cdda.StartAddr.FAD, dummy);
				libGDR_GetTrack(last_track, dummy, cdda.EndAddr.FAD);
			}
			DEBUG_LOG(REIOS, "GDROM: CMD PLAY first_track %x last_track %x repeats %x start_fad %x end_fad %x", first_track, last_track, cdda.repeats,
					cdda.StartAddr.FAD, cdda.EndAddr.FAD);
			cdda.status = cdda_t::Playing;
			cdda.CurrAddr.FAD = cdda.StartAddr.FAD;
			SecNumber.Status = GD_PLAY;
		}
		else
		{
			gd_hle_state.status = GDC_ERR;
			cdda.status = cdda_t::NoInfo;
			SecNumber.Status = GD_STANDBY;
		}
		break;

	case GDCC_PAUSE:
		DEBUG_LOG(REIOS, "GDROM: CMD PAUSE");
		if (cdda.status == cdda_t::Playing)
			cdda.status = cdda_t::Paused;
		SecNumber.Status = GD_PAUSE;
		break;

	case GDCC_DMA_READ_REQ:
		{
			u32 sector = gd_hle_state.params[0] & 0xffffff;
			u32 num = gd_hle_state.params[1];

			DEBUG_LOG(REIOS, "GDROM: CMD READ Sector=%d, Num=%d", sector, num);
			gd_hle_state.status = GDC_CONTINUE;
			gd_hle_state.multi_read_sector = sector;
			gd_hle_state.multi_read_count = num * 2048;
			gd_hle_state.multi_read_total = gd_hle_state.multi_read_count;
			gd_hle_state.multi_read_offset = 0;
			gd_hle_state.result[2] = 2048;
			gd_hle_state.result[3] = num > 0 ? GDC_WAIT_IRQ : GDC_WAIT_INTERNAL;
		}
		break;

	case GDCC_GETSCD:
		GDCC_HLE_GETSCD();
		break;

	case GDCC_REQ_MODE:
		{
			u32 dest = gd_hle_state.params[0];
			debugf("GDROM: REQ_MODE dest:%x", dest);
			WriteMem32(dest, GD_HardwareInfo.speed);
			WriteMem32(dest + 4, (GD_HardwareInfo.standby_hi << 8) | GD_HardwareInfo.standby_lo);
			WriteMem32(dest + 8, GD_HardwareInfo.read_flags);
			WriteMem32(dest + 12, GD_HardwareInfo.read_retry);

			// record size of pio transfer to gdrom
			gd_hle_state.result[2] = 0xa;
		}
		break;

	case GDCC_SET_MODE:
		{
			auto [speed, standby, read_flags, read_retry] = gd_hle_state.params;

			debugf("GDROM: SET_MODE speed %x standby %x read_flags %x read_retry %x", speed, standby, read_flags, read_retry);

			GD_HardwareInfo.speed = speed;
			GD_HardwareInfo.standby_hi = (standby & 0xff00) >> 8;
			GD_HardwareInfo.standby_lo = standby & 0xff;
			GD_HardwareInfo.read_flags = read_flags;
			GD_HardwareInfo.read_retry = read_retry;

			// record size of pio transfer to gdrom
			gd_hle_state.result[2] = 0xa;
		}
		break;

	case GDCC_GET_VERSION:
		{
			u32 dest = gd_hle_state.params[0];
			// params[1] 0

			debugf("GDROM: GDCC_GET_VERSION dest %x", dest);

			char ver[] = "GDC Version 1.10 1999-03-31 ";
			u32 len = (u32)strlen(ver);

			// 0x8c0013b8 (offset 0xd0 in the gdrom state struct) is then loaded and
			// overwrites the last byte. no idea what this is, but seems to be hard
			// coded to 0x02 on boot
			ver[len - 1] = 0x02;

			for (u32 i = 0; i < len; i++)
				WriteMem8(dest++, ver[i]);
		}
		break;

	case GDCC_REQ_STAT:
		{
			// odd, but this function seems to get passed 4 unique pointers
			u32 dst0 = gd_hle_state.params[0];	// repeat
			u32 dst1 = gd_hle_state.params[1];	// track
			u32 dst2 = gd_hle_state.params[2];	// toc
			u32 dst3 = gd_hle_state.params[3];	// index

			debugf("GDROM: GDCC_REQ_STAT dst0=%08x dst1=%08x dst2=%08x dst3=%08x", dst0, dst1, dst2, dst3);

			// bit   |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
			// byte  |     |     |     |     |     |     |     |
			// ------------------------------------------------------
			// 0     |  0  |  0  |  0  |  0  | status
			// ------------------------------------------------------
			// 1     |  0  |  0  |  0  |  0  | repeat count
			// ------------------------------------------------------
			// 2-3   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0
			WriteMem32(dst0, (cdda.repeats << 8) | (SecNumber.Status == GD_STANDBY ? GD_PAUSE : SecNumber.Status));

			// bit   |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
			// byte  |     |     |     |     |     |     |     |
			// ------------------------------------------------------
			// 0     | subcode q track number
			// ------------------------------------------------------
			// 1-3   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0
			const u32 fad = cdda.status == cdda_t::Paused || cdda.status == cdda_t::Playing ? cdda.CurrAddr.FAD : gd_hle_state.cur_sector;
			u32 elapsed;
			u32 tracknum = libGDR_GetTrackNumber(fad, elapsed);
			WriteMem32(dst1, tracknum);

			// bit   |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
			// byte  |     |     |     |     |     |     |     |
			// ------------------------------------------------------
			// 0-2  | fad (little-endian)
			// ------------------------------------------------------
			// 3    | ADR                    | Control
			u8 adr, ctrl;
			libGDR_GetTrackAdrAndControl(tracknum, adr, ctrl);
			u32 out = (adr << 28) | (ctrl << 24) | (fad & 0x00ffffff);
			WriteMem32(dst2, out);

			// bit   |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
			// byte  |     |     |     |     |     |     |     |
			// ------------------------------------------------------
			// 0     | subcode q index number
			// ------------------------------------------------------
			// 1-3   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0
			WriteMem32(dst3, 1);

			// record pio transfer size
			gd_hle_state.result[2] = 0xa;
		}
		break;

	case GDCC_MULTI_DMAREAD:
	case GDCC_MULTI_PIOREAD:
		{
			u32 sector = gd_hle_state.params[0] & 0xffffff;
			u32 num = gd_hle_state.params[1];
			// params[2] seekAhead (wince)
			bool dma = cc == GDCC_MULTI_DMAREAD;

			DEBUG_LOG(REIOS, "GDROM: MULTI_%sREAD Sector=%d, Num=%d SeekAhead=%d", dma ? "DMA" : "PIO", sector, num, gd_hle_state.params[2]);

			gd_hle_state.status = GDC_CONTINUE;
			gd_hle_state.multi_read_sector = sector;
			gd_hle_state.multi_read_count = num * 2048;
			gd_hle_state.multi_read_total = gd_hle_state.multi_read_count;
			gd_hle_state.multi_read_offset = 0;

			// wild guesses here
			gd_hle_state.result[2] = 0;
			gd_hle_state.result[3] = 0;
		}
		break;

	case GDCC_REQ_DMA_TRANS:
	case GDCC_REQ_PIO_TRANS:
		{
			u32 dest = gd_hle_state.params[0];
			u32 size = gd_hle_state.params[1];
			bool dma = cc == GDCC_REQ_DMA_TRANS;
			DEBUG_LOG(REIOS, "GDROM: REQ_%s_TRANS dest %x size %x", dma ? "DMA" : "PIO",
					dest, size);
			if (dma)
				multi_xfer<true>();
			else
				multi_xfer<false>();
		}
		break;

	default:
		WARN_LOG(REIOS, "GDROM: Unknown GDROM CC:%X", cc);
		break;
	}
	if (gd_hle_state.status == GDC_BUSY)
		gd_hle_state.status = GDC_COMPLETE;
	gd_hle_state.command = GDCC_NONE;
}

#define r Sh4cntx.r

void gdrom_hle_op()
{
	if (SYSCALL_GDROM == r[6])		// GDROM SYSCALL
	{
		switch(r[7])				// COMMAND CODE
		{
		case GDROM_REQ_CMD:
			// Enqueue a command for the GDROM subsystem to execute.
			//
			// Args:
			//	r4 = command code
			//	r5 = pointer to parameter block for the command, can be NULL if the command does not take parameters
			//
			// Returns: a request id (>0) if successful, 0 if failed
			debugf("GDROM: HLE REQ_CMD CC:%X  param ptr: %X bios status %d", r[4], r[5], gd_hle_state.status);
			if (gd_hle_state.status != GDC_OK)
			{
				r[0] = 0;
			}
			else
			{
				for (int i = 0; i < 4; i++)
				{
					try {
						gd_hle_state.params[i] = r[5] == 0 ? 0 : ReadMem32(r[5] + i * 4);
					} catch (SH4ThrownException&) {
						// Ignore page faults. happens for commands not taking params
						gd_hle_state.params[i] = 0;
					}
				}
				memset(gd_hle_state.result, 0, sizeof(gd_hle_state.result));
				if (gd_hle_state.next_request_id == ~0u || gd_hle_state.next_request_id == 0)
					gd_hle_state.next_request_id = 1;
				gd_hle_state.last_request_id = r[0] = gd_hle_state.next_request_id++;
				gd_hle_state.status = GDC_BUSY;
				gd_hle_state.command = (gd_command)r[4];
				gd_hle_state.multi_read_count = 0;
			}
			break;

		case GDROM_GET_CMD_STAT:
			// Check if an enqueued command has completed.
			//
			// Args:
			//	r4 = request id
			//	r5 = pointer to four 32 bit integers to receive extended status information. The first is a generic error code:
			// GDC_ERR_NOERR          0x00
			// GDC_ERR_RECOVERED      0x01
			// GDC_ERR_NOTREADY       0x02
			// GDC_ERR_MEDIUM         0x03
			// GDC_ERR_HARDWARE       0x04
			// GDC_ERR_ILLEGALREQUEST 0x05
			// GDC_ERR_UNITATTENTION  0x06
			// GDC_ERR_DATAPROTECT    0x07
			// GDC_ERR_ABORTED        0x0B
			// GDC_ERR_NOREADABLE     0x10
			// GDC_ERR_G1SEMAPHORE    0x20
			//
			// Returns:
			//	GDC_OK - no such request active (idle)
			//	GDC_BUSY - request is still being processed
			//	GDC_COMPLETE - request has completed (if queried again, you will get GDC_OK)
			//	GDC_CONTINUE - multi request has data available
			//  GDC_SMPHR_BUSY ?
			//	GDC_ERR - request has failed (examine extended status information for cause of failure)
			try {
				WriteMem32(r[5], gd_hle_state.result[0]);		// error
				WriteMem32(r[5] + 4, gd_hle_state.result[1]);	// error1
				WriteMem32(r[5] + 8, gd_hle_state.result[2]);	// size
				WriteMem32(r[5] + 12, gd_hle_state.result[3]);	// wait state (if busy)
			} catch (SH4ThrownException&) {
			}
			if (gd_hle_state.status == GDC_OK || gd_hle_state.status == GDC_BUSY)
			{
				r[0] = gd_hle_state.status;
			}
			else if (r[4] != gd_hle_state.last_request_id)
			{
				r[0] = GDC_OK; // no such request active
			}
			else
			{
				if (gd_hle_state.status == GDC_CONTINUE && gd_hle_state.command == GDCC_REQ_PIO_TRANS)
					r[0] = GDC_BUSY;	// Bust-a-move 4 likes this
				else
					r[0] = gd_hle_state.status;	// completed or error
				// Fixes NBA 2K
				if (gd_hle_state.status == GDC_CONTINUE && gd_hle_state.multi_read_count == 0)
				{
					gd_hle_state.status = GDC_COMPLETE;
					gd_hle_state.result[3] = 0;
				}
				else if (gd_hle_state.status != GDC_CONTINUE)
				{
					gd_hle_state.status = GDC_OK;
					gd_hle_state.last_request_id = 0xFFFFFFFF;
				}
			}
			debugf("GDROM: HLE GET_CMD_STAT REQID:%X  param ptr: %X -> %X : %x %x %x %x", r[4], r[5], r[0],
					gd_hle_state.result[0], gd_hle_state.result[1], gd_hle_state.result[2], gd_hle_state.result[3]);
			break;

		case GDROM_EXEC_SERVER:
			// In order for enqueued commands to get processed, this function must be called a few times.
			debugf("GDROM: HLE EXEC_SERVER");
			if (gd_hle_state.status == GDC_BUSY || (gd_hle_state.status == GDC_CONTINUE && gd_hle_state.command == GDCC_REQ_PIO_TRANS))
			{
				GD_HLE_Command(gd_hle_state.command);
			}
			break;

		case GDROM_INIT_SYSTEM:
			// Initialize the GDROM subsystem. Should be called before any requests are enqueued.
			DEBUG_LOG(REIOS, "GDROM: HLE INIT_SYSTEM");
			gd_hle_state = {};
			break;

		case GDROM_RESET:
			// Resets the drive.
			DEBUG_LOG(REIOS, "GDROM: HLE RESET");
			gd_hle_state.last_request_id = 0xFFFFFFFF;
			gd_hle_state.status = GDC_OK;
			break;

		case GDROM_GET_DRV_STAT:
			{
				// Checks the general condition of the drive.
				//
				// Args:
				//	r4 = pointer to two 32 bit integers, to receive the drive status. The first is the current drive status (gd_drv_stat),
				//       the second is the type of disc inserted (if any).
				//
				// Returns: 0 OK, -1 ERR, 1 BUSY, 2 COMPLETE, 3 CONTINUE
				gd_drv_stat status;
				u32 discType;
				if (SecNumber.Status == GD_BUSY)
				{
					status = GD_STAT_BUSY;
					discType = 0;
				}
				else
				{
					discType = libGDR_GetDiscType();
					switch (discType)
					{
					case Open:
						status = GD_STAT_OPEN;
						discType = 0;
						break;
					case NoDisk:
						status = GD_STAT_NODISC;
						discType = 0;
						break;
					default:
						if (gd_hle_state.status == GDC_CONTINUE || SecNumber.Status == GD_PLAY)
							status = GD_STAT_PLAY;
						else
							status = GD_STAT_PAUSE;
						if (memcmp(ip_meta.disk_type, "GD-ROM", sizeof(ip_meta.disk_type)) == 0)
							discType = GdRom;
						break;
					}
				}
				WriteMem32(r[4], (u32)status);
				WriteMem32(r[4] + 4, discType);
				debugf("GDROM: HLE GET_DRV_STAT r4:%X -> %x %x", r[4], status, discType);
				r[0] = GDC_OK;
			}
			break;

		case GDROM_READ_ABORT:
			// Tries to abort a previously enqueued command.
			//
			// Args:
			//	r4 = request id
			//
			// Returns: GDC_OK, GDC_ERR
			INFO_LOG(REIOS, "GDROM: HLE GDROM_ABORT_COMMAND req id%x", r[4]);
			if (r[4] == gd_hle_state.last_request_id
					&& (gd_hle_state.status == GDC_CONTINUE || gd_hle_state.status == GDC_BUSY || gd_hle_state.status == GDC_COMPLETE))
			{
				r[0] = GDC_OK;
				gd_hle_state.multi_read_count = 0;
				gd_hle_state.xfer_end_time = 0;
			}
			else
			{
				r[0] = GDC_ERR;
			}
			break;


		case GDROM_CHANGE_DATA_TYPE:
			// Sets/gets the sector format for read commands.
			//
			// Args:
			//	r4 = pointer to a struct of four 32 bit integers containing new values, or to receive the old values
			//	Field	Function
			//	0 	Get/Set, if 0 the mode will be set, if 1 it will be queried.
			//	1 	? (always 8192)
			//	2 	1024 = mode 1, 2048 = mode 2, 0 = auto detect
			//	3 	Sector size in bytes (normally 2048)
			//
			// Returns: GDC_OK, GDC_ERR
			DEBUG_LOG(REIOS, "GDROM: HLE CHANGE_DATA_TYPE PTR_r4:%X",r[4]);
			for(std::size_t i = 0; i < std::size(SecMode); i++) {
				SecMode[i] = ReadMem32(r[4]+(i<<2));
				DEBUG_LOG(REIOS, "%08X", SecMode[i]);
			}
			r[0] = GDC_OK;
			break;

		case GDROM_G1_DMA_END:
			DEBUG_LOG(REIOS, "GDROM: G1_DMA_END callback %x arg %x", r[4], r[5]);
			gd_hle_state.multi_callback = r[4];
			gd_hle_state.multi_callback_arg = r[5];
			r[0] = GDC_OK;
			if (gd_hle_state.multi_callback != 0 && gd_hle_state.dma_trans_ended)	// FIXME hack for 2K sports games
			{
				r[4] = gd_hle_state.multi_callback_arg;
				Sh4cntx.pc = gd_hle_state.multi_callback;
				gd_hle_state.dma_trans_ended = false;
			}
			asic_CancelInterrupt(holly_GDROM_DMA);
			break;

		case GDROM_REQ_DMA_TRANS:
			gd_hle_state.params[0] = ReadMem32(r[5]);		// buffer
			gd_hle_state.params[1] = ReadMem32(r[5] + 4);	// size
			DEBUG_LOG(REIOS, "GDROM: REQ_DMA_TRANS req_id %x dest %x size %x",
					r[4], gd_hle_state.params[0], gd_hle_state.params[1]);

			if (gd_hle_state.status != GDC_CONTINUE || gd_hle_state.params[1] > gd_hle_state.multi_read_count || gd_hle_state.params[1] == 0)
			{
				r[0] = GDC_ERR;
			}
			else
			{
				multi_xfer<true>();
				r[0] = GDC_OK;
			}
			break;

		case GDROM_REQ_PIO_TRANS:
			gd_hle_state.params[0] = ReadMem32(r[5]);
			gd_hle_state.params[1] = ReadMem32(r[5] + 4);
			DEBUG_LOG(REIOS, "GDROM: REQ_PIO_TRANS req_id %x dest %x size %x",
					r[4], gd_hle_state.params[0], gd_hle_state.params[1]);
			if (gd_hle_state.status != GDC_CONTINUE || gd_hle_state.params[1] > gd_hle_state.multi_read_count)
			{
				r[0] = GDC_ERR;
			}
			else
			{
				gd_hle_state.command = GDCC_REQ_PIO_TRANS;
				r[0] = GDC_OK;
			}
			break;

		case GDROM_CHECK_DMA_TRANS:
			{
				// r4 handle
				u32 len_addr = r[5];
				DEBUG_LOG(REIOS, "GDROM: CHECK_DMA_TRANS req_id %x len_addr %x -> %x", r[4], len_addr, gd_hle_state.multi_read_count);
				WriteMem32(len_addr, gd_hle_state.multi_read_count);
				if (gd_hle_state.status == GDC_CONTINUE)
				{
					r[0] = GDC_OK;
				}
				else
				{
					r[0] = GDC_BUSY;
				}
			}
			break;

		case GDROM_SET_PIO_CALLBACK:
			DEBUG_LOG(REIOS, "GDROM: SET_PIO_CALLBACK callback %x arg %x", r[4], r[5]);
			gd_hle_state.multi_callback = r[4];
			gd_hle_state.multi_callback_arg = r[5];
			r[0] = GDC_OK;
			break;

		case GDROM_CHECK_PIO_TRANS:
			{
				u32 len_addr = r[5];
				DEBUG_LOG(REIOS, "GDROM: CHECK_PIO_TRANS req_id %x len_addr %x -> %x", r[4], len_addr, gd_hle_state.multi_read_count);
				if (gd_hle_state.status == GDC_CONTINUE)
				{
					WriteMem32(len_addr, gd_hle_state.multi_read_count);
					r[0] = GDC_OK;
				}
				else
				{
					r[0] = GDC_ERR;
				}
			}
			break;

		default:
			WARN_LOG(REIOS, "GDROM: Unknown SYSCALL: %X",r[7]);
			break;
		}
	}
	else							// MISC 
	{
		switch(r[7])
		{
		case MISC_INIT:
			// Initializes all the syscall vectors to their default values.
			// Returns: zero
			WARN_LOG(REIOS, "GDROM: MISC_INIT not implemented");
			r[0] = GDC_OK;
			break;

		case MISC_SETVECTOR:
			// Sets/clears the handler for one of the eight superfunctions for this vector. Setting a handler is only allowed if it not currently set.
			//
			// Args:
			//	r4 = superfunction number (0-7)
			//	r5 = pointer to handler function, or NULL to clear
			//
			// Returns: zero if successful, -1 if setting/clearing the handler fails
			WARN_LOG(REIOS, "GDROM: MISC_SETVECTOR not implemented");
			break;

		default:
			WARN_LOG(REIOS, "GDROM: Unknown MISC command %x", r[7]);
			break;
		}
	}
}
