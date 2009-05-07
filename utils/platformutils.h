/*=====================================================================
platformutils.h
---------------
File created by ClassTemplate on Mon Jun 06 00:24:52 2005
Code By Nicholas Chapman.
=====================================================================*/
#ifndef __PLATFORMUTILS_H_666_
#define __PLATFORMUTILS_H_666_


#include <vector>
#include <string>


/*=====================================================================
PlatformUtils
-------------

=====================================================================*/
namespace PlatformUtils
{


class PlatformUtilsExcep
{
public:
	PlatformUtilsExcep(const std::string& s_) : s(s_) {}
	const std::string& what() const { return s; }
private:
	std::string s;
};


void Sleep(int x);//make current thread sleep for x milliseconds


unsigned int getNumLogicalProcessors();


void getMACAddresses(std::vector<std::string>& addresses_out); // throws PlatformUtilsExcep


class CPUInfo
{
public:
	char vendor[13];
	bool mmx, sse1, sse2, sse3;
	unsigned int stepping;
	unsigned int model;
	unsigned int family;
	char proc_brand[48];
};

void getCPUInfo(CPUInfo& info_out); // throws PlatformUtilsExcep

/*
This is something like "C:\Documents and Settings\username\Application Data" on XP, and "C:\Users\Nicolas Chapman\AppData\Roaming" on Vista.
*/
const std::string getAPPDataDirPath(); // throws PlatformUtilsExcep


/*
Returns a directory that is writeable by the app.
On Vista, this can't be indigo_base_dir_path, because that path might be in program files, and so won't be writeable.
*/
const std::string getOrCreateAppDataDirectory(const std::string& app_base_path, const std::string& app_name); // throws PlatformUtilsExcep

const std::string getFullPathToCurrentExecutable(); // throws PlatformUtilsExcep, only works on Windows.

}//end namespace PlatformUtils


#endif //__PLATFORMUTILS_H_666_
