// botwar hook: a dbghelp.dll proxy that patches World in Conflict 1.0.1.1 in memory.
// Parts are ported from wic-client by Nukem9 and Tenerefis (LGPL-3.0, see LICENSE).
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sal.h>
#include <stddef.h>
#include <stdint.h>
#include <initializer_list>

// Appends a line to botwar_hook.log next to the executable.
void HookLog(_Printf_format_string_ const char* aFormat, ...);

// Replaces the bytes at anAddress if they are anOriginal. Succeeds without writing if they already
// are aReplacement; any other contents are logged and left alone.
bool PatchBytes(uintptr_t anAddress, std::initializer_list<uint8_t> anOriginal, std::initializer_list<uint8_t> aReplacement, const char* aName);

// Replaces the whole function at anAddress with aTarget (a 5-byte jmp) if it starts with aPrologue.
bool ReplaceFunction(uintptr_t anAddress, const void* aTarget, std::initializer_list<uint8_t> aPrologue, const char* aName);

// Points the call instruction at anAddress (E8 rel32) at aTarget if it currently calls anOriginalTarget.
bool RedirectCall(uintptr_t anAddress, uintptr_t anOriginalTarget, const void* aTarget, const char* aName);

// Points aModule's import of anExport (from aDll) at aTarget. Returns the address it pointed at
// before, or NULL if the import was not found.
void* HookImport(HMODULE aModule, const char* aDll, const char* anExport, const void* aTarget);

// Resolves the Massgate host names to [massgate] host= from anIniPath (default 127.0.0.1).
bool InstallMassgateRedirect(const char* anIniPath);

// Runs in DllMain when the process is wic.exe. Returns false if the Massgate redirect failed.
bool PatchGame(const char* anExeDirectory);

// Runs in DllMain when the process is wic_ds.exe. Returns false if a patch that keeps the server
// away from the public Massgate DNS names failed.
bool PatchDedicatedServer(const char* anExeDirectory);

// Names the server's bots after callsigns from the file given in botwar_hook.ini. Only for the
// 1.0.1.1 build.
bool InstallBotNames(const char* anIniPath);
