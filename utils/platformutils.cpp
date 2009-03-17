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
#include <iostream> //TEMP


//make current thread sleep for x milliseconds
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

/*
unsigned int PlatformUtils::getMinWorkingSetSize()
{
	SIZE_T min, max;
	GetProcessWorkingSetSize(GetCurrentProcess(), &min, &max);

	return (unsigned int)min;
}
unsigned int PlatformUtils::getMaxWorkingSetSize()
{
	SIZE_T min, max;
	GetProcessWorkingSetSize(GetCurrentProcess(), &min, &max);

	return (unsigned int)max;
}
*/

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

	addresses_out.resize(0);
	while(pAdapterInfo)
	{
		addresses_out.push_back("");
		for(UINT i = 0; i < pAdapterInfo->AddressLength; i++)
		{
			addresses_out.back() = addresses_out.back() + leftPad(toHexString((unsigned char)pAdapterInfo->Address[i]), '0', 2) + ((i < pAdapterInfo->AddressLength-1) ? "-" : "");
		}

		pAdapterInfo = pAdapterInfo->Next;    // Progress through linked list
	}
#else
	int fd = socket(AF_INET, SOCK_DGRAM, 0);

	if(fd == -1)
		throw PlatformUtilsExcep("socket() Failed.");

	struct ifreq ifr;
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1); // Assuming we want eth0

	ioctl(fd, SIOCGIFHWADDR, &ifr); // Retrieve MAC address

	close(fd);

	addresses_out.resize(1);
	for(unsigned i=0; i<6; ++i)
	{
		addresses_out.back() = addresses_out.back() + leftPad(toHexString((unsigned char)ifr.ifr_hwaddr.sa_data[i]), '0', 2) + ((i < 5) ? "-" : "");
	}
#endif
}


#define cpuid(in,a,b,c,d)\
  asm("cpuid": "=a" (a), "=b" (b), "=c" (c), "=d" (d) : "a" (in));


void PlatformUtils::getCPUInfo(CPUInfo& info_out)
{
#if defined(WIN32) || defined(WIN64)
	int CPUInfo[4];
	__cpuid(
		CPUInfo,
		1 //infotype
		);

	const int MMX_FLAG = 1 << 23;
	const int SSE_FLAG = 1 << 25;
	const int SSE2_FLAG = 1 << 26;
	const int SSE3_FLAG = 1;
	info_out.mmx = (CPUInfo[3] & MMX_FLAG ) != 0;
	info_out.sse1 = (CPUInfo[3] & SSE_FLAG ) != 0;
	info_out.sse2 = (CPUInfo[3] & SSE2_FLAG ) != 0;
	info_out.sse3 = (CPUInfo[2] & SSE3_FLAG ) != 0;
#else
	unsigned int a, b, c, d;
	unsigned int in = 0;
	cpuid(0, a, b, c, d);

	std::cout << a << std::endl;
	std::cout << b << std::endl;
	std::cout << c << std::endl;
	std::cout << d << std::endl;

	cpuid(1, a, b, c, d);

	info_out.mmx = (d & MMX_FLAG) != 0;
	info_out.sse1 = (d & SSE_FLAG ) != 0;
	info_out.sse2 = (d & SSE2_FLAG ) != 0;
	info_out.sse3 = (c & SSE3_FLAG ) != 0;
#endif
}
