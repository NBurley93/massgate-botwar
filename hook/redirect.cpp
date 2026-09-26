// Resolves the Massgate host names to the local server (or [massgate] host= in botwar_hook.ini),
// in wic_ds.exe and wic.exe. Both resolve names only through gethostbyname.
#include "hook.h"

#include <string.h>
#include <winsock.h>

namespace
{
	typedef hostent* (PASCAL* GetHostByNameFunc)(const char* aName);
	GetHostByNameFunc	ourGetHostByName;
	char				ourMassgateHost[256];

	bool EndsWith(const char* aName, const char* aDomain)
	{
		const size_t nameLength = strlen(aName);
		const size_t domainLength = strlen(aDomain);
		if (nameLength == domainLength)
			return _stricmp(aName, aDomain) == 0;
		return nameLength > domainLength && aName[nameLength - domainLength - 1] == '.'
			&& _stricmp(aName + nameLength - domainLength, aDomain) == 0;
	}

	hostent* PASCAL RedirectedGetHostByName(const char* aName)
	{
		// The domains wic-client redirects: Massgate itself, plus the developer's and publisher's.
		if (aName && (EndsWith(aName, "massgate.net") || EndsWith(aName, "massive.se") || EndsWith(aName, "ubisoft.com")))
		{
			HookLog("Resolving %s as %s.", aName, ourMassgateHost);
			return ourGetHostByName(ourMassgateHost);
		}
		HookLog("Resolving %s (not redirected).", aName ? aName : "(null)");
		return ourGetHostByName(aName);
	}
}

bool InstallMassgateRedirect(const char* anIniPath)
{
	// botwar_hook.ini can point the game and server at another machine:
	//   [massgate]
	//   host=192.168.1.10
	GetPrivateProfileStringA("massgate", "host", "127.0.0.1", ourMassgateHost, sizeof(ourMassgateHost), anIniPath);

	ourGetHostByName = reinterpret_cast<GetHostByNameFunc>(HookImport(GetModuleHandleA(NULL), "WS2_32.dll", "gethostbyname", &RedirectedGetHostByName));
	if (!ourGetHostByName)
	{
		HookLog("Could not hook the gethostbyname import.");
		return false;
	}
	HookLog("Massgate names resolve to %s.", ourMassgateHost);
	return true;
}
