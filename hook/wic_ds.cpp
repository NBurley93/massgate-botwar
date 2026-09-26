// Patches for wic_ds.exe 1.0.1.1 (image base 0x400000; addresses are fixed).
#include "hook.h"

#include <string.h>

namespace
{
	const uintptr_t		BUILD_STRING_ADDRESS = 0x00753C64;
	const char			BUILD_STRING[] = "henrik.davidsson/MSV-BUILD-04 at 10:57:07 on Jun 10 2009.\n";

	// ---------------------------------------------------------------------------------------------
	// Bots in ranked matches

	bool AllowRankedBots()
	{
		// EXD_DedicatedServer::ReadIniFile, ranked rules: "Ranked game, forcing bot mode to none"
		// runs unless BotMode is already 0. Always take the jump that skips it.
		const bool keepBotMode = PatchBytes(0x004073EA, { 0x74, 0x30 }, { 0xEB, 0x30 }, "ranked bot mode");

		// EXR_MOSGameFinderServer::ReportStatsAtEndOfGame aborts the whole report when a slot has
		// profile ID 0 ("BOTS DETECTED IN RANKED GAME"). Without the check, all bots are reported as
		// one profile-0 entry, which Massgate skips.
		const bool reportWithBots = PatchBytes(0x0056258C, { 0x0F, 0x84, 0x00, 0x02, 0x00, 0x00 }, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 }, "ranked bot stats");

		return keepBotMode && reportWithBots;
	}

	// ---------------------------------------------------------------------------------------------
	// Commander AI fixes (from wic-client): the stock server crashes on assertions that the AI trips.

	// EX_CAI_Type's weapon getters read the shooter at myMaxDamageShooterIndex, which is -1 for
	// some units. The originals assert, then read before the start of the array.
	const size_t	CAI_TYPE_NUM_SHOOTERS = 0x10C;
	const size_t	CAI_TYPE_SHOOTERS = 0x118;
	const size_t	CAI_TYPE_MAX_DAMAGE_SHOOTER_INDEX = 0x120;
	const size_t	SHOOTER_SIZE = 0x3C;

	template <typename T, size_t FieldOffset>
	T __fastcall GetShooterValue(const uint8_t* aType)
	{
		const int numShooters = *reinterpret_cast<const int*>(aType + CAI_TYPE_NUM_SHOOTERS);
		const uint8_t* shooters = *reinterpret_cast<const uint8_t* const*>(aType + CAI_TYPE_SHOOTERS);
		int index = *reinterpret_cast<const int*>(aType + CAI_TYPE_MAX_DAMAGE_SHOOTER_INDEX);
		if (numShooters <= 0 || !shooters)
			return T();
		if (index < 0 || index >= numShooters)
			index = 0;
		return *reinterpret_cast<const T*>(shooters + index * SHOOTER_SIZE + FieldOffset);
	}

	bool ReplaceShooterGetters()
	{
		// __thiscall without arguments: `this` in ecx, like __fastcall's first argument.
		struct Getter { uintptr_t myAddress; const void* myReplacement; const char* myName; };
		const Getter getters[] =
		{
			{ 0x006CA630, &GetShooterValue<unsigned int, 0x28>, "EX_CAI_Type::GetWeaponDamageDirect" },
			{ 0x006CA6C0, &GetShooterValue<unsigned int, 0x2C>, "EX_CAI_Type::GetWeaponDamageBlast" },
			{ 0x006CA750, &GetShooterValue<unsigned int, 0x20>, "EX_CAI_Type::GetArmorPiercingDirect" },
			{ 0x006CA7E0, &GetShooterValue<unsigned int, 0x24>, "EX_CAI_Type::GetArmorPiercingBlast" },
			{ 0x006CA870, &GetShooterValue<float, 0x18>, "EX_CAI_Type::GetFiringRate" },
			{ 0x006CA900, &GetShooterValue<unsigned int, 0x1C>, "EX_CAI_Type::GetBulletsPerMag" },
			{ 0x006CA990, &GetShooterValue<float, 0x30>, "EX_CAI_Type::GetReloadTime" },
			{ 0x006CAA20, &GetShooterValue<float, 0x10>, "EX_CAI_Type::GetAccuracy" },
		};
		bool ok = true;
		for (const Getter& getter : getters)
			ok &= ReplaceFunction(getter.myAddress, getter.myReplacement, { 0x56, 0x57, 0x8B, 0xF9, 0x83, 0xBF, 0x20, 0x01 }, getter.myName);
		return ok;
	}

	bool IgnoreAssertions()
	{
		// Each assertion has an "ignore always" flag; setting it skips the assertion, which would
		// otherwise stop the server.
		const uintptr_t ignoreFlags[] =
		{
			0x00959B0A,	// EX_CAI_Type.cpp(281): 0
			0x00960160,	// ex_cai_decisiontree_node_branching.cpp(166): lowest<100000.0f
			0x009598F4,	// EX_AIPlayerContainer.cpp(2246): movementSpeed>0 && movementSpeed<900000
			0x00960023,	// ex_cai_scoutmap.cpp(215): tile>=0
			0x00959BAA,	// ex_cai_militarybrain.cpp(470): guard<100
			0x0096013C,	// ex_cai_supportweaponmanager.cpp(926): length*2<aSupportWeapon->myTargetAreaLength
			0x008F36E8,	// EXG_Projectile.cpp(413): aBlastRadius > 0.0f
			0x008F39A9,	// EXG_MovementSpline.cpp(106): myWaypoints[29].myWaypoint.y >= minHeight
			0x008F3988,	// EXG_CopterMover.cpp(546): minHeight <= destination.y
			0x008F3965,	// WICG_MultiAgentShooterTarget.cpp(147): myNumTargets
			0x008EF13C,	// mc_keytree.h(161): aKey == current->myKey
			0x008F3516,	// mc_keytree.h(161): aKey == current->myKey
			0x008F01ED,	// WICO_HierarchicalHeightMap.cpp(227): iFromGridX in range
			0x008F01EC,	// WICO_HierarchicalHeightMap.cpp(228): iFromGridZ in range
			0x008F01EB,	// WICO_HierarchicalHeightMap.cpp(229): iToGridX in range
			0x008F01EA,	// WICO_HierarchicalHeightMap.cpp(230): iToGridZ in range
			0x008F01E9,	// WICO_HierarchicalHeightMap.cpp(285): ix in range
			0x008F01E8,	// WICO_HierarchicalHeightMap.cpp(286): ix in range
			0x008F01E7,	// WICO_HierarchicalHeightMap.cpp(287): iz in range
			0x008F01E6,	// WICO_HierarchicalHeightMap.cpp(288): iz in range
			0x008F01E5,	// WICO_HierarchicalHeightMap.cpp(402): ix in range
			0x008F01E4,	// WICO_HierarchicalHeightMap.cpp(403): ix in range
			0x008F01E3,	// WICO_HierarchicalHeightMap.cpp(404): iz in range
			0x008F01E2,	// WICO_HierarchicalHeightMap.cpp(405): iz in range
			0x008F3693,	// EXG_Container.cpp(1052): No free slot in container
			0x008F6A4F,	// WICO_StatsManager.cpp(32): aPlayerNum < EX_MAX_NUM_PLAYERS
			0x0095986E,	// EXCO_CloudType.cpp(88): myTimeToLive > 0.0f
			0x0095986C,	// EXCO_CloudType.cpp(91): myRadius > 0.0f
		};
		bool ok = true;
		for (uintptr_t flag : ignoreFlags)
			ok &= PatchBytes(flag, { 0x00 }, { 0x01 }, "assertion flag");
		return ok;
	}
}

bool PatchDedicatedServer(const char* anExeDirectory)
{
	const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleA(NULL));
	const IMAGE_NT_HEADERS* headers = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(dos) + dos->e_lfanew);
	const uintptr_t imageStart = reinterpret_cast<uintptr_t>(dos);
	const uintptr_t imageEnd = imageStart + headers->OptionalHeader.SizeOfImage;
	const bool knownBuild = imageStart == 0x00400000 && BUILD_STRING_ADDRESS + sizeof(BUILD_STRING) <= imageEnd
		&& memcmp(reinterpret_cast<const void*>(BUILD_STRING_ADDRESS), BUILD_STRING, sizeof(BUILD_STRING)) == 0;

	// Settings live in botwar_hook.ini next to wic_ds.exe.
	char iniPath[MAX_PATH];
	strcpy_s(iniPath, anExeDirectory);
	strcat_s(iniPath, "\\botwar_hook.ini");

	// The redirect does not depend on the build, so it is installed either way.
	if (!InstallMassgateRedirect(iniPath))
		return false;

	if (!knownBuild)
	{
		HookLog("Unknown wic_ds.exe build (1.0.1.1 is required); only the Massgate redirect is active.");
		return true;
	}

	const bool bots = AllowRankedBots();
	const bool getters = ReplaceShooterGetters();
	const bool assertions = IgnoreAssertions();
	HookLog("Ranked bots: %s. Commander AI fixes: %s.", bots ? "enabled" : "FAILED", getters && assertions ? "applied" : "partly FAILED");
	InstallBotNames(iniPath);
	return true;
}
