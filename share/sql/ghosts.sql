-- Ghost ladder: simulated players that give a single-player setup a populated ladder.
-- Idempotent: applied by scripts/setup-db.ps1 on every run.
--
-- Each ghost is an ordinary profile (Profiles/PlayerStats/BestOfLadder rows, owned by one
-- locked account). MMS_GhostLadder creates them from the callsign file passed with -ghosts
-- and advances them with simulated matches; this table holds what the simulation needs.

CREATE TABLE IF NOT EXISTS `GhostPlayers` (
  `profileId` int(10) unsigned NOT NULL,
  `callsign` varchar(22) NOT NULL,
  -- 0..1; drives score per match and win rate.
  `skill` float NOT NULL default '0.5',
  -- Average number of matches played per day.
  `matchesPerDay` float NOT NULL default '1',
  -- Preferred role: 1 = armor, 2 = air, 3 = infantry, 4 = support.
  `favoriteRole` tinyint(3) unsigned NOT NULL default '1',
  -- Simulated up to this point in time (unix time).
  `lastSimulated` int(10) unsigned NOT NULL default '0',
  -- 0 once the callsign is removed from the file: the ghost stops playing and ages off the ladder.
  `isActive` tinyint(3) unsigned NOT NULL default '1',
  PRIMARY KEY  (`profileId`),
  UNIQUE KEY `callsign` (`callsign`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;
