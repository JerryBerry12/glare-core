/*=====================================================================
AsyncTextureLoader.h
--------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include <utils/ThreadSafeRefCounted.h>
#include <utils/Reference.h>
#include <string>
#include <map>
class OpenGLTexture;
class OpenGLEngine;


class AsyncTextureLoadedHandler
{
public:
	virtual void textureLoaded(Reference<OpenGLTexture> texture, const std::string& local_filename) = 0;
};


/*=====================================================================
AsyncTextureLoader
------------------
In Emscripten, downloads file asynchronously with emscripten_async_wget2, then creates an OpenGLTexture from it.
In a desktop build, just loads the texture synchronously.
=====================================================================*/
class AsyncTextureLoader : public ThreadSafeRefCounted
{
public:
	// server_hostname: something like "substrata.info"
	// url_path_prefix: Path to data dir, something like "/webclient/data"
	AsyncTextureLoader(const std::string& server_hostname, const std::string& url_path_prefix, OpenGLEngine* opengl_engine);

	// Constructor for non-web, loading locally off disk
	// local_path_prefix: Path to data dir, something like base_dir + "/data"
	AsyncTextureLoader(const std::string& local_path_prefix, OpenGLEngine* opengl_engine);

	~AsyncTextureLoader();

	
	// local_path should be a path relative to the 'data' directory, for example "resources/foam_windowed.ktx2"
	// handler->textureLoaded will be called when the texture is loaded.
	void startLoadingTexture(const std::string& local_path, AsyncTextureLoadedHandler* handler);
	void cancelLoadingTexture(const std::string& local_path);

	struct LoadingTexInfo : public RefCounted
	{
		AsyncTextureLoadedHandler* handler;
		int emscripten_handle;
	};

	std::map<std::string, Reference<LoadingTexInfo>> tex_info; // Map from local filename to LoadingTexInfo ref.

	std::string local_path_prefix;
	std::string url_path_prefix;
	std::string server_hostname;
	OpenGLEngine* opengl_engine;
};