/*=====================================================================
platformutils.cpp
-----------------
File created by ClassTemplate on Mon Jun 06 00:24:52 2005
Code By Nicholas Chapman.
=====================================================================*/
#include "platformutils.h"

#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#else
#include <time.h>
#endif


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

unsigned int PlatformUtils::getNumLogicalProcessors()
{
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);
	return system_info.dwNumberOfProcessors;

	//on linux: /proc/cpuinfo
}