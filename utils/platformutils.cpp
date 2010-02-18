/*=====================================================================
platformutils.cpp
-----------------
File created by ClassTemplate on Mon Jun 06 00:24:52 2005
Code By Nicholas Chapman.
=====================================================================*/
#include "platformutils.h"


#if defined(WIN32) || defined(WIN64)
// Stop windows.h from defining the min() and max() macros
#define NOMINMAX
#include <windows.h>
#include <Iphlpapi.h>
#include <intrin.h>
#include <shlobj.h>
#else
#include <time.h>
#include <unistd.h>
#include <string.h> /* for strncpy */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#endif


#include <cassert>
#include "../utils/stringutils.h"
#include "../utils/fileutils.h"
#include "../indigo/globals.h"
#include <cstdlib>
#include <iostream>//TEMP
#include <algorithm>

#if defined(OSX)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif


// Make current thread sleep for x milliseconds
void PlatformUtils::Sleep(int x)
{
#if defined(WIN32) || defined(WIN64)
	::Sleep(x);
#else
	int numseconds = x / 1000;
	int fraction = x % 1000;//fractional seconds in ms
	struct timespec t;
	t.tv_sec = numseconds;   //seconds
	t.tv_nsec = fraction * 1000000; //nanoseconds
	nanosleep(&t, NULL);
#endif
}


unsigned int PlatformUtils::getNumLogicalProcessors()
{
#if defined(WIN32) || defined(WIN64)
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);
	return system_info.dwNumberOfProcessors;
#else
	return sysconf(_SC_NPROCESSORS_CONF);
#endif
}


#if defined(WIN32) || defined(WIN64)
#else
#ifndef OSX
// From http://www.creatis.insa-lyon.fr/~malaterre/gdcm/getether/mac_addr_sys.c
long mac_addr_sys ( u_char *addr)
{
    struct ifreq ifr;
    struct ifreq *IFR;
    struct ifconf ifc;
    char buf[1024];
    int s, i;
    int ok = 0;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s==-1) {
        return -1;
    }

    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    ioctl(s, SIOCGIFCONF, &ifc);

    IFR = ifc.ifc_req;
    for (i = ifc.ifc_len / sizeof(struct ifreq); --i >= 0; IFR++) {

        strcpy(ifr.ifr_name, IFR->ifr_name);
        if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0) {
            if (! (ifr.ifr_flags & IFF_LOOPBACK)) {
                if (ioctl(s, SIOCGIFHWADDR, &ifr) == 0) {
                    ok = 1;
                    break;
                }
            }
        }
    }

    close(s);
    if (ok) {
        bcopy( ifr.ifr_hwaddr.sa_data, addr, 6);
    }
    else {
        return -1;
    }
    return 0;
}
#endif
#endif


#if defined(WIN32) || defined(WIN64)
class MyAdapterInfo
{
public:
	std::string MAC;
	DWORD index;
};


inline static bool myAdapterInfoComparisonPred(const MyAdapterInfo& a, const MyAdapterInfo& b)
{
	return a.index < b.index;
}
#endif


void PlatformUtils::getMACAddresses(std::vector<std::string>& addresses_out)
{
#if defined(WIN32) || defined(WIN64)
	IP_ADAPTER_INFO AdapterInfo[16];		// Allocate information
											// for up to 16 NICs
	DWORD dwBufLen = sizeof(AdapterInfo);	// Save memory size of buffer

	const DWORD dwStatus = GetAdaptersInfo(      // Call GetAdapterInfo
		AdapterInfo,	// [out] buffer to receive data
		&dwBufLen		// [in] size of receive data buffer
	);

	if(dwStatus != ERROR_SUCCESS)
		throw PlatformUtilsExcep("GetAdaptersInfo Failed.");

	PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo; // Contains pointer to current adapter info.

	
	std::vector<MyAdapterInfo> adapters;

	while(pAdapterInfo)
	{
		/*std::cout << "--------Details----------" << std::endl;
		std::cout << "AdapterName: " << pAdapterInfo->AdapterName << std::endl;
		std::cout << "Description: " << pAdapterInfo->Description << std::endl;
		std::cout << "Index: " << pAdapterInfo->Index << std::endl;
		std::cout << "-------------------------" << std::endl;*/

		adapters.push_back(MyAdapterInfo());
		for(UINT i = 0; i < pAdapterInfo->AddressLength; i++)
		{
			adapters.back().MAC = adapters.back().MAC + leftPad(toHexString((unsigned char)pAdapterInfo->Address[i]), '0', 2) + ((i < pAdapterInfo->AddressLength-1) ? "-" : "");
		}
		adapters.back().index = pAdapterInfo->Index;

		pAdapterInfo = pAdapterInfo->Next;    // Progress through linked list
	}

	// Sort by index
	std::sort(adapters.begin(), adapters.end(), myAdapterInfoComparisonPred);

	addresses_out.resize(adapters.size());
	for(size_t i=0; i<addresses_out.size(); ++i)
		addresses_out[i] = adapters[i].MAC;

#elif defined(OSX)
	int			mib[6];
	size_t	 	len;
	char			*buf;
	unsigned char		*ptr;
	struct if_msghdr	*ifm;
	struct sockaddr_dl	*sdl;

	mib[0] = CTL_NET;
	mib[1] = AF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_LINK;
	mib[4] = NET_RT_IFLIST;
	if ((mib[5] = if_nametoindex("en1")) == 0) {
		throw PlatformUtilsExcep("if_nametoindex error");
	}

	if (sysctl(mib, 6, NULL, &len, NULL, 0) < 0) {
		throw PlatformUtilsExcep("sysctl 1 error");
	}

	if ((buf = (char *)malloc(len)) == NULL) {
		throw PlatformUtilsExcep("malloc error");
	}

	if (sysctl(mib, 6, buf, &len, NULL, 0) < 0) {
		throw PlatformUtilsExcep("sysctl 2 error");
	}

	ifm = (struct if_msghdr *)buf;
	sdl = (struct sockaddr_dl *)(ifm + 1);
	ptr = (unsigned char *)LLADDR(sdl);

	char buffer[100];

	sprintf(buffer, "%02x-%02x-%02x-%02x-%02x-%02x", *ptr, *(ptr+1), *(ptr+2), *(ptr+3), *(ptr+4), *(ptr+5));

	addresses_out.resize(0);
	addresses_out.push_back(std::string(buffer));

#else
	// else Linux
	addresses_out.resize(1);
	unsigned char addr[6];
	if(mac_addr_sys(addr) != 0)
		throw PlatformUtilsExcep("mac_addr_sys failed.");

	for(unsigned i=0; i<6; ++i)
	{
		addresses_out.back() = addresses_out.back() + leftPad(toHexString((unsigned char)addr[i]), '0', 2) + ((i < 5) ? "-" : "");
	}
#endif
}


static void doCPUID(unsigned int infotype, unsigned int* out)
{
#if defined(WIN32) || defined(WIN64)
	int CPUInfo[4];
	__cpuid(
		CPUInfo,
		infotype // infotype
		);
	memcpy(out, CPUInfo, 16);
#else
	// ebx saving is necessary for PIC
	__asm__ volatile(
			"mov %%ebx, %%esi\n\t"
			"cpuid\n\t"
			"xchg %%ebx, %%esi"
            : "=a" (out[0]),
			"=S" (out[1]),
            "=c" (out[2]),
            "=d" (out[3])
            : "0" (infotype)
     );
#endif
}


void PlatformUtils::getCPUInfo(CPUInfo& info_out)
{
	const int MMX_FLAG = 1 << 23;
	const int SSE_FLAG = 1 << 25;
	const int SSE2_FLAG = 1 << 26;
	const int SSE3_FLAG = 1;

	unsigned int CPUInfo[4];
	doCPUID(0, CPUInfo);

	const int highest_param = CPUInfo[0];
	if(highest_param < 1)
		throw PlatformUtilsExcep("CPUID highest_param error.");

	memcpy(info_out.vendor, &CPUInfo[1], 4);
	memcpy(info_out.vendor + 4, &CPUInfo[3], 4);
	memcpy(info_out.vendor + 8, &CPUInfo[2], 4);
	info_out.vendor[12] = 0;

	doCPUID(1, CPUInfo);

	info_out.mmx = (CPUInfo[3] & MMX_FLAG ) != 0;
	info_out.sse1 = (CPUInfo[3] & SSE_FLAG ) != 0;
	info_out.sse2 = (CPUInfo[3] & SSE2_FLAG ) != 0;
	info_out.sse3 = (CPUInfo[2] & SSE3_FLAG ) != 0;
	info_out.stepping = CPUInfo[0] & 0xF;
	info_out.model = (CPUInfo[0] >> 4) & 0xF;
	info_out.family = (CPUInfo[0] >> 8) & 0xF;

	doCPUID(0x80000000, CPUInfo);

	const unsigned int highest_extended_param = CPUInfo[0];
	if(highest_extended_param < 0x80000004)
		throw PlatformUtilsExcep("CPUID highest_extended_param error.");

	doCPUID(0x80000002, CPUInfo);
	memcpy(info_out.proc_brand + 0, CPUInfo + 0, 4);
	memcpy(info_out.proc_brand + 4, CPUInfo + 1, 4);
	memcpy(info_out.proc_brand + 8, CPUInfo + 2, 4);
	memcpy(info_out.proc_brand + 12, CPUInfo + 3, 4);
	doCPUID(0x80000003, CPUInfo);
	memcpy(info_out.proc_brand + 16, CPUInfo + 0, 4);
	memcpy(info_out.proc_brand + 20, CPUInfo + 1, 4);
	memcpy(info_out.proc_brand + 24, CPUInfo + 2, 4);
	memcpy(info_out.proc_brand + 28, CPUInfo + 3, 4);
	doCPUID(0x80000004, CPUInfo);
	memcpy(info_out.proc_brand + 32, CPUInfo + 0, 4);
	memcpy(info_out.proc_brand + 36, CPUInfo + 1, 4);
	memcpy(info_out.proc_brand + 40, CPUInfo + 2, 4);
	memcpy(info_out.proc_brand + 44, CPUInfo + 3, 4);
}


const std::string PlatformUtils::getAPPDataDirPath() // throws PlatformUtilsExcep
{
#if defined(WIN32) || defined(WIN64)
	TCHAR path[MAX_PATH];
	const HRESULT res = SHGetFolderPath(
		NULL, // hwndOwner
		CSIDL_APPDATA, // nFolder
		NULL, // hToken
		SHGFP_TYPE_CURRENT, // dwFlags
		path // pszPath
		);

	if(res != S_OK)
		throw PlatformUtilsExcep("SHGetFolderPath() failed, Error code: " + toString(res));

	return StringUtils::WToUTF8String(path);
#else
	throw PlatformUtilsExcep("getAPPDataDirPath() is only valid on Windows.");
#endif
}


const std::string PlatformUtils::getTempDirPath() // throws PlatformUtilsExcep
{
#if defined(WIN32) || defined(WIN64)
	TCHAR path[MAX_PATH];
	const DWORD num_chars_rqrd = GetTempPath(
		MAX_PATH,
		path
	);

	if(num_chars_rqrd == 0 || num_chars_rqrd > MAX_PATH)
		throw PlatformUtilsExcep("GetTempPath() failed.");

	const std::string p = StringUtils::WToUTF8String(path);
	return ::eatSuffix(p, "\\"); // Remove trailing backslash.
#elif defined(OSX)
	return "/tmp";
#else
	// Linux
	return "/tmp";
#endif
}


const std::string PlatformUtils::getOrCreateAppDataDirectory(const std::string& app_base_path, const std::string& app_name)
{
#if defined(WIN32) || defined(WIN64)
	// e.g. C:\Users\Nicolas Chapman\AppData\Roaming
	const std::string appdatapath_base = PlatformUtils::getAPPDataDirPath();

	// e.g. C:\Users\Nicolas Chapman\AppData\Roaming\Indigo Renderer
	const std::string appdatapath = FileUtils::join(appdatapath_base, app_name);

	// Create the dir if it doesn't exist
	try
	{
		if(!FileUtils::fileExists(appdatapath))
			FileUtils::createDir(appdatapath);
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		throw PlatformUtilsExcep(e.what());
	}
	return appdatapath;
#else
	return app_base_path;
#endif
}


// http://stackoverflow.com/questions/143174/c-c-how-to-obtain-the-full-path-of-current-directory
const std::string PlatformUtils::getFullPathToCurrentExecutable() // throws PlatformUtilsExcep, only works on Windows.
{
#if defined(WIN32) || defined(WIN64)
	TCHAR buf[2048];
	const DWORD result = GetModuleFileName(
		NULL, // hModule "If this parameter is NULL, GetModuleFileName retrieves the path of the executable file of the current process."
		buf,
		2048
		);

	if(result == 0)
		throw PlatformUtilsExcep("GetModuleFileName failed.");
	else
		return StringUtils::WToUTF8String(buf);
#else
	throw PlatformUtilsExcep("getFullPathToCurrentExecutable only supported on Windows.");
#endif
}


int PlatformUtils::execute(const std::string& command)
{
	return std::system(command.c_str());
}


void PlatformUtils::openFileBrowserWindowAtLocation(const std::string& select_path)
{
#if defined(WIN32) || defined(WIN64)

	//execute("Explorer.exe /select," + select_path + "");

	STARTUPINFO startupInfo;
	ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);
	PROCESS_INFORMATION procInfo;

	if(CreateProcess(
		NULL, // L"explorer", // app name
		const_cast<wchar_t*>(StringUtils::UTF8ToPlatformUnicodeEncoding("explorer /select," + FileUtils::toPlatformSlashes(select_path)).c_str()), // command line
		NULL,
		NULL,
		0,
		0,
		NULL,
		NULL,
		&startupInfo,
		&procInfo
		) == 0)
	{
		// Failure
		throw PlatformUtilsExcep("openFileBrowserWindowAtLocation Failed: error code: " + toString((unsigned int)GetLastError()));
	}

	/*const HINSTANCE res = ShellExecute(
		NULL,
		L"explore", // operation
		StringUtils::UTF8ToPlatformUnicodeEncoding(select_path).c_str(), // file
		NULL, // parameters
		NULL, // directory
		SW_SHOWNORMAL // cmd
	);

	// "If the function succeeds, it returns a value greater than 32." - http://msdn.microsoft.com/en-us/library/bb762153%28VS.85%29.aspx
	if((int)res <= 32)
	{
		const int r = (int)res;
		if(r == ERROR_FILE_NOT_FOUND)
			throw PlatformUtilsExcep("ERROR_FILE_NOT_FOUND");
		else if(r == ERROR_PATH_NOT_FOUND)
			throw PlatformUtilsExcep("ERROR_PATH_NOT_FOUND");
		else if(r == ERROR_BAD_FORMAT)
			throw PlatformUtilsExcep("ERROR_BAD_FORMAT");
		else if(r == SE_ERR_ACCESSDENIED)
			throw PlatformUtilsExcep("SE_ERR_ACCESSDENIED");
		else if(r == SE_ERR_ASSOCINCOMPLETE)
			throw PlatformUtilsExcep("SE_ERR_ASSOCINCOMPLETE");
		else if(r == SE_ERR_FNF)
			throw PlatformUtilsExcep("SE_ERR_FNF");
		else if(r == SE_ERR_NOASSOC)
			throw PlatformUtilsExcep("SE_ERR_NOASSOC");
		else if(r == SE_ERR_PNF)
			throw PlatformUtilsExcep("SE_ERR_PNF");
		else if(r == SE_ERR_SHARE)
			throw PlatformUtilsExcep("SE_ERR_SHARE");
		else
			throw PlatformUtilsExcep("openFileBrowserWindowAtLocation: Unknown error");
	}*/


#elif defined(OSX)

	// Uses applescript
	std::string command = "osascript -e 'tell application \"Finder\" to activate' -e 'tell application \"Finder\" to reveal POSIX file \"" + select_path + "\"'";

	system(command.c_str());

#else
	// Linux
	throw PlatformUtilsExcep("openFileBrowserWindowAtLocation not available on Linux.");
#endif
}


const std::string PlatformUtils::getLastErrorString()
{
#if defined(WIN32) || defined(WIN64)
	std::vector<wchar_t> buf(2048);

	const DWORD result = FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, // See http://blogs.msdn.com/oldnewthing/archive/2007/11/28/6564257.aspx on why FORMAT_MESSAGE_IGNORE_INSERTS should be used.
		NULL, // source
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		&buf[0],
		buf.size(),
		NULL // arguments
	);
	if(result == 0)
		return "[Unknown (error code=" + toString((int)GetLastError()) + "]";
	else
		return StringUtils::PlatformToUTF8UnicodeEncoding(std::wstring(&buf[0])) + " (error code=" + toString((int)GetLastError()) + ")";
#else
#error implement me!
#endif
}


void PlatformUtils::testPlatformUtils()
{
	try
	{
		//openFileBrowserWindowAtLocation("C:\\testscenes");
		//openFileBrowserWindowAtLocation("C:\\testscenes\\sun_glare_test.igs");
	}
	catch(PlatformUtilsExcep& e)
	{
		conPrint(e.what());
	}
}
