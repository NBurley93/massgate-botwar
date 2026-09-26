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
#ifndef MMS_GHOSTLADDER_H
#define MMS_GHOSTLADDER_H

#include "MT_Thread.h"
#include "MT_Event.h"
#include "MMS_Constants.h"
#include "MMG_Stats.h"
#include "mdb_mysqlconnection.h"

// Simulated players ("ghosts") that populate the ladder on a server with a handful of
// real players, so ladder percentages - and with them the officer ranks - mean something.
//
// Enabled with -ghosts <callsign file>. Every callsign becomes an ordinary profile owned
// by one locked account. Ghosts that do not exist yet are created at startup, before the
// player ladder is loaded, with a career and three weeks of ladder history. After that the
// thread plays the matches each ghost would have played since it was last simulated,
// through MMS_PlayerStats::UpdatePlayerStats like any reported match, whenever a real
// match is reported (and once at startup).
class MMS_GhostLadder : public MT_Thread
{
public:
	// Creates the instance and the missing ghosts if -ghosts was given. Call after the rank
	// definitions are loaded and before the ladder updater is created.
	static void				Create(const MMS_Settings& theSettings, MDB_MySqlConnection* aWriteConnection);
	static MMS_GhostLadder*	GetInstance() { return ourInstance; }

	// Wakes the simulation; called after a real match has been reported.
	void					OnMatchReported();

	virtual void			Run();

	class Persona
	{
	public:
		float				mySkill;			// 0..1
		float				myMatchesPerDay;
		unsigned char		myFavoriteRole;		// 1 armor, 2 air, 3 infantry, 4 support
	};

	class Random
	{
	public:
		explicit Random(unsigned int aSeed);
		unsigned int		Next();
		float				Uniform();			// [0, 1)
		float				Uniform(float aMin, float aMax);
		float				Gaussian();			// mean 0, deviation 1
		unsigned int		Poisson(float aMean);
	private:
		unsigned int		myState;
	};

	// One simulated match for a ghost, as a server would report it.
	static MMG_Stats::PlayerMatchStats	SimulateMatch(unsigned int aProfileId, const Persona& aPersona, Random& aRandom);

private:
							MMS_GhostLadder(const MMS_Settings& theSettings);
							~MMS_GhostLadder();

	bool					PrivCreateMissingGhosts(MDB_MySqlConnection& aConnection, const char* aCallsignFile);
	bool					PrivReadCallsigns(const char* aCallsignFile, MC_GrowingArray<MC_StaticString<32> >& someCallsigns);
	unsigned int			PrivGetGhostAccount(MDB_MySqlConnection& aConnection);
	bool					PrivCreateGhost(MDB_MySqlConnection& aConnection, unsigned int anAccountId, const char* aCallsign);
	void					PrivUpdateGhostRanks(MDB_MySqlConnection& aConnection);
	void					PrivSimulateElapsedTime();

	static MMS_GhostLadder*	ourInstance;

	const MMS_Settings&		mySettings;
	MDB_MySqlConnection*	myConnection;
	MT_Event				myWakeEvent;
	Random					myRandom;
};

#endif
