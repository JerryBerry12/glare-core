/*=====================================================================
fileutils.cpp
-------------
File created by ClassTemplate on Sat Mar 01 18:05:09 2003
Code By Nicholas Chapman.
=====================================================================*/
#include "fileutils.h"

#if defined(WIN32) || defined(WIN64)
// Stop windows.h from defining the min() and max() macros
#define NOMINMAX
#include <windows.h>
#else
#include <stdio.h>
#include <sys/stat.h>
#endif

#include <zlib.h>
#include <assert.h>
#include "stringutils.h"
#include "../indigo/TestUtils.h"
#include "../indigo/globals.h"


namespace FileUtils
{


#if defined(WIN32) || defined(WIN64)
const std::string PLATFORM_DIR_SEPARATOR = "\\";
const char PLATFORM_DIR_SEPARATOR_CHAR = '\\';
#else
const std::string PLATFORM_DIR_SEPARATOR = "/";
const char PLATFORM_DIR_SEPARATOR_CHAR = '/';
#endif


const std::string join(const std::string& dirpath, const std::string& filename)
{
	if(dirpath == "")
		return filename;

	if(isPathAbsolute(filename))
		return filename;

	return dirpath + PLATFORM_DIR_SEPARATOR + filename;
}


const std::string toPlatformSlashes(const std::string& pathname)
{
	std::string result = pathname;
	//NOTE: inefficient.
	::replaceChar(result, '\\', PLATFORM_DIR_SEPARATOR_CHAR);
	::replaceChar(result, '/', PLATFORM_DIR_SEPARATOR_CHAR);
	return result;
}


int getNumDirs(const std::string& dirname)
{
	int count = 0;

	for(unsigned int i=0; i<dirname.size(); ++i)
	{
		if(dirname[i] == '\\' || dirname[i] == '/')
			count++;
	}

	return count + 1;
}

/*
const std::string getFirstNDirs(const std::string& dirname, int n)
{
	assert(n >= 1);

	//const std::string& path = toPlatformSlashes(dirname);



	int nthslashpos = 0;

	for(int i=0; i<n && nthslashpos != std::string::npos; ++i)
	{
		if(i == 0)
		{
			nthslashpos = dirname.find_first_of('\\');

			if(nthslashpos == std::string::npos)
				nthslashpos = dirname.find_first_of('/');
		}
		else
		{
			int lastnthslashpos = nthslashpos;
			nthslashpos = dirname.find_first_of('\\', lastnthslashpos + 1);

			if(nthslashpos == std::string::npos)
				nthslashpos = dirname.find_first_of('/', lastnthslashpos + 1);
		}
	}

	if(nthslashpos != std::string::npos)
	{
		return dirname.substr(0, nthslashpos);
	}
	else
		return dirname;

		//if(firstslashpos == std::string::npos)
		//	firstslashpos = dirname.find_first_of('/');


}*/


/*void splitDirName(const std::string& dirname, std::string& rootdir_out,
						std::string& rest_out)
{
	std::string::size_type firstslashpos = dirname.find_first_of('\\');

	if(firstslashpos == std::string::npos)
		firstslashpos = dirname.find_first_of('/');

	if(firstslashpos == std::string::npos)
	{
		rootdir_out = "";
		rest_out = dirname;
		return;
	}

	rootdir_out = dirname.substr(0, firstslashpos);

	if(dirname.size() > firstslashpos + 1)
		rest_out = dirname.substr(firstslashpos + 1, (int)dirname.size() - (firstslashpos + 1));
	else
		rest_out = "";

	return;
}*/


void createDir(const std::string& dirname)
{
#if defined(WIN32) || defined(WIN64)

	const BOOL result = ::CreateDirectoryA(dirname.c_str(), NULL);
	if(!result)
		throw FileUtilsExcep("Failed to create directory '" + dirname + "'");
#else
	// Create with r/w/x permissions for user and group
	if(mkdir(dirname.c_str(), S_IRWXU | S_IRWXG) != 0)
		throw FileUtilsExcep("Failed to create directory '" + dirname + "'");
#endif
}

void createDirsForPath(const std::string& path)
{
	std::vector<std::string> dirs;
	getDirectoriesFromPath(path, dirs);

	std::string dir;
	for(unsigned int i=0; i<dirs.size(); ++i)
	{
		//if(i > 0 || !isPathAbsolute(path)) // don't create first
		//{

		dir += dirs[i];
		if(!(fileExists(dir) || hasSuffix(dir, ":")))
			createDir(dir);

		dir += PLATFORM_DIR_SEPARATOR;
	}
}


void createDirIfDoesNotExist(const std::string& dirname)
{
	if(!fileExists(dirname))
		createDir(dirname);
}


/*
#if defined(WIN32) || defined(WIN64)
bool createDir(const std::string& dirname)
{
	bool created_dir = false;

	const int numdirs = getNumDirs(dirname);

	for(int i=1; i<=numdirs; ++i)
	{
		const std::string firstndirs = getFirstNDirs(dirname, i);

		if(!dirExists(firstndirs))
		{
			const BOOL result = ::CreateDirectoryA(firstndirs.c_str(), NULL);

			assert(result);

			created_dir = true;
		}
	}

	return created_dir;
}
#endif
*/






	/*std::string dirname = dirname_;
	bool created_dir = false;

	while(1)
	{
		std::string rootdir, restdirs;

		splitDirName(dirname, rootdir, restdirs);

		if(rootdir == "")
		{
			if(restdirs != "")
				if(!dirExists(restdirs))
				{
					const BOOL result = ::CreateDirectory(restdirs.c_str(), NULL);

					assert(result);

					created_dir = true;
				}

			break;
		}
		else
		{
			if(!dirExists(rootdir))
			{
				const BOOL result = ::CreateDirectory(rootdir.c_str(), NULL);

				assert(result);

				created_dir = true;
			}
		}

		dirname = restdirs;//recurse
	}

	return created_dir;

//	return (bool)::CreateDirectory(dirname.c_str(), NULL);
}*/



const std::string getDirectory(const std::string& pathname_)
{
	const std::string path = toPlatformSlashes(pathname_);

	const std::string::size_type lastslashpos = path.find_last_of(PLATFORM_DIR_SEPARATOR_CHAR);

	if(lastslashpos == std::string::npos)
		return "";

	return path.substr(0, lastslashpos);


	/*std::string forwardslashpath = pathname;
	::replaceChar(forwardslashpath, '\\', '/');

	int lastslashpos = pathname.find_last_of('/');

	if(lastslashpos == std::string::npos)
		return "";

	assert(lastslashpos < pathname.size());

	return pathname.substr(0, lastslashpos+1);*/



	/*int lastslashpos = pathname.find_last_of('\\');

	if(lastslashpos == std::string::npos)
		lastslashpos = pathname.find_last_of('/');

	if(lastslashpos == std::string::npos)
		return "";

	assert(lastslashpos < pathname.size());

	return pathname.substr(0, lastslashpos+1);*/
}


const std::string getFilename(const std::string& pathname)
{
	//-----------------------------------------------------------------
	//replace all \'s with /'s.
	//-----------------------------------------------------------------
	std::string pathname_copy = pathname;
	replaceChar(pathname_copy, '\\', '/');


	const int lastslashpos = pathname_copy.find_last_of('/');

	if(lastslashpos == std::string::npos)//if no slashes
	{
		return pathname;
	}
	else
	{
		const int filenamepos = lastslashpos + 1;

		if(filenamepos < (int)pathname.size())
			return pathname.substr(filenamepos, (int)pathname.size() - filenamepos);
		else
			return "";
	}
}


/*
//creates the directory the file is in according to pathname
//returns false if fails or directory already exists
#if defined(WIN32) || defined(WIN64)
bool createDirForPathname(const std::string& pathname)
{
	std::string dir = getDirectory(pathname);

	if(dir != "")
	{
		//lopp of '\' or '/'
		if(!dir.empty() && dir[(int)dir.size() - 1] == '\\' || dir[(int)dir.size() - 1] == '/')
			dir = dir.substr(0, (int)dir.size() - 1);

		return createDir(dir);
	}
	else
	{
		return false;//cause already exists
	}
}
#endif
*/

/*
#if defined(WIN32) || defined(WIN64)
bool dirExists(const std::string& dirname)
{
	WIN32_FIND_DATA find_data;
	HANDLE search_handle = FindFirstFileA(dirname.c_str(), &find_data);

	const bool foundit = search_handle != INVALID_HANDLE_VALUE;

	//FIXME: should check this is actually a dir

	FindClose(search_handle);

	return foundit;

}
#endif
*/

bool fileExists(const std::string& pathname)
{
#if defined(WIN32) || defined(WIN64)
	WIN32_FIND_DATA find_data;
	HANDLE search_handle = FindFirstFile(pathname.c_str(), &find_data);

	const bool foundit = search_handle != INVALID_HANDLE_VALUE;

	if(foundit)
		FindClose(search_handle);

	return foundit;
#else
	struct stat buffer;
	const int status = stat(pathname.c_str(), &buffer);
	return status == 0;
#endif
}




void getDirectoriesFromPath(const std::string& pathname_, std::vector<std::string>& dirs_out)
{
	dirs_out.clear();


	//-----------------------------------------------------------------
	//replace all /'s with \'s.
	//-----------------------------------------------------------------
	std::string pathname = pathname_;
	replaceChar(pathname, '/', '\\');


	int startpos = 0;

	while(1)
	{

		const std::string::size_type slashpos = pathname.find_first_of('\\', startpos);

		if(slashpos == std::string::npos)
			break;

		assert(slashpos - startpos >= 0);
		assert(startpos >= 0);
		assert(slashpos < pathname.size());
		dirs_out.push_back(pathname.substr(startpos, slashpos - startpos));

		startpos = slashpos + 1;
	}
}


/*bool isPathEqualOrLower(const std::string& pathname)
{
	std::vector<std::string> dirs;
	FileUtils::getDirs(pathname, dirs);

	int relative_height = 0;//positive means higher up file tree (root at top)

	for(unsigned int i=0; i<dirs.size(); ++i)
	{
		if(dirs[i] == "..")
		{
			relative_height++;
		}
		else if(dirs[i] == ".")
		{
		}
		else
		{
			relative_height--;
		}
	}

	return relative_height <= 0;
}*/

bool isPathSafe(const std::string& pathname)
{
	if(pathname.size() == 0)
		return false;

	if(isPathAbsolute(pathname))
		return false;

	std::vector<std::string> dirs;
	getDirectoriesFromPath(pathname, dirs);
	for(unsigned int i=0; i<dirs.size(); ++i)
		if(dirs[i] == "..")
			return false;

	return true;

	//int charindex = 0;

	//if(pathname[0] == '\\')//can't start with backslash
	//	return false;

	/*bool lastcharwasslash = true;//treat as starting with backslash

	for(unsigned int i=0; i<pathname.size(); ++i)
	{
		if(pathname[i] == '\\')
		{
			if(lastcharwasslash)
				return false;//don't allow two in a row

			lastcharwasslash = true;
		}
		else if(::isAlphaNumeric(pathname[i]) || pathname[i] == '_' ||
				pathname[i] == '.' || pathname[i] == '-' || pathname[i] == '+')
		{
			lastcharwasslash = false;
		}
		else
		{
			//invalid char
			return false;
		}
	}

	if(lastcharwasslash)
		return false;

	return isPathEqualOrLower(pathname);*/
}


void readEntireFile(std::ifstream& file, std::string& filecontents_out)
{
	filecontents_out = "";

	if(!file)
		return;

	file.seekg(0, std::ios_base::end);

	const int filesize = file.tellg();

	file.seekg(0, std::ios_base::beg);

	filecontents_out.resize(filesize);

	if(filecontents_out.size() > 0)
		file.read(&(*filecontents_out.begin()), filesize);
}

void readEntireFile(std::ifstream& file, std::vector<unsigned char>& filecontents_out)
{
	filecontents_out.resize(0);

	if(!file)
		return;

	file.seekg(0, std::ios_base::end);

	const int filesize = file.tellg();

	file.seekg(0, std::ios_base::beg);

	filecontents_out.resize(filesize);

	if(filecontents_out.size() > 0)
		file.read((char*)&(*filecontents_out.begin()), filesize);
}


void readEntireFile(const std::string& pathname,
					std::string& filecontents_out)
{
	std::ifstream infile(pathname.c_str(), std::ios::binary);

	if(!infile)
		throw FileUtilsExcep("could not open '" + pathname + "' for reading.");

	readEntireFile(infile, filecontents_out);
}

void readEntireFile(const std::string& pathname,
					std::vector<unsigned char>& filecontents_out)
{
	std::ifstream infile(pathname.c_str(), std::ios::binary);

	if(!infile)
		throw FileUtilsExcep("could not open '" + pathname + "' for reading.");

	readEntireFile(infile, filecontents_out);
}


void writeEntireFile(const std::string& pathname,
					 const std::vector<unsigned char>& filecontents)
{
	std::ofstream file(pathname.c_str(), std::ios::binary);

	if(!file)
		throw FileUtilsExcep("Could not open '" + pathname + "' for writing.");

	if(filecontents.size() > 0)
		file.write((const char*)&(*filecontents.begin()), filecontents.size());

	if(file.bad())
		throw FileUtilsExcep("Write to '" + pathname + "' failed.");
}


void writeEntireFile(const std::string& pathname,
					 const std::string& filecontents)
{
	std::ofstream file(pathname.c_str(), std::ios::binary);

	if(!file)
		throw FileUtilsExcep("Could not open '" + pathname + "' for writing.");

	if(filecontents.size() > 0)
		file.write(filecontents.c_str(), filecontents.size());

	if(file.bad())
		throw FileUtilsExcep("Write to '" + pathname + "' failed.");
}



const std::string dayOfWeek(int day)
{
	if(day == 0) return "Sun";
	else if(day == 1) return "Mon";
	else if(day == 2) return "Tue";
	else if(day == 3) return "Wed";
	else if(day == 4) return "Thu";
	else if(day == 5) return "Fri";
	else if(day == 6) return "Sat";
	else
	{
		assert(0);
		return "";
	}
}

const std::string getMonthString(int month)
{
	if(month == 1) return "Jan";
	else if(month == 2) return "Feb";
	else if(month == 3) return "Mar";
	else if(month == 4) return "Apr";
	else if(month == 5) return "May";
	else if(month == 6) return "Jun";
	else if(month == 7) return "Jul";
	else if(month == 8) return "Aug";
	else if(month == 9) return "Sep";
	else if(month == 10) return "Oct";
	else if(month == 11) return "Nov";
	else if(month == 12) return "Dec";
	else
	{
		assert(0);
		return "";
	}
}

const std::string widenToTwoChars(const std::string& numeral)
{
	assert(numeral.size() >= 1 && numeral.size() <= 2);

	if(numeral.size() == 1)
		return "0" + numeral;
	else
		return numeral;
}




#if defined(WIN32) || defined(WIN64)
const std::string getCurrentDir()
{
/*DWORD GetCurrentDirectory(
  DWORD nBufferLength,  // size, in characters, of directory buffer
  LPTSTR lpBuffer       // pointer to buffer for current directory
);
*/

	char buf[512];

	GetCurrentDirectoryA(511, buf);

	return std::string(buf);
}

#endif


void copyFile(const std::string& srcpath, const std::string& dstpath)
{
#if defined(WIN32) || defined(WIN64)
	if(!CopyFileA(
		srcpath.c_str(),
		dstpath.c_str(),
		FALSE // fail if exists
		))
	{
		throw FileUtilsExcep("Failed to copy file '" + srcpath + "' to '" + dstpath + "'.");
	}

#else
	std::vector<unsigned char> data;
	readEntireFile(srcpath, data);
	writeEntireFile(dstpath, data);
#endif
}


void deleteFile(const std::string& path)
{
#if defined(WIN32) || defined(WIN64)
	if(!DeleteFileA(
		path.c_str()
		))
	{
		throw FileUtilsExcep("Failed to delete file '" + path + "'.");
	}

#else
	if(remove(path.c_str()) != 0)
		throw FileUtilsExcep("Failed to delete file '" + path + "'.");
#endif
}

void deleteEmptyDirectory(const std::string& path)
{
#if defined(WIN32) || defined(WIN64)
	if(!RemoveDirectoryA(
		path.c_str()
		))
	{
		throw FileUtilsExcep("Failed to delete directory '" + path + "'.");
	}

#else
	if(rmdir(path.c_str()) != 0)
		throw FileUtilsExcep("Failed to delete directory '" + path + "'.");
#endif
}

void moveFile(const std::string& srcpath, const std::string& dstpath)
{
#if defined(WIN32) || defined(WIN64)
	if(!MoveFileExA(srcpath.c_str(), dstpath.c_str(), MOVEFILE_REPLACE_EXISTING))
		throw FileUtilsExcep("Failed to move file '" + srcpath + "' to '" + dstpath + "'.");
#else
	if(rename(srcpath.c_str(), dstpath.c_str()) != 0)
		throw FileUtilsExcep("Failed to move file '" + srcpath + "' to '" + dstpath + "'.");
#endif
}



bool isPathAbsolute(const std::string& p)
{
#if defined(WIN32) || defined(WIN64)
	return (p.length() > 1 && hasPrefix(p.substr(1, p.length()-1), ":")) ||// e.g. C:/programming/
		hasPrefix(p, "\\");//network share
#else
	return hasPrefix(p, "/");
#endif
}


uint32 fileChecksum(const std::string& p) // throws FileUtilsExcep if file not found.
{
	std::vector<unsigned char> contents;
	readEntireFile(p, contents);

	if(contents.size() == 0)
		throw FileUtilsExcep("Failed to compute checksum over file '" + p + ", file is empty.");

	const unsigned int initial_crc = crc32(0, 0, 0);
	return crc32(initial_crc, &contents[0], contents.size() * sizeof(unsigned char));
}


void doUnitTests()
{
	conPrint("FileUtils::doUnitTests()");

#if defined(WIN32) || defined(WIN64)
	testAssert(isPathAbsolute("C:/windows"));
	testAssert(isPathAbsolute("a:/dsfsdf/sdfsdf/sdf/"));
	testAssert(isPathAbsolute("a:\\dsfsdf"));
	testAssert(isPathAbsolute("\\\\lust\\dsfsdf"));

	testAssert(!isPathAbsolute("a/b/"));
	testAssert(!isPathAbsolute("somefile"));
	testAssert(!isPathAbsolute("."));
	testAssert(!isPathAbsolute(".."));
	testAssert(!isPathAbsolute(""));
	testAssert(!isPathSafe("c:\\windows\\haxored.dll"));
	testAssert(!isPathSafe("c:\\haxored.dll"));
	testAssert(!isPathSafe("c:\\"));
	testAssert(!isPathSafe("c:\\haxored.txt"));	//no absolute path names allowed
	testAssert(!isPathSafe("..\\..\\haxored.dll"));
	testAssert(!isPathSafe("..\\..\\haxored"));
	testAssert(!isPathSafe("a\\..\\..\\haxored"));
	testAssert(!isPathSafe("a/../../b/../haxored"));
	//testAssert(!isPathSafe("/b/file.txt"));	//can't start with /
	testAssert(!isPathSafe("\\b/file.txt"));	//can't start with backslash
	testAssert(!isPathSafe("\\localhost\\c\\something.txt"));	//can't start with host thingey
	testAssert(!isPathSafe("\\123.123.123.123\\c\\something.txt"));	//can't start with host thingey

	testAssert(isPathSafe("something.txt"));
	testAssert(isPathSafe("dir\\something.txt"));
//	testAssert(!isPathSafe("dir/something.txt"));//don't use forward slashes!!!!
	testAssert(isPathSafe("a\\b\\something.txt"));
	testAssert(isPathSafe("a\\b\\something"));
	testAssert(isPathSafe("a\\.\\something"));
#else
	testAssert(isPathAbsolute("/etc/"));
	testAssert(!isPathAbsolute("dfgfdgdf/etc/"));

#endif


	testAssert(eatExtension("hello.there") == "hello.");


	const std::string TEST_PATH = "TESTING_TEMP_FILE";
	const std::string TEST_PATH2 = "TESTING_TEMP_FILE_2";
	writeEntireFile(TEST_PATH, std::vector<unsigned char>(0, 100));
	testAssert(fileExists(TEST_PATH));
	moveFile(TEST_PATH, TEST_PATH2);
	testAssert(!fileExists(TEST_PATH));
	testAssert(fileExists(TEST_PATH2));
	deleteFile(TEST_PATH2);
	testAssert(!fileExists(TEST_PATH2));

	// Test dir stuff

	const std::string TEST_PATH3 = "TEMP_TESTING_DIR/TESTING_TEMP_FILE";
	testAssert(getDirectory(TEST_PATH3) == "TEMP_TESTING_DIR");
	testAssert(getFilename(TEST_PATH3) == "TESTING_TEMP_FILE");
	testAssert(isPathSafe(TEST_PATH3));

	createDirsForPath(TEST_PATH3);
	testAssert(fileExists("TEMP_TESTING_DIR"));
	deleteEmptyDirectory("TEMP_TESTING_DIR");
	testAssert(!fileExists("TEMP_TESTING_DIR"));

	const std::string TEST_PATH_4 = "TEMP_TESTING_DIR/a/b";
	createDirsForPath(TEST_PATH_4);
	testAssert(fileExists("TEMP_TESTING_DIR"));
	testAssert(fileExists("TEMP_TESTING_DIR/a"));
	deleteEmptyDirectory("TEMP_TESTING_DIR/a");
	deleteEmptyDirectory("TEMP_TESTING_DIR");
	testAssert(!fileExists("TEMP_TESTING_DIR"));
	testAssert(!fileExists("TEMP_TESTING_DIR/a"));

	//createDirsForPath("c:/temp/test/a");

	conPrint("FileUtils::doUnitTests() Done.");
}



//Wed Jan 02 02:03:55 1980\n\0
/*
const std:	conPrint("FileUtils::doUnitTests()");
:string getAscTimeFileLastModified(const std::string& filename)
{


	HANDLE file = CreateFile(filename.c_str(), GENERIC_READ, FILE_SHARE_READ,
			conPrint("FileUtils::doUnitTests()");
							NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if(file == INVALID_HANDLE_VALUE)
		return "";

	FILETIME last_write_time;

	//TEMP HACK USING CREATED TIME INSTEAD OF LAST ACCESSED TIME
	bool result = GetFileTime(file, NULL, NULL, &last_write_time);
	assert(result);

	FILETIME local_lastwrite_time;

	result = FileTimeToLocalFileTime(&last_write_time, &local_lastwrite_time);
	assert(result);


	SYSTEMTIME systime;

	result = FileTimeToSystemTime(&local_lastwrite_time, &systime);
	assert(result);

	std::string timestring;

	timestring += dayOfWeek(systime.wDayOfWeek) + " ";
	timestring += getMonthString(systime.wMonth) + " ";


	timestring += widenToTwoChars(::toString(systime.wDay)) + " ";

	timestring += widenToTwoChars(::toString(systime.wHour)) + ":" +
		widenToTwoChars(::toString(systime.wMinute)) + ":" +
		widenToTwoChars(::toString(systime.wSecond)) + " ";

	timestring += ::toString(systime.wYear);


	return timestring;
*/
/*	FILE* file = fopen(filename.c_str(), "rb");

	if(!file)
		return "";

	struct _stat statistics;

	const int result = _fstat(file, &statistics);

	if(!result)
		return "";

	return ctime(&statistics.st_ctime);
}*/


/*
const Date getFileLastModifiedDate(const std::string& filename)
{
	HANDLE file = CreateFile(filename.c_str(), GENERIC_READ, FILE_SHARE_READ,
							NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if(file == INVALID_HANDLE_VALUE)
		return Date();

	FILETIME last_write_time;

	//TEMP HACK USING CREATED TIME INSTEAD OF LAST ACCESSED TIME
	bool result = GetFileTime(file, NULL, NULL, &last_write_time);
	assert(result);

	FILETIME local_lastwrite_time;

	result = FileTimeToLocalFileTime(&last_write_time, &local_lastwrite_time);
	assert(result);


	SYSTEMTIME systime;

	result = FileTimeToSystemTime(&local_lastwrite_time, &systime);
	assert(result);

	return Date(systime);
}
*/
} //end namespace FileUtils






