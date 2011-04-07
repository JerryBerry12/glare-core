/*=====================================================================
FileChecksum.cpp
-------------------
Copyright Glare Technologies Limited 2010 -
Generated at Tue Mar 02 14:36:09 +1300 2010
=====================================================================*/
#include "FileChecksum.h"


#include "Exception.h"
#include "fileutils.h"
#include <vector>
#include <zlib.h>


namespace FileChecksum
{


uint32 fileChecksum(const std::string& p) // throws Indigo::Exception if file not found.
{
	try
	{
		std::vector<unsigned char> contents;
		FileUtils::readEntireFile(p, contents);

		if(contents.size() == 0)
			throw Indigo::Exception("Failed to compute checksum over file '" + p + ", file is empty.");

		const unsigned int initial_crc = crc32(0, 0, 0);
		return crc32(initial_crc, &contents[0], (unsigned int)(contents.size() * sizeof(unsigned char)));
	}
	catch(FileUtils::FileUtilsExcep& e)
	{
		throw Indigo::Exception(e.what());
	}
}


}
