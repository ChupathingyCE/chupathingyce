/*
XBOX_SUPPORT.CPP

What the Xbox link needs besides the game, the SDK libraries and
xbox_dsound.c.

Direct3D: the game links with an Aug 2001 Direct3D (the state numbering of
the January SDK, which later SDKs changed), built to run inside the kernel.
That build allocates through D3DK::MemAlloc and MemFree, which the kernel
provided; here they come from the pool, as there.

The game: fast_ftol_C is a Halo function missing from the reconstruction
(as in port/linux/src/halo_linker_common.c).

The debug monitor: see below.

This file is compiled with the later SDK's headers, not the game's.
*/

#include <xtl.h>

extern "C" {
PVOID WINAPI ExAllocatePoolWithTag(SIZE_T size, ULONG tag);
VOID WINAPI ExFreePool(PVOID memory);
ULONG __cdecl DbgPrint(PCSTR format, ...);
}

/* ---------- Direct3D */

#define D3D_POOL_TAG 'D3DK'

namespace D3DK {

void *WINAPI MemAllocNoZero(DWORD size)
{
	return ExAllocatePoolWithTag(size, D3D_POOL_TAG);
}

void *WINAPI MemAlloc(DWORD size)
{
	void *memory= ExAllocatePoolWithTag(size, D3D_POOL_TAG);

	if (memory)
	{
		ZeroMemory(memory, size);
	}

	return memory;
}

void WINAPI MemFree(void *memory)
{
	if (memory)
	{
		ExFreePool(memory);
	}
}

}

/* ---------- the game */

extern "C" long fast_ftol_C(float value)
{
	long result;

	__asm
	{
		fld value
		fistp result
	}

	return result;
}

/* ---------- the debug monitor

The game walks the loaded modules' sections through xbdm.dll, which only a
development kit or a debug BIOS loads; a title importing it does not start
on a retail console. The walk finds no modules. */

#define XBDM_ENDOFLIST ((HRESULT)0x82DB0104L)

extern "C" HRESULT WINAPI DmWalkLoadedModules(void **walk, void *module)
{
	(void)walk;
	(void)module;
	return XBDM_ENDOFLIST;
}

extern "C" HRESULT WINAPI DmWalkModuleSections(void **walk, const char *name, void *section)
{
	(void)walk;
	(void)name;
	(void)section;
	return XBDM_ENDOFLIST;
}

extern "C" HRESULT WINAPI DmCloseModuleSections(void *walk)
{
	(void)walk;
	return 0;
}
