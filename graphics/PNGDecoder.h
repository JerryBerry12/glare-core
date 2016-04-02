/*=====================================================================
PNGDecoder.h
------------
Copyright Glare Technologies Limited 2016 -
File created by ClassTemplate on Wed Jul 26 22:08:57 2006
=====================================================================*/
#pragma once


#include "../utils/Reference.h"
#include "../utils/Platform.h"
#include <map>
#include <string>
class Map2D;
class Bitmap;
class UInt8ComponentValueTraits;
template <class V, class VTraits> class ImageMap;


/*=====================================================================
PNGDecoder
----------
Loading and saving of PNG files.
=====================================================================*/
class PNGDecoder
{
public:
	PNGDecoder();
	~PNGDecoder();


	// throws ImFormatExcep
	static Reference<Map2D> decode(const std::string& path);
	
	// throws ImFormatExcep
	static void write(const Bitmap& bitmap, const std::map<std::string, std::string>& metadata, const std::string& path);
	static void write(const Bitmap& bitmap, const std::string& path); // Write with no metadata
	static void write(const ImageMap<uint8, UInt8ComponentValueTraits>& bitmap, const std::string& path); // Write with no metadata


	static const std::map<std::string, std::string> getMetaData(const std::string& image_path);

	static void test();
};
