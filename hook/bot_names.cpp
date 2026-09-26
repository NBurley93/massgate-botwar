// Names bots after callsigns from the ghost ladder's pool (share/ghosts/callsigns.txt), so the
// players you fight are the ones on the ladder. wic_ds.exe 1.0.1.1 only.
//
// EX_AIPlayerContainer::CreatePlayer (0x68C8A0) names a bot after its AI definition's myUIName
// ("Infantry Aggressive" and the like) and passes that to the player info setup (0x45C110). The
// hook lets that call run, then renames the bot before CreatePlayer announces the new player.
#include "hook.h"

#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

namespace
{
	const uintptr_t	CREATE_PLAYER_SETUP_CALL = 0x0068CA99;	// in EX_AIPlayerContainer::CreatePlayer
	const uintptr_t	PLAYER_INFO_SETUP = 0x0045C110;			// EXCO_PlayerInfo setup, __thiscall, 8 arguments
	const uintptr_t	WIDE_STRING_ASSIGN = 0x00403800;		// MC_Str<wchar_t>::operator=(const wchar_t*)

	// EXCO_PlayerInfo (the type argument ends up in myType at +0x4)
	const size_t	PLAYER_INFO_NAME = 0x238;
	const uint32_t	PLAYER_TYPE_AI = 1;

	// Same rules as the ghost ladder (MMS_GhostLadder.cpp); the game shows up to 24 characters.
	const size_t	MIN_CALLSIGN_LENGTH = 3;
	const size_t	MAX_CALLSIGN_LENGTH = 22;

	typedef void (__thiscall* PlayerInfoSetupFunc)(void* aPlayerInfo, uint32_t aTeam, uint32_t aName, uint32_t anAIConfigFile,
		uint32_t aType, uint32_t aReadyFlag, uint32_t aRole, uint32_t aVoipId, uint32_t anAdminFlag);
	typedef void (__thiscall* WideStringAssignFunc)(void* aString, const wchar_t* aValue);

	std::vector<std::wstring>		ourCallsigns;
	// Which callsign each player slot's bot has, so bots in one match never share a name.
	std::map<const void*, size_t>	ourCallsignBySlot;
	uint32_t						ourRandomState;

	uint32_t NextRandom()
	{
		// xorshift32
		ourRandomState ^= ourRandomState << 13;
		ourRandomState ^= ourRandomState >> 17;
		ourRandomState ^= ourRandomState << 5;
		return ourRandomState;
	}

	bool IsValidCallsign(const char* aCallsign)
	{
		const size_t length = strlen(aCallsign);
		if (length < MIN_CALLSIGN_LENGTH || length > MAX_CALLSIGN_LENGTH)
			return false;
		for (const char* c = aCallsign; *c; c++)
		{
			if (*c < 32 || *c > 126 || *c == '|' || *c == '\\')
				return false;
		}
		return !strstr(aCallsign, "CLAN") && !strstr(aCallsign, "PLAYER") && !strstr(aCallsign, "PROFILE");
	}

	bool ReadCallsigns(const char* aPath)
	{
		FILE* file = NULL;
		if (fopen_s(&file, aPath, "rt") != 0 || !file)
			return false;

		char line[256];
		while (fgets(line, sizeof(line), file))
		{
			char* start = line;
			while (*start == ' ' || *start == '\t')
				start++;
			char* end = start + strlen(start);
			while (end > start && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
				*--end = 0;
			if (!*start || *start == '#' || !IsValidCallsign(start))
				continue;

			std::wstring callsign(start, end);
			bool duplicate = false;
			for (size_t i = 0; i < ourCallsigns.size() && !duplicate; i++)
				duplicate = _wcsicmp(ourCallsigns[i].c_str(), callsign.c_str()) == 0;
			if (!duplicate)
				ourCallsigns.push_back(callsign);
		}
		fclose(file);
		return true;
	}

	size_t PickCallsign(const void* aSlot)
	{
		std::vector<bool> taken(ourCallsigns.size(), false);
		for (const auto& assignment : ourCallsignBySlot)
		{
			if (assignment.first != aSlot)
				taken[assignment.second] = true;
		}
		std::vector<size_t> available;
		for (size_t i = 0; i < ourCallsigns.size(); i++)
		{
			if (!taken[i])
				available.push_back(i);
		}
		// With a pool smaller than the number of bots, names repeat rather than run out.
		return available.empty() ? NextRandom() % ourCallsigns.size() : available[NextRandom() % available.size()];
	}

	// Called in place of the player info setup; __fastcall matches __thiscall here (this in ecx,
	// edx unused, callee pops the stack arguments).
	void __fastcall SetupPlayerInfo(uint8_t* aPlayerInfo, void* /*edx*/, uint32_t aTeam, uint32_t aName, uint32_t anAIConfigFile,
		uint32_t aType, uint32_t aReadyFlag, uint32_t aRole, uint32_t aVoipId, uint32_t anAdminFlag)
	{
		reinterpret_cast<PlayerInfoSetupFunc>(PLAYER_INFO_SETUP)(aPlayerInfo, aTeam, aName, anAIConfigFile, aType, aReadyFlag, aRole, aVoipId, anAdminFlag);
		if (aType != PLAYER_TYPE_AI)
			return;

		const size_t index = PickCallsign(aPlayerInfo);
		ourCallsignBySlot[aPlayerInfo] = index;
		const wchar_t* aiName = *reinterpret_cast<const wchar_t* const*>(aPlayerInfo + PLAYER_INFO_NAME);
		HookLog("Bot on team %u (%ls) is %ls.", aTeam, aiName ? aiName : L"", ourCallsigns[index].c_str());
		reinterpret_cast<WideStringAssignFunc>(WIDE_STRING_ASSIGN)(aPlayerInfo + PLAYER_INFO_NAME, ourCallsigns[index].c_str());
	}
}

bool InstallBotNames(const char* anIniPath)
{
	char path[MAX_PATH];
	GetPrivateProfileStringA("bots", "callsigns", "", path, sizeof(path), anIniPath);
	if (!path[0])
	{
		HookLog("Bots keep their AI names: no [bots] callsigns= in botwar_hook.ini.");
		return true;
	}
	if (!ReadCallsigns(path) || ourCallsigns.empty())
	{
		HookLog("Bots keep their AI names: no usable callsigns in %s.", path);
		return false;
	}

	ourRandomState = GetTickCount() ^ (GetCurrentProcessId() << 16) ^ 0x9E3779B9u;
	if (!ourRandomState)
		ourRandomState = 1;

	if (!RedirectCall(CREATE_PLAYER_SETUP_CALL, PLAYER_INFO_SETUP, &SetupPlayerInfo, "bot names"))
		return false;
	HookLog("Bots are named from %u callsigns in %s.", static_cast<unsigned int>(ourCallsigns.size()), path);
	return true;
}
