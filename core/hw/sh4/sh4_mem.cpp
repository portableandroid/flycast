/*
	Mostly buggy, old, glue code that somehow still works
	Most of the work is now delegated on vtlb and only helpers are here
*/
#include "types.h"

#include "sh4_mem.h"
#include "hw/holly/sb_mem.h"
#include "sh4_mmr.h"
#include "hw/pvr/elan.h"
#include "hw/pvr/pvr_mem.h"
#include "hw/mem/addrspace.h"
#include "hw/sh4/modules/mmu.h"

#ifdef STRICT_MODE
#include "sh4_cache.h"
#endif

//main system mem
RamRegion mem_b;

// Memory handlers
ReadMem8Func ReadMem8;
ReadMem16Func ReadMem16;
ReadMem16Func IReadMem16;
ReadMem32Func ReadMem32;
ReadMem64Func ReadMem64;

WriteMem8Func WriteMem8;
WriteMem16Func WriteMem16;
WriteMem32Func WriteMem32;
WriteMem64Func WriteMem64;

//AREA 1
static addrspace::handler area1_32b;

static void map_area1_init()
{
	area1_32b = addrspaceRegisterHandlerTemplate(pvr_read32p, pvr_write32p);
}

static void map_area1(u32 base)
{
	// VRAM
	
	//Lower 32 mb map
	//64b interface
	addrspace::mapBlock(&vram[0], 0x04 | base, 0x04 | base, VRAM_MASK);
	//32b interface
	addrspace::mapHandler(area1_32b, 0x05 | base, 0x05 | base);
	
	//Upper 32 mb mirror
	//0x0600 to 0x07FF
	addrspace::mirrorMapping(0x06 | base, 0x04 | base, 0x02);
}

//AREA 2: Naomi2 elan

//AREA 3
static void map_area3_init()
{
}

static void map_area3(u32 base)
{
	// System RAM
	addrspace::mapBlockMirror(&mem_b[0], 0x0C | base,0x0F | base, RAM_SIZE);
}

//AREA 4
static addrspace::handler area4_handler_lower;
static addrspace::handler area4_handler_upper;

static void map_area4_init()
{
	area4_handler_lower = addrspace::registerHandler(pvr_read_area4<u8, false>, pvr_read_area4<u16, false>, pvr_read_area4<u32, false>,
									pvr_write_area4<u8, false>, pvr_write_area4<u16, false>, pvr_write_area4<u32, false>);
	area4_handler_upper = addrspace::registerHandler(pvr_read_area4<u8, true>, pvr_read_area4<u16, true>, pvr_read_area4<u32, true>,
									pvr_write_area4<u8, true>, pvr_write_area4<u16, true>, pvr_write_area4<u32, true>);
}

static void map_area4(u32 base)
{
	// VRAM 64b/32b interface
	addrspace::mapHandler(area4_handler_lower, 0x11 | base, 0x11 | base);
	// upper mirror
	addrspace::mapHandler(area4_handler_upper, 0x13 | base, 0x13 | base);
}


//AREA 5	--	Ext. Device
//Read Ext.Device
template <class T>
T DYNACALL ReadMem_extdev_T(u32 addr)
{
	INFO_LOG(SH4, "Read ext. device (Area 5) undefined @ %08x", addr);
	return (T)0;
}

//Write Ext.Device
template <class T>
void DYNACALL WriteMem_extdev_T(u32 addr,T data)
{
	INFO_LOG(SH4, "Write ext. device (Area 5) undefined @ %08x: %x", addr, (u32)data);
}

addrspace::handler area5_handler;
static void map_area5_init()
{
	area5_handler = addrspaceRegisterHandlerTemplate(ReadMem_extdev_T, WriteMem_extdev_T);
}

static void map_area5(u32 base)
{
	//map whole region to plugin handler
	addrspace::mapHandler(area5_handler, base | 0x14, base | 0x17);
}

//AREA 6	--	Unassigned 
static void map_area6_init()
{
}
static void map_area6(u32 base)
{
}

//set vmem to default values
void mem_map_default()
{
	addrspace::init();

	//U0/P0
	//0x0xxx xxxx	-> normal memmap
	//0x2xxx xxxx	-> normal memmap
	//0x4xxx xxxx	-> normal memmap
	//0x6xxx xxxx	-> normal memmap
	//-----------
	//P1
	//0x8xxx xxxx	-> normal memmap
	//-----------
	//P2
	//0xAxxx xxxx	-> normal memmap
	//-----------
	//P3
	//0xCxxx xxxx	-> normal memmap
	//-----------
	//P4
	//0xExxx xxxx	-> internal area

	//Init Memmaps (register handlers)
	map_area0_init();
	map_area1_init();
	elan::vmem_init();
	map_area3_init();
	map_area4_init();
	map_area5_init();
	map_area6_init();

	// 00-E0: 8 times the normal memmap mirrors
	for (int i = 0; i < 8; i++)
	{
		map_area0(i << 5); // Bios,Flahsrom,i/f regs,Ext. Device,Sound Ram
		map_area1(i << 5); // VRAM
		elan::vmem_map(i << 5); // Naomi2 Elan
		map_area3(i << 5); // RAM
		map_area4(i << 5); // TA
		map_area5(i << 5); // Ext. Device
		map_area6(i << 5); // Unassigned
	}
	map_area7(); // On Chip RAM

	// E0-FF: P4 region
	map_p4();
}
void mem_Init()
{
	//Allocate mem for memory/bios/flash

	sh4_area0_Init();
	sh4_mmr_init();
}

//Reset Sysmem/Regs -- Pvr is not changed , bios/flash are not zeroed out
void mem_Reset(bool hard)
{
	//mem is reset on hard restart (power on), not soft reset
	if (hard)
		mem_b.zero();

	//Reset registers
	sh4_area0_Reset(hard);
	sh4_mmr_reset(hard);
}

void mem_Term()
{
	sh4_mmr_term();
	sh4_area0_Term();

	addrspace::term();
}

void WriteMemBlock_nommu_dma(u32 dst, u32 src, u32 size)
{
	bool dst_ismem, src_ismem;
	void* dst_ptr = addrspace::writeConst(dst, dst_ismem, 4);
	void* src_ptr = addrspace::readConst(src, src_ismem, 4);

	if (dst_ismem && src_ismem)
	{
		memcpy(dst_ptr, src_ptr, size);
	}
	else if (src_ismem)
	{
		WriteMemBlock_nommu_ptr(dst, (u32*)src_ptr, size);
	}
	else
	{
		verify(size % 4 == 0);
		for (u32 i = 0; i < size; i += 4)
			WriteMem32_nommu(dst + i, ReadMem32_nommu(src + i));
	}
}

void WriteMemBlock_nommu_ptr(u32 dst, const u32 *src, u32 size)
{
	bool dst_ismem;

	void* dst_ptr = addrspace::writeConst(dst, dst_ismem, 4);

	if (dst_ismem)
	{
		memcpy(dst_ptr, src, size);
	}
	else
	{
		for (u32 i = 0; i < size;)
		{
			u32 left = size - i;
			if (left >= 4)
			{
				WriteMem32_nommu(dst + i, src[i >> 2]);
				i += 4;
			}
			else if (left >= 2)
			{
				WriteMem16_nommu(dst + i, ((u16 *)src)[i >> 1]);
				i += 2;
			}
			else
			{
				WriteMem8_nommu(dst + i, ((u8 *)src)[i]);
				i++;
			}
		}
	}
}

void WriteMemBlock_nommu_sq(u32 dst, const SQBuffer *src)
{
	// destination address is 32-byte aligned
	SQBuffer *pdst = (SQBuffer *)GetMemPtr(dst, sizeof(SQBuffer));
	if (pdst != nullptr)
	{
		*pdst = *src;
	}
	else
	{
		for (u32 i = 0; i < sizeof(SQBuffer); i += 4)
			WriteMem32_nommu(dst + i, *(const u32 *)&src->data[i]);
	}
}

//Get pointer to ram area , nullptr if error
//For debugger(gdb) - dynarec
u8* GetMemPtr(u32 addr, u32 size)
{
	if (((addr >> 29) & 7) == 7)
		// P4
		return nullptr;
	if (((addr >> 26) & 7) == 3)
	{
		// Area 3
		if ((addr & RAM_MASK) + size > RAM_SIZE)
			return nullptr;
		else
			return &mem_b[addr & RAM_MASK];
	}
	return nullptr;
}

void SetMemoryHandlers()
{
#ifdef STRICT_MODE
	static bool interpreterRunning;

	if (config::DynarecEnabled && interpreterRunning)
	{
		// Flush caches when interp -> dynarec
		ocache.WriteBackAll();
		icache.Invalidate();
	}

	if (!config::DynarecEnabled)
	{
		interpreterRunning = true;
		IReadMem16 = &IReadCachedMem;
		ReadMem8 = &ReadCachedMem<u8>;
		ReadMem16 = &ReadCachedMem<u16>;
		ReadMem32 = &ReadCachedMem<u32>;
		ReadMem64 = &ReadCachedMem<u64>;

		WriteMem8 = &WriteCachedMem<u8>;
		WriteMem16 = &WriteCachedMem<u16>;
		WriteMem32 = &WriteCachedMem<u32>;
		WriteMem64 = &WriteCachedMem<u64>;

		return;
	}
	interpreterRunning = false;
#endif
	if (mmu_enabled())
	{
		IReadMem16 = &mmu_IReadMem16;
		ReadMem8 = &mmu_ReadMem<u8>;
		ReadMem16 = &mmu_ReadMem<u16>;
		ReadMem32 = &mmu_ReadMem<u32>;
		ReadMem64 = &mmu_ReadMem<u64>;

		WriteMem8 = &mmu_WriteMem<u8>;
		WriteMem16 = &mmu_WriteMem<u16>;
		WriteMem32 = &mmu_WriteMem<u32>;
		WriteMem64 = &mmu_WriteMem<u64>;
	}
	else
	{
		ReadMem8 = &addrspace::read8;
		ReadMem16 = &addrspace::read16;
		IReadMem16 = &addrspace::read16;
		ReadMem32 = &addrspace::read32;
		ReadMem64 = &addrspace::read64;

		WriteMem8 = &addrspace::write8;
		WriteMem16 = &addrspace::write16;
		WriteMem32 = &addrspace::write32;
		WriteMem64 = &addrspace::write64;
	}
}
