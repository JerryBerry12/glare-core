/*=====================================================================
TextureLoading.h
-----------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#pragma once


#include "OpenGLTexture.h"
#include "../graphics/ImageMap.h"
#include "../utils/RefCounted.h"
#include "../utils/ThreadSafeRefCounted.h"
#include "../utils/Reference.h"
#include "../utils/Mutex.h"
#include "../utils/TaskManager.h"
#include <unordered_set>
#include <map>


class OpenGLEngine;


// Result of texture loading
class TextureData : public ThreadSafeRefCounted
{
public:
	size_t compressedSizeBytes() const;

	size_t W, H, bytes_pp;
	js::Vector<uint8, 16> compressed_data; // Compressed data for all mip-map levels.
	std::vector<size_t> level_offsets;

	Reference<const ImageMapUInt8> converted_image;
};


//struct BuildUInt8MapTextureDataScratchState
//{
//	BuildUInt8MapTextureDataScratchState() : task_manager(NULL) {}
//	~BuildUInt8MapTextureDataScratchState() { delete task_manager; }
//
//	//std::vector<Reference<glare::Task> > compress_tasks;
//	glare::TaskManager* task_manager;
//};


class TextureDataManager : public ThreadSafeRefCounted
{
public:
	// Thread-safe
	Reference<TextureData> getOrBuildTextureData(const std::string& key, const ImageMapUInt8* imagemap, const Reference<OpenGLEngine>& opengl_engine/*, BuildUInt8MapTextureDataScratchState& scratch_state*/);

	Reference<TextureData> getTextureData(const std::string& key); // returns null ref if not present.

	bool isTextureDataInserted(const std::string& key) const; // Thread-safe

	void insertBuiltTextureData(const std::string& key, Reference<TextureData> data);

	void removeTextureData(const std::string& key);

	void clear();

	size_t getTotalMemUsage() const;
private:
	std::map<std::string, Reference<TextureData> > loaded_textures;
	mutable Mutex mutex;
};


/*=====================================================================
TextureLoading
--------------
Code for building compressed, mip-map level texture data, and for loading it into OpenGL.
Tests are in TextureLoadingTests.cpp
=====================================================================*/
class TextureLoading
{
public:
	friend class TextureLoadingTests;

	// Init stb_compress_dxt lib.
	static void init();

	// Builds compressed, mip-map level data.
	// Uses opengl_engine->getTaskManager() for multi-threading.
	// May return a reference to imagemap in the returned TextureData.
	static Reference<TextureData> buildUInt8MapTextureData(const ImageMapUInt8* imagemap, const Reference<OpenGLEngine>& opengl_engine, bool multithread);

	// Load the built texture data into OpenGL.
	static Reference<OpenGLTexture> loadTextureIntoOpenGL(const TextureData& texture_data, const Reference<OpenGLEngine>& opengl_engine,
		OpenGLTexture::Filtering filtering, OpenGLTexture::Wrapping wrapping);
private:
	static void downSampleToNextMipMapLevel(size_t prev_W, size_t prev_H, size_t N, const uint8* prev_level_image_data, size_t level_W, size_t level_H, uint8* data_out);
};
