/*=====================================================================
License.h
---------
File created by ClassTemplate on Thu Mar 19 14:06:32 2009
=====================================================================*/
#ifndef __LICENSE_H_666_
#define __LICENSE_H_666_


#include <string>


/*=====================================================================
License
-------

=====================================================================*/
class License
{
public:

	class LicenseExcep
	{
	public:
		LicenseExcep(const std::string& s_) : s(s_) {}
		const std::string& what() const { return s; }
	private:
		std::string s;
	};


	enum LicenceType
	{
		FULL
	};

	static const std::string licenseTypeToString(LicenceType t)
	{
		if(t == FULL)
			return "Full";
		else
			return "[Unknown]";
	}

	static bool verifyLicense(const std::string& indigo_base_path, LicenceType& license_type_out, std::string& user_id_out); // throws LicenseExcep

	// A combination of the CPU type and MAC address
	static const std::string getHardwareIdentifier(); // throws LicenseExcep

};


#endif //__LICENSE_H_666_
