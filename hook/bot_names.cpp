// Makes bots ghosts from the ghost ladder, so the players you fight are the ones on the ladder.
// wic_ds.exe 1.0.1.1 only.
//
// With the roster Massgate writes (profile id and callsign per ghost), a bot gets a ghost's name
// and profile id; the server then reports the bot's match for that ghost. With only a callsign
// file, bots just get the names and still report as profile 0 (which Massgate ignores).
//
// EX_AIPlayerContainer::CreatePlayer (0x68C8A0) names a bot after its AI definition's myUIName
// ("Infantry Aggressive" and the like) and passes that to the player info setup (0x45C110). The
// hook lets that call run, then renames the bot before CreatePlayer announces the new player.
#include "hook.h"

#include <stdio.h>
#include <stdlib.h>
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
	const size_t	PLAYER_INFO_PROFILE_ID = 0x8;		// myMassgateProfileId
	const size_t	PLAYER_INFO_NAME = 0x238;
	const uint32_t	PLAYER_TYPE_AI = 1;

	// Same rules as the ghost ladder (MMS_GhostLadder.cpp); the game shows up to 24 characters.
	const size_t	MIN_CALLSIGN_LENGTH = 3;
	const size_t	MAX_CALLSIGN_LENGTH = 22;

	typedef void (__thiscall* PlayerInfoSetupFunc)(void* aPlayerInfo, uint32_t aTeam, uint32_t aName, uint32_t anAIConfigFile,
		uint32_t aType, uint32_t aReadyFlag, uint32_t aRole, uint32_t aVoipId, uint32_t anAdminFlag);
	typedef void (__thiscall* WideStringAssignFunc)(void* aString, const wchar_t* aValue);

	struct Ghost
	{
		std::wstring	myCallsign;
		uint32_t		myProfileId;	// 0 when names come from a callsign file
	};

	std::vector<Ghost>				ourGhosts;
	// Which ghost each player slot's bot is, so bots in one match never share one.
	std::map<const void*, size_t>	ourGhostBySlot;
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

	void AddGhost(const char* aCallsign, const char* anEnd, uint32_t aProfileId)
	{
		std::wstring callsign(aCallsign, anEnd);
		for (const Ghost& ghost : ourGhosts)
		{
			if (_wcsicmp(ghost.myCallsign.c_str(), callsign.c_str()) == 0)
				return;
		}
		ourGhosts.push_back({ callsign, aProfileId });
	}

	// "<profileId>\t<callsign>" per line, written by Massgate (MMS_GhostLadder::PrivWriteRoster).
	bool ReadRoster(const char* aPath)
	{
		FILE* file = NULL;
		if (fopen_s(&file, aPath, "rt") != 0 || !file)
			return false;

		char line[256];
		while (fgets(line, sizeof(line), file))
		{
			char* callsign = strchr(line, '\t');
			if (!callsign)
				continue;
			*callsign++ = 0;
			char* end = callsign + strlen(callsign);
			while (end > callsign && (end[-1] == '\n' || end[-1] == '\r'))
				*--end = 0;
			const uint32_t profileId = strtoul(line, NULL, 10);
			if (profileId && IsValidCallsign(callsign))
				AddGhost(callsign, end, profileId);
		}
		fclose(file);
		return true;
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
			if (*start && *start != '#' && IsValidCallsign(start))
				AddGhost(start, end, 0);
		}
		fclose(file);
		return true;
	}

	size_t PickGhost(const void* aSlot)
	{
		std::vector<bool> taken(ourGhosts.size(), false);
		for (const auto& assignment : ourGhostBySlot)
		{
			if (assignment.first != aSlot)
				taken[assignment.second] = true;
		}
		std::vector<size_t> available;
		for (size_t i = 0; i < ourGhosts.size(); i++)
		{
			if (!taken[i])
				available.push_back(i);
		}
		// With a pool smaller than the number of bots, names repeat rather than run out.
		return available.empty() ? NextRandom() % ourGhosts.size() : available[NextRandom() % available.size()];
	}

	// Called in place of the player info setup; __fastcall matches __thiscall here (this in ecx,
	// edx unused, callee pops the stack arguments).
	void __fastcall SetupPlayerInfo(uint8_t* aPlayerInfo, void* /*edx*/, uint32_t aTeam, uint32_t aName, uint32_t anAIConfigFile,
		uint32_t aType, uint32_t aReadyFlag, uint32_t aRole, uint32_t aVoipId, uint32_t anAdminFlag)
	{
		reinterpret_cast<PlayerInfoSetupFunc>(PLAYER_INFO_SETUP)(aPlayerInfo, aTeam, aName, anAIConfigFile, aType, aReadyFlag, aRole, aVoipId, anAdminFlag);
		if (aType != PLAYER_TYPE_AI)
			return;

		const size_t index = PickGhost(aPlayerInfo);
		ourGhostBySlot[aPlayerInfo] = index;
		const Ghost& ghost = ourGhosts[index];
		const wchar_t* aiName = *reinterpret_cast<const wchar_t* const*>(aPlayerInfo + PLAYER_INFO_NAME);
		HookLog("Bot on team %u (%ls) is %ls (profile %u).", aTeam, aiName ? aiName : L"", ghost.myCallsign.c_str(), ghost.myProfileId);
		reinterpret_cast<WideStringAssignFunc>(WIDE_STRING_ASSIGN)(aPlayerInfo + PLAYER_INFO_NAME, ghost.myCallsign.c_str());
		*reinterpret_cast<uint32_t*>(aPlayerInfo + PLAYER_INFO_PROFILE_ID) = ghost.myProfileId;
	}
}

bool InstallBotNames(const char* anIniPath)
{
	// The roster (bots become ghosts) wins over the callsign file (bots only get the names).
	char path[MAX_PATH];
	GetPrivateProfileStringA("bots", "roster", "", path, sizeof(path), anIniPath);
	const bool haveRoster = path[0] && ReadRoster(path) && !ourGhosts.empty();
	if (path[0] && !haveRoster)
		HookLog("No usable ghost roster in %s (is Massgate running with ghosts?).", path);
	if (!haveRoster)
	{
		GetPrivateProfileStringA("bots", "callsigns", "", path, sizeof(path), anIniPath);
		if (!path[0])
		{
			HookLog("Bots keep their AI names: no [bots] roster= or callsigns= in botwar_hook.ini.");
			return true;
		}
		if (!ReadCallsigns(path) || ourGhosts.empty())
		{
			HookLog("Bots keep their AI names: no usable callsigns in %s.", path);
			return false;
		}
	}

	ourRandomState = GetTickCount() ^ (GetCurrentProcessId() << 16) ^ 0x9E3779B9u;
	if (!ourRandomState)
		ourRandomState = 1;

	if (!RedirectCall(CREATE_PLAYER_SETUP_CALL, PLAYER_INFO_SETUP, &SetupPlayerInfo, "bot names"))
		return false;
	if (haveRoster)
		HookLog("Bots are ghosts: %u from %s.", static_cast<unsigned int>(ourGhosts.size()), path);
	else
		HookLog("Bots are named from %u callsigns in %s.", static_cast<unsigned int>(ourGhosts.size()), path);
	return true;
}
