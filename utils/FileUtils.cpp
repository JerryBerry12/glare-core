/*=====================================================================
fileutils.cpp
-------------
File created by ClassTemplate on Sat Mar 01 18:05:09 2003
Code By Nicholas Chapman.
=====================================================================*/
#include "FileUtils.h"


#include "../utils/IncludeWindows.h"
#if !defined(_WIN32)
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>	// For open / close
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#endif

#include <cstring>
#include <stdlib.h>
#include <assert.h>
#include "StringUtils.h"
#include "MemMappedFile.h"
#include "Exception.h"
#include "PlatformUtils.h"


namespace FileUtils
{


#if defined(_WIN32) || defined(_WIN64)
const char* PLATFORM_DIR_SEPARATOR = "\\";
const char PLATFORM_DIR_SEPARATOR_CHAR = '\\';
#else
const char* PLATFORM_DIR_SEPARATOR = "/";
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
#if defined(_WIN32) || defined(_WIN64)

	const BOOL result = ::CreateDirectory(StringUtils::UTF8ToPlatformUnicodeEncoding(dirname).c_str(), NULL);
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
		//std::cout << "dirs[" << i << "]: '" << dirs[i] << "'" << std::endl;
		//if(i > 0 || !isPathAbsolute(path)) // don't create first
		//{
		if(dirs[i] == "")
		{
			assert(i == 0);
			assert(std::string(PLATFORM_DIR_SEPARATOR) == "/");
			// This is the root dir on Unix.  Don't try and create it.
			dir = "/";
		}
		else // Else this is not the root dir.
		{
			dir += dirs[i];

			//std::cout << "dir: '" << dir << "'" << std::endl;

			if(!(fileExists(dir) || hasSuffix(dir, ":")))
				createDir(dir);

			dir += PLATFORM_DIR_SEPARATOR;
		}
	}
}


void createDirIfDoesNotExist(const std::string& dirname)
{
	if(!fileExists(dirname))
		createDir(dirname);
}


/*
#if defined(_WIN32) || defined(_WIN64)
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


	const size_t lastslashpos = pathname_copy.find_last_of('/');

	if(lastslashpos == std::string::npos)//if no slashes
	{
		return pathname;
	}
	else
	{
		const size_t filenamepos = lastslashpos + 1;

		if(filenamepos < pathname.size())
			return pathname.substr(filenamepos, (int)pathname.size() - filenamepos);
		else
			return "";
	}
}


const std::vector<std::string> getFilesInDir(const std::string& dir_path)
{
#if defined(_WIN32) || defined(_WIN64)
	WIN32_FIND_DATA find_data;
	HANDLE search_handle = FindFirstFile(StringUtils::UTF8ToPlatformUnicodeEncoding(dir_path + "\\*").c_str(), &find_data);
	if(search_handle == INVALID_HANDLE_VALUE)
		throw FileUtilsExcep("Failed to open dir '" + dir_path + "': " + PlatformUtils::getLastErrorString());


	std::vector<std::string> paths;
	paths.push_back(StringUtils::PlatformToUTF8UnicodeEncoding(find_data.cFileName));

	while(FindNextFile(search_handle, &find_data) != 0)
	{
		paths.push_back(StringUtils::PlatformToUTF8UnicodeEncoding(find_data.cFileName));
	}

	FindClose(search_handle);

	return paths;
#else
	DIR* dir = opendir(dir_path.c_str());
	if(!dir)
		throw FileUtilsExcep("Failed to open dir '" + dir_path + "'");

	std::vector<std::string> paths;

	struct dirent* f = NULL;
	while((f = readdir(dir)) != NULL)
	{
		paths.push_back(f->d_name);	
	}

	closedir(dir);

	return paths;
#endif
}



/*
//creates the directory the file is in according to pathname
//returns false if fails or directory already exists
#if defined(_WIN32) || defined(_WIN64)
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
#if defined(_WIN32) || defined(_WIN64)
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
#if defined(_WIN32) || defined(_WIN64)

	WIN32_FIND_DATA find_data;
	HANDLE search_handle = FindFirstFile(StringUtils::UTF8ToPlatformUnicodeEncoding(pathname).c_str(), &find_data);

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


uint64 getFileSize(const std::string& path)
{
	// NOTE: This code is from MemMappedFile.  Could use GetFileAttributesEx instead (see http://msdn.microsoft.com/en-us/library/windows/desktop/aa364946(v=vs.85).aspx )
#if defined(_WIN32)
	HANDLE file_handle = CreateFile(
		StringUtils::UTF8ToPlatformUnicodeEncoding(path).c_str(),
		GENERIC_READ,
		FILE_SHARE_READ, // share mode - Use FILE_SHARE_READ so that other processes can read the file as well.
		NULL, // security attributes
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if(file_handle == INVALID_HANDLE_VALUE)
		throw FileUtilsExcep("CreateFile failed: " + PlatformUtils::getLastErrorString());

	// Get size of file
	LARGE_INTEGER file_size_li;
	BOOL res = GetFileSizeEx(
		file_handle,
		&file_size_li
	);
	if(!res)
		throw FileUtilsExcep("GetFileSizeEx failed: " + PlatformUtils::getLastErrorString());

	// Close the file
	res = CloseHandle(file_handle);
	assert(res);

	return file_size_li.QuadPart;
#else
	//NOTE: could change to use stat(), see http://stackoverflow.com/questions/5793030/how-to-get-file-size-on-disk-on-linux

	int linux_file_handle = ::open(
		StringUtils::UTF8ToPlatformUnicodeEncoding(path).c_str(),
		O_RDONLY);

	if(linux_file_handle <= 0)
		throw FileUtilsExcep("File open failed.");

	off_t file_size = lseek(linux_file_handle, 0, SEEK_END);

	// Close the file
	::close(linux_file_handle);

	return file_size;
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


	std::string::size_type startpos = 0;

	while(1)
	{

		const std::string::size_type slashpos = pathname.find_first_of('\\', startpos);

		if(slashpos == std::string::npos)
			break;

		assert(slashpos - startpos >= 0);
		assert(startpos >= 0);
		assert(slashpos < pathname.size());

		//if(slashpos - startpos > 0) // Don't include zero length dirs, e.g. the root dir on Unix.
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


bool isDirectory(const std::string& pathname)
{
#if defined(_WIN32) || defined(_WIN64)
	WIN32_FILE_ATTRIBUTE_DATA file_data;
	GetFileAttributesEx(StringUtils::UTF8ToPlatformUnicodeEncoding(pathname).c_str(), GetFileExInfoStandard, &file_data);

	if(file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		return true;

	return false;
#else
	struct stat buffer;
	const int status = stat(pathname.c_str(), &buffer);
	if(status != 0)
		return false; // No such file

	return S_ISDIR(buffer.st_mode);
#endif
}


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


void readEntireFile(const std::string& pathname,
					std::string& filecontents_out)
{
	try
	{
		MemMappedFile file(pathname);
		filecontents_out.resize(file.fileSize());
		if(file.fileSize() > 0)
			std::memcpy(&filecontents_out[0], file.fileData(), file.fileSize());
	}
	catch(Indigo::Exception& e)
	{
		throw FileUtilsExcep("Could not open '" + pathname + "' for reading: " + e.what());
	}
}


void readEntireFile(const std::string& pathname,
					std::vector<unsigned char>& filecontents_out)
{
	try
	{
		MemMappedFile file(pathname);
		filecontents_out.resize(file.fileSize());
		if(file.fileSize() > 0)
			std::memcpy(&filecontents_out[0], file.fileData(), file.fileSize());
	}
	catch(Indigo::Exception& e)
	{
		throw FileUtilsExcep("Could not open '" + pathname + "' for reading: " + e.what());
	}
}


void writeEntireFile(const std::string& pathname,
					 const std::vector<unsigned char>& filecontents)
{
	std::ofstream file(convertUTF8ToFStreamPath(pathname).c_str(), std::ios::binary);

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
	std::ofstream file(convertUTF8ToFStreamPath(pathname).c_str(), std::ios::binary);

	if(!file)
		throw FileUtilsExcep("Could not open '" + pathname + "' for writing.");

	if(filecontents.size() > 0)
		file.write(filecontents.c_str(), filecontents.size());

	if(file.bad())
		throw FileUtilsExcep("Write to '" + pathname + "' failed.");
}


void writeEntireFileTextMode(const std::string& pathname,
					 const std::string& filecontents)
{
	std::ofstream file(convertUTF8ToFStreamPath(pathname).c_str());

	if(!file)
		throw FileUtilsExcep("Could not open '" + pathname + "' for writing.");

	if(filecontents.size() > 0)
		file.write(filecontents.c_str(), filecontents.size());

	if(file.bad())
		throw FileUtilsExcep("Write to '" + pathname + "' failed.");
}


void writeEntireFile(const std::string& pathname, const char* data, size_t data_size)
{
	std::ofstream file(convertUTF8ToFStreamPath(pathname).c_str(), std::ios::binary);

	if(!file)
		throw FileUtilsExcep("Could not open '" + pathname + "' for writing.");

	if(data_size)
		file.write(data, data_size);

	if(file.bad())
		throw FileUtilsExcep("Write to '" + pathname + "' failed.");
}


void readEntireFileTextMode(const std::string& pathname, std::string& s_out) // throws FileUtilsExcep
{
	std::ifstream infile(convertUTF8ToFStreamPath(pathname).c_str());

	if(!infile)
		throw FileUtilsExcep("could not open '" + pathname + "' for reading.");

	s_out = "";
	std::string line;
	while(infile)
	{
		std::getline(infile, line);

		//if((line.size() > 0) && (line[0] == '\r'))
		//	line = line.substr(1, line.size() - 1);
		s_out += line + "\n"; // NOTE: use platform newline string?
	}
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


#if defined(_WIN32) || defined(_WIN64)
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
#if defined(_WIN32) || defined(_WIN64)

	if(!CopyFile(
		StringUtils::UTF8ToPlatformUnicodeEncoding(srcpath).c_str(),
		StringUtils::UTF8ToPlatformUnicodeEncoding(dstpath).c_str(),
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
#if defined(_WIN32) || defined(_WIN64)
	if(!DeleteFile(
		StringUtils::UTF8ToPlatformUnicodeEncoding(path).c_str()
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
#if defined(_WIN32) || defined(_WIN64)
	if(!RemoveDirectory(
		StringUtils::UTF8ToPlatformUnicodeEncoding(path).c_str()
		))
	{
		throw FileUtilsExcep("Failed to delete directory '" + path + "'.");
	}

#else
	if(rmdir(path.c_str()) != 0)
		throw FileUtilsExcep("Failed to delete directory '" + path + "'.");
#endif
}


// Throws FileUtilsExcep
void deleteDirectoryRecursive(const std::string& path)
{
	if(!isDirectory(path)) return;

	const std::vector<std::string> files = getFilesInDir(path);

	for(size_t i = 0; i < files.size(); ++i)
	{
		if(files[i] != "." && files[i] != "..")
		{
			std::string file_path = join(path, files[i]);

			if(isDirectory(file_path))
				deleteDirectoryRecursive(file_path);
			else 
				deleteFile(file_path);
		}
	}

	deleteEmptyDirectory(path);
}


void moveFile(const std::string& srcpath, const std::string& dstpath)
{
#if defined(_WIN32) || defined(_WIN64)
	// NOTE: To make this atomic on Windows, we could use ReplaceFile, with some care.
	// See http://msdn.microsoft.com/en-us/library/windows/desktop/aa365512(v=vs.85).aspx
	if(!MoveFileEx(
		StringUtils::UTF8ToPlatformUnicodeEncoding(srcpath).c_str(),
		StringUtils::UTF8ToPlatformUnicodeEncoding(dstpath).c_str(),
		MOVEFILE_REPLACE_EXISTING
	))
		throw FileUtilsExcep("Failed to move file '" + srcpath + "' to '" + dstpath + "'.");
#else
	if(rename(srcpath.c_str(), dstpath.c_str()) != 0)
		throw FileUtilsExcep("Failed to move file '" + srcpath + "' to '" + dstpath + "': " + PlatformUtils::getLastErrorString());
#endif
}


bool isPathAbsolute(const std::string& p)
{
	// Take into account both Windows and Unix prefixes.
	// This fixes a bug with network rendering across both Windows and Unix machines: http://www.indigorenderer.com/forum/viewtopic.php?f=5&t=11482
	// Note that this is maybe not the ideal way to solve the problem.

	return 
		(p.length() > 1 && hasPrefix(p.substr(1, p.length()-1), ":")) || // e.g. C:/programming/
		hasPrefix(p, "\\") || // Windows network share, e.g. \\Avarice
		hasPrefix(p, "/"); // Unix root
}


FILE* openFile(const std::string& pathname, const std::string& openmode)
{
#if defined(_WIN32) || defined(_WIN64)
	// If we are on Windows, then, in order to use Unicode filenames, we will convert from UTF-8 to wstring and use _wfopen()
	return _wfopen(StringUtils::UTF8ToWString(pathname).c_str(), StringUtils::UTF8ToWString(openmode).c_str());
#else
	// On Linux (and on OS X?), fopen accepts UTF-8 encoded Unicode filenames natively.
	return fopen(pathname.c_str(), openmode.c_str());
#endif
}


// remove non alphanumeric characters etc..
const std::string makeOSFriendlyFilename(const std::string& name)
{
	//std::string r(name.size(), ' ');
	std::string r;
	for(unsigned int i=0; i<name.size(); ++i)
	{
		if(::isAlphaNumeric(name[i]) || name[i] == ' ' || name[i] == '_' || name[i] == '.' || name[i] == '(' || name[i] == ')' || name[i] == '-')
			r = ::appendChar(r, name[i]); //r[i] = name[i];
		else
			r += "_" + int32ToString(r[i]); //r[i] = '_' + ;
	}

	return r;
}


#if defined(_WIN32)

static const std::string getCanonicalPath(const std::string& p)
{
	// NOTE: When we can assume Vista support, use GetFinalPathNameByHandle().
	return p;
}

#else

static const std::string getCanonicalPath(const std::string& p)
{
	char buf[PATH_MAX];
	char* result = realpath(p.c_str(), buf);
	if(result == NULL)
		throw FileUtilsExcep("realpath failed.  errno: " + toString(errno));
	return std::string(buf);
}

#endif


/*
Changes slashes to platform slashes.  Also tries to guess the correct case by scanning directory and doing case-insensitive matches.
On Unix (Linux / OS X) will also return the canonical path name.
*/
const std::string getActualOSPath(const std::string& path_)
{
	const std::string path = toPlatformSlashes(path_);

	if(fileExists(path))
		return getCanonicalPath(path);
	
	// We don't have an exact match.
	// Try to guess the correct case by scanning directory and doing case-insensitive matches.

	const std::string dir = getDirectory(path);
	const std::vector<std::string> files = getFilesInDir(dir);

	const std::string target_filename_lowercase = ::toLowerCase(getFilename(path));

	for(size_t i=0; i<files.size(); ++i)
	{
		if(::toLowerCase(files[i]) == target_filename_lowercase)
			return getCanonicalPath(join(dir, files[i]));
	}

	throw FileUtilsExcep("Could not find file '" + path_ + "'");
}


const std::string getPathKey(const std::string& pathname)
{
	const std::string use_path = FileUtils::getActualOSPath(pathname);

#if defined(_WIN32)
	return ::toLowerCase(use_path);
#else
	return use_path;
#endif
}


uint64 getFileCreatedTime(const std::string& filename)
{
#if defined(_WIN32)
	HANDLE file = CreateFile(
		StringUtils::UTF8ToPlatformUnicodeEncoding(filename).c_str(), 
		GENERIC_READ, 
		FILE_SHARE_READ,
		NULL, // lpSecurityAttributes 
		OPEN_EXISTING, // OPEN_EXISTING = Opens a file or device, only if it exists.
		FILE_ATTRIBUTE_NORMAL, // dwFlagsAndAttributes
		NULL // hTemplateFile
	);

	if(file == INVALID_HANDLE_VALUE)
		throw FileUtilsExcep("Failed to open file '" + filename + "': " + PlatformUtils::getLastErrorString());

	FILETIME creation_time;
	BOOL res = GetFileTime(
		file,
		&creation_time,
		nullptr,
		nullptr
	);

	if(res == FALSE)
		throw FileUtilsExcep("Failed to get created time for file '" + filename + "': " + PlatformUtils::getLastErrorString());

	const uint64 time_uint64 = ((uint64)creation_time.dwHighDateTime << 32) + creation_time.dwLowDateTime;
	return time_uint64;
#else
	throw FileUtilsExcep("getFileCreatedTime() not implemented on non-Windows.");
#endif
}



#if (defined(_WIN32) || defined(_WIN64)) && !defined(__MINGW32__)

const std::wstring convertUTF8ToFStreamPath(const std::string& p)
{
	return StringUtils::UTF8ToPlatformUnicodeEncoding(p);
}

#elif defined(__MINGW32__)

const std::string convertUTF8ToFStreamPath(const std::string& p)
{
	return p;
}

#else

const std::string convertUTF8ToFStreamPath(const std::string& p)
{
	return StringUtils::UTF8ToPlatformUnicodeEncoding(p);
}

#endif


#if (BUILD_TESTS)

}


#include "PlatformUtils.h"
#include "../indigo/TestUtils.h"
#include "../utils/ConPrint.h"


namespace FileUtils
{


void doUnitTests()
{
	conPrint("FileUtils::doUnitTests()");



	try
	{

		//TEMP:
		const std::vector<std::string> files = getFilesInDir(TestUtils::getIndigoTestReposDir() + "/testfiles");
		for(size_t i=0; i<files.size(); ++i)
			conPrint("file: " + files[i]);

#if defined(_WIN32) || defined(_WIN64)
		testAssert(getActualOSPath(TestUtils::getIndigoTestReposDir() + "/testfiles/sphere.obj") == TestUtils::getIndigoTestReposDir() + "\\testfiles\\sphere.obj");
		testAssert(getActualOSPath(TestUtils::getIndigoTestReposDir() + "/testfiles/SpHerE.ObJ") == TestUtils::getIndigoTestReposDir() + "\\testfiles\\SpHerE.ObJ");
#else
		testAssert(getActualOSPath(TestUtils::getIndigoTestReposDir() + "/testfiles/sphere.obj") == TestUtils::getIndigoTestReposDir() + "/testfiles/sphere.obj");
		testAssert(getActualOSPath(TestUtils::getIndigoTestReposDir() + "/testfiles/SpHerE.ObJ") == TestUtils::getIndigoTestReposDir() + "/testfiles/sphere.obj");
		testAssert(getActualOSPath(TestUtils::getIndigoTestReposDir() + "\\testfiles/SpHerE.ObJ") == TestUtils::getIndigoTestReposDir() + "/testfiles/sphere.obj");
		testAssert(getActualOSPath(TestUtils::getIndigoTestReposDir() + "/testfiles\\SpHerE.ObJ") == TestUtils::getIndigoTestReposDir() + "/testfiles/sphere.obj");
#endif
	}
	catch(FileUtilsExcep& e)
	{
		failTest(e.what());
	}






	const std::string euro_txt_pathname = TestUtils::getIndigoTestReposDir() + "/testfiles/\xE2\x82\xAC.txt";

	try
	{
		testAssert(fileExists(euro_txt_pathname)); // Euro sign.txt


		std::string contents;
		FileUtils::readEntireFile(euro_txt_pathname, contents);

		const std::string target_contents = "\xE2\x82\xAC";

		conPrint("'" + contents + "'");

	//	testAssert(contents == target_contents);
	}
	catch(FileUtilsExcep& e)
	{
		conPrint(e.what());
		testAssert(0);
	}

	// Test openFile() with a Unicode pathname
	FILE* infile = FileUtils::openFile(euro_txt_pathname, "rb");
	testAssert(infile != NULL);
	if(infile) fclose(infile);


	// Test std::ifstream with a Unicode pathname
	{
	std::ifstream file(FileUtils::convertUTF8ToFStreamPath(euro_txt_pathname).c_str(), std::ios_base::in | std::ios_base::binary);

	testAssert(file.is_open());
	}

	// Test std::ifstream without a Unicode pathname
	{
	std::ifstream file(FileUtils::convertUTF8ToFStreamPath(TestUtils::getIndigoTestReposDir() + "/testfiles/a_test_mesh.obj").c_str(), std::ios_base::in | std::ios_base::binary);

	testAssert(file.is_open());
	}


	////////////// Test getFileSize() ////////////
	try
	{
		testAssert(getFileSize(TestUtils::getIndigoTestReposDir() + "/testfiles/a_test_mesh.obj") == 231);
	}
	catch(FileUtilsExcep& e)
	{
		failTest(e.what());
	}

	try
	{
		getFileSize(TestUtils::getIndigoTestReposDir() + "/testfiles/a_test_mesh_that_doesnt_exist.obj");

		failTest("No exception thrown by getFileSize() for nonexistent file.");
	}
	catch(FileUtilsExcep&)
	{
		//Test successful.
	}
	catch(...)
	{
		failTest("Wrong type of exception thrown by getFileSize()");
	}

	/////////////////////////////////////////////



#if defined(_WIN32) || defined(_WIN64)
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


	try
	{
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

		// Windows sometimes takes a while to delete the dir, so wait a while if it's not done yet.
		for(int i=0; i<10; ++i)
		{
			if(fileExists("TEMP_TESTING_DIR"))
				::PlatformUtils::Sleep(20);
			else
				break;
		}
		testAssert(!fileExists("TEMP_TESTING_DIR"));
		testAssert(!fileExists("TEMP_TESTING_DIR/a"));

		// Test getFilesInDir
		createDir("TEMP_TESTING_DIR");
		writeEntireFile("TEMP_TESTING_DIR/a", std::vector<unsigned char>(0, 100));
		writeEntireFile("TEMP_TESTING_DIR/b", std::vector<unsigned char>(0, 100));
		
		const std::vector<std::string> files = getFilesInDir("TEMP_TESTING_DIR");
		testAssert(files.size() == 4);
		bool found_a = false;
		bool found_b = false;
		for(size_t i=0; i<files.size(); ++i)
		{
			if(files[i] == "a")
				found_a = true;
			if(files[i] == "b")
				found_b = true;
			conPrint("file[" + ::toString((int) i) + "]: '" + files[i] + "'");
		}
		testAssert(found_a);
		testAssert(found_b);

		deleteFile("TEMP_TESTING_DIR/a");
		deleteFile("TEMP_TESTING_DIR/b");
		deleteEmptyDirectory("TEMP_TESTING_DIR");

		// Test deleteDirectoryRecursive
		createDir("TEMP_TESTING_DIR");
		writeEntireFile("TEMP_TESTING_DIR/a", std::vector<unsigned char>(0, 100));
		writeEntireFile("TEMP_TESTING_DIR/b", std::vector<unsigned char>(0, 100));
		createDir("TEMP_TESTING_DIR/subdir");
		writeEntireFile("TEMP_TESTING_DIR/subdir/b", std::vector<unsigned char>(0, 100));

		deleteDirectoryRecursive("TEMP_TESTING_DIR");

		// Test deletion of empty dir with deleteDirectoryRecursive
		createDir("TEMP_TESTING_DIR");

		deleteDirectoryRecursive("TEMP_TESTING_DIR");



		//============ Test getFileCreatedTime() ====================
#if defined(_WIN32)
		testAssert(getFileCreatedTime(TestUtils::getIndigoTestReposDir() + "/testfiles/a_test_mesh.obj") > 0);
#endif
	}
	catch(FileUtilsExcep& e)
	{
		conPrint(e.what());
		testAssert(!"FileUtilsExcep");
	}

	//createDirsForPath("c:/temp/test/a");

	conPrint("FileUtils::doUnitTests() Done.");
}
#endif


} // end namespace FileUtils
