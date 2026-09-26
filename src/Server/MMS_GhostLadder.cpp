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
#include "StdAfx.h"
#include "MMS_GhostLadder.h"

#include "MC_CommandLine.h"
#include "MC_IniFile.h"
#include "MMG_Constants.h"
#include "MMS_BestOfLadder.h"
#include "MMS_InitData.h"
#include "MMS_PersistenceCache.h"
#include "MMS_PlayerStats.h"
#include "MT_ThreadingTools.h"
#include "ML_Logger.h"
#include "mdb_stringconversion.h"

#include <math.h>
#include <time.h>

namespace
{
	// The account that owns every ghost profile. It has no usable password and is banned,
	// so nobody can log in as a ghost.
	const char*			GHOST_ACCOUNT_EMAIL = "ghosts@botwar.invalid";

	const unsigned int	SECONDS_PER_DAY = 24 * 3600;
	const unsigned int	MAX_CAREER_DAYS = 900;
	const unsigned int	MAX_CAREER_MATCHES = 4000;
	// Reports above PlayerScoreSuspect (3000 by default) are logged as possible stat padding.
	const unsigned int	MAX_MATCH_SCORE = 2900;

	// Profile name rules from MMS_AccountConnectionHandler (adding a profile), except the
	// length: players are held to 13 characters to leave room for a clan tag, which the server
	// prepends at runtime (up to 23 characters in total). Ghosts never join a clan, so they
	// may use that room, up to the width of Profiles.profileName and GhostPlayers.callsign.
	const unsigned int	MIN_PROFILENAME_LENGTH = 3;
	const unsigned int	MAX_PROFILENAME_LENGTH = 22;

	unsigned int HashCallsign(const char* aCallsign)
	{
		// FNV-1a over the lowercase name: a ghost's persona does not depend on the order of
		// the file, and survives restarts.
		unsigned int hash = 2166136261u;
		for (const char* c = aCallsign; *c; c++)
		{
			hash ^= (unsigned char)tolower(*c);
			hash *= 16777619u;
		}
		return hash;
	}

	float Clamp(float aValue, float aMin, float aMax)
	{
		return aValue < aMin ? aMin : (aValue > aMax ? aMax : aValue);
	}

	MMS_GhostLadder::Persona MakePersona(const char* aCallsign)
	{
		MMS_GhostLadder::Random random(HashCallsign(aCallsign));
		MMS_GhostLadder::Persona persona;
		// Bell-shaped around 0.5, stretched so a few ghosts are genuinely good or bad.
		const float bell = (random.Uniform() + random.Uniform() + random.Uniform()) / 3.0f;
		persona.mySkill = Clamp(0.5f + (bell - 0.5f) * 1.8f, 0.02f, 0.98f);
		// Log-uniform: most ghosts play a match or two a day, some grind, some drop by.
		persona.myMatchesPerDay = (float)exp(random.Uniform((float)log(0.15), (float)log(3.5)));
		persona.myFavoriteRole = (unsigned char)(1 + random.Next() % 4);
		return persona;
	}

	bool IsValidCallsign(const char* aCallsign)
	{
		const size_t length = strlen(aCallsign);
		if (length < MIN_PROFILENAME_LENGTH || length > MAX_PROFILENAME_LENGTH)
			return false;
		for (const char* c = aCallsign; *c; c++)
		{
			if (*c < 32 || *c > 126 || *c == '|' || *c == '\\')
				return false;
		}
		return !strstr(aCallsign, "CLAN") && !strstr(aCallsign, "PLAYER") && !strstr(aCallsign, "PROFILE");
	}

	// The PlayerStats columns a ghost's generated career fills in.
	class Career
	{
	public:
		Career() { memset(this, 0, sizeof(*this)); }

		void Add(const MMG_Stats::PlayerMatchStats& aMatch)
		{
			sc_tot += aMatch.scoreTotal;
			sc_highest = __max(sc_highest, aMatch.scoreTotal);
			sc_inf += aMatch.scoreAsInfantry;
			sc_highinf = __max(sc_highinf, aMatch.scoreAsInfantry);
			sc_sup += aMatch.scoreAsSupport;
			sc_highsup = __max(sc_highsup, aMatch.scoreAsSupport);
			sc_arm += aMatch.scoreAsArmor;
			sc_higharm = __max(sc_higharm, aMatch.scoreAsArmor);
			sc_air += aMatch.scoreAsAir;
			sc_highair = __max(sc_highair, aMatch.scoreAsAir);
			sc_damen += aMatch.scoreByDamagingEnemies;
			sc_ta += aMatch.scoreByUsingTacticalAids;
			sc_cpc += aMatch.scoreByCommandPointCaptures;
			sc_rep += aMatch.scoreByRepairing;
			sc_fort += aMatch.scoreByFortifying;
			t_USA += aMatch.timePlayedAsUSA;
			t_USSR += aMatch.timePlayedAsUSSR;
			t_NATO += aMatch.timePlayedAsNATO;
			t_inf += aMatch.timePlayedAsInfantry;
			t_sup += aMatch.timePlayedAsSupport;
			t_arm += aMatch.timePlayedAsArmor;
			t_air += aMatch.timePlayedAsAir;
			n_matches++;
			n_matcheswon += aMatch.matchWon;
			n_matcheslost += aMatch.matchLost;
			if (aMatch.matchType == MATCHTYPE_DOMINATION)
			{
				n_dommatches++;
				n_dommatcheswon += aMatch.matchWon;
			}
			else if (aMatch.matchType == MATCHTYPE_ASSAULT)
			{
				n_assmatches++;
				n_assmatcheswon += aMatch.matchWon;
			}
			else if (aMatch.matchType == MATCHTYPE_TOW)
			{
				n_towmatches++;
				n_towmatcheswon += aMatch.matchWon;
			}
			n_cwinstr = aMatch.matchWon ? n_cwinstr + 1 : 0;
			n_bwinstr = __max(n_bwinstr, n_cwinstr);
			n_ukills += aMatch.numberOfUnitsKilled;
			n_ulost += aMatch.numberOfUnitsLost;
			n_cpc += aMatch.numberOfCommandPointCaptures;
			n_rps += aMatch.numberOfReinforcementPointsSpent;
			n_taps += aMatch.numberOfTacticalAidPointsSpent;
			n_tach += aMatch.numberOfTacticalAidCriticalHits;
			n_bplayer += (aMatch.bestData & MMG_Stats::PlayerMatchStats::BEST_PLAYER) ? 1 : 0;
		}

		unsigned int sc_tot, sc_highest, sc_inf, sc_highinf, sc_sup, sc_highsup, sc_arm, sc_higharm, sc_air, sc_highair;
		unsigned int sc_damen, sc_ta, sc_cpc, sc_rep, sc_fort;
		unsigned int t_USA, t_USSR, t_NATO, t_inf, t_sup, t_arm, t_air;
		unsigned int n_matches, n_matcheswon, n_matcheslost;
		unsigned int n_dommatches, n_dommatcheswon, n_assmatches, n_assmatcheswon, n_towmatches, n_towmatcheswon;
		unsigned int n_cwinstr, n_bwinstr, n_ukills, n_ulost, n_cpc, n_rps, n_taps, n_tach, n_bplayer;
	};

	class LadderEntry
	{
	public:
		LadderEntry() : myProfileId(0), myScore(0) { }
		LadderEntry(unsigned int aProfileId, unsigned int aScore) : myProfileId(aProfileId), myScore(aScore) { }
		// Highest total first, like MMS_BestOfLadder.
		bool operator<(const LadderEntry& aRhs) const { return myScore > aRhs.myScore; }
		bool operator>(const LadderEntry& aRhs) const { return myScore < aRhs.myScore; }
		bool operator==(const LadderEntry& aRhs) const { return myScore == aRhs.myScore; }

		unsigned int myProfileId;
		unsigned int myScore;
	};

	class Ghost
	{
	public:
		unsigned int				myProfileId;
		unsigned int				myLastSimulated;
		MMS_GhostLadder::Persona	myPersona;
	};

	void ReadLadderSettings(MDB_MySqlConnection& aConnection, unsigned int& aNumDays, unsigned int& aNumGames)
	{
		aNumDays = MMS_BestOfLadder::DEFAULT_LADDER_ENTRY_AGE / SECONDS_PER_DAY;
		aNumGames = MMS_BestOfLadder::DEFAULT_LADDER_SCORE_COUNT;

		MDB_MySqlQuery query(aConnection);
		MDB_MySqlResult result;
		if (!query.Ask(result, "SELECT aVariable, aValue FROM Settings WHERE aVariable IN ('BestOfLadderNumDays', 'BestOfLadderNumGames')"))
			return;
		MDB_MySqlRow row;
		while (result.GetNextRow(row))
		{
			MC_StaticString<64> name = row["aVariable"];
			if (name == "BestOfLadderNumDays")
				aNumDays = __max(1, (int)row["aValue"]);
			else if (name == "BestOfLadderNumGames")
				aNumGames = __max(1, (int)row["aValue"]);
		}
	}
}

MMS_GhostLadder* MMS_GhostLadder::ourInstance = NULL;

// ------------------------------------------------------------------------------------------
// Random

MMS_GhostLadder::Random::Random(unsigned int aSeed)
: myState(aSeed ? aSeed : 0x9e3779b9u)
{
}

unsigned int
MMS_GhostLadder::Random::Next()
{
	// xorshift32
	myState ^= myState << 13;
	myState ^= myState >> 17;
	myState ^= myState << 5;
	return myState;
}

float
MMS_GhostLadder::Random::Uniform()
{
	return (Next() >> 8) * (1.0f / 16777216.0f);
}

float
MMS_GhostLadder::Random::Uniform(float aMin, float aMax)
{
	return aMin + (aMax - aMin) * Uniform();
}

float
MMS_GhostLadder::Random::Gaussian()
{
	// Box-Muller
	const float u = __max(Uniform(), 1e-7f);
	return (float)(sqrt(-2.0 * log(u)) * cos(6.283185307 * Uniform()));
}

unsigned int
MMS_GhostLadder::Random::Poisson(float aMean)
{
	if (aMean <= 0.0f)
		return 0;
	if (aMean > 60.0f)
		return (unsigned int)__max(0.0f, aMean + sqrt(aMean) * Gaussian() + 0.5f);
	// Knuth
	const double limit = exp(-aMean);
	double product = Uniform();
	unsigned int count = 0;
	while (product > limit)
	{
		count++;
		product *= Uniform();
	}
	return count;
}

// ------------------------------------------------------------------------------------------
// Match simulation

MMG_Stats::PlayerMatchStats
MMS_GhostLadder::SimulateMatch(unsigned int aProfileId, const Persona& aPersona, Random& aRandom)
{
	MMG_Stats::PlayerMatchStats stats;
	stats.profileId = aProfileId;

	const unsigned short length = (unsigned short)aRandom.Uniform(15.0f * 60.0f, 30.0f * 60.0f);
	const bool won = aRandom.Uniform() < 0.3f + 0.4f * aPersona.mySkill;

	const float meanScore = 250.0f + 1100.0f * aPersona.mySkill;
	float score = meanScore * (1.0f + 0.35f * aRandom.Gaussian());
	if (won)
		score *= 1.1f;
	stats.scoreTotal = (unsigned short)Clamp(score, 40.0f, (float)MAX_MATCH_SCORE);

	unsigned char role = aPersona.myFavoriteRole;
	if (aRandom.Uniform() > 0.6f)
		role = (unsigned char)(1 + aRandom.Next() % 4);
	switch (role)
	{
	case 1: stats.scoreAsArmor = stats.scoreTotal; stats.timePlayedAsArmor = length; break;
	case 2: stats.scoreAsAir = stats.scoreTotal; stats.timePlayedAsAir = length; break;
	case 3: stats.scoreAsInfantry = stats.scoreTotal; stats.timePlayedAsInfantry = length; break;
	default: stats.scoreAsSupport = stats.scoreTotal; stats.timePlayedAsSupport = length; break;
	}

	switch (aRandom.Next() % 3)
	{
	case 0: stats.timePlayedAsUSA = length; break;
	case 1: stats.timePlayedAsUSSR = length; break;
	default: stats.timePlayedAsNATO = length; break;
	}

	// Split the score the way real matches look: mostly damage, then command points and
	// tactical aids; support players also repair and fortify. The ratios keep the
	// statpadder heuristics in MMS_PlayerStats quiet (about 26 points per kill).
	stats.scoreByDamagingEnemies = (unsigned short)(stats.scoreTotal * 0.6f);
	stats.scoreByCommandPointCaptures = (unsigned short)(stats.scoreTotal * 0.15f);
	stats.scoreByUsingTacticalAids = (unsigned short)(stats.scoreTotal * 0.1f);
	if (role == 4)
	{
		stats.scoreByRepairing = (unsigned short)(stats.scoreTotal * 0.1f);
		stats.scoreByFortifying = (unsigned short)(stats.scoreTotal * 0.05f);
	}
	stats.numberOfUnitsKilled = (unsigned short)(stats.scoreByDamagingEnemies / 26 + aRandom.Next() % 3);
	stats.numberOfUnitsLost = (unsigned short)(stats.numberOfUnitsKilled * (1.4f - 0.8f * aPersona.mySkill) + aRandom.Next() % 4);
	stats.numberOfCommandPointCaptures = (unsigned short)__min(10, stats.scoreByCommandPointCaptures / 40);
	stats.numberOfReinforcementPointsSpent = (unsigned short)(length * aRandom.Uniform(2.0f, 4.0f));
	stats.numberOfTacticalAidPointsSpent = (unsigned short)(stats.scoreByUsingTacticalAids * 2);
	stats.numberOfTacticalAidCriticalHits = (unsigned short)(aRandom.Next() % 3);
	stats.numberOfRoleChanges = (unsigned short)(aRandom.Next() % 3);

	const float mode = aRandom.Uniform();
	stats.matchType = (unsigned char)(mode < 0.6f ? MATCHTYPE_DOMINATION : (mode < 0.8f ? MATCHTYPE_ASSAULT : MATCHTYPE_TOW));
	stats.matchWon = won ? 1 : 0;
	stats.matchLost = won ? 0 : 1;
	stats.matchWasFlawlessVictory = (won && aRandom.Uniform() < 0.05f) ? 1 : 0;
	if (won && aRandom.Uniform() < 0.15f * aPersona.mySkill)
		stats.bestData |= MMG_Stats::PlayerMatchStats::BEST_PLAYER;

	stats.timeTotalMatchLength = length;
	stats.totalTimePlayed = length;
	stats.wasPlayingAtMatchEnd = 1;
	return stats;
}

// ------------------------------------------------------------------------------------------
// Setup

MMS_GhostLadder::MMS_GhostLadder(const MMS_Settings& theSettings)
: mySettings(theSettings)
, myConnection(NULL)
, myRandom((unsigned int)time(NULL) ^ GetCurrentProcessId())
{
	myGhostProfileIds.Init(512, 256, false);
}

MMS_GhostLadder::~MMS_GhostLadder()
{
	delete myConnection;
}

void
MMS_GhostLadder::Create(const MMS_Settings& theSettings, MDB_MySqlConnection* aWriteConnection)
{
	assert(ourInstance == NULL);

	const char* configFile = "config.ini";
	MC_CommandLine::GetInstance()->GetStringValue("config", configFile);
	MC_IniFile config(configFile);
	if (!config.Process() || !config.HasKey("ghosts.callsigns"))
	{
		LOG_INFO("Ghost ladder disabled (no [ghosts] callsigns= in %s).", configFile);
		return;
	}
	MC_StaticString<1024> callsignFile = config.GetString("ghosts.callsigns");

	MMS_GhostLadder* ghostLadder = new MMS_GhostLadder(theSettings);
	ghostLadder->myConnection = new MDB_MySqlConnection(
		theSettings.WriteDbHost,
		theSettings.WriteDbUser,
		theSettings.WriteDbPassword,
		MMS_InitData::GetDatabaseName(),
		false);
	if (!ghostLadder->myConnection->Connect()
		|| !ghostLadder->PrivCreateMissingGhosts(*aWriteConnection, callsignFile.GetBuffer())
		|| !ghostLadder->PrivLoadGhostProfileIds(*aWriteConnection))
	{
		LOG_ERROR("Ghost ladder disabled: setup failed.");
		delete ghostLadder;
		return;
	}
	ghostLadder->PrivUpdateGhostRanks(*aWriteConnection);
	if (config.HasKey("ghosts.roster"))
	{
		MC_StaticString<1024> rosterFile = config.GetString("ghosts.roster");
		ghostLadder->PrivWriteRoster(*aWriteConnection, rosterFile.GetBuffer());
	}
	ourInstance = ghostLadder;
}

bool
MMS_GhostLadder::PrivLoadGhostProfileIds(MDB_MySqlConnection& aConnection)
{
	MDB_MySqlQuery query(aConnection);
	MDB_MySqlResult result;
	if (!query.Ask(result, "SELECT profileId FROM GhostPlayers"))
		return false;
	MDB_MySqlRow row;
	while (result.GetNextRow(row))
		myGhostProfileIds.Add((unsigned int)row["profileId"]);
	myGhostProfileIds.Sort();
	return true;
}

void
MMS_GhostLadder::PrivWriteRoster(MDB_MySqlConnection& aConnection, const char* aRosterFile)
{
	// The dedicated-server hook reads this to give each bot a ghost's name and profile, so
	// the bot's match counts for that ghost: "<profileId>\t<callsign>" per active ghost.
	MDB_MySqlQuery query(aConnection);
	MDB_MySqlResult result;
	if (!query.Ask(result, "SELECT profileId, callsign FROM GhostPlayers WHERE isActive=1 ORDER BY profileId"))
		return;

	FILE* file = fopen(aRosterFile, "wt");
	if (!file)
	{
		LOG_ERROR("Could not write the ghost roster %s.", aRosterFile);
		return;
	}
	unsigned int numGhosts = 0;
	MDB_MySqlRow row;
	while (result.GetNextRow(row))
	{
		fprintf(file, "%u\t%s\n", (unsigned int)row["profileId"], (const char*)row["callsign"]);
		numGhosts++;
	}
	fclose(file);
	LOG_INFO("Ghost roster: %u ghosts written to %s.", numGhosts, aRosterFile);
}

bool
MMS_GhostLadder::PrivReadCallsigns(const char* aCallsignFile, MC_GrowingArray<MC_StaticString<32> >& someCallsigns)
{
	FILE* file = fopen(aCallsignFile, "rt");
	if (!file)
	{
		LOG_ERROR("Could not open ghost callsign file %s.", aCallsignFile);
		return false;
	}

	char line[256];
	while (fgets(line, sizeof(line), file))
	{
		// Trim whitespace at both ends.
		char* start = line;
		while (*start == ' ' || *start == '\t')
			start++;
		char* end = start + strlen(start);
		while (end > start && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
			*--end = 0;

		if (!*start || *start == '#')
			continue;
		if (!IsValidCallsign(start))
		{
			LOG_ERROR("Ignoring ghost callsign '%s': names must be %u-%u printable ASCII characters without CLAN, PLAYER, PROFILE, | or \\.", start, MIN_PROFILENAME_LENGTH, MAX_PROFILENAME_LENGTH);
			continue;
		}

		bool duplicate = false;
		for (int i = 0; i < someCallsigns.Count() && !duplicate; i++)
			duplicate = _stricmp(someCallsigns[i].GetBuffer(), start) == 0;
		if (!duplicate)
			someCallsigns.Add(MC_StaticString<32>(start));
	}
	fclose(file);
	return true;
}

unsigned int
MMS_GhostLadder::PrivGetGhostAccount(MDB_MySqlConnection& aConnection)
{
	MDB_MySqlQuery query(aConnection);
	MDB_MySqlResult result;
	MC_StaticString<256> sql;

	sql.Format("SELECT accountid FROM Accounts WHERE email='%s'", GHOST_ACCOUNT_EMAIL);
	if (!query.Ask(result, sql.GetBuffer()))
		return 0;
	MDB_MySqlRow row;
	if (result.GetNextRow(row))
		return row["accountid"];

	// No password hash can match '!', and the account is banned for good measure.
	sql.Format("INSERT INTO Accounts (email, password, country, isBanned, dateAdded) VALUES ('%s', '!', 'US', 1, NOW())", GHOST_ACCOUNT_EMAIL);
	if (!query.Modify(result, sql.GetBuffer()))
		return 0;
	return (unsigned int)query.GetLastInsertId();
}

bool
MMS_GhostLadder::PrivCreateMissingGhosts(MDB_MySqlConnection& aConnection, const char* aCallsignFile)
{
	MC_GrowingArray<MC_StaticString<32> > callsigns;
	callsigns.Init(256, 256, false);
	if (!PrivReadCallsigns(aCallsignFile, callsigns))
		return false;

	MDB_MySqlQuery query(aConnection);
	MDB_MySqlResult result;
	// Checked through information_schema: a query on a missing table stops debug builds.
	MDB_MySqlRow row;
	if (!query.Ask(result, "SELECT COUNT(*) AS n FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='GhostPlayers'")
		|| !result.GetNextRow(row) || (int)row["n"] == 0)
	{
		LOG_ERROR("The GhostPlayers table is missing; apply share/sql/ghosts.sql (scripts/setup-db.ps1 does).");
		return false;
	}

	const unsigned int accountId = PrivGetGhostAccount(aConnection);
	if (!accountId)
	{
		LOG_ERROR("Could not find or create the ghost account.");
		return false;
	}

	// Ghosts seeded by earlier versions lack these rows (see PrivCreateGhost).
	if (!query.Modify(result, "INSERT IGNORE INTO PlayerMedals (profileId) SELECT profileId FROM GhostPlayers")
		|| !query.Modify(result, "INSERT IGNORE INTO PlayerBadges (profileId) SELECT profileId FROM GhostPlayers"))
		return false;

	// Ghosts whose callsign left the file stop playing; the rest are (re)activated below.
	if (!query.Modify(result, "UPDATE GhostPlayers SET isActive=0"))
		return false;

	unsigned int numCreated = 0;
	unsigned int numActive = 0;
	for (int i = 0; i < callsigns.Count(); i++)
	{
		const char* callsign = callsigns[i].GetBuffer();
		MC_StaticString<1024> callsignSql;
		aConnection.MakeSqlString(callsignSql, callsign);

		MC_StaticString<256> sql;
		sql.Format("UPDATE GhostPlayers SET isActive=1 WHERE callsign='%s'", callsignSql.GetBuffer());
		if (!query.Modify(result, sql.GetBuffer()))
			return false;
		if (result.GetAffectedNumberOrRows() > 0)
		{
			numActive++;
			continue;
		}

		// Never take a name a real player already uses (names compare case-insensitively).
		sql.Format("SELECT profileId FROM Profiles WHERE normalizedProfileName='%s'", callsignSql.GetBuffer());
		if (!query.Ask(result, sql.GetBuffer()))
			return false;
		if (result.GetNextRow(row))
		{
			LOG_ERROR("Ignoring ghost callsign '%s': a player already uses that name.", callsign);
			continue;
		}

		if (!PrivCreateGhost(aConnection, accountId, callsign))
			return false;
		numCreated++;
		numActive++;
	}

	LOG_INFO("Ghost ladder: %u active ghosts (%u created) from %s.", numActive, numCreated, aCallsignFile);
	return true;
}

bool
MMS_GhostLadder::PrivCreateGhost(MDB_MySqlConnection& aConnection, unsigned int anAccountId, const char* aCallsign)
{
	const Persona persona = MakePersona(aCallsign);
	Random random(HashCallsign(aCallsign) ^ 0x5bd1e995u);

	unsigned int ladderDays, ladderGames;
	ReadLadderSettings(aConnection, ladderDays, ladderGames);

	const unsigned int now = (unsigned int)time(NULL);
	// Weighted towards recent joiners, so the ladder is a pyramid rather than all veterans.
	const float seniority = random.Uniform();
	const unsigned int careerDays = ladderDays + (unsigned int)(seniority * seniority * (MAX_CAREER_DAYS - ladderDays));
	const unsigned int careerStart = now - careerDays * SECONDS_PER_DAY;
	const unsigned int ladderStart = now - ladderDays * SECONDS_PER_DAY;
	// Not every day is a playing day.
	const unsigned int numMatches = __min(MAX_CAREER_MATCHES,
		random.Poisson(persona.myMatchesPerDay * careerDays * random.Uniform(0.4f, 0.9f)));

	// Play the career. Matches inside the ladder window also compete for the ghost's
	// ladder entries, which keep the best ladderGames scores (winners count extra).
	Career career;
	MC_GrowingArray<unsigned int> ladderScores;
	ladderScores.Init(64, 64, false);
	MC_GrowingArray<unsigned int> ladderTimes;
	ladderTimes.Init(64, 64, false);
	unsigned int lastMatch = careerStart;
	for (unsigned int i = 0; i < numMatches; i++)
	{
		const MMG_Stats::PlayerMatchStats match = SimulateMatch(0, persona, random);
		career.Add(match);

		const unsigned int playedAt = careerStart + (unsigned int)(random.Uniform() * (now - careerStart));
		lastMatch = __max(lastMatch, playedAt);
		if (playedAt >= ladderStart)
		{
			const float multiplier = match.matchWon ? (float)mySettings.PlayerWinningScoreMultiplier : 1.0f;
			ladderScores.Add((unsigned int)(match.scoreTotal * multiplier));
			ladderTimes.Add(playedAt);
		}
	}

	MC_StaticString<1024> callsignSql;
	aConnection.MakeSqlString(callsignSql, aCallsign);

	MDB_MySqlTransaction trans(aConnection);
	MDB_MySqlResult result;
	MC_StaticString<4096> sql;

	sql.Format("INSERT INTO Profiles (accountId, profileName, normalizedProfileName, lastLogin, isDeleted) "
		"VALUES (%u, '%s', '%s', FROM_UNIXTIME(%u), 'no')",
		anAccountId, callsignSql.GetBuffer(), callsignSql.GetBuffer(), lastMatch);
	if (!trans.Execute(result, sql.GetBuffer()))
		return false;
	const unsigned int profileId = (unsigned int)trans.GetLastInsertId();

	// Profile creation (MMS_AccountConnectionHandler) also adds these; loading a profile's
	// stats fails without the medals row, and the game then gets disconnected when it asks
	// for the profile's medals.
	sql.Format("INSERT INTO PlayerMedals (profileId) VALUES (%u)", profileId);
	if (!trans.Execute(result, sql.GetBuffer()))
		return false;
	sql.Format("INSERT INTO PlayerBadges (profileId) VALUES (%u)", profileId);
	if (!trans.Execute(result, sql.GetBuffer()))
		return false;

	sql.Format("INSERT INTO PlayerStats (profileId, massgateMemberSince, "
		"sc_tot, sc_highest, sc_inf, sc_highinf, sc_sup, sc_highsup, sc_arm, sc_higharm, sc_air, sc_highair, "
		"sc_damen, sc_ta, sc_cpc, sc_rep, sc_fort, "
		"t_USA, t_USSR, t_NATO, t_inf, t_sup, t_arm, t_air, "
		"n_matches, n_matcheswon, n_matcheslost, "
		"n_dommatches, n_dommatcheswon, n_assmatches, n_assmatcheswon, n_towmatches, n_towmatcheswon, "
		"n_cwinstr, n_bwinstr, n_ukills, n_ulost, n_cpc, n_rps, n_taps, n_tach, n_bplayer) "
		"VALUES (%u, FROM_UNIXTIME(%u), "
		"%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, "
		"%u, %u, %u, %u, %u, "
		"%u, %u, %u, %u, %u, %u, %u, "
		"%u, %u, %u, "
		"%u, %u, %u, %u, %u, %u, "
		"%u, %u, %u, %u, %u, %u, %u, %u, %u)",
		profileId, careerStart,
		career.sc_tot, career.sc_highest, career.sc_inf, career.sc_highinf, career.sc_sup, career.sc_highsup, career.sc_arm, career.sc_higharm, career.sc_air, career.sc_highair,
		career.sc_damen, career.sc_ta, career.sc_cpc, career.sc_rep, career.sc_fort,
		career.t_USA, career.t_USSR, career.t_NATO, career.t_inf, career.t_sup, career.t_arm, career.t_air,
		career.n_matches, career.n_matcheswon, career.n_matcheslost,
		career.n_dommatches, career.n_dommatcheswon, career.n_assmatches, career.n_assmatcheswon, career.n_towmatches, career.n_towmatcheswon,
		career.n_cwinstr, career.n_bwinstr, career.n_ukills, career.n_ulost, career.n_cpc, career.n_rps, career.n_taps, career.n_tach, career.n_bplayer);
	if (!trans.Execute(result, sql.GetBuffer()))
		return false;

	// The best ladderGames scores of the window, each at a time it was played.
	ladderScores.Sort();
	const int numLadderEntries = __min(ladderScores.Count(), (int)ladderGames);
	for (int i = 0; i < numLadderEntries; i++)
	{
		sql.Format("INSERT INTO BestOfLadder (profileId, score, entered) VALUES (%u, %u, FROM_UNIXTIME(%u))",
			profileId, ladderScores[ladderScores.Count() - 1 - i], ladderTimes[i]);
		if (!trans.Execute(result, sql.GetBuffer()))
			return false;
	}

	sql.Format("INSERT INTO GhostPlayers (profileId, callsign, skill, matchesPerDay, favoriteRole, lastSimulated, isActive) "
		"VALUES (%u, '%s', %f, %f, %u, %u, 1)",
		profileId, callsignSql.GetBuffer(), persona.mySkill, persona.myMatchesPerDay, persona.myFavoriteRole, now);
	if (!trans.Execute(result, sql.GetBuffer()))
		return false;

	return trans.Commit();
}

void
MMS_GhostLadder::PrivUpdateGhostRanks(MDB_MySqlConnection& aConnection)
{
	// Ranks are normally (re)computed when MMS_PlayerStats flushes a profile it updated.
	// Seeded ghosts have not played through it yet, so give them the rank their career and
	// ladder position earn, computed the way MMS_BestOfLadder and MMS_PlayerStats do.
	unsigned int ladderDays, ladderGames;
	ReadLadderSettings(aConnection, ladderDays, ladderGames);

	MDB_MySqlQuery query(aConnection);
	MDB_MySqlResult result;
	MC_StaticString<512> sql;
	sql.Format("SELECT profileId, score FROM BestOfLadder WHERE entered >= FROM_UNIXTIME(%u) ORDER BY profileId, score DESC",
		(unsigned int)time(NULL) - ladderDays * SECONDS_PER_DAY);
	if (!query.Ask(result, sql.GetBuffer()))
		return;

	MC_GrowingArray<LadderEntry> ladder;
	ladder.Init(1024, 1024, false);
	MDB_MySqlRow row;
	unsigned int currentProfile = 0;
	unsigned int currentTotal = 0;
	unsigned int currentCount = 0;
	while (result.GetNextRow(row))
	{
		const unsigned int profileId = row["profileId"];
		if (profileId != currentProfile)
		{
			if (currentProfile)
				ladder.Add(LadderEntry(currentProfile, currentTotal));
			currentProfile = profileId;
			currentTotal = 0;
			currentCount = 0;
		}
		if (currentCount++ < ladderGames)
			currentTotal += (unsigned int)row["score"];
	}
	if (currentProfile)
		ladder.Add(LadderEntry(currentProfile, currentTotal));
	ladder.Sort();

	if (!query.Ask(result, "SELECT g.profileId, s.sc_tot FROM GhostPlayers g JOIN PlayerStats s ON s.profileId = g.profileId"))
		return;

	MDB_MySqlTransaction trans(aConnection);
	MDB_MySqlResult updateResult;
	const float numItems = (float)__max(ladder.Count(), 1);
	while (result.GetNextRow(row))
	{
		const unsigned int profileId = row["profileId"];
		const unsigned int totalScore = row["sc_tot"];
		unsigned int position = (unsigned int)-1;
		for (int i = 0; i < ladder.Count() && position == (unsigned int)-1; i++)
			if (ladder[i].myProfileId == profileId)
				position = i;
		// Off the ladder counts as 0%: enough for the ranks that only need score.
		const unsigned int percent = (position == (unsigned int)-1) ? 0 : 100 - (unsigned int)((position * 100.0f) / numItems);
		const unsigned int rank = MMS_PersistenceCache::GetRank(percent, totalScore);
		sql.Format("UPDATE PlayerStats SET `rank`=GREATEST(`rank`, %u), maxLadderPercent=GREATEST(maxLadderPercent, %u) WHERE profileId=%u",
			rank, percent, profileId);
		trans.Execute(updateResult, sql.GetBuffer());
	}
	trans.Commit();
}

// ------------------------------------------------------------------------------------------
// Simulation

void
MMS_GhostLadder::OnMatchReported()
{
	myWakeEvent.Signal();
}

void
MMS_GhostLadder::Run()
{
	MT_ThreadingTools::SetCurrentThreadName("GhostLadder");
	LOG_INFO("Started.");

	// Catch up on the time Massgate was not running.
	PrivSimulateElapsedTime();

	while (!StopRequested())
	{
		bool signalled = false;
		myWakeEvent.TimedWaitForSignal(1000, signalled);
		if (signalled)
		{
			myWakeEvent.ClearSignal();
			PrivSimulateElapsedTime();
		}
	}

	LOG_INFO("Stopped.");
}

void
MMS_GhostLadder::PrivSimulateElapsedTime()
{
	unsigned int ladderDays, ladderGames;
	ReadLadderSettings(*myConnection, ladderDays, ladderGames);

	MDB_MySqlQuery query(*myConnection);
	MDB_MySqlResult result;
	if (!query.Ask(result, "SELECT profileId, skill, matchesPerDay, favoriteRole, lastSimulated FROM GhostPlayers WHERE isActive=1"))
		return;

	MC_GrowingArray<Ghost> ghosts;
	ghosts.Init(256, 256, false);
	MDB_MySqlRow row;
	while (result.GetNextRow(row))
	{
		Ghost ghost;
		ghost.myProfileId = row["profileId"];
		ghost.myLastSimulated = row["lastSimulated"];
		ghost.myPersona.mySkill = row["skill"];
		ghost.myPersona.myMatchesPerDay = row["matchesPerDay"];
		ghost.myPersona.myFavoriteRole = (unsigned char)(unsigned int)row["favoriteRole"];
		ghosts.Add(ghost);
	}

	const unsigned int now = (unsigned int)time(NULL);
	const unsigned int maxElapsed = ladderDays * SECONDS_PER_DAY;
	MMS_PlayerStats* playerStats = MMS_PlayerStats::GetInstance();
	unsigned int numMatches = 0;
	unsigned int numPlayed = 0;

	for (int i = 0; i < ghosts.Count() && !StopRequested(); i++)
	{
		const Ghost& ghost = ghosts[i];
		const unsigned int elapsed = __min(maxElapsed, now > ghost.myLastSimulated ? now - ghost.myLastSimulated : 0);
		// Coming back after a long break still only gives a ghost the matches that fit
		// in the ladder window.
		const unsigned int matches = __min(ladderGames,
			myRandom.Poisson(ghost.myPersona.myMatchesPerDay * elapsed / (float)SECONDS_PER_DAY));

		for (unsigned int m = 0; m < matches; m++)
		{
			MMG_Stats::PlayerMatchStats stats = SimulateMatch(ghost.myProfileId, ghost.myPersona, myRandom);
			playerStats->UpdatePlayerStats(stats);
		}

		MC_StaticString<256> sql;
		if (matches)
		{
			// Seen recently: somewhere in the last few hours of the elapsed time.
			const unsigned int lastSeen = now - (unsigned int)(myRandom.Uniform() * __min(elapsed, 3 * 3600u));
			sql.Format("UPDATE Profiles SET lastLogin=FROM_UNIXTIME(%u) WHERE profileId=%u", lastSeen, ghost.myProfileId);
			query.Modify(result, sql.GetBuffer());
			numPlayed++;
			numMatches += matches;
		}
		sql.Format("UPDATE GhostPlayers SET lastSimulated=%u WHERE profileId=%u", now, ghost.myProfileId);
		query.Modify(result, sql.GetBuffer());
	}

	if (numMatches)
		LOG_INFO("Ghost ladder: %u ghosts played %u matches.", numPlayed, numMatches);
}
