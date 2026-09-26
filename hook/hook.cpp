#include "hook.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace
{
	char ourLogPath[MAX_PATH];

	bool WriteMemory(uintptr_t anAddress, const uint8_t* someData, size_t aSize)
	{
		DWORD oldProtect;
		if (!VirtualProtect(reinterpret_cast<void*>(anAddress), aSize, PAGE_EXECUTE_READWRITE, &oldProtect))
			return false;
		memcpy(reinterpret_cast<void*>(anAddress), someData, aSize);
		VirtualProtect(reinterpret_cast<void*>(anAddress), aSize, oldProtect, &oldProtect);
		FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(anAddress), aSize);
		return true;
	}

	void FormatBytes(char* aBuffer, size_t aBufferSize, const uint8_t* someBytes, size_t aCount)
	{
		aBuffer[0] = 0;
		for (size_t i = 0; i < aCount && (i + 1) * 3 < aBufferSize; i++)
			sprintf_s(aBuffer + i * 3, aBufferSize - i * 3, "%02X ", someBytes[i]);
	}
}

void HookLog(const char* aFormat, ...)
{
	if (!ourLogPath[0])
		return;
	FILE* file = NULL;
	if (fopen_s(&file, ourLogPath, "a") != 0 || !file)
		return;

	SYSTEMTIME now;
	GetLocalTime(&now);
	fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
	va_list args;
	va_start(args, aFormat);
	vfprintf(file, aFormat, args);
	va_end(args);
	fputc('\n', file);
	fclose(file);
}

bool PatchBytes(uintptr_t anAddress, std::initializer_list<uint8_t> anOriginal, std::initializer_list<uint8_t> aReplacement, const char* aName)
{
	const size_t size = anOriginal.size();
	if (aReplacement.size() != size)
	{
		HookLog("%s: original and replacement differ in length.", aName);
		return false;
	}

	const uint8_t* current = reinterpret_cast<const uint8_t*>(anAddress);
	if (memcmp(current, aReplacement.begin(), size) == 0)
		return true;
	if (memcmp(current, anOriginal.begin(), size) != 0)
	{
		char found[64];
		FormatBytes(found, sizeof(found), current, size);
		HookLog("%s: unexpected bytes at 0x%08X (%s), not patched.", aName, static_cast<unsigned int>(anAddress), found);
		return false;
	}
	if (!WriteMemory(anAddress, aReplacement.begin(), size))
	{
		HookLog("%s: could not write to 0x%08X.", aName, static_cast<unsigned int>(anAddress));
		return false;
	}
	return true;
}

bool ReplaceFunction(uintptr_t anAddress, const void* aTarget, std::initializer_list<uint8_t> aPrologue, const char* aName)
{
	const uint8_t* current = reinterpret_cast<const uint8_t*>(anAddress);
	if (memcmp(current, aPrologue.begin(), aPrologue.size()) != 0)
	{
		char found[64];
		FormatBytes(found, sizeof(found), current, aPrologue.size());
		HookLog("%s: unexpected prologue at 0x%08X (%s), not replaced.", aName, static_cast<unsigned int>(anAddress), found);
		return false;
	}

	uint8_t jump[5] = { 0xE9 };
	const int32_t offset = static_cast<int32_t>(reinterpret_cast<uintptr_t>(aTarget) - (anAddress + sizeof(jump)));
	memcpy(jump + 1, &offset, sizeof(offset));
	if (!WriteMemory(anAddress, jump, sizeof(jump)))
	{
		HookLog("%s: could not write to 0x%08X.", aName, static_cast<unsigned int>(anAddress));
		return false;
	}
	return true;
}

void* HookImport(HMODULE aModule, const char* aDll, const char* anExport, const void* aTarget)
{
	// Imports are matched by their resolved address, so it does not matter whether the module
	// imports them by name or by ordinal. The loader has bound the executable's imports before it
	// runs any DllMain.
	HMODULE dll = GetModuleHandleA(aDll);
	void* original = dll ? reinterpret_cast<void*>(GetProcAddress(dll, anExport)) : NULL;
	if (!original)
		return NULL;

	uint8_t* base = reinterpret_cast<uint8_t*>(aModule);
	const IMAGE_NT_HEADERS* headers = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
	const IMAGE_DATA_DIRECTORY& directory = headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!directory.VirtualAddress)
		return NULL;

	for (const IMAGE_IMPORT_DESCRIPTOR* import = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); import->Name; import++)
	{
		if (_stricmp(reinterpret_cast<const char*>(base + import->Name), aDll) != 0)
			continue;
		for (IMAGE_THUNK_DATA* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + import->FirstThunk); thunk->u1.Function; thunk++)
		{
			if (reinterpret_cast<void*>(thunk->u1.Function) != original)
				continue;
			const uintptr_t target = reinterpret_cast<uintptr_t>(aTarget);
			if (!WriteMemory(reinterpret_cast<uintptr_t>(&thunk->u1.Function), reinterpret_cast<const uint8_t*>(&target), sizeof(target)))
				return NULL;
			return original;
		}
	}
	return NULL;
}

BOOL APIENTRY DllMain(HMODULE aModule, DWORD aReason, LPVOID aReserved)
{
	if (aReason != DLL_PROCESS_ATTACH)
		return TRUE;
	DisableThreadLibraryCalls(aModule);

	char exePath[MAX_PATH];
	const DWORD length = GetModuleFileNameA(NULL, exePath, MAX_PATH);
	if (!length || length >= MAX_PATH)
		return TRUE;
	char* exeName = strrchr(exePath, '\\');
	if (!exeName)
		return TRUE;
	*exeName++ = 0;

	// Other executables that load dbghelp.dll from the game folder (the game itself, the modkit)
	// only get the forwarding exports.
	if (_stricmp(exeName, "wic_ds.exe") != 0)
		return TRUE;

	sprintf_s(ourLogPath, "%s\\botwar_hook.log", exePath);
	DeleteFileA(ourLogPath);
	HookLog("botwar hook loaded into %s\\%s.", exePath, exeName);

	if (!PatchDedicatedServer(exePath))
	{
		// Failing closed: without the redirect the server would talk to whoever answers for the
		// public *.massgate.net names.
		HookLog("Stopping the server: the Massgate redirect could not be installed.");
		MessageBoxA(NULL, "The botwar hook could not redirect Massgate to the local server, so the dedicated server will not start.\n\nSee botwar_hook.log in the game folder.", "botwar hook", MB_ICONERROR);
		// Not ExitProcess: that would run other DLLs' detach code under the loader lock.
		TerminateProcess(GetCurrentProcess(), 1);
	}
	return TRUE;
}
