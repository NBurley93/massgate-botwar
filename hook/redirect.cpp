// Network lockdown for wic_ds.exe and wic.exe.
//
// Name lookups: the Massgate host names resolve to the local server ([massgate] host= in
// botwar_hook.ini, default 127.0.0.1); IP addresses, localhost, this computer's names and the
// names in [network] allow= resolve normally; everything else fails. The game would otherwise
// fetch from the domains of the original service and its patch mirrors, whose owners are
// unknown today (its patch manifest lists executables there, and the launcher runs patches).
// Both programs resolve names only through gethostbyname.
//
// Opening things (ShellExecuteA, used for links and to start downloaded patches): only http(s)
// links to the hosts in [network] openurls= (default github.com) and existing files inside the
// game folder are opened. The Massgate banner link opens [network] bannerurl= instead.
#include "hook.h"

#include <shellapi.h>
#include <string.h>
#include <string>
#include <vector>
#include <winsock.h>

namespace
{
	typedef hostent* (PASCAL* GetHostByNameFunc)(const char* aName);
	typedef HINSTANCE (WINAPI* ShellExecuteAFunc)(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT);

	GetHostByNameFunc			ourGetHostByName;
	ShellExecuteAFunc			ourShellExecuteA;
	char						ourMassgateHost[256];
	std::vector<std::string>	ourAllowedNames;		// resolve normally
	std::vector<std::string>	ourAllowedUrlHosts;		// may be opened in a browser
	std::string					ourGameDirectory;		// with a trailing backslash
	std::string					ourBannerUrl;			// opened for the Massgate banner link

	bool EndsWith(const char* aName, const char* aDomain)
	{
		const size_t nameLength = strlen(aName);
		const size_t domainLength = strlen(aDomain);
		if (nameLength == domainLength)
			return _stricmp(aName, aDomain) == 0;
		return nameLength > domainLength && aName[nameLength - domainLength - 1] == '.'
			&& _stricmp(aName + nameLength - domainLength, aDomain) == 0;
	}

	std::vector<std::string> ReadList(const char* aSection, const char* aKey, const char* aDefault, const char* anIniPath)
	{
		char value[1024];
		GetPrivateProfileStringA(aSection, aKey, aDefault, value, sizeof(value), anIniPath);
		std::vector<std::string> items;
		char* context = NULL;
		for (char* item = strtok_s(value, ", ", &context); item; item = strtok_s(NULL, ", ", &context))
			items.push_back(item);
		return items;
	}

	bool IsIpAddress(const char* aName)
	{
		int dots = 0;
		for (const char* c = aName; *c; c++)
		{
			if (*c == '.')
				dots++;
			else if (*c < '0' || *c > '9')
				return false;
		}
		return dots == 3;
	}

	bool IsMassgateName(const char* aName)
	{
		// The domains wic-client redirects: Massgate itself, plus the developer's and publisher's.
		return EndsWith(aName, "massgate.net") || EndsWith(aName, "massive.se") || EndsWith(aName, "ubisoft.com");
	}

	bool IsAllowedName(const char* aName)
	{
		if (IsIpAddress(aName) || _stricmp(aName, "localhost") == 0)
			return true;
		for (const std::string& allowed : ourAllowedNames)
		{
			if (_stricmp(aName, allowed.c_str()) == 0)
				return true;
		}
		return false;
	}

	hostent* PASCAL FilteredGetHostByName(const char* aName)
	{
		if (!aName)
			return ourGetHostByName(aName);
		if (IsMassgateName(aName))
		{
			HookLog("Resolving %s as %s.", aName, ourMassgateHost);
			return ourGetHostByName(ourMassgateHost);
		}
		if (IsAllowedName(aName))
			return ourGetHostByName(aName);

		HookLog("Blocked looking up %s.", aName);
		SetLastError(WSAHOST_NOT_FOUND);	// what WSASetLastError does
		return NULL;
	}

	bool IsAllowedUrl(const char* aUrl)
	{
		const char* host = NULL;
		if (_strnicmp(aUrl, "http://", 7) == 0)
			host = aUrl + 7;
		else if (_strnicmp(aUrl, "https://", 8) == 0)
			host = aUrl + 8;
		if (!host)
			return false;
		const std::string hostName(host, strcspn(host, "/:?#"));
		// No credentials or other tricks in front of the host.
		if (hostName.find('@') != std::string::npos || hostName.empty())
			return false;
		for (const std::string& allowed : ourAllowedUrlHosts)
		{
			if (EndsWith(hostName.c_str(), allowed.c_str()))
				return true;
		}
		return false;
	}

	bool IsGameFile(const char* aPath, const char* aDirectory)
	{
		char full[MAX_PATH];
		std::string path = aPath;
		const bool isRelative = !(aPath[0] == '\\' || aPath[0] == '/' || (aPath[0] && aPath[1] == ':'));
		if (aDirectory && *aDirectory && isRelative)
			path = std::string(aDirectory) + "\\" + aPath;
		const DWORD length = GetFullPathNameA(path.c_str(), MAX_PATH, full, NULL);
		if (!length || length >= MAX_PATH)
			return false;
		const DWORD attributes = GetFileAttributesA(full);
		return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)
			&& _strnicmp(full, ourGameDirectory.c_str(), ourGameDirectory.size()) == 0;
	}

	// The launcher and main menu banners link to www.massgate.net/from_ingame/redirect_banner.php
	// (with or without http://); that link opens [network] bannerurl= instead.
	bool IsMassgateBannerLink(const char* aTarget)
	{
		const char* host = aTarget;
		if (_strnicmp(host, "http://", 7) == 0)
			host += 7;
		else if (_strnicmp(host, "https://", 8) == 0)
			host += 8;
		const char* path = strchr(host, '/');
		if (!path)
			return false;
		const std::string hostName(host, path - host);
		return IsMassgateName(hostName.c_str()) && _strnicmp(path, "/from_ingame/redirect_banner.php", 32) == 0;
	}

	HINSTANCE WINAPI FilteredShellExecuteA(HWND aWindow, LPCSTR anOperation, LPCSTR aFile, LPCSTR someParameters, LPCSTR aDirectory, INT aShowCommand)
	{
		if (aFile && IsMassgateBannerLink(aFile) && !ourBannerUrl.empty())
		{
			HookLog("Banner link %s opens %s instead.", aFile, ourBannerUrl.c_str());
			aFile = ourBannerUrl.c_str();
			someParameters = NULL;
		}
		if (aFile && (IsAllowedUrl(aFile) || (!strstr(aFile, "://") && IsGameFile(aFile, aDirectory))))
		{
			HookLog("Opening %s.", aFile);
			return ourShellExecuteA(aWindow, anOperation, aFile, someParameters, aDirectory, aShowCommand);
		}
		HookLog("Blocked opening %s%s%s.", aFile ? aFile : "(null)", someParameters && *someParameters ? " with " : "", someParameters && *someParameters ? someParameters : "");
		return reinterpret_cast<HINSTANCE>(static_cast<INT_PTR>(SE_ERR_ACCESSDENIED));
	}
}

bool InstallMassgateRedirect(const char* anIniPath)
{
	// botwar_hook.ini can point the game and server at another machine, and widen the lockdown:
	//   [massgate]
	//   host=192.168.1.10
	//   [network]
	//   allow=my-lan-server, another-name
	//   openurls=github.com, example.org
	//   bannerurl=https://example.org/	(what the Massgate banner opens; empty: nothing)
	GetPrivateProfileStringA("massgate", "host", "127.0.0.1", ourMassgateHost, sizeof(ourMassgateHost), anIniPath);
	ourAllowedNames = ReadList("network", "allow", "", anIniPath);
	ourAllowedUrlHosts = ReadList("network", "openurls", "github.com", anIniPath);
	char bannerUrl[1024];
	GetPrivateProfileStringA("network", "bannerurl", "https://github.com/NBurley93/massgate-botwar", bannerUrl, sizeof(bannerUrl), anIniPath);
	ourBannerUrl = bannerUrl;

	char computerName[256];
	DWORD size = sizeof(computerName);
	if (GetComputerNameExA(ComputerNameDnsHostname, computerName, &size))
		ourAllowedNames.push_back(computerName);
	size = sizeof(computerName);
	if (GetComputerNameExA(ComputerNameDnsFullyQualified, computerName, &size))
		ourAllowedNames.push_back(computerName);
	size = sizeof(computerName);
	if (GetComputerNameA(computerName, &size))
		ourAllowedNames.push_back(computerName);
	// The Massgate host itself may be a name.
	if (!IsIpAddress(ourMassgateHost))
		ourAllowedNames.push_back(ourMassgateHost);

	ourGameDirectory = anIniPath;
	ourGameDirectory.erase(ourGameDirectory.find_last_of('\\') + 1);

	ourGetHostByName = reinterpret_cast<GetHostByNameFunc>(HookImport(GetModuleHandleA(NULL), "WS2_32.dll", "gethostbyname", &FilteredGetHostByName));
	if (!ourGetHostByName)
	{
		HookLog("Could not hook the gethostbyname import.");
		return false;
	}
	HookLog("Massgate names resolve to %s; other names only if local or allowed.", ourMassgateHost);

	ourShellExecuteA = reinterpret_cast<ShellExecuteAFunc>(HookImport(GetModuleHandleA(NULL), "SHELL32.dll", "ShellExecuteA", &FilteredShellExecuteA));
	if (!ourShellExecuteA)
	{
		HookLog("Could not hook the ShellExecuteA import.");
		return false;
	}
	std::string hosts;
	for (const std::string& host : ourAllowedUrlHosts)
		hosts += (hosts.empty() ? "" : ", ") + host;
	HookLog("Opening only links to %s and files in the game folder.", hosts.empty() ? "(no hosts)" : hosts.c_str());
	return true;
}
