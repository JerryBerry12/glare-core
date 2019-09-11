/*=====================================================================
TextureLoading.cpp
------------------
Copyright Glare Technologies Limited 2019 -
=====================================================================*/
#include "IncludeOpenGL.h"
#include "TextureLoading.h"


#include "OpenGLEngine.h"
#include "../graphics/ImageMap.h"
#include "../graphics/DXTCompression.h"
#include "../maths/mathstypes.h"
#include "../utils/Timer.h"
#include "../utils/Task.h"
#include "../utils/TaskManager.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/ArrayRef.h"
#include <vector>


// Thread-safe
Reference<TextureData> TextureDataManager::getOrBuildTextureData(const ImageMapUInt8* imagemap, const Reference<OpenGLEngine>& opengl_engine/*, BuildUInt8MapTextureDataScratchState& scratch_state*/)
{
	Lock lock(mutex);

	auto res = loaded_textures.find(imagemap);
	if(res != loaded_textures.end())
		return res->second;
	else
	{
		Reference<TextureData> data = TextureLoading::buildUInt8MapTextureData(imagemap, opengl_engine/*, scratch_state*/);
		loaded_textures[imagemap] = data;
		return data;
	}
}


bool TextureDataManager::isTextureDataInserted(const ImageMapUInt8* imagemap) const
{
	Lock lock(mutex);

	return loaded_textures.count(imagemap) > 0;
}


void TextureDataManager::insertBuiltTextureData(const ImageMapUInt8* imagemap, Reference<TextureData> data)
{
	Lock lock(mutex);

	loaded_textures[imagemap] = data;
}


void TextureDataManager::removeTextureData(const ImageMapUInt8* imagemap)
{
	Lock lock(mutex);

	loaded_textures.erase(imagemap);
}


void TextureDataManager::clear()
{
	Lock lock(mutex);

	loaded_textures.clear();
}


void TextureLoading::init()
{
	DXTCompression::init();
}


// Downsize previous mip level image to current mip level.
// Just uses kinda crappy 2x2 pixel box filter.
Reference<ImageMapUInt8> TextureLoading::downSampleToNextMipMapLevel(const ImageMapUInt8& prev_mip_level_image, size_t level_W, size_t level_H)
{
	Timer timer;
	const size_t old_W = prev_mip_level_image.getWidth();
	const size_t old_H = prev_mip_level_image.getHeight();

	Reference<ImageMapUInt8> new_image = new ImageMapUInt8(level_W, level_H, prev_mip_level_image.getN());

	uint8* const dst_data = new_image->getData();
	const uint8* const src_data = prev_mip_level_image.getData();
	const size_t src_W = prev_mip_level_image.getWidth();
	const size_t N = prev_mip_level_image.getN();

	// Because the new width is max(1, floor(old_width/2)), we have old_width >= new_width*2 in all cases apart from when old_width = 1.
	// If old_width >= new_width*2, then 
	// old_width >= (new_width-1)*2 + 2
	// old_width > (new_width-1)*2 + 1
	// (new_width-1)*2 + 1 < old_width
	// in other words the support pixels of the box filter are guarenteed to be in range (< old_width)
	// Likewise for old height etc..
	assert((old_W == 1) || ((level_W - 1) * 2 + 1 < old_W));
	assert((old_H == 1) || ((level_H - 1) * 2 + 1 < old_H));

	assert(N == 3 || N == 4);
	if(N == 3)
	{
		if(old_W == 1)
		{
			// This is 1xN texture being downsized to 1xfloor(N/2)
			assert(level_W == 1 && old_H > 1);
			assert((level_H - 1) * 2 + 1 < old_H);

			for(int y=0; y<(int)level_H; ++y)
			{
				int val[3] = { 0, 0, 0 };
				size_t sx = 0;
				size_t sy = y*2;
				{
					const uint8* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
				}
				sy = y*2 + 1;
				{
					const uint8* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
				}
				uint8* const dest_pixel = dst_data + (0 + level_W * y) * N;
				dest_pixel[0] = val[0] / 2;
				dest_pixel[1] = val[1] / 2;
				dest_pixel[2] = val[2] / 2;
			}
		}
		else if(old_H == 1)
		{
			// This is Nx1 texture being downsized to floor(N/2)x1
			assert(level_H == 1 && old_W > 1);
			assert((level_W - 1) * 2 + 1 < old_W);

			for(int x=0; x<(int)level_W; ++x)
			{
				int val[3] = { 0, 0, 0 };
				int sx = x*2;
				int sy = 0;
				{
					const uint8* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
				}
				sx = x*2 + 1;
				{
					const uint8* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
				}
				uint8* const dest_pixel = dst_data + (x + level_W * 0) * N;
				dest_pixel[0] = val[0] / 2;
				dest_pixel[1] = val[1] / 2;
				dest_pixel[2] = val[2] / 2;
			}
		}
		else
		{
			assert(old_W >= 2 && old_H >= 2);
			assert((level_W - 1) * 2 + 1 < old_W);
			assert((level_H - 1) * 2 + 1 < old_H);

			// In this case all reads should be in-bounds
			for(int y=0; y<(int)level_H; ++y)
				for(int x=0; x<(int)level_W; ++x)
				{
					int val[3] = { 0, 0, 0 };
					int sx = x*2;
					int sy = y*2;
					{
						const uint8* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
					}
					sx = x*2 + 1;
					{
						const uint8* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
					}
					sx = x*2;
					sy = y*2 + 1;
					{
						const uint8* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
					}
					sx = x*2 + 1;
					{
						const uint8* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
					}

					uint8* const dest_pixel = dst_data + (x + level_W * y) * N;
					dest_pixel[0] = val[0] / 4;
					dest_pixel[1] = val[1] / 4;
					dest_pixel[2] = val[2] / 4;
				}
		}
	}
	else
	{
		if(old_W == 1)
		{
			// This is 1xN texture being downsized to 1xfloor(N/2)
			assert(level_W == 1 && old_H > 1);
			assert((level_H - 1) * 2 + 1 < old_H);

			for(int y=0; y<(int)level_H; ++y)
			{
				int val[4] = { 0, 0, 0, 0 };
				int sx = 0;
				int sy = y*2;
				{
					const uint8* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
					val[3] += src[3];
				}
				sy = y*2 + 1;
				{
					const uint8* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
					val[3] += src[3];
				}
				uint8* const dest_pixel = dst_data + (0 + level_W * y) * N;
				dest_pixel[0] = val[0] / 2;
				dest_pixel[1] = val[1] / 2;
				dest_pixel[2] = val[2] / 2;
				dest_pixel[3] = val[3] / 2;
			}
		}
		else if(old_H == 1)
		{
			// This is Nx1 texture being downsized to floor(N/2)x1
			assert(level_H == 1 && old_W > 1);
			assert((level_W - 1) * 2 + 1 < old_W);

			for(int x=0; x<(int)level_W; ++x)
			{
				int val[4] = { 0, 0, 0, 0 };
				int sx = x*2;
				int sy = 0;
				{
					const uint8* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
					val[3] += src[3];
				}
				sx = x*2 + 1;
				{
					const uint8* src = src_data + (sx + src_W * sy) * N;
					val[0] += src[0];
					val[1] += src[1];
					val[2] += src[2];
					val[3] += src[3];
				}
				uint8* const dest_pixel = dst_data + (x + level_W * 0) * N;
				dest_pixel[0] = val[0] / 2;
				dest_pixel[1] = val[1] / 2;
				dest_pixel[2] = val[2] / 2;
				dest_pixel[3] = val[3] / 2;
			}
		}
		else
		{
			assert(old_W >= 2 && old_H >= 2);
			assert((level_W - 1) * 2 + 1 < old_W);
			assert((level_H - 1) * 2 + 1 < old_H);

			// In this case all reads should be in-bounds
			for(int y=0; y<(int)level_H; ++y)
				for(int x=0; x<(int)level_W; ++x)
				{
					int val[4] = { 0, 0, 0, 0 };
					int sx = x*2;
					int sy = y*2;
					{
						const uint8* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
						val[3] += src[3];
					}
					sx = x*2 + 1;
					{
						const uint8* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
						val[3] += src[3];
					}
					sx = x*2;
					sy = y*2 + 1;
					{
						const uint8* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
						val[3] += src[3];
					}
					sx = x*2 + 1;
					{
						const uint8* src = src_data + (sx + src_W * sy) * N;
						val[0] += src[0];
						val[1] += src[1];
						val[2] += src[2];
						val[3] += src[3];
					}

					uint8* const dest_pixel = dst_data + (x + level_W * y) * N;
					dest_pixel[0] = val[0] / 4;
					dest_pixel[1] = val[1] / 4;
					dest_pixel[2] = val[2] / 4;
					dest_pixel[3] = val[3] / 4;
				}
		}
	}

	//conPrint("downSampleToNextMipMapLevel took " + timer.elapsedString());
	return new_image;
}


Reference<TextureData> TextureLoading::buildUInt8MapTextureData(const ImageMapUInt8* imagemap, const Reference<OpenGLEngine>& opengl_engine/*,
	BuildUInt8MapTextureDataScratchState& state*/)
{
	// conPrint("Creating new OpenGL texture.");
	Reference<TextureData> texture_data = new TextureData();

	// If we have a 1 or 2 bytes per pixel texture, convert to 3 or 4.
	// Handling such textures without converting them here would have to be done in the shaders, which we don't do currently.
	Reference<const ImageMapUInt8> converted_image;
	if(imagemap->getN() == 1)
	{
		// Convert to RGB:
		ImageMapUInt8Ref new_image = new ImageMapUInt8(imagemap->getWidth(), imagemap->getHeight(), 3);
		const uint8* const src  = imagemap->getData();
		uint8* const dest = new_image->getData();
		const size_t num_pixels = imagemap->numPixels();
		for(size_t i=0; i<num_pixels; ++i)
		{
			const uint8 r = src[i];
			dest[i*3 + 0] = r;
			dest[i*3 + 1] = r;
			dest[i*3 + 2] = r;
		}
		converted_image = new_image;
	}
	else if(imagemap->getN() == 2)
	{
		// Convert to RGBA:
		ImageMapUInt8Ref new_image = new ImageMapUInt8(imagemap->getWidth(), imagemap->getHeight(), 4);
		const uint8* const src  = imagemap->getData();
		uint8* const dest = new_image->getData();
		const size_t num_pixels = imagemap->numPixels();
		for(size_t i=0; i<num_pixels; ++i)
		{
			const uint8 r = src[i*2 + 0];
			const uint8 a = src[i*2 + 1];
			dest[i*4 + 0] = r;
			dest[i*4 + 1] = r;
			dest[i*4 + 2] = r;
			dest[i*4 + 3] = a;
		}
		converted_image = new_image;
	}
	else
		converted_image = imagemap;

	// Try and load as a DXT texture compression
	const bool compressed_sRGB_support = opengl_engine->GL_EXT_texture_sRGB_support && opengl_engine->GL_EXT_texture_compression_s3tc_support;
	const size_t W = converted_image->getWidth();
	const size_t H = converted_image->getHeight();
	const size_t bytes_pp = converted_image->getBytesPerPixel();
	texture_data->W = W;
	texture_data->H = H;
	texture_data->bytes_pp = bytes_pp;
	if(opengl_engine->settings.compress_textures && compressed_sRGB_support && (bytes_pp == 3 || bytes_pp == 4))
	{
		//opengl_tex->makeGLTexture(/*format=*/(bytes_pp == 3) ? OpenGLTexture::Format_Compressed_SRGB_Uint8 : OpenGLTexture::Format_Compressed_SRGBA_Uint8);

		Reference<const ImageMapUInt8> prev_mip_level_image = converted_image;

		// Work out number of levels we need
		int num_levels = 0;
		for(size_t k=0; ; ++k)
		{
			const size_t level_W = myMax((size_t)1, W / ((size_t)1 << k));
			const size_t level_H = myMax((size_t)1, H / ((size_t)1 << k));
			num_levels++;
			if(level_W == 1 && level_H == 1)
				break;
		}
		texture_data->compressed_data.resize(num_levels);

		DXTCompression::TempData compress_temp_data;

		for(size_t k=0; ; ++k) // For each mipmap level:
		{
			// See https://www.khronos.org/opengl/wiki/Texture#Mipmap_completeness
			const size_t level_W = myMax((size_t)1, W / ((size_t)1 << k));
			const size_t level_H = myMax((size_t)1, H / ((size_t)1 << k));

			// conPrint("Building mipmap level " + toString(k) + " data, dims: " + toString(level_W) + " x " + toString(level_H));

			Reference<const ImageMapUInt8> mip_level_image;
			if(k == 0)
				mip_level_image = converted_image;
			else
			{
				// Downsize previous mip level image to current mip level
				mip_level_image = downSampleToNextMipMapLevel(*prev_mip_level_image, level_W, level_H);
			}

			const size_t compressed_size = DXTCompression::getCompressedSizeBytes(mip_level_image.ptr());

			texture_data->compressed_data[k].resizeNoCopy(compressed_size);
			
			DXTCompression::compress(opengl_engine->getTaskManager(), compress_temp_data, mip_level_image.ptr(),
				texture_data->compressed_data[k].data(), texture_data->compressed_data[k].size());

			//opengl_tex->setMipMapLevelData((int)k, level_W, level_H, /*tex data=*/compressed);

			// If we have just finished computing the last mipmap level, we are done.
			if(level_W == 1 && level_H == 1)
				break;

			prev_mip_level_image = mip_level_image;
		}

		//opengl_tex->setTexParams(opengl_engine, filtering, wrapping);
	}
	else // Else if not using a compressed texture format:
	{
		if(converted_image->getN() != 3 && converted_image->getN() != 4)
			throw Indigo::Exception("Texture has unhandled number of components: " + toString(converted_image->getN()));

		texture_data->converted_image = converted_image;
	}

	return texture_data;
}



Reference<OpenGLTexture> TextureLoading::loadTextureIntoOpenGL(const TextureData& texture_data, const Reference<OpenGLEngine>& opengl_engine,
	OpenGLTexture::Filtering filtering, OpenGLTexture::Wrapping wrapping)
{
	// conPrint("Creating new OpenGL texture.");
	Reference<OpenGLTexture> opengl_tex = new OpenGLTexture();

	// Try and load as a DXT texture compression
	const bool compressed_sRGB_support = opengl_engine->GL_EXT_texture_sRGB_support && opengl_engine->GL_EXT_texture_compression_s3tc_support;
	const size_t W = texture_data.W;
	const size_t H = texture_data.H;
	const size_t bytes_pp = texture_data.bytes_pp;
	if(opengl_engine->settings.compress_textures && compressed_sRGB_support && (bytes_pp == 3 || bytes_pp == 4))
	{
		opengl_tex->makeGLTexture(/*format=*/(bytes_pp == 3) ? OpenGLTexture::Format_Compressed_SRGB_Uint8 : OpenGLTexture::Format_Compressed_SRGBA_Uint8);

		for(size_t k=0; k<texture_data.compressed_data.size(); ++k)
		{
			const size_t level_W = myMax((size_t)1, W / ((size_t)1 << k));
			const size_t level_H = myMax((size_t)1, H / ((size_t)1 << k));

			opengl_tex->setMipMapLevelData((int)k, level_W, level_H, /*tex data=*/texture_data.compressed_data[k]);
		}

		opengl_tex->setTexParams(opengl_engine, filtering, wrapping);
	}
	else // Else if not using a compressed texture format:
	{
		OpenGLTexture::Format format;
		if(texture_data.bytes_pp == 3)
			format = OpenGLTexture::Format_SRGB_Uint8;
		else if(texture_data.bytes_pp == 4)
			format = OpenGLTexture::Format_SRGBA_Uint8;
		else
			throw Indigo::Exception("Texture has unhandled number of components: " + toString(texture_data.bytes_pp));

		opengl_tex->load(W, H, ArrayRef<uint8>(texture_data.converted_image->getData(), texture_data.converted_image->getDataSize()), opengl_engine,
			format, filtering, wrapping
		);
	}

	return opengl_tex;
}
