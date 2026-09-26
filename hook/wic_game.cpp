// Patches for wic.exe 1.0.1.1 (the Steam build). Its code is encrypted by SteamStub until the
// stub runs at the entry point, after this DLL's DllMain, so only data (the import table and
// .rdata) can be checked or changed here.
#include "hook.h"

#include <string.h>
#include <map>
#include <string>

namespace
{
	const uintptr_t		VERSION_STRING_ADDRESS = 0x00CF41A0;	// .rdata
	const char			VERSION_STRING[] = "World in Conflict v1.0.1.1 (b35)";

	// ---------------------------------------------------------------------------------------------
	// CD key product check
	//
	// The game accepts a World in Conflict key (product 1 or 2) only without Soviet Assault
	// installed (assault.dat) and a Soviet Assault key (3 or 4) only with it, in three places:
	// - entering multiplayer (0x875A62), EXMASS_Client logs in only if 0x8779D0 finds no mismatch
	//   for its key, and shows the key screen otherwise;
	// - the key screen (WICMASS_EnterCdKeyScreenHandler, 0x841EA0) takes the stored key and skips
	//   itself unless 0x8417B0 reports the mismatch;
	// - WIC_ValidateCdKeyTask (0xBB3790) runs the validator 0x79BD90, which rejects the mismatch.
	// So a Soviet Assault install with a plain key asks for the key every time multiplayer is
	// entered. Patching only the screen loops: it hands the key back to the client, whose check
	// shows the screen again. Accept either product in all three; the key must still be well-formed, and Massgate checks it
	// when logging in.
	//
	// The code is only decrypted once SteamStub has run, so the patches are applied from the game's
	// first GetCommandLineA call (from its C runtime startup) whose code bytes match.

	typedef LPSTR (WINAPI* GetCommandLineAFunc)();
	GetCommandLineAFunc		ourGetCommandLineA;
	volatile LONG			ourCdKeyCheckPatched;

	const uintptr_t			CDKEY_CLIENT_PRODUCT_MISMATCH = 0x008779D0;	// returns whether the key's product does not match
	const uintptr_t			CDKEY_SCREEN_PRODUCT_MISMATCH = 0x008417B0;	// the same, for the key screen
	const uintptr_t			CDKEY_PLAIN_PRODUCT_BRANCH = 0x0079BDF6;	// je valid, when no assault.dat
	const uintptr_t			CDKEY_ASSAULT_PRODUCT_BRANCH = 0x0079BE0B;	// je invalid, when no assault.dat

	bool IsDecrypted()
	{
		static const uint8_t clientMismatchStart[] = { 0x83, 0xEC, 0x0C };			// sub esp,0Ch
		static const uint8_t mismatchStart[] = { 0x8A, 0x4E, 0x10 };				// mov cl,[esi+10h]
		static const uint8_t plainBranch[] = { 0x74, 0x19 };
		static const uint8_t assaultBranch[] = { 0x0F, 0x84, 0xE6, 0x00, 0x00, 0x00 };
		return memcmp(reinterpret_cast<const void*>(CDKEY_CLIENT_PRODUCT_MISMATCH), clientMismatchStart, sizeof(clientMismatchStart)) == 0
			&& memcmp(reinterpret_cast<const void*>(CDKEY_SCREEN_PRODUCT_MISMATCH), mismatchStart, sizeof(mismatchStart)) == 0
			&& memcmp(reinterpret_cast<const void*>(CDKEY_PLAIN_PRODUCT_BRANCH), plainBranch, sizeof(plainBranch)) == 0
			&& memcmp(reinterpret_cast<const void*>(CDKEY_ASSAULT_PRODUCT_BRANCH), assaultBranch, sizeof(assaultBranch)) == 0;
	}

	void PatchCdKeyCheckOnceDecrypted()
	{
		// Still encrypted: try again on the next call.
		if (!IsDecrypted() || InterlockedExchange(&ourCdKeyCheckPatched, 1))
			return;
		// xor al,al; ret
		const bool client = PatchBytes(CDKEY_CLIENT_PRODUCT_MISMATCH, { 0x83, 0xEC, 0x0C }, { 0x32, 0xC0, 0xC3 }, "CD key product check (Massgate client)");
		const bool screen = PatchBytes(CDKEY_SCREEN_PRODUCT_MISMATCH, { 0x8A, 0x4E, 0x10 }, { 0x32, 0xC0, 0xC3 }, "CD key product check (key screen)");
		const bool plain = PatchBytes(CDKEY_PLAIN_PRODUCT_BRANCH, { 0x74, 0x19 }, { 0xEB, 0x19 }, "CD key product check (plain)");
		const bool assault = PatchBytes(CDKEY_ASSAULT_PRODUCT_BRANCH, { 0x0F, 0x84, 0xE6, 0x00, 0x00, 0x00 }, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 }, "CD key product check (Soviet Assault)");
		if (client && screen && plain && assault)
			HookLog("CD keys of either product are accepted.");
		else
			HookLog("Could not patch the CD key product check.");
	}

	LPSTR WINAPI GetCommandLineAfterDecryption()
	{
		if (!ourCdKeyCheckPatched)
			PatchCdKeyCheckOnceDecrypted();
		return ourGetCommandLineA();
	}

	// ---------------------------------------------------------------------------------------------
	// Registry trace ([debug] registry=1): logs the game's registry use under the Massive
	// Entertainment key, with value names, types, sizes and results but never the data (it holds
	// the CD key).

	CRITICAL_SECTION					ourKeyLock;
	std::map<HKEY, std::string>			ourKeyPaths;

	typedef LSTATUS (APIENTRY* RegOpenKeyExAFunc)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
	typedef LSTATUS (APIENTRY* RegCreateKeyExAFunc)(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM, const LPSECURITY_ATTRIBUTES, PHKEY, LPDWORD);
	typedef LSTATUS (APIENTRY* RegCreateKeyAFunc)(HKEY, LPCSTR, PHKEY);
	typedef LSTATUS (APIENTRY* RegQueryValueExAFunc)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
	typedef LSTATUS (APIENTRY* RegSetValueExAFunc)(HKEY, LPCSTR, DWORD, DWORD, const BYTE*, DWORD);
	typedef LSTATUS (APIENTRY* RegCloseKeyFunc)(HKEY);

	RegOpenKeyExAFunc		ourRegOpenKeyExA;
	RegCreateKeyExAFunc		ourRegCreateKeyExA;
	RegCreateKeyAFunc		ourRegCreateKeyA;
	RegQueryValueExAFunc	ourRegQueryValueExA;
	RegSetValueExAFunc		ourRegSetValueExA;
	RegCloseKeyFunc			ourRegCloseKey;

	std::string KeyPath(HKEY aKey)
	{
		if (aKey == HKEY_LOCAL_MACHINE)
			return "HKLM";
		if (aKey == HKEY_CURRENT_USER)
			return "HKCU";
		if (aKey == HKEY_CLASSES_ROOT)
			return "HKCR";
		if (aKey == HKEY_USERS)
			return "HKU";
		EnterCriticalSection(&ourKeyLock);
		const auto found = ourKeyPaths.find(aKey);
		const std::string path = found != ourKeyPaths.end() ? found->second : "?";
		LeaveCriticalSection(&ourKeyLock);
		return path;
	}

	bool IsTraced(const std::string& aPath)
	{
		std::string lower(aPath);
		for (char& c : lower)
			c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		return lower.find("massive") != std::string::npos;
	}

	void RememberKey(HKEY aParent, const char* aSubKey, HKEY aKey, const char* anOperation, LSTATUS aResult)
	{
		const std::string path = KeyPath(aParent) + (aSubKey && *aSubKey ? std::string("\\") + aSubKey : std::string());
		if (aResult == ERROR_SUCCESS)
		{
			EnterCriticalSection(&ourKeyLock);
			ourKeyPaths[aKey] = path;
			LeaveCriticalSection(&ourKeyLock);
		}
		if (IsTraced(path))
			HookLog("registry: %s %s -> %ld", anOperation, path.c_str(), aResult);
	}

	LSTATUS APIENTRY TracedRegOpenKeyExA(HKEY aKey, LPCSTR aSubKey, DWORD anOptions, REGSAM aSam, PHKEY aResult)
	{
		const LSTATUS result = ourRegOpenKeyExA(aKey, aSubKey, anOptions, aSam, aResult);
		RememberKey(aKey, aSubKey, result == ERROR_SUCCESS ? *aResult : NULL, (aSam & (KEY_SET_VALUE | KEY_CREATE_SUB_KEY)) ? "open (write)" : "open (read)", result);
		return result;
	}

	LSTATUS APIENTRY TracedRegCreateKeyExA(HKEY aKey, LPCSTR aSubKey, DWORD aReserved, LPSTR aClass, DWORD anOptions, REGSAM aSam, const LPSECURITY_ATTRIBUTES anAttributes, PHKEY aResult, LPDWORD aDisposition)
	{
		const LSTATUS result = ourRegCreateKeyExA(aKey, aSubKey, aReserved, aClass, anOptions, aSam, anAttributes, aResult, aDisposition);
		RememberKey(aKey, aSubKey, result == ERROR_SUCCESS ? *aResult : NULL, "create", result);
		return result;
	}

	LSTATUS APIENTRY TracedRegCreateKeyA(HKEY aKey, LPCSTR aSubKey, PHKEY aResult)
	{
		const LSTATUS result = ourRegCreateKeyA(aKey, aSubKey, aResult);
		RememberKey(aKey, aSubKey, result == ERROR_SUCCESS ? *aResult : NULL, "create", result);
		return result;
	}

	LSTATUS APIENTRY TracedRegQueryValueExA(HKEY aKey, LPCSTR aValueName, LPDWORD aReserved, LPDWORD aType, LPBYTE someData, LPDWORD aDataSize)
	{
		const LSTATUS result = ourRegQueryValueExA(aKey, aValueName, aReserved, aType, someData, aDataSize);
		const std::string path = KeyPath(aKey);
		if (IsTraced(path))
			HookLog("registry: query %s : %s -> %ld (type %lu, %lu bytes)", path.c_str(), aValueName ? aValueName : "(default)", result,
				aType ? *aType : 0, aDataSize ? *aDataSize : 0);
		return result;
	}

	LSTATUS APIENTRY TracedRegSetValueExA(HKEY aKey, LPCSTR aValueName, DWORD aReserved, DWORD aType, const BYTE* someData, DWORD aDataSize)
	{
		const LSTATUS result = ourRegSetValueExA(aKey, aValueName, aReserved, aType, someData, aDataSize);
		const std::string path = KeyPath(aKey);
		if (IsTraced(path))
			HookLog("registry: set %s : %s -> %ld (type %lu, %lu bytes)", path.c_str(), aValueName ? aValueName : "(default)", result, aType, aDataSize);
		return result;
	}

	LSTATUS APIENTRY TracedRegCloseKey(HKEY aKey)
	{
		EnterCriticalSection(&ourKeyLock);
		ourKeyPaths.erase(aKey);
		LeaveCriticalSection(&ourKeyLock);
		return ourRegCloseKey(aKey);
	}

	template <typename Func>
	bool HookRegistryImport(const char* anExport, Func aTarget, Func& anOriginal)
	{
		anOriginal = reinterpret_cast<Func>(HookImport(GetModuleHandleA(NULL), "ADVAPI32.dll", anExport, reinterpret_cast<const void*>(aTarget)));
		if (!anOriginal)
			HookLog("registry trace: could not hook %s.", anExport);
		return anOriginal != NULL;
	}

	void InstallRegistryTrace()
	{
		InitializeCriticalSection(&ourKeyLock);
		// Close first, so no key is forgotten while the others are being hooked.
		HookRegistryImport("RegCloseKey", &TracedRegCloseKey, ourRegCloseKey);
		HookRegistryImport("RegOpenKeyExA", &TracedRegOpenKeyExA, ourRegOpenKeyExA);
		HookRegistryImport("RegCreateKeyExA", &TracedRegCreateKeyExA, ourRegCreateKeyExA);
		HookRegistryImport("RegCreateKeyA", &TracedRegCreateKeyA, ourRegCreateKeyA);
		HookRegistryImport("RegQueryValueExA", &TracedRegQueryValueExA, ourRegQueryValueExA);
		HookRegistryImport("RegSetValueExA", &TracedRegSetValueExA, ourRegSetValueExA);
		HookLog("Registry trace on (values under the Massive Entertainment key, no data).");
	}
}

bool PatchGame(const char* anExeDirectory)
{
	char iniPath[MAX_PATH];
	strcpy_s(iniPath, anExeDirectory);
	strcat_s(iniPath, "\\botwar_hook.ini");

	const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleA(NULL));
	const IMAGE_NT_HEADERS* headers = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(dos) + dos->e_lfanew);
	const uintptr_t imageStart = reinterpret_cast<uintptr_t>(dos);
	const uintptr_t imageEnd = imageStart + headers->OptionalHeader.SizeOfImage;
	const bool knownBuild = imageStart == 0x00400000 && VERSION_STRING_ADDRESS + sizeof(VERSION_STRING) <= imageEnd
		&& memcmp(reinterpret_cast<const void*>(VERSION_STRING_ADDRESS), VERSION_STRING, sizeof(VERSION_STRING) - 1) == 0;
	if (knownBuild)
		HookLog("Game build 1.0.1.1 (b35).");
	else
		HookLog("Unknown game build; only the Massgate redirect is active.");

	if (!InstallMassgateRedirect(iniPath))
		return false;

	if (knownBuild)
	{
		ourGetCommandLineA = reinterpret_cast<GetCommandLineAFunc>(HookImport(GetModuleHandleA(NULL), "KERNEL32.dll", "GetCommandLineA", &GetCommandLineAfterDecryption));
		if (!ourGetCommandLineA)
			HookLog("Could not hook GetCommandLineA; the CD key product check stays.");
	}

	if (GetPrivateProfileIntA("debug", "registry", 0, iniPath))
		InstallRegistryTrace();
	return true;
}
