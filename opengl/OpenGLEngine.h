/*=====================================================================
OpenGLEngine.h
--------------
Copyright Glare Technologies Limited 2020 -
=====================================================================*/
#pragma once


#include "BasicOpenGLTypes.h"
#include "OpenGLMeshRenderData.h"
#include "TextureLoading.h"
#include "OpenGLTexture.h"
#include "OpenGLProgram.h"
#include "GLMemUsage.h"
#include "ShadowMapping.h"
#include "VertexBufferAllocator.h"
#include "DrawIndirectBuffer.h"
#include "UniformBufOb.h"
#include "VBO.h"
#include "VAO.h"
#include "SSBO.h"
#include "OpenGLCircularBuffer.h"
#include "../graphics/colour3.h"
#include "../graphics/Colour4f.h"
#include "../graphics/AnimationData.h"
#include "../graphics/BatchedMesh.h"
#include "../physics/jscol_aabbox.h"
#include "../maths/vec2.h"
#include "../maths/vec3.h"
#include "../maths/Matrix2.h"
#include "../maths/matrix3.h"
#include "../maths/plane.h"
#include "../maths/Matrix4f.h"
#include "../maths/PCG32.h"
#include "../maths/Quat.h"
#include "../utils/Timer.h"
#include "../utils/Vector.h"
#include "../utils/Reference.h"
#include "../utils/RefCounted.h"
#include "../utils/Task.h"
#include "../utils/ThreadSafeRefCounted.h"
#include "../utils/StringUtils.h"
#include "../utils/PrintOutput.h"
#include "../utils/ManagerWithCache.h"
#include "../utils/PoolAllocator.h"
#include "../utils/GeneralMemAllocator.h"
#include "../utils/ThreadManager.h"
#include "../utils/SmallArray.h"
#include "../physics/HashedGrid2.h"
#include <assert.h>
#include <unordered_set>
#include <set>
namespace Indigo { class Mesh; }
namespace glare { class TaskManager; }
class Map2D;
class TextureServer;
class UInt8ComponentValueTraits;
class TerrainSystem;
namespace glare { class BestFitAllocator; }
template <class V, class VTraits> class ImageMap;



struct OpenGLUniformVal // variant class
{
	OpenGLUniformVal() {}
	OpenGLUniformVal(int i) : intval(i) {}
	union
	{
		Vec3f vec3;
		Vec2f vec2;
		int intval;
		float floatval;
	};
};


class OpenGLMaterial
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	OpenGLMaterial()
	:	transparent(false),
		hologram(false),
		albedo_linear_rgb(0.7f, 0.7f, 0.7f),
		alpha(1.f),
		emission_linear_rgb(0.7f, 0.7f, 0.7f),
		emission_scale(0.f),
		roughness(0.5f),
		tex_matrix(1,0,0,1),
		tex_translation(0,0),
		userdata(0),
		fresnel_scale(0.5f),
		metallic_frac(0.f),
		gen_planar_uvs(false),
		draw_planar_uv_grid(false),
		convert_albedo_from_srgb(false),
		imposter(false),
		imposterable(false),
		use_wind_vert_shader(false),
		double_sided(false),
		materialise_effect(false),
		begin_fade_out_distance(100.f),
		end_fade_out_distance(120.f),
		materialise_lower_z(0),
		materialise_upper_z(1),
		materialise_start_time(-20000),
		material_data_index(-1)
	{}

	Colour3f albedo_linear_rgb; // First approximation to material colour.  Linear sRGB.
	float alpha; // Used for transparent mats.
	Colour3f emission_linear_rgb; // Linear sRGB in [0, 1]
	float emission_scale; // [0, inf)

	bool imposter; // Use imposter shader?
	bool imposterable; // Fade out with distance
	bool transparent;
	bool hologram; // E.g. just emission, no light scattering.
	bool gen_planar_uvs;
	bool draw_planar_uv_grid;
	bool convert_albedo_from_srgb;
	bool use_wind_vert_shader;
	bool double_sided;
	bool materialise_effect;

	Reference<OpenGLTexture> albedo_texture;
	Reference<OpenGLTexture> metallic_roughness_texture;
	Reference<OpenGLTexture> lightmap_texture;
	Reference<OpenGLTexture> texture_2;
	Reference<OpenGLTexture> backface_albedo_texture;
	Reference<OpenGLTexture> transmission_texture;
	Reference<OpenGLTexture> emission_texture;

	Reference<OpenGLProgram> shader_prog;
	Reference<OpenGLProgram> depth_draw_shader_prog;

	Matrix2f tex_matrix;
	Vec2f tex_translation;

	float roughness;
	float fresnel_scale;
	float metallic_frac;
	float begin_fade_out_distance; // Used when imposterable is true
	float end_fade_out_distance; // Used when imposterable is true

	float materialise_lower_z;
	float materialise_upper_z;
	float materialise_start_time;
	
	uint64 userdata;
	std::string tex_path;      // Kind-of user-data.  Only used in textureLoaded currently, which should be removed/refactored.
	std::string metallic_roughness_tex_path;      // Kind-of user-data.  Only used in textureLoaded currently, which should be removed/refactored.
	std::string lightmap_path; // Kind-of user-data.  Only used in textureLoaded currently, which should be removed/refactored.
	std::string emission_tex_path; // Kind-of user-data.  Only used in textureLoaded currently, which should be removed/refactored.

	js::Vector<OpenGLUniformVal, 16> user_uniform_vals;

	// UniformBufObRef uniform_ubo;
	int material_data_index;
};


#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4324) // Disable 'structure was padded due to __declspec(align())' warning.
#endif


struct GLObjectAnimNodeData
{
	GLARE_ALIGNED_16_NEW_DELETE

	GLObjectAnimNodeData() : procedural_transform(Matrix4f::identity()) {}

	Matrix4f node_hierarchical_to_object; // The overall transformation from bone space to object space, computed by walking up the node hierarchy.  Ephemeral data computed every frame.
	Matrix4f last_pre_proc_to_object; // Same as node_hierarchical_to_object, but without procedural_transform applied.
	Quatf last_rot;
	Matrix4f procedural_transform;
};


struct GlInstanceInfo
{
	Matrix4f to_world; // For imposters, this will not have rotation baked in.
	js::AABBox aabb_ws;
};

struct GLObject
{
	GLObject() noexcept;
	~GLObject();

	GLARE_ALIGNED_16_NEW_DELETE

	// For placement new in PoolAllocator:
#if __cplusplus >= 201703L
		void* operator new  (size_t size, std::align_val_t alignment, void* ptr) { return ptr; }
#else
		void* operator new  (size_t /*size*/, void* ptr) { return ptr; }
#endif

	void enableInstancing(VertexBufferAllocator& allocator, const void* instance_matrix_data, size_t instance_matrix_data_size); // Enables instancing attributes, and builds vert_vao.

	Matrix4f ob_to_world_matrix;
	Matrix4f ob_to_world_inv_transpose_matrix; // inverse transpose of upper-left part of to-world matrix.

	js::AABBox aabb_ws;

	Reference<OpenGLMeshRenderData> mesh_data;

	VAORef vert_vao; // Overrides mesh_data->vert_vao if non-null.  Having a vert_vao here allows us to enable instancing, by binding to the instance_matrix_vbo etc..

	//IndexBufAllocationHandle instance_matrix_vbo_handle;
	VBORef instance_matrix_vbo;
	VBORef instance_colour_vbo;
	
	bool always_visible; // For objects like the move/rotate arrows, that should be visible even when behind other objects.

	bool is_imposter;
	bool is_instanced_ob_with_imposters; // E.g. is a tree object or a tree imposter.
	int num_instances_to_draw; // e.g. num matrices built in instance_matrix_vbo.
	js::Vector<GlInstanceInfo, 16> instance_info; // Used for updating instance + imposter matrices.
	
	std::vector<OpenGLMaterial> materials;

	SmallArray<OpenGLBatch, 1> depth_draw_batches; // Index batches, use for depth buffer drawing for shadow mapping.  
	// We will use a SmallArray for this with N = 1, since the most likely number of batches is 1.

	int object_type; // 0 = tri mesh
	float line_width;
	uint32 random_num;

	// Current animation state:
	int current_anim_i; // Index into animations.
	int next_anim_i;
	double transition_start_time;
	double transition_end_time;
	double use_time_offset;

	js::Vector<GLObjectAnimNodeData, 16> anim_node_data;
	js::Vector<Matrix4f, 16> joint_matrices;

	static const int MAX_NUM_LIGHT_INDICES = 8;
	int light_indices[MAX_NUM_LIGHT_INDICES];

	//glare::PoolAllocator* allocator;
	//int allocation_index;

	int per_ob_vert_data_index;
	int joint_matrices_base_index;
	glare::BestFitAllocator::BlockInfo* joint_matrices_block;

	// From ThreadSafeRefCounted.  Manually define this stuff here, so refcount can be defined not at the start of the structure, which wastes space due to alignment issues.
	inline glare::atomic_int decRefCount() const { return refcount.decrement(); }
	inline void incRefCount() const { refcount.increment(); }
	inline glare::atomic_int getRefCount() const { return refcount; }
	mutable glare::AtomicInt refcount;
};

typedef Reference<GLObject> GLObjectRef;


void doDestroyGLOb(GLObject* ob);

// Template specialisation of destroyAndFreeOb for GLObject.  This is called when being freed by a Reference.
// We will use this to free from the OpenGLEngine PoolAllocator if the object was allocated from there.
template <>
inline void destroyAndFreeOb<GLObject>(GLObject* ob)
{
	doDestroyGLOb(ob);
}




struct GLObjectHash
{
	size_t operator() (const GLObjectRef& ob) const
	{
		return (size_t)ob.getPointer() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};


struct OverlayObject : public ThreadSafeRefCounted
{
	GLARE_ALIGNED_16_NEW_DELETE

	OverlayObject();

	Matrix4f ob_to_world_matrix;

	Reference<OpenGLMeshRenderData> mesh_data;
	
	OpenGLMaterial material;
};
typedef Reference<OverlayObject> OverlayObjectRef;


struct OverlayObjectHash
{
	size_t operator() (const OverlayObjectRef& ob) const
	{
		return (size_t)ob.getPointer() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};


class OpenGLEngineSettings
{
public:
	OpenGLEngineSettings() : enable_debug_output(false), shadow_mapping(false), compress_textures(false), /*use_final_image_buffer(false),*/ depth_fog(false), render_sun_and_clouds(true), max_tex_mem_usage(1024 * 1024 * 1024ull),
		use_grouped_vbo_allocator(true), use_general_arena_mem_allocator(true), msaa_samples(4) {}

	bool enable_debug_output;
	bool shadow_mapping;
	bool compress_textures;
	//bool use_final_image_buffer; // Render to an off-screen buffer, which can be used for post-processing.  Required for bloom post-processing.
	bool depth_fog;
	bool render_sun_and_clouds;
	bool use_grouped_vbo_allocator; // Use the best-fit allocator to group multiple vertex buffers into one VBO.  Faster rendering but uses more GPU RAM due to unused space in the VBOs.
	bool use_general_arena_mem_allocator; // Use GeneralMemAllocator with a 2GB arena for general CPU size mem allocations.
	int msaa_samples; // MSAA samples, used if use_final_image_buffer is true.  <= 1 to disable MSAA.

	uint64 max_tex_mem_usage; // Default: 1GB
};


// Should match LightData struct in phong_frag_shader.glsl etc.
struct LightGPUData
{
	LightGPUData() : light_type(0), cone_cos_angle_start(0.8f), cone_cos_angle_end(0.85f) {}
	Vec4f pos;
	Vec4f dir;
	Colour4f col;
	int light_type; // 0 = point light, 1 = spotlight
	float cone_cos_angle_start;
	float cone_cos_angle_end;
};


struct GLLight : public ThreadSafeRefCounted
{
	LightGPUData gpu_data;
	int buffer_index;
};

typedef Reference<GLLight> GLLightRef;


// The OpenGLEngine contains one or more OpenGLScene.
// An OpenGLScene is a set of objects, plus a camera transform, and associated information.
// The current scene being rendered by the OpenGLEngine can be set with OpenGLEngine::setCurrentScene().
class OpenGLScene : public ThreadSafeRefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	OpenGLScene(OpenGLEngine& engine);

	friend class OpenGLEngine;

	void setPerspectiveCameraTransform(const Matrix4f& world_to_camera_space_matrix_, float sensor_width_, float lens_sensor_dist_, float render_aspect_ratio_, float lens_shift_up_distance_,
		float lens_shift_right_distance_, float viewport_aspect_ratio);
	void setOrthoCameraTransform(const Matrix4f& world_to_camera_space_matrix_, float sensor_width_, float render_aspect_ratio_, float lens_shift_up_distance_,
		float lens_shift_right_distance_, float viewport_aspect_ratio);
	void setDiagonalOrthoCameraTransform(const Matrix4f& world_to_camera_space_matrix_, float sensor_width_, float render_aspect_ratio_, float viewport_aspect_ratio);
	void setIdentityCameraTransform();

	void calcCamFrustumVerts(float near_dist, float far_dist, Vec4f* verts_out);

	void unloadAllData();

	GLMemUsage getTotalMemUsage() const;

	Colour3f background_colour;

	bool use_z_up; // Should the +z axis be up (for cameras, sun lighting etc..).  True by default.  If false, y is up.
	// NOTE: Use only with CameraType_Identity currently, clip planes won't be computed properly otherwise.

	float bloom_strength; // [0-1].  Strength 0 turns off bloom.  0 by default.

	float wind_strength; // Default = 1.

	js::Vector<Vec4f, 16> blob_shadow_locations;
private:
	float use_sensor_width;
	float use_sensor_height;
	float sensor_width;
	
	float lens_sensor_dist;
	float render_aspect_ratio;
	float lens_shift_up_distance;
	float lens_shift_right_distance;

	enum CameraType
	{
		CameraType_Identity, // Identity camera transform.
		CameraType_Perspective,
		CameraType_Orthographic,
		CameraType_DiagonalOrthographic
	};

	CameraType camera_type;

	Matrix4f world_to_camera_space_matrix;
	Matrix4f cam_to_world;
public:
	// NOTE: Use std::set instead of unordered_set, so that iteration over objects is in memory order.
	std::set<Reference<GLObject>> objects;
	std::set<Reference<GLObject>> animated_objects; // Objects for which we need to update the animation data (bone matrices etc.) every frame.
	std::set<Reference<GLObject>> transparent_objects;
	std::set<Reference<GLObject>> always_visible_objects; // For objects like the move/rotate arrows, that should be visible even when behind other objects.
	std::set<Reference<OverlayObject>> overlay_objects; // UI overlays
	std::set<Reference<GLObject>> objects_with_imposters;
	std::set<Reference<GLObject>> materialise_objects; // Objects currently playing materialise effect
	
	GLObjectRef env_ob;

	std::set<Reference<GLLight>> lights;

	HashedGrid2<GLLight*, std::hash<GLLight*>> light_grid;

private:
	float max_draw_dist;
	float near_draw_dist;

	Planef frustum_clip_planes[6];
	int num_frustum_clip_planes;
	Vec4f frustum_verts[8];
	js::AABBox frustum_aabb;
};


typedef Reference<OpenGLScene> OpenGLSceneRef;


struct OpenGLSceneHash
{
	size_t operator() (const OpenGLSceneRef& scene) const
	{
		return (size_t)scene.getPointer() >> 3; // Assuming 8-byte aligned, get rid of lower zero bits.
	}
};


struct FrameBufTextures
{
	Reference<OpenGLTexture> colour_texture;
	Reference<OpenGLTexture> emission_texture;
	Reference<OpenGLTexture> depth_texture;
};


/*
We will allocate a certain number of bits to each type of ID.
The more expensive the state change, the more significant bit position we allocate the IDs.
index type is in here so we can group together more calls for multi-draw-indirect.
If the actual ID exceeds this number of bits, rendering will still be correct, we just will do more state changes than strictly needed.

program_index:   8 bits
VAO id:	        10 bits
vert VBO id:     6 bits
index VBO id:    6 bits
index type bits: 2 bits
*/
struct BatchDrawInfo
{
	BatchDrawInfo() {}
	BatchDrawInfo(uint32 program_index, uint32 vao_id, uint32 vert_vbo_id, uint32 idx_vbo_id, uint32 index_type_bits, const GLObject* ob_, uint32 batch_i_) 
	:	prog_vao_key(((program_index & 255u) << 24) | ((vao_id & 1023u) << 14) | ((vert_vbo_id & 63u) << 8) | ((idx_vbo_id & 63u) << 2) | index_type_bits),
		batch_i(batch_i_), ob(ob_)
	{
		assert(index_type_bits <= 2);
	}
	std::string keyDescription() const;

	uint32 prog_vao_key;
	uint32 batch_i;
	const GLObject* ob;
	
	//uint32 flags; // 1 = use shading normals

	bool operator < (const BatchDrawInfo& other) const
	{
		if(prog_vao_key < other.prog_vao_key)
			return true;
		else if(prog_vao_key > other.prog_vao_key)
			return false;
		else
			return ob->mesh_data.ptr() < other.ob->mesh_data.ptr();
	}
};


// Matches that defined in phong_frag_shader.glsl.
struct PhongUniforms
{
	Colour4f diffuse_colour; // linear sRGB
	Colour4f emission_colour; // linear sRGB
	Vec2f texture_upper_left_matrix_col0;
	Vec2f texture_upper_left_matrix_col1;
	Vec2f texture_matrix_translation;

	uint64 diffuse_tex; // Bindless texture handle
	uint64 metallic_roughness_tex; // Bindless texture handle
	uint64 lightmap_tex; // Bindless texture handle
	uint64 emission_tex; // Bindless texture handle
	uint64 backface_albedo_tex; // Bindless texture handle
	uint64 transmission_tex; // Bindless texture handle

	int flags;
	float roughness;
	float fresnel_scale;
	float metallic_frac;
	float begin_fade_out_distance;
	float end_fade_out_distance;

	float materialise_lower_z;
	float materialise_upper_z;
	float materialise_start_time;

	int light_indices[8];
};


// Matches that defined in transparent_frag_shader.glsl.
struct TransparentUniforms
{
	Colour4f diffuse_colour; // linear sRGB
	Colour4f emission_colour; // linear sRGB
	Vec2f texture_upper_left_matrix_col0;
	Vec2f texture_upper_left_matrix_col1;
	Vec2f texture_matrix_translation;

	uint64 diffuse_tex; // Bindless texture handle
	uint64 emission_tex; // Bindless texture handle

	int flags;
	float roughness;

	int light_indices[8];
};


struct MaterialCommonUniforms
{
	Vec4f sundir_cs;
	float time;
};


// Matches DepthUniforms defined in depth_frag_shader.glsl
SSE_CLASS_ALIGN DepthUniforms
{
public:
	Vec2f texture_upper_left_matrix_col0;
	Vec2f texture_upper_left_matrix_col1;
	Vec2f texture_matrix_translation;

	uint64 diffuse_tex; // Bindless texture handle

	float materialise_lower_z;
	float materialise_upper_z;
	float materialise_start_time;
};


struct SharedVertUniforms
{
	Matrix4f proj_matrix; // same for all objects
	Matrix4f view_matrix; // same for all objects
	//#if NUM_DEPTH_TEXTURES > 0
	Matrix4f shadow_texture_matrix[ShadowMapping::NUM_DYNAMIC_DEPTH_TEXTURES + ShadowMapping::NUM_STATIC_DEPTH_TEXTURES]; // same for all objects
	//#endif
	Vec4f campos_ws; // same for all objects
	float vert_uniforms_time;
	float wind_strength;
};


struct PerObjectVertUniforms
{
	Matrix4f model_matrix; // per-object
	Matrix4f normal_matrix; // per-object
};


// See https://www.khronos.org/registry/OpenGL-Refpages/gl4/html/glMultiDrawElementsIndirect.xhtml
struct DrawElementsIndirectCommand
{
	uint32 count;
	uint32 instanceCount;
	uint32 firstIndex;
	uint32 baseVertex;
	uint32 baseInstance;
};


struct MeshDataLoadingProgress
{
	MeshDataLoadingProgress() : vert_total_size_B(0), vert_next_i(0), index_total_size_B(0), index_next_i(0) {}

	bool done() { return vert_next_i >= vert_total_size_B && index_next_i >= index_total_size_B; }

	std::string summaryString() const;

	size_t vert_total_size_B;
	size_t vert_next_i;
	size_t index_total_size_B;
	size_t index_next_i;
};

class OpenGLEngine : public ThreadSafeRefCounted
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	OpenGLEngine(const OpenGLEngineSettings& settings);
	~OpenGLEngine();

	friend class TextureLoading;

	//---------------------------- Initialisation/deinitialisation --------------------------
	void initialise(const std::string& data_dir, TextureServer* texture_server, PrintOutput* print_output); // data_dir should have 'shaders' and 'gl_data' in it.
	bool initSucceeded() const { return init_succeeded; }
	std::string getInitialisationErrorMsg() const { return initialisation_error_msg; }

	void unloadAllData();

	const std::string getPreprocessorDefines() const { return preprocessor_defines; } // for compiling shader programs
	const std::string getVersionDirective() const { return version_directive; } // for compiling shader programs
	//----------------------------------------------------------------------------------------


	//---------------------------- Adding and removing objects -------------------------------

	Reference<GLObject> allocateObject();

	// Add object to world.  Doesn't load textures for object.
	void addObject(const Reference<GLObject>& object);
	void addObjectAndLoadTexturesImmediately(const Reference<GLObject>& object);
	void removeObject(const Reference<GLObject>& object);
	bool isObjectAdded(const Reference<GLObject>& object) const;

	// Overlay objects are considered to be in OpenGL clip-space coordinates.
	// The x axis is to the right, y up, and negative z away from the camera into the scene.
	// x=-1 is the left edge of the viewport, x=+1 is the right edge.
	// y=-1 is the bottom edge of the viewport, y=+1 is the top edge.
	//
	// Overlay objects are drawn back-to-front, without z-testing, using the overlay shaders.
	void addOverlayObject(const Reference<OverlayObject>& object);
	void removeOverlayObject(const Reference<OverlayObject>& object);
	//----------------------------------------------------------------------------------------


	//---------------------------- Lights ----------------------------------------------------
	void addLight(GLLightRef light);
	void removeLight(GLLightRef light);
	void lightUpdated(GLLightRef light);
	void setLightPos(GLLightRef light, const Vec4f& new_pos);
	//----------------------------------------------------------------------------------------

	//---------------------------- Updating objects ------------------------------------------
	void updateObjectTransformData(GLObject& object);
	void objectTransformDataChanged(GLObject& object); // Update object data on GPU
	const js::AABBox getAABBWSForObjectWithTransform(GLObject& object, const Matrix4f& to_world);

	void newMaterialUsed(OpenGLMaterial& mat, bool use_vert_colours, bool uses_instancing, bool uses_skinning);
	void objectMaterialsUpdated(GLObject& object);
	void materialTextureChanged(GLObject& object, OpenGLMaterial& mat);  // Update material data on GPU
	//----------------------------------------------------------------------------------------


	//---------------------------- Object selection ------------------------------------------
	void selectObject(const Reference<GLObject>& object);
	void deselectObject(const Reference<GLObject>& object);
	void deselectAllObjects();
	void setSelectionOutlineColour(const Colour4f& col);
	void setSelectionOutlineWidth(float line_width_px);
	//----------------------------------------------------------------------------------------


	//---------------------------- Object queries --------------------------------------------
	bool isObjectInCameraFrustum(const GLObject& object);
	//----------------------------------------------------------------------------------------


	//---------------------------- Camera queries --------------------------------------------
	Vec4f getCameraPositionWS() const;

	// Return window coordinates - e.g. coordinates in the viewport, for a given world space position.  (0,0) is the top left of the viewport.
	bool getWindowCoordsForWSPos(const Vec4f& pos_ws, Vec2f& coords_out) const; // Returns true if in front of camera
	//----------------------------------------------------------------------------------------


	//---------------------------- Texture loading -------------------------------------------
	// Return an OpenGL texture based on tex_path.  Loads it from disk if needed.  Blocking.
	// Throws glare::Exception if texture could not be loaded.
	Reference<OpenGLTexture> getTexture(const std::string& tex_path, bool allow_compression = true);

	// If the texture identified by key has been loaded into OpenGL, then return the OpenGL texture.
	// If the texture is not loaded, return a null reference.
	// Throws glare::Exception
	Reference<OpenGLTexture> getTextureIfLoaded(const OpenGLTextureKey& key, bool use_sRGB, bool use_mipmaps = true);

	Reference<OpenGLTexture> loadCubeMap(const std::vector<Reference<Map2D> >& face_maps,
		OpenGLTexture::Filtering filtering = OpenGLTexture::Filtering_Fancy, OpenGLTexture::Wrapping wrapping = OpenGLTexture::Wrapping_Repeat);

	// If the texture identified by key has been loaded into OpenGL, then return the OpenGL texture.
	// Otherwise load the texure from map2d into OpenGL immediately.
	Reference<OpenGLTexture> getOrLoadOpenGLTextureForMap2D(const OpenGLTextureKey& key, const Map2D& map2d, /*BuildUInt8MapTextureDataScratchState& state,*/
		OpenGLTexture::Filtering filtering = OpenGLTexture::Filtering_Fancy, OpenGLTexture::Wrapping wrapping = OpenGLTexture::Wrapping_Repeat, bool allow_compression = true, bool use_sRGB = true, bool use_mipmaps = true);

	void addOpenGLTexture(const OpenGLTextureKey& key, const Reference<OpenGLTexture>& tex); // Adds to opengl_textures.  Assigns texture to all inserted objects that are using it according to opengl_mat.tex_path.

	void removeOpenGLTexture(const OpenGLTextureKey& key); // Erases from opengl_textures.

	bool isOpenGLTextureInsertedForKey(const OpenGLTextureKey& key) const;

	TextureServer* getTextureServer() { return texture_server; }

	bool textureCompressionSupportedAndEnabled() const { return GL_EXT_texture_compression_s3tc_support && settings.compress_textures; }
	//------------------------------- End texture loading ------------------------------------


	//------------------------------- Camera management --------------------------------------
	void setPerspectiveCameraTransform(const Matrix4f& world_to_camera_space_matrix, float sensor_width, float lens_sensor_dist, float render_aspect_ratio, float lens_shift_up_distance,
		float lens_shift_right_distance);

	// If world_to_camera_space_matrix is the identity, results in a camera, with forwards = +y, right = +x, up = +z. 
	// Left clip plane will be at camera position x - sensor_width/2, right clip plane at camera position x + sensor_width/2
	// Cam space width = sensor_width.
	void setOrthoCameraTransform(const Matrix4f& world_to_camera_space_matrix, float sensor_width, float render_aspect_ratio, float lens_shift_up_distance,
		float lens_shift_right_distance);

	void setDiagonalOrthoCameraTransform(const Matrix4f& world_to_camera_space_matrix, float sensor_width, float render_aspect_ratio);

	void setIdentityCameraTransform(); // See also use_z_up to use z-up like opengl.
	//----------------------------------------------------------------------------------------


	//------------------------------- Environment material/map management --------------------
	void setSunDir(const Vec4f& d);
	const Vec4f getSunDir() const;

	void setEnvMapTransform(const Matrix3f& transform);

	void setEnvMat(const OpenGLMaterial& env_mat);
	const OpenGLMaterial& getEnvMat() const { return current_scene->env_ob->materials[0]; }

	void setCirrusTexture(const Reference<OpenGLTexture>& tex);
	//----------------------------------------------------------------------------------------


	//--------------------------------------- Drawing ----------------------------------------
	void setDrawWireFrames(bool draw_wireframes_) { draw_wireframes = draw_wireframes_; }
	void setMaxDrawDistance(float d) { current_scene->max_draw_dist = d; } // Set far draw distance
	void setNearDrawDistance(float d) { current_scene->near_draw_dist = d; } // Set near draw distance
	void setCurrentTime(float time);

	void draw();
	//----------------------------------------------------------------------------------------


	//--------------------------------------- Viewport ----------------------------------------
	// This will be the resolution at which the main render framebuffer is allocated, for which res bloom textures are allocated etc..
	void setMainViewport(int main_viewport_w_, int main_viewport_h_);


	void setViewport(int viewport_w_, int viewport_h_);
	int getViewPortWidth()  const { return viewport_w; }
	int getViewPortHeight() const { return viewport_h; }
	float getViewPortAspectRatio() const { return (float)getViewPortWidth() / (float)(getViewPortHeight()); } // Viewport width / viewport height.
	//----------------------------------------------------------------------------------------


	//----------------------------------- Mesh functions -------------------------------------
	Reference<OpenGLMeshRenderData> getLineMeshData(); // A line from (0, 0, 0) to (1, 0, 0)
	Reference<OpenGLMeshRenderData> getSphereMeshData();
	Reference<OpenGLMeshRenderData> getCubeMeshData();
	Reference<OpenGLMeshRenderData> getUnitQuadMeshData(); // A quad from (0, 0, 0) to (1, 1, 0)
	Reference<OpenGLMeshRenderData> getCylinderMesh(); // A cylinder from (0,0,0), to (0,0,1) with radius 1;

	GLObjectRef makeAABBObject(const Vec4f& min_, const Vec4f& max_, const Colour4f& col);
	GLObjectRef makeArrowObject(const Vec4f& startpos, const Vec4f& endpos, const Colour4f& col, float radius_scale);
	GLObjectRef makeSphereObject(float radius, const Colour4f& col);
	GLObjectRef makeCylinderObject(float radius, const Colour4f& col); // A cylinder from (0,0,0), to (0,0,1) with radius 1;

	static Matrix4f AABBObjectTransform(const Vec4f& min_os, const Vec4f& max_os);
	static Matrix4f arrowObjectTransform(const Vec4f& startpos, const Vec4f& endpos, float radius_scale);
	

	static Reference<OpenGLMeshRenderData> buildMeshRenderData(VertexBufferAllocator& allocator, const js::Vector<Vec3f, 16>& vertices, const js::Vector<Vec3f, 16>& normals, const js::Vector<Vec2f, 16>& uvs, const js::Vector<uint32, 16>& indices);

	static void initialiseMeshDataLoadingProgress(OpenGLMeshRenderData& data, MeshDataLoadingProgress& loading_progress);
	static void partialLoadOpenGLMeshDataIntoOpenGL(VertexBufferAllocator& allocator, OpenGLMeshRenderData& data, MeshDataLoadingProgress& loading_progress, size_t& total_bytes_uploaded_in_out, size_t max_total_upload_bytes);

	static void loadOpenGLMeshDataIntoOpenGL(VertexBufferAllocator& allocator, OpenGLMeshRenderData& data);
	//---------------------------- End mesh functions ----------------------------------------


	//----------------------------------- Readback -------------------------------------------
	float getPixelDepth(int pixel_x, int pixel_y);

	Reference<ImageMap<uint8, UInt8ComponentValueTraits> > getRenderedColourBuffer();
	//----------------------------------------------------------------------------------------


	//----------------------------------- Target framebuffer ---------------------------------
	// Set the primary render target frame buffer.
	void setTargetFrameBuffer(const Reference<FrameBuffer> frame_buffer) { target_frame_buffer = frame_buffer; }
	void setTargetFrameBufferAndViewport(const Reference<FrameBuffer> frame_buffer); // Set target framebuffer, also set viewport to the whole framebuffer.

	Reference<FrameBuffer> getTargetFrameBuffer() const { return target_frame_buffer; }
	//----------------------------------------------------------------------------------------
	

	//----------------------------------- Scene management -----------------------------------
	void addScene(const Reference<OpenGLScene>& scene);
	void removeScene(const Reference<OpenGLScene>& scene);
	void setCurrentScene(const Reference<OpenGLScene>& scene);
	OpenGLScene* getCurrentScene() { return current_scene.ptr(); }
	//----------------------------------------------------------------------------------------


	//----------------------------------- Diagnostics ----------------------------------------
	GLMemUsage getTotalMemUsage() const;

	std::string getDiagnostics() const;

	void setProfilingEnabled(bool enabled) { query_profiling_enabled = enabled; }
	//----------------------------------------------------------------------------------------

	//----------------------------------- Settings ----------------------------------------
	//void setMSAAEnabled(bool enabled);

	bool openglDriverVendorIsIntel() const; // Works after opengl_vendor is set in initialise().
	//----------------------------------------------------------------------------------------

	void shaderFileChanged(); // Called by ShaderFileWatcherThread, from another thread.
private:
	void updateMaterialDataOnGPU(const GLObject& ob, size_t mat_index);
	void calcCamFrustumVerts(float near_dist, float far_dist, Vec4f* verts_out);
	void assignLightsToObject(GLObject& ob);
	void assignLightsToAllObjects();
public:
	void assignShaderProgToMaterial(OpenGLMaterial& material, bool use_vert_colours, bool uses_instancing, bool uses_skinning);
private:
	// Set uniforms that are the same for every batch for the duration of this frame.
	void setSharedUniformsForProg(const OpenGLProgram& shader_prog, const Matrix4f& view_mat, const Matrix4f& proj_mat);
	void drawBatch(const GLObject& ob, const OpenGLMaterial& opengl_mat, const OpenGLProgram& shader_prog, const OpenGLMeshRenderData& mesh_data, const OpenGLBatch& batch);
	void buildOutlineTextures();
private:
	void drawDebugPlane(const Vec3f& point_on_plane, const Vec3f& plane_normal, const Matrix4f& view_matrix, const Matrix4f& proj_matrix,
		float plane_draw_half_width);
	void drawDebugSphere(const Vec4f& point, float radius, const Matrix4f& view_matrix, const Matrix4f& proj_matrix);

public:
	static void getUniformLocations(Reference<OpenGLProgram>& phong_prog, bool shadow_mapping_enabled, UniformLocations& phong_locations_out);
private:
	void doPhongProgramBindingsForProgramChange(const UniformLocations& locations);
	void setUniformsForPhongProg(const GLObject& ob, const OpenGLMaterial& opengl_mat, const OpenGLMeshRenderData& mesh_data, PhongUniforms& uniforms);
	void setUniformsForTransparentProg(const GLObject& ob, const OpenGLMaterial& opengl_mat, const OpenGLMeshRenderData& mesh_data);
	void partiallyClearBuffer(const Vec2f& begin, const Vec2f& end);
	Matrix4f getReverseZMatrixOrIdentity() const;

	void addDebugHexahedron(const Vec4f* verts_ws, const Colour4f& col);

	OpenGLProgramRef getProgramWithFallbackOnError(const ProgramKey& key);

	OpenGLProgramRef getPhongProgram(const ProgramKey& key); // Throws glare::Exception on shader compilation failure.
	OpenGLProgramRef getTransparentProgram(const ProgramKey& key); // Throws glare::Exception on shader compilation failure.
	OpenGLProgramRef getImposterProgram(const ProgramKey& key); // Throws glare::Exception on shader compilation failure.
	OpenGLProgramRef getDepthDrawProgram(const ProgramKey& key); // Throws glare::Exception on shader compilation failure.
	OpenGLProgramRef getDepthDrawProgramWithFallbackOnError(const ProgramKey& key);
public:
	OpenGLProgramRef buildProgram(const std::string& shader_name_prefix, const ProgramKey& key); // Throws glare::Exception on shader compilation failure.
	uint32 getAndIncrNextProgramIndex() { return next_program_index++; }

	glare::TaskManager& getTaskManager();

	void textureBecameUnused(const OpenGLTexture* tex);
	void textureBecameUsed(const OpenGLTexture* tex);
private:
	void trimTextureUsage();
	void updateInstanceMatricesForObWithImposters(GLObject& ob, bool for_shadow_mapping);
	void bindMeshData(const OpenGLMeshRenderData& mesh_data);
	void bindMeshData(const GLObject& ob);
	bool checkUseProgram(const OpenGLProgram* prog);
	void submitBufferedDrawCommands();
	void sortBatchDrawInfos();
	void getCameraShadowMappingPlanesAndAABB(float near_dist, float far_dist, float max_shadowing_dist, Planef* shadow_clip_planes_out, js::AABBox& shadow_vol_aabb_out);
	void assignLoadedTextureToObMaterials(const std::string& path, Reference<OpenGLTexture> opengl_texture);
	void expandPerObVertDataBuffer();
	void expandPhongBuffer();
	void expandJointMatricesBuffer(size_t min_extra_needed);
	void checkMDIGPUDataCorrect();

	bool init_succeeded;
	std::string initialisation_error_msg;
	
	Vec4f sun_dir; // Dir to sun.
	Vec4f sun_dir_cam_space;

	Reference<OpenGLMeshRenderData> line_meshdata;
	Reference<OpenGLMeshRenderData> sphere_meshdata;
	Reference<OpenGLMeshRenderData> arrow_meshdata;
	Reference<OpenGLMeshRenderData> cube_meshdata;
	Reference<OpenGLMeshRenderData> unit_quad_meshdata;
	Reference<OpenGLMeshRenderData> cylinder_meshdata;


	int main_viewport_w, main_viewport_h;

	int viewport_w, viewport_h;
	
	Planef shadow_clip_planes[6];
	std::vector<OverlayObject*> temp_obs;

#if !defined(OSX)
	GLuint timer_query_id;
#endif

	uint64 num_face_groups_submitted;
	uint64 num_indices_submitted;
	uint64 num_aabbs_submitted;

	std::string preprocessor_defines;
	std::string version_directive;

	// Map from preprocessor defs to built program.
	std::map<ProgramKey, OpenGLProgramRef> progs;
	OpenGLProgramRef fallback_phong_prog;
	OpenGLProgramRef fallback_transparent_prog;
	OpenGLProgramRef fallback_depth_prog;
	uint32 next_program_index;

	Reference<OpenGLProgram> env_prog;
	int env_diffuse_colour_location;
	int env_have_texture_location;
	int env_diffuse_tex_location;
	int env_texture_matrix_location;
	int env_sundir_cs_location;
	int env_noise_tex_location;
	int env_fbm_tex_location;
	int env_cirrus_tex_location;

	Reference<OpenGLTexture> fbm_tex;
	Reference<OpenGLTexture> blue_noise_tex;
	Reference<OpenGLTexture> noise_tex;
	Reference<OpenGLTexture> cirrus_tex;

	Reference<OpenGLProgram> overlay_prog;
	int overlay_diffuse_colour_location;
	int overlay_have_texture_location;
	int overlay_diffuse_tex_location;
	int overlay_texture_matrix_location;

	Reference<OpenGLProgram> clear_prog;

	Reference<OpenGLProgram> outline_prog_no_skinning; // Used for drawing constant flat shaded pixels currently.
	Reference<OpenGLProgram> outline_prog_with_skinning; // Used for drawing constant flat shaded pixels currently.

	Reference<OpenGLProgram> edge_extract_prog;
	int edge_extract_tex_location;
	int edge_extract_col_location;
	int edge_extract_line_width_location;

	Colour4f outline_colour;
	float outline_width_px;

	Reference<OpenGLProgram> downsize_prog;
	Reference<OpenGLProgram> downsize_from_main_buf_prog;
	Reference<OpenGLProgram> gaussian_blur_prog;

	Reference<OpenGLProgram> final_imaging_prog; // Adds bloom, tonemaps

	//size_t vert_mem_used; // B
	//size_t index_mem_used; // B

	std::string data_dir;

	Reference<ShadowMapping> shadow_mapping;

	// Reference<TerrainSystem> terrain_system;

	OverlayObjectRef clear_buf_overlay_ob;

	double draw_time;
	Timer draw_timer;

	ManagerWithCache<OpenGLTextureKey, Reference<OpenGLTexture>, OpenGLTextureKeyHash> opengl_textures;
private:
	size_t outline_tex_w, outline_tex_h;
	Reference<FrameBuffer> outline_solid_framebuffer;
	Reference<FrameBuffer> outline_edge_framebuffer;
	Reference<OpenGLTexture> outline_solid_tex;
	Reference<OpenGLTexture> outline_edge_tex;
	OpenGLMaterial outline_solid_mat; // Material for drawing selected objects with a flat, unshaded colour.
	Reference<OpenGLMeshRenderData> outline_quad_meshdata;
	OpenGLMaterial outline_edge_mat; // Material for drawing the actual edge overlay.

	std::vector<Reference<FrameBuffer> > downsize_framebuffers;
	std::vector<Reference<OpenGLTexture> > downsize_target_textures;

	std::vector<Reference<FrameBuffer> > blur_framebuffers_x; // Blurred in x direction
	std::vector<Reference<OpenGLTexture> > blur_target_textures_x;

	std::vector<Reference<FrameBuffer> > blur_framebuffers; // Blurred in both directions:
	std::vector<Reference<OpenGLTexture> > blur_target_textures;

	Reference<FrameBuffer> main_render_framebuffer;

	OpenGLTextureRef main_colour_texture;
	OpenGLTextureRef transparent_accum_texture;
	OpenGLTextureRef av_transmittance_texture;
	OpenGLTextureRef main_depth_texture;

	//Reference<FrameBuffer> pre_water_framebuffer;
	//OpenGLTextureRef pre_water_colour_tex;
	//OpenGLTextureRef pre_water_depth_tex;

	std::unordered_set<GLObject*> selected_objects;

	bool draw_wireframes;

	GLObjectRef debug_arrow_ob;
	GLObjectRef debug_sphere_ob;

	std::vector<GLObjectRef> debug_joint_obs;

	Reference<OpenGLTexture> cosine_env_tex;
	Reference<OpenGLTexture> specular_env_tex;

	float current_time;

	TextureServer* texture_server;

	Reference<FrameBuffer> target_frame_buffer;

	mutable Mutex task_manager_mutex;
	glare::TaskManager* task_manager GUARDED_BY(task_manager_mutex); // Used for building 8-bit texture data (DXT compression, mip-map data building).  Lazily created when needed.
public:
	std::string opengl_vendor;
	std::string opengl_renderer;
	std::string opengl_version;
	std::string glsl_version;

	bool GL_EXT_texture_sRGB_support;
	bool GL_EXT_texture_compression_s3tc_support;
	bool GL_ARB_bindless_texture_support;
	bool GL_ARB_clip_control_support;
	bool GL_ARB_shader_storage_buffer_object_support;
	float max_anisotropy;
	bool use_bindless_textures;
	bool use_multi_draw_indirect;
	bool use_reverse_z;

	OpenGLEngineSettings settings;

	uint64 frame_num;

	bool are_8bit_textures_sRGB; // If true, load textures as sRGB, otherwise treat them as in an 8-bit colour space.  Defaults to true.
	// Currently compressed textures are always treated as sRGB.

private:
	std::unordered_set<Reference<OpenGLScene>, OpenGLSceneHash> scenes;
	Reference<OpenGLScene> current_scene;

	PCG32 rng;

	uint64 last_num_obs_in_frustum;

	js::Vector<BatchDrawInfo, 16> batch_draw_info;
	uint32 num_prog_changes;
	uint32 num_batches_bound;
	uint32 num_vao_binds;
	uint32 num_vbo_binds;
	uint32 num_index_buf_binds;
	
	uint32 last_num_prog_changes;
	uint32 last_num_batches_bound;
	uint32 last_num_vao_binds;
	uint32 last_num_vbo_binds;
	uint32 last_num_index_buf_binds;

	uint32 depth_draw_last_num_prog_changes;
	uint32 depth_draw_last_num_batches_bound;
	uint32 depth_draw_last_num_vao_binds;
	uint32 depth_draw_last_num_vbo_binds;

	uint32 last_num_animated_obs_processed;

	uint32 num_multi_draw_indirect_calls;

	Timer fps_display_timer;
	int num_frames_since_fps_timer_reset;


	double last_anim_update_duration;
	double last_depth_map_gen_GPU_time;
	double last_render_GPU_time;
	double last_draw_CPU_time;
	double last_fps;

	bool query_profiling_enabled;


	UniformBufObRef phong_uniform_buf_ob;
	UniformBufObRef transparent_uniform_buf_ob;
	UniformBufObRef material_common_uniform_buf_ob;
	UniformBufObRef depth_uniform_buf_ob;
	UniformBufObRef shared_vert_uniform_buf_ob;
	UniformBufObRef per_object_vert_uniform_buf_ob;

	// Some temporary vectors:
	js::Vector<Matrix4f, 16> node_matrices;
	js::Vector<AnimationKeyFrameLocation, 16> key_frame_locs;

	js::Vector<Matrix4f, 16> temp_matrices;


	// For MDI:
	js::Vector<uint32, 16> ob_and_mat_indices_buffer;
	SSBORef ob_and_mat_indices_ssbo;
	OpenGLCircularBuffer ob_and_mat_indices_circ_buf;

	DrawIndirectBufferRef draw_indirect_buffer;
	OpenGLCircularBuffer draw_indirect_circ_buf;

	js::Vector<DrawElementsIndirectCommand, 16> draw_commands;
	
	GLenum current_index_type;
	const OpenGLProgram* current_bound_prog;
	const VAO* current_bound_VAO;
	const GLObject* current_uniforms_ob; // Last object for which we uploaded joint matrices, and set the per_object_vert_uniform_buf_ob.  Used to stop uploading the same uniforms repeatedly for the same object.

	
public:
	PrintOutput* print_output; // May be NULL

	uint64 tex_mem_usage; // Running sum of GPU RAM used by inserted textures.

	uint64 max_tex_mem_usage;

	VertexBufferAllocatorRef vert_buf_allocator;

	uint64 total_available_GPU_mem_B; // Set by NVidia drivers
	uint64 total_available_GPU_VBO_mem_B; // Set by AMD drivers

	Reference<glare::Allocator> mem_allocator;

private:
	glare::PoolAllocator object_pool_allocator;

	SSBORef light_buffer; // SSBO
	UniformBufObRef light_ubo; // UBO for Mac.

	std::set<int> light_free_indices;

	std::set<int> per_ob_vert_data_free_indices;
	SSBORef per_ob_vert_data_buffer; // SSBO

	std::set<int> phong_buffer_free_indices;
	SSBORef phong_buffer; // SSBO

	SSBORef joint_matrices_ssbo;
	Reference<glare::BestFitAllocator> joint_matrices_allocator;

	glare::AtomicInt shader_file_changed;

	ThreadManager thread_manager; // For ShaderFileWatcherThread

	bool running_in_renderdoc;
};


#ifdef _WIN32
#pragma warning(pop)
#endif


void checkForOpenGLErrors();


