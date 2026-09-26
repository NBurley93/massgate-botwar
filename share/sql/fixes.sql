-- Corrections to databasestructure.sql for things the released server code expects.
-- Idempotent: applied by scripts/setup-db.ps1 on every run.

-- Referenced by MMS_AccountAuthenticationUpdater (refreshes validUntil for logged-in
-- tokens every few minutes) but missing from the released dump. Nothing else reads or
-- writes it, so it only has to exist for that UPDATE to succeed.
CREATE TABLE IF NOT EXISTS `Authentication` (
  `tokenId` int(10) unsigned NOT NULL default '0',
  `validUntil` datetime NOT NULL default '0000-00-00 00:00:00',
  PRIMARY KEY  (`tokenId`)
) ENGINE=InnoDB DEFAULT CHARSET=latin1;
