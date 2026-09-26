// Massgate
// Copyright (C) 2017 Ubisoft Entertainment
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// A stand-in for wic_ds.exe that uses the same client library (MMG_TrackableServer) to
// register a ranked server with Massgate and report end-of-match player stats.
//
//   FakeDedicatedServer -massgateserver 127.0.0.1 -cdkey <key> -profile <id> [-bots]
//
// With -bots, a profileId 0 entry (what wic_ds reports for bots once its "no bots in
// ranked" check is patched out) is sent ahead of the player's entry in the same message.
// Exits 0 if the server is still connected to Massgate after the report.

#include "stdafx.h"

#include "MC_CommandLine.h"
#include "MC_Debug.h"
#include "MI_Time.h"
#include "MN_WinsockNet.h"
#include "MMG_CdKeyValidator.h"
#include "MMG_ServerVariables.h"
#include "MMG_Stats.h"
#include "MMG_TrackableServer.h"

#include <stdio.h>
#include <stdlib.h>

// MC_Mem: log crashes and exit instead of showing the "BOOOOOM!" dialog (this is a headless test tool).
extern bool ourDisableBoooomBoxFlag;

namespace
{
	class NoCallbacks : public MMG_TrackableServer::Callbacks
	{
	public:
		bool ReserveSlot(const unsigned int, const unsigned int, unsigned int&) { return false; }
		bool PreloadMap(const unsigned __int64, const unsigned int, const bool) { return false; }
		bool LoadTournamentMap(const unsigned __int64, const unsigned int, const unsigned int, unsigned int, unsigned int, MC_LocString) { return false; }
		bool FinalMatchInit(const MMG_MatchChallenge::MatchSetup&) { return false; }
		bool KickClient(unsigned int) { return false; }
		void RemoveMap(unsigned __int64) { }
	};

	// Runs the connection for aMilliseconds, or until aStopWhenRegistered and registration succeeded.
	bool Pump(MMG_TrackableServer* aServer, unsigned int aMilliseconds, bool aStopWhenRegistered)
	{
		const unsigned int start = GetTickCount();
		while (GetTickCount() - start < aMilliseconds)
		{
			if (!aServer->Update())
				return false;
			// Massgate sends the public id once the server is registered.
			if (aStopWhenRegistered && aServer->GetGameInfo().myServerId != 0)
				return true;
			Sleep(20);
		}
		return !aStopWhenRegistered;
	}

	MMG_Stats::PlayerMatchStats MakeStats(unsigned int aProfileId, unsigned short aScore, bool aWon)
	{
		MMG_Stats::PlayerMatchStats stats;
		stats.profileId = aProfileId;
		stats.scoreTotal = aScore;
		stats.scoreAsArmor = aScore;
		stats.scoreByDamagingEnemies = aScore;
		stats.timeTotalMatchLength = 20 * 60;
		stats.timePlayedAsUSA = 20 * 60;
		stats.timePlayedAsArmor = 20 * 60;
		stats.totalTimePlayed = 20.0f * 60.0f;
		stats.numberOfUnitsKilled = aScore / 25;
		stats.matchWon = aWon ? 1 : 0;
		stats.matchLost = aWon ? 0 : 1;
		stats.wasPlayingAtMatchEnd = 1;
		return stats;
	}
}

int main(int argc, char* argv[])
{
	ourDisableBoooomBoxFlag = true;
	MC_CommandLine::Create(NULL);
	MI_Time::Create();
	MC_Debug::Init("LOG_FakeDedicatedServer.txt", "ERR_FakeDedicatedServer.txt", true);
	MN_WinsockNet::Create(2, 2);
	MN_WinsockNet::PostInitialize(); // starts the socket teardown thread that closing a connection needs

	const char* cdKey = NULL;
	int profileId = 0;
	if (!MC_CommandLine::GetInstance()->GetStringValue("cdkey", cdKey) || !cdKey
		|| !MC_CommandLine::GetInstance()->GetIntValue("profile", profileId) || profileId <= 0)
	{
		puts("usage: FakeDedicatedServer -massgateserver <host> -cdkey <key without dashes> -profile <profileId> [-bots]");
		return 2;
	}
	const bool withBots = MC_CommandLine::GetInstance()->IsPresent("bots");

	MMG_CdKey::Validator validator;
	validator.SetKey(cdKey);
	MMG_CdKey::Validator::EncryptionKey encryptionKey;
	if (!validator.IsKeyValid() || !validator.GetEncryptionKey(encryptionKey))
	{
		puts("FAIL: invalid CD key");
		return 2;
	}

	if (!MMG_TrackableServer::Create())
	{
		puts("FAIL: could not create MMG_TrackableServer");
		return 2;
	}
	MMG_TrackableServer* server = MMG_TrackableServer::GetInstance();
	NoCallbacks callbacks;
	server->AddCallbackInterface(&callbacks);
	server->SetCDKeyInfromation(validator.GetSequenceNumber(), encryptionKey);

	MMG_ServerStartupVariables vars;
	vars.myServerName = L"FakeDedicatedServer";
	vars.myServerReliablePort = 48999;
	vars.myMassgateCommPort = 48999;
	vars.myGameVersion = 35;		// what wic_ds.exe 1.0.1.1 reports
	vars.myProtocolVersion = 126;
	vars.myMaxNumPlayers = 16;
	vars.myIsRanked = 1;
	vars.myIsDedicated = 1;
	vars.myServerType = NORMAL_SERVER;
	vars.myHasDominationMaps = true;
	server->ServerStarted(vars);

	if (!Pump(server, 15000, true))
	{
		puts("FAIL: server did not register with Massgate");
		return 1;
	}
	puts("registered as a ranked server");

	MC_GrowingArray<MMG_Stats::PlayerMatchStats> stats;
	stats.Init(4, 4, false);
	if (withBots)
		stats.Add(MakeStats(0, 250, false));
	stats.Add(MakeStats((unsigned int)profileId, 1234, true));
	server->ReportPlayerStats(stats, 0x1234567812345678ULL);
	printf("reported %d stats entries (%s)\n", stats.Count(), withBots ? "bot first, then player" : "player only");

	if (!Pump(server, 3000, false))
	{
		puts("FAIL: Massgate dropped the connection after the stats report");
		return 1;
	}
	puts("OK: still connected after the stats report");
	// ServerStopped() drops the connection; Update() must not be called after it.
	server->ServerStopped();
	return 0;
}
