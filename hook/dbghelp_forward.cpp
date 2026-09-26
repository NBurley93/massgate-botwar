// Forwards the dbghelp.dll exports to the Windows copy (SysWOW64 for this 32-bit process).
//
// A linker forwarder ("dbghelp.X") would resolve back to this DLL, since it is the module already
// loaded as dbghelp.dll. Each export is instead a stub that pushes its index and jumps to a common
// thunk, which loads the Windows dbghelp.dll on first use (never in DllMain) and jumps to the real
// function with the caller's stack untouched.
#include "hook.h"

#include <stdio.h>

namespace
{
	enum ExportIndex
	{
#define DBGHELP_EXPORT(aName) Index_##aName,
#include "dbghelp_exports.inc"
#undef DBGHELP_EXPORT
		NumExports
	};

	const char* const ourExportNames[] =
	{
#define DBGHELP_EXPORT(aName) #aName,
#include "dbghelp_exports.inc"
#undef DBGHELP_EXPORT
	};

	void* volatile ourExports[NumExports];
}

extern "C" void* __stdcall ResolveDbghelpExport(unsigned int anIndex)
{
	if (void* function = ourExports[anIndex])
		return function;

	char path[MAX_PATH];
	const UINT length = GetSystemDirectoryA(path, MAX_PATH);
	HMODULE dbghelp = NULL;
	// A full path loads the Windows copy even though a module named dbghelp.dll (this one) is loaded.
	if (length && length < MAX_PATH && strcat_s(path, "\\dbghelp.dll") == 0)
		dbghelp = LoadLibraryA(path);
	void* function = dbghelp ? reinterpret_cast<void*>(GetProcAddress(dbghelp, ourExportNames[anIndex])) : NULL;
	if (!function)
	{
		// There is no way to return from a function whose signature is unknown here.
		char message[256];
		sprintf_s(message, "The botwar hook could not find %s in the Windows dbghelp.dll.", ourExportNames[anIndex]);
		MessageBoxA(NULL, message, "botwar hook", MB_ICONERROR);
		TerminateProcess(GetCurrentProcess(), 1);
	}
	ourExports[anIndex] = function;
	return function;
}

extern "C" __declspec(naked) void ForwardToDbghelp()
{
	// On entry: [esp] = export index, [esp+4] = the caller's return address, then its arguments.
	__asm
	{
		push	eax
		push	ecx
		push	edx
		push	dword ptr [esp + 12]
		call	ResolveDbghelpExport
		mov		dword ptr [esp + 12], eax
		pop		edx
		pop		ecx
		pop		eax
		ret
	}
}

#define DBGHELP_EXPORT(aName) \
	extern "C" __declspec(naked) void Forward_##aName() \
	{ \
		__asm push Index_##aName \
		__asm jmp ForwardToDbghelp \
	} \
	__pragma(comment(linker, "/export:" #aName "=_Forward_" #aName))
#include "dbghelp_exports.inc"
#undef DBGHELP_EXPORT
