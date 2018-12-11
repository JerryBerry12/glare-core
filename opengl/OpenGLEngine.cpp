/*=====================================================================
OpenGLEngine.cpp
----------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "IncludeOpenGL.h"
#include "OpenGLEngine.h"


#include "OpenGLProgram.h"
#include "OpenGLShader.h"
#include "ShadowMapping.h"
#include "../dll/include/IndigoMesh.h"
#include "../graphics/ImageMap.h"
#include "../graphics/imformatdecoder.h"
#include "../graphics/SRGBUtils.h"
#define STB_DXT_STATIC 1
#define STB_DXT_IMPLEMENTATION 1
#include "../libs/stb/stb_dxt.h"
#include "../indigo/globals.h"
#include "../indigo/TextureServer.h"
#include "../indigo/TestUtils.h"
#include "../maths/vec3.h"
#include "../maths/GeometrySampling.h"
#include "../maths/matrix3.h"
#include "../utils/Lock.h"
#include "../utils/Mutex.h"
#include "../utils/Clock.h"
#include "../utils/Timer.h"
#include "../utils/Platform.h"
#include "../utils/FileUtils.h"
#include "../utils/Reference.h"
#include "../utils/StringUtils.h"
#include "../utils/Exception.h"
#include "../utils/BitUtils.h"
#include "../utils/Vector.h"
#include "../utils/Task.h"
#include "../utils/IncludeXXHash.h"
#include "../utils/ArrayRef.h"
#include "../utils/HashMapInsertOnly2.h"
#include "../utils/IncludeHalf.h"
#include "../utils/TaskManager.h"
#include <algorithm>


static const bool PROFILE = false;
static const bool MEM_PROFILE = false;


OpenGLEngine::OpenGLEngine(const OpenGLEngineSettings& settings_)
:	init_succeeded(false),
	anisotropic_filtering_supported(false),
	settings(settings_),
	draw_wireframes(false),
	frame_num(0),
	current_time(0.f),
	task_manager(NULL)
{
	viewport_aspect_ratio = 1;
	max_draw_dist = 1;
	render_aspect_ratio = 1;
	lens_shift_up_distance = 0;
	lens_shift_right_distance = 0;
	viewport_w = viewport_h = 100;

	sun_dir = normalise(Vec4f(0.2,0.2,1,0));
	env_ob = new GLObject();
	env_ob->ob_to_world_matrix = Matrix4f::identity();
	env_ob->ob_to_world_inv_tranpose_matrix = Matrix4f::identity();
	env_ob->materials.resize(1);

	camera_type = CameraType_Perspective;
}


OpenGLEngine::~OpenGLEngine()
{
	delete task_manager;
}


static const Vec4f FORWARDS_OS(0.0f, 1.0f, 0.0f, 0.0f); // Forwards in local camera (object) space.
static const Vec4f UP_OS(0.0f, 0.0f, 1.0f, 0.0f);
static const Vec4f RIGHT_OS(1.0f, 0.0f, 0.0f, 0.0f);


void OpenGLEngine::setPerspectiveCameraTransform(const Matrix4f& world_to_camera_space_matrix_, float sensor_width_, float lens_sensor_dist_, float render_aspect_ratio_, float lens_shift_up_distance_,
	float lens_shift_right_distance_)
{
	camera_type = CameraType_Perspective;

	this->sensor_width = sensor_width_;
	this->world_to_camera_space_matrix = world_to_camera_space_matrix_;
	this->lens_sensor_dist = lens_sensor_dist_;
	this->render_aspect_ratio = render_aspect_ratio_;
	this->lens_shift_up_distance = lens_shift_up_distance_;
	this->lens_shift_right_distance = lens_shift_right_distance_;

	if(viewport_aspect_ratio > render_aspect_ratio) // if viewport has a wider aspect ratio than the render:
	{
		use_sensor_height = sensor_width / render_aspect_ratio; // Keep vertical field of view the same
		use_sensor_width = use_sensor_height * viewport_aspect_ratio; // enlarge horizontal field of view as needed
	}
	else
	{
		use_sensor_width = sensor_width; // Keep horizontal field of view the same
		use_sensor_height = sensor_width / viewport_aspect_ratio; // Enlarge vertical field of view as needed
	}


	// Make clipping planes
	const Vec4f lens_center(lens_shift_right_distance, 0, lens_shift_up_distance, 1);
	const Vec4f sensor_center(0, -lens_sensor_dist, 0, 1);

	const Vec4f sensor_bottom = (sensor_center - UP_OS    * (use_sensor_height * 0.5f));
	const Vec4f sensor_top    = (sensor_center + UP_OS    * (use_sensor_height * 0.5f));
	const Vec4f sensor_left   = (sensor_center - RIGHT_OS * (use_sensor_width  * 0.5f));
	const Vec4f sensor_right  = (sensor_center + RIGHT_OS * (use_sensor_width  * 0.5f));

	Planef planes_cs[5]; // Clipping planes in camera space

	// We won't bother with a culling plane at the near clip plane, since it near draw distance is so close to zero, so it's unlikely to cull any objects.

	planes_cs[0] = Planef(
		Vec4f(0,0,0,1) + FORWARDS_OS * this->max_draw_dist, // pos.
		FORWARDS_OS // normal
	); // far clip plane of frustum

	const Vec4f left_normal = normalise(crossProduct(UP_OS, lens_center - sensor_right));
	planes_cs[1] = Planef(
		lens_center,
		left_normal
	); // left

	const Vec4f right_normal = normalise(crossProduct(lens_center - sensor_left, UP_OS));
	planes_cs[2] = Planef(
		lens_center,
		right_normal
	); // right

	const Vec4f bottom_normal = normalise(crossProduct(lens_center - sensor_top, RIGHT_OS));
	planes_cs[3] = Planef(
		lens_center,
		bottom_normal
	); // bottom

	const Vec4f top_normal = normalise(crossProduct(RIGHT_OS, lens_center - sensor_bottom));
	planes_cs[4] = Planef(
		lens_center,
		top_normal
	); // top

	// Transform clipping planes to world space
	world_to_camera_space_matrix.getInverseForAffine3Matrix(this->cam_to_world);

	for(int i=0; i<5; ++i)
	{
		// Get point on plane and plane normal in Camera space.
		const Vec4f plane_point_cs = planes_cs[i].getPointOnPlane();

		const Vec4f plane_normal_cs = planes_cs[i].getNormal();

		// Transform from camera space -> world space, then world space -> object space.
		const Vec4f plane_point_ws = cam_to_world * plane_point_cs;
		const Vec4f normal_ws = normalise(cam_to_world * plane_normal_cs);

		frustum_clip_planes[i] = Planef(plane_point_ws, normal_ws);
	}

	this->num_frustum_clip_planes = 5;


	// Calculate frustum verts
	Vec4f verts_cs[5];
	const float shift_up_d    = lens_shift_up_distance    * (max_draw_dist / lens_sensor_dist); // distance verts at far end of frustum are shifted up
	const float shift_right_d = lens_shift_right_distance * (max_draw_dist / lens_sensor_dist); // distance verts at far end of frustum are shifted up

	const float d_w = use_sensor_width  * max_draw_dist / (2 * lens_sensor_dist);
	const float d_h = use_sensor_height * max_draw_dist / (2 * lens_sensor_dist);

	verts_cs[0] = lens_center;
	verts_cs[1] = lens_center + FORWARDS_OS * max_draw_dist + RIGHT_OS * (-d_w + shift_right_d) + UP_OS * (-d_h + shift_up_d); // bottom left
	verts_cs[2] = lens_center + FORWARDS_OS * max_draw_dist + RIGHT_OS * ( d_w + shift_right_d) + UP_OS * (-d_h + shift_up_d); // bottom right
	verts_cs[3] = lens_center + FORWARDS_OS * max_draw_dist + RIGHT_OS * ( d_w + shift_right_d) + UP_OS * ( d_h + shift_up_d); // top right
	verts_cs[4] = lens_center + FORWARDS_OS * max_draw_dist + RIGHT_OS * (-d_w + shift_right_d) + UP_OS * ( d_h + shift_up_d); // top left

	frustum_aabb = js::AABBox::emptyAABBox();
	for(int i=0; i<5; ++i)
	{
		assert(verts_cs[i][3] == 1.f);
		frustum_verts[i] = cam_to_world * verts_cs[i];
		const Vec4f frustum_vert_ws = cam_to_world * verts_cs[i];
		//conPrint("frustum_verts " + toString(i) + ": " + frustum_verts[i].toString());
		frustum_aabb.enlargeToHoldPoint(frustum_vert_ws);
	}
}


void OpenGLEngine::calcCamFrustumVerts(float near_dist, float far_dist, Vec4f* verts_out)
{
	// Calculate frustum verts
	const float shift_up_d    = lens_shift_up_distance    / lens_sensor_dist; // distance verts at far end of frustum are shifted up
	const float shift_right_d = lens_shift_right_distance / lens_sensor_dist; // distance verts at far end of frustum are shifted up

	const float d_w = use_sensor_width  / (2 * lens_sensor_dist);
	const float d_h = use_sensor_height / (2 * lens_sensor_dist);

	const Vec4f lens_center_cs(lens_shift_right_distance, 0, lens_shift_up_distance, 1);
	const Vec4f lens_center_ws = cam_to_world * lens_center_cs;

	const Vec4f forwards_ws = cam_to_world * FORWARDS_OS;
	const Vec4f up_ws = cam_to_world * UP_OS;
	const Vec4f right_ws = cam_to_world * RIGHT_OS;

	// Verts on near plane
	verts_out[0] = lens_center_ws + forwards_ws * near_dist + right_ws * (-d_w + shift_right_d) * near_dist + up_ws * (-d_h + shift_up_d) * near_dist; // bottom left
	verts_out[1] = lens_center_ws + forwards_ws * near_dist + right_ws * ( d_w + shift_right_d) * near_dist + up_ws * (-d_h + shift_up_d) * near_dist; // bottom right
	verts_out[2] = lens_center_ws + forwards_ws * near_dist + right_ws * ( d_w + shift_right_d) * near_dist + up_ws * ( d_h + shift_up_d) * near_dist; // top right
	verts_out[3] = lens_center_ws + forwards_ws * near_dist + right_ws * (-d_w + shift_right_d) * near_dist + up_ws * ( d_h + shift_up_d) * near_dist; // top left

	// Verts on far plane
	verts_out[4] = lens_center_ws + forwards_ws * far_dist + right_ws * (-d_w + shift_right_d) * far_dist + up_ws * (-d_h + shift_up_d) * far_dist; // bottom left
	verts_out[5] = lens_center_ws + forwards_ws * far_dist + right_ws * ( d_w + shift_right_d) * far_dist + up_ws * (-d_h + shift_up_d) * far_dist; // bottom right
	verts_out[6] = lens_center_ws + forwards_ws * far_dist + right_ws * ( d_w + shift_right_d) * far_dist + up_ws * ( d_h + shift_up_d) * far_dist; // top right
	verts_out[7] = lens_center_ws + forwards_ws * far_dist + right_ws * (-d_w + shift_right_d) * far_dist + up_ws * ( d_h + shift_up_d) * far_dist; // top left
}


void OpenGLEngine::setOrthoCameraTransform(const Matrix4f& world_to_camera_space_matrix_, float sensor_width_, float render_aspect_ratio_, float lens_shift_up_distance_,
	float lens_shift_right_distance_)
{
	camera_type = CameraType_Orthographic;

	this->sensor_width = sensor_width_;
	this->world_to_camera_space_matrix = world_to_camera_space_matrix_;
	this->render_aspect_ratio = render_aspect_ratio_;
	this->lens_shift_up_distance = lens_shift_up_distance_;
	this->lens_shift_right_distance = lens_shift_right_distance_;

	if(viewport_aspect_ratio > render_aspect_ratio) // if viewport has a wider aspect ratio than the render:
	{
		use_sensor_height = sensor_width / render_aspect_ratio; // Keep vertical field of view the same
		use_sensor_width = use_sensor_height * viewport_aspect_ratio; // enlarge horizontal field of view as needed
	}
	else
	{
		use_sensor_width = sensor_width; // Keep horizontal field of view the same
		use_sensor_height = sensor_width / viewport_aspect_ratio; // Enlarge vertical field of view as needed
	}


	// Make camera view volume clipping planes
	const Vec4f lens_center(lens_shift_right_distance, 0, lens_shift_up_distance, 1);

	Planef planes_cs[6]; // Clipping planes in camera space

	planes_cs[0] = Planef(
		Vec4f(0,0,0,1), // pos.
		-FORWARDS_OS // normal
	); // near clip plane of frustum

	planes_cs[1] = Planef(
		Vec4f(0, 0, 0, 1) + FORWARDS_OS * this->max_draw_dist, // pos.
		FORWARDS_OS // normal
	); // far clip plane of frustum

	const Vec4f left_normal = -RIGHT_OS;
	planes_cs[2] = Planef(
		lens_center + left_normal * use_sensor_width/2,
		left_normal
	); // left

	const Vec4f right_normal = RIGHT_OS;
	planes_cs[3] = Planef(
		lens_center + right_normal * use_sensor_width/2,
		right_normal
	); // right

	const Vec4f bottom_normal = -UP_OS;
	planes_cs[4] = Planef(
		lens_center + bottom_normal * use_sensor_height/2,
		bottom_normal
	); // bottom

	const Vec4f top_normal = UP_OS;
	planes_cs[5] = Planef(
		lens_center + top_normal * use_sensor_height/2,
		top_normal
	); // top
	
	// Transform clipping planes to world space
	world_to_camera_space_matrix.getInverseForAffine3Matrix(this->cam_to_world);
	
	for(int i=0; i<6; ++i)
	{
		// Get point on plane and plane normal in Camera space.
		const Vec4f plane_point_cs = planes_cs[i].getPointOnPlane();

		const Vec4f plane_normal_cs = planes_cs[i].getNormal();

		// Transform from camera space -> world space, then world space -> object space.
		const Vec4f plane_point_ws = cam_to_world * plane_point_cs;
		const Vec4f normal_ws = normalise(cam_to_world * plane_normal_cs);

		frustum_clip_planes[i] = Planef(plane_point_ws, normal_ws);
	}

	this->num_frustum_clip_planes = 6;

	// Calculate frustum verts
	Vec4f verts_cs[8];
	verts_cs[0] = lens_center                               + RIGHT_OS * -use_sensor_width + UP_OS * -use_sensor_height; // bottom left
	verts_cs[1] = lens_center                               + RIGHT_OS *  use_sensor_width + UP_OS * -use_sensor_height; // bottom right
	verts_cs[2] = lens_center                               + RIGHT_OS *  use_sensor_width + UP_OS *  use_sensor_height; // top right
	verts_cs[3] = lens_center                               + RIGHT_OS * -use_sensor_width + UP_OS *  use_sensor_height; // top left
	verts_cs[4] = lens_center + FORWARDS_OS * max_draw_dist + RIGHT_OS * -use_sensor_width + UP_OS * -use_sensor_height; // bottom left
	verts_cs[5] = lens_center + FORWARDS_OS * max_draw_dist + RIGHT_OS *  use_sensor_width + UP_OS * -use_sensor_height; // bottom right
	verts_cs[6] = lens_center + FORWARDS_OS * max_draw_dist + RIGHT_OS *  use_sensor_width + UP_OS *  use_sensor_height; // top right
	verts_cs[7] = lens_center + FORWARDS_OS * max_draw_dist + RIGHT_OS * -use_sensor_width + UP_OS *  use_sensor_height; // top left

	frustum_aabb = js::AABBox::emptyAABBox();
	for(int i=0; i<8; ++i)
	{
		assert(verts_cs[i][3] == 1.f);
		frustum_verts[i] = cam_to_world * verts_cs[i];
		const Vec4f frustum_vert_ws = cam_to_world * verts_cs[i];
		//conPrint("frustum_verts " + toString(i) + ": " + frustum_verts[i].toString());
		frustum_aabb.enlargeToHoldPoint(frustum_vert_ws);
	}
}


void OpenGLEngine::setCurrentTime(float time)
{
	this->current_time = time;
}


void OpenGLEngine::setSunDir(const Vec4f& d)
{
	this->sun_dir = d;
}


void OpenGLEngine::setEnvMapTransform(const Matrix3f& transform)
{
	this->env_ob->ob_to_world_matrix = Matrix4f(transform, Vec3f(0.f));
	this->env_ob->ob_to_world_matrix.getUpperLeftInverseTranspose(this->env_ob->ob_to_world_inv_tranpose_matrix);
}


void OpenGLEngine::setEnvMat(const OpenGLMaterial& env_mat_)
{
	this->env_ob->materials[0] = env_mat_;
	this->env_ob->materials[0].shader_prog = env_prog;//TEMP
}


#if !defined(OSX)
static void 
#ifdef _WIN32
	// NOTE: not sure what this should be on non-windows platforms.  APIENTRY does not seem to be defined with GCC on Linux 64.
	APIENTRY 
#endif
myMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) 
{
	// See https://www.opengl.org/sdk/docs/man/html/glDebugMessageControl.xhtml

	std::string typestr;
	switch(type)
	{
	case GL_DEBUG_TYPE_ERROR: typestr = "Error"; break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typestr = "Deprecated Behaviour"; break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: typestr = "Undefined Behaviour"; break;
	case GL_DEBUG_TYPE_PORTABILITY: typestr = "Portability"; break;
	case GL_DEBUG_TYPE_PERFORMANCE: typestr = "Performance"; break;
	case GL_DEBUG_TYPE_MARKER: typestr = "Marker"; break;
	case GL_DEBUG_TYPE_PUSH_GROUP: typestr = "Push group"; break;
	case GL_DEBUG_TYPE_POP_GROUP: typestr = "Pop group"; break;
	case GL_DEBUG_TYPE_OTHER: typestr = "Other"; break;
	default: typestr = "Unknown"; break;
	}

	std::string severitystr;
	switch(severity)
	{
	case GL_DEBUG_SEVERITY_LOW: severitystr = "low"; break;
	case GL_DEBUG_SEVERITY_MEDIUM: severitystr = "medium"; break;
	case GL_DEBUG_SEVERITY_HIGH : severitystr = "high"; break;
	case GL_DEBUG_SEVERITY_NOTIFICATION : severitystr = "notification"; break;
	case GL_DONT_CARE: severitystr = "Don't care"; break;
	default: severitystr = "Unknown"; break;
	}

	if(severity != GL_DEBUG_SEVERITY_NOTIFICATION) // Don't print out notifications by default.
	{
		conPrint("==============================================================");
		conPrint("OpenGL msg, severity: " + severitystr + ", type: " + typestr + ":");
		conPrint(std::string(message));
		conPrint("==============================================================");
	}
}
#endif


void OpenGLEngine::buildMeshRenderData(OpenGLMeshRenderData& meshdata, const js::Vector<Vec3f, 16>& vertices, const js::Vector<Vec3f, 16>& normals, const js::Vector<Vec2f, 16>& uvs, const js::Vector<uint32, 16>& indices)
{
	meshdata.has_uvs = !uvs.empty();
	meshdata.has_shading_normals = !normals.empty();
	meshdata.batches.resize(1);
	meshdata.batches[0].material_index = 0;
	meshdata.batches[0].num_indices = (uint32)indices.size();
	meshdata.batches[0].prim_start_offset = 0;

	meshdata.aabb_os = js::AABBox::emptyAABBox();

	js::Vector<float, 16> combined_data;
	const int NUM_COMPONENTS = 8;
	combined_data.resizeNoCopy(NUM_COMPONENTS * vertices.size());
	for(size_t i=0; i<vertices.size(); ++i)
	{
		combined_data[i*NUM_COMPONENTS + 0] = vertices[i].x;
		combined_data[i*NUM_COMPONENTS + 1] = vertices[i].y;
		combined_data[i*NUM_COMPONENTS + 2] = vertices[i].z;
		combined_data[i*NUM_COMPONENTS + 3] = normals[i].x;
		combined_data[i*NUM_COMPONENTS + 4] = normals[i].y;
		combined_data[i*NUM_COMPONENTS + 5] = normals[i].z;
		combined_data[i*NUM_COMPONENTS + 6] = uvs[i].x;
		combined_data[i*NUM_COMPONENTS + 7] = uvs[i].y;

		meshdata.aabb_os.enlargeToHoldPoint(Vec4f(vertices[i].x, vertices[i].y, vertices[i].z, 1.f));
	}

	meshdata.vert_vbo = new VBO(&combined_data[0], combined_data.dataSizeBytes());
	meshdata.vert_indices_buf = new VBO(&indices[0], indices.dataSizeBytes(), GL_ELEMENT_ARRAY_BUFFER);
	meshdata.index_type = GL_UNSIGNED_INT;

	VertexSpec spec;
	const uint32 vert_stride = (uint32)(sizeof(float) * 3 + (sizeof(float) * 3) + (sizeof(float) * 2)); // also vertex size.
	
	VertexAttrib pos_attrib;
	pos_attrib.enabled = true;
	pos_attrib.num_comps = 3;
	pos_attrib.type = GL_FLOAT;
	pos_attrib.normalised = false;
	pos_attrib.stride = vert_stride;
	pos_attrib.offset = 0;
	spec.attributes.push_back(pos_attrib);

	VertexAttrib normal_attrib;
	normal_attrib.enabled = true;
	normal_attrib.num_comps = 3;
	normal_attrib.type = GL_FLOAT;
	normal_attrib.normalised = false;
	normal_attrib.stride = vert_stride;
	normal_attrib.offset = sizeof(float) * 3; // goes after position
	spec.attributes.push_back(normal_attrib);

	VertexAttrib uv_attrib;
	uv_attrib.enabled = true;
	uv_attrib.num_comps = 2;
	uv_attrib.type = GL_FLOAT;
	uv_attrib.normalised = false;
	uv_attrib.stride = vert_stride;
	uv_attrib.offset = (uint32)(sizeof(float) * 3 + sizeof(float) * 3); // after position and possibly normal.
	spec.attributes.push_back(uv_attrib);

	meshdata.vert_vao = new VAO(meshdata.vert_vbo, spec);
}


void OpenGLEngine::getPhongUniformLocations(Reference<OpenGLProgram>& phong_prog, bool shadow_mapping_enabled, PhongUniformLocations& phong_locations_out)
{
	phong_locations_out.diffuse_colour_location				= phong_prog->getUniformLocation("diffuse_colour");
	phong_locations_out.have_shading_normals_location		= phong_prog->getUniformLocation("have_shading_normals");
	phong_locations_out.have_texture_location				= phong_prog->getUniformLocation("have_texture");
	phong_locations_out.diffuse_tex_location				= phong_prog->getUniformLocation("diffuse_tex");
	phong_locations_out.cosine_env_tex_location				= phong_prog->getUniformLocation("cosine_env_tex");
	phong_locations_out.specular_env_tex_location			= phong_prog->getUniformLocation("specular_env_tex");
	phong_locations_out.texture_matrix_location				= phong_prog->getUniformLocation("texture_matrix");
	phong_locations_out.sundir_cs_location					= phong_prog->getUniformLocation("sundir_cs");
	phong_locations_out.roughness_location					= phong_prog->getUniformLocation("roughness");
	phong_locations_out.fresnel_scale_location				= phong_prog->getUniformLocation("fresnel_scale");
	phong_locations_out.campos_ws_location					= phong_prog->getUniformLocation("campos_ws");
	phong_locations_out.metallic_frac_location				= phong_prog->getUniformLocation("metallic_frac");
	
	if(shadow_mapping_enabled)
	{
		phong_locations_out.dynamic_depth_tex_location		= phong_prog->getUniformLocation("dynamic_depth_tex");
		phong_locations_out.static_depth_tex_location			= phong_prog->getUniformLocation("static_depth_tex");
		phong_locations_out.shadow_texture_matrix_location	= phong_prog->getUniformLocation("shadow_texture_matrix");
	}
}


void OpenGLEngine::initialise(const std::string& data_dir_)
{
	data_dir = data_dir_;

#if !defined(OSX)
	if(gl3wInit() != 0)
	{
		conPrint("gl3wInit failed.");
		init_succeeded = false;
		initialisation_error_msg = "gl3wInit failed.";
		return;
	}
#endif

	conPrint("OpenGL version: " + std::string((const char*)glGetString(GL_VERSION)));

#if !defined(OSX)
	// Check to see if OpenGL 3.0 is supported, which is required for our VAO usage etc...  (See https://www.opengl.org/wiki/History_of_OpenGL#OpenGL_3.0_.282008.29 etc..)
	if(!gl3wIsSupported(3, 0))
	{
		init_succeeded = false;
		initialisation_error_msg = "OpenGL version is too old (< v3.0), version is " + std::string((const char*)glGetString(GL_VERSION));
		conPrint(initialisation_error_msg);
		return;
	}
#endif

#if BUILD_TESTS && !defined(OSX)
	//if(GLEW_ARB_debug_output)
	{
		// Enable error message handling,.
		// See "Porting Source to Linux: Valve�s Lessons Learned": https://developer.nvidia.com/sites/default/files/akamai/gamedev/docs/Porting%20Source%20to%20Linux.pdf
		glDebugMessageCallback(myMessageCallback, NULL); 
		glEnable(GL_DEBUG_OUTPUT);
	}
	//else
	//	conPrint("GLEW_ARB_debug_output OpenGL extension not available.");
#endif

	// Check if anisotropic texture filtering is available, and get max anisotropy if so.  
	// See 'Texture Mapping in OpenGL: Beyond the Basics' - http://www.informit.com/articles/article.aspx?p=770639&seqNum=2
	//if(GLEW_EXT_texture_filter_anisotropic)
	{
		anisotropic_filtering_supported = true;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_anisotropy);
	}

	this->GL_EXT_texture_sRGB_support = false;
	this->GL_EXT_texture_compression_s3tc_support = false;

	// Check OpenGL extensions
	GLint n = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &n);
	for(GLint i = 0; i < n; i++)
	{
		const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, i);
		if(stringEqual(ext, "GL_EXT_texture_sRGB")) this->GL_EXT_texture_sRGB_support = true;
		if(stringEqual(ext, "GL_EXT_texture_compression_s3tc")) this->GL_EXT_texture_compression_s3tc_support = true;
	}
	

	// init stb_compress_dxt before it's called from multiple threads
	{
		uint8 dummy[16];
		uint8 src[64];
		std::memset(src, 0, sizeof(src));
		stb_compress_dxt_block(dummy, src, /*alpha=*/0, /*mode=*/STB_DXT_NORMAL);
	}


	// Set up the rendering context, define display lists etc.:
	glClearColor(32.f / 255.f, 32.f / 255.f, 32.f / 255.f, 1.f);

	glEnable(GL_DEPTH_TEST);	// Enable z-buffering
	glDisable(GL_CULL_FACE);	// Disable backface culling

#if !defined(OSX)
	if(PROFILE)
		glGenQueries(1, &timer_query_id);
#endif

	// Create VBOs for sphere
	{
		int phi_res = 100;
		int theta_res = 50;

		js::Vector<Vec3f, 16> verts;
		verts.resize(phi_res * theta_res * 4);
		js::Vector<Vec3f, 16> normals;
		normals.resize(phi_res * theta_res * 4);
		js::Vector<Vec2f, 16> uvs;
		uvs.resize(phi_res * theta_res * 4);
		js::Vector<uint32, 16> indices;
		indices.resize(phi_res * theta_res * 6); // two tris per quad

		int i = 0;
		int quad_i = 0;
		for(int phi_i=0; phi_i<phi_res; ++phi_i)
		for(int theta_i=0; theta_i<theta_res; ++theta_i)
		{
			const float phi_1 = Maths::get2Pi<float>() * phi_i / phi_res;
			const float phi_2 = Maths::get2Pi<float>() * (phi_i + 1) / phi_res;
			const float theta_1 = Maths::pi<float>() * theta_i / theta_res;
			const float theta_2 = Maths::pi<float>() * (theta_i + 1) / theta_res;

			const Vec4f p1 = GeometrySampling::dirForSphericalCoords(phi_1, theta_1);
			const Vec4f p2 = GeometrySampling::dirForSphericalCoords(phi_2, theta_1);
			const Vec4f p3 = GeometrySampling::dirForSphericalCoords(phi_2, theta_2);
			const Vec4f p4 = GeometrySampling::dirForSphericalCoords(phi_1, theta_2);

			verts[i    ] = Vec3f(p1); 
			verts[i + 1] = Vec3f(p2); 
			verts[i + 2] = Vec3f(p3); 
			verts[i + 3] = Vec3f(p4);

			normals[i    ] = Vec3f(p1); 
			normals[i + 1] = Vec3f(p2);
			normals[i + 2] = Vec3f(p3); 
			normals[i + 3] = Vec3f(p4);

			uvs[i    ] = Vec2f(phi_1, theta_1); 
			uvs[i + 1] = Vec2f(phi_2, theta_1); 
			uvs[i + 2] = Vec2f(phi_2, theta_2); 
			uvs[i + 3] = Vec2f(phi_1, theta_2);

			indices[quad_i + 0] = i + 0; 
			indices[quad_i + 1] = i + 1; 
			indices[quad_i + 2] = i + 2; 
			indices[quad_i + 3] = i + 0;
			indices[quad_i + 4] = i + 2;
			indices[quad_i + 5] = i + 3;

			i += 4;
			quad_i += 6;
		}

		sphere_meshdata = new OpenGLMeshRenderData();
		buildMeshRenderData(*sphere_meshdata, verts, normals, uvs, indices);
	}

	this->env_ob->mesh_data = sphere_meshdata;


	try
	{
		std::string preprocessor_defines;
		// On OS X, we can't just not define things, we need to define them as zero or we get GLSL syntax errors.
		preprocessor_defines += "#define SHADOW_MAPPING " + (settings.shadow_mapping ? std::string("1") : std::string("0")) + "\n";
		preprocessor_defines += "#define NUM_DEPTH_TEXTURES " + (settings.shadow_mapping ? 
			toString(shadow_mapping->numDynamicDepthTextures() + shadow_mapping->numStaticDepthTextures()) : std::string("0")) + "\n";
		
		preprocessor_defines += "#define NUM_DYNAMIC_DEPTH_TEXTURES " + (settings.shadow_mapping ? 
			toString(shadow_mapping->numDynamicDepthTextures()) : std::string("0")) + "\n";
		
		preprocessor_defines += "#define NUM_STATIC_DEPTH_TEXTURES " + (settings.shadow_mapping ? 
			toString(shadow_mapping->numStaticDepthTextures()) : std::string("0")) + "\n";

		preprocessor_defines += "#define DEPTH_TEXTURE_SCALE_MULT " + (settings.shadow_mapping ? toString(shadow_mapping->getDynamicDepthTextureScaleMultiplier()) : std::string("1.0")) + "\n";

		const std::string use_shader_dir = data_dir + "/shaders";

		{
			const std::string use_defs = preprocessor_defines + "#define ALPHA_TEST 0\n";

			phong_prog = new OpenGLProgram(
				"phong",
				new OpenGLShader(use_shader_dir + "/phong_vert_shader.glsl", use_defs, GL_VERTEX_SHADER),
				new OpenGLShader(use_shader_dir + "/phong_frag_shader.glsl", use_defs, GL_FRAGMENT_SHADER)
			);
		}
		getPhongUniformLocations(phong_prog, settings.shadow_mapping, phong_locations);

		{
			const std::string use_defs = preprocessor_defines + "#define ALPHA_TEST 1\n";

			phong_with_alpha_test_prog = new OpenGLProgram(
				"phong",
				new OpenGLShader(use_shader_dir + "/phong_vert_shader.glsl", use_defs, GL_VERTEX_SHADER),
				new OpenGLShader(use_shader_dir + "/phong_frag_shader.glsl", use_defs, GL_FRAGMENT_SHADER)
			);
		}
		getPhongUniformLocations(phong_with_alpha_test_prog, settings.shadow_mapping, phong_with_alpha_test_locations);

		transparent_prog = new OpenGLProgram(
			"transparent",
			new OpenGLShader(use_shader_dir + "/transparent_vert_shader.glsl", preprocessor_defines, GL_VERTEX_SHADER),
			new OpenGLShader(use_shader_dir + "/transparent_frag_shader.glsl", preprocessor_defines, GL_FRAGMENT_SHADER)
		);
		transparent_colour_location					= transparent_prog->getUniformLocation("colour");
		transparent_have_shading_normals_location	= transparent_prog->getUniformLocation("have_shading_normals");
		transparent_sundir_cs_location				= transparent_prog->getUniformLocation("sundir_cs");
		transparent_specular_env_tex_location		= transparent_prog->getUniformLocation("specular_env_tex");
		transparent_campos_ws_location				= transparent_prog->getUniformLocation("campos_ws");

		env_prog = new OpenGLProgram(
			"env",
			new OpenGLShader(use_shader_dir + "/env_vert_shader.glsl", preprocessor_defines, GL_VERTEX_SHADER),
			new OpenGLShader(use_shader_dir + "/env_frag_shader.glsl", preprocessor_defines, GL_FRAGMENT_SHADER)
		);
		env_diffuse_colour_location		= env_prog->getUniformLocation("diffuse_colour");
		env_have_texture_location		= env_prog->getUniformLocation("have_texture");
		env_diffuse_tex_location		= env_prog->getUniformLocation("diffuse_tex");
		env_texture_matrix_location		= env_prog->getUniformLocation("texture_matrix");
		env_sundir_cs_location			= env_prog->getUniformLocation("sundir_cs");

		overlay_prog = new OpenGLProgram(
			"overlay",
			new OpenGLShader(use_shader_dir + "/overlay_vert_shader.glsl", preprocessor_defines, GL_VERTEX_SHADER),
			new OpenGLShader(use_shader_dir + "/overlay_frag_shader.glsl", preprocessor_defines, GL_FRAGMENT_SHADER)
		);
		overlay_diffuse_colour_location		= overlay_prog->getUniformLocation("diffuse_colour");
		overlay_have_texture_location		= overlay_prog->getUniformLocation("have_texture");
		overlay_diffuse_tex_location		= overlay_prog->getUniformLocation("diffuse_tex");
		overlay_texture_matrix_location		= overlay_prog->getUniformLocation("texture_matrix");
		
		outline_prog = new OpenGLProgram(
			"outline",
			new OpenGLShader(use_shader_dir + "/outline_vert_shader.glsl", preprocessor_defines, GL_VERTEX_SHADER),
			new OpenGLShader(use_shader_dir + "/outline_frag_shader.glsl", preprocessor_defines, GL_FRAGMENT_SHADER)
		);

		edge_extract_prog = new OpenGLProgram(
			"edge_extract",
			new OpenGLShader(use_shader_dir + "/edge_extract_vert_shader.glsl", preprocessor_defines, GL_VERTEX_SHADER),
			new OpenGLShader(use_shader_dir + "/edge_extract_frag_shader.glsl", preprocessor_defines, GL_FRAGMENT_SHADER)
		);
		edge_extract_tex_location		= edge_extract_prog->getUniformLocation("tex");


		if(settings.shadow_mapping)
		{
			{
				const std::string use_defs = preprocessor_defines + "#define ALPHA_TEST 0\n";
				depth_draw_mat.shader_prog = new OpenGLProgram(
					"depth",
					new OpenGLShader(use_shader_dir + "/depth_vert_shader.glsl", use_defs, GL_VERTEX_SHADER),
					new OpenGLShader(use_shader_dir + "/depth_frag_shader.glsl", use_defs, GL_FRAGMENT_SHADER)
				);
				this->depth_proj_view_model_matrix_location = depth_draw_mat.shader_prog->getUniformLocation("proj_view_model_matrix");
			}
			{
				const std::string use_defs = preprocessor_defines + "#define ALPHA_TEST 1\n";
				depth_draw_with_alpha_test_mat.shader_prog = new OpenGLProgram(
					"depth with alpha test",
					new OpenGLShader(use_shader_dir + "/depth_vert_shader.glsl", use_defs, GL_VERTEX_SHADER),
					new OpenGLShader(use_shader_dir + "/depth_frag_shader.glsl", use_defs, GL_FRAGMENT_SHADER)
				);
				depth_diffuse_tex_location		= depth_draw_with_alpha_test_mat.shader_prog->getUniformLocation("diffuse_tex");
				depth_texture_matrix_location	= depth_draw_with_alpha_test_mat.shader_prog->getUniformLocation("texture_matrix");
				this->depth_with_alpha_proj_view_model_matrix_location = depth_draw_with_alpha_test_mat.shader_prog->getUniformLocation("proj_view_model_matrix");
			}

			shadow_mapping = new ShadowMapping();
			shadow_mapping->init();

			{
				clear_buf_overlay_ob =  new OverlayObject();
				clear_buf_overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0, 0, -0.9999f);
				clear_buf_overlay_ob->material.albedo_rgb = Colour3f(1.f, 0.5f, 0.2f);
				clear_buf_overlay_ob->material.shader_prog = this->overlay_prog;
				clear_buf_overlay_ob->mesh_data = OpenGLEngine::makeOverlayQuadMesh();
			}

			if(false)
			{
				// Add overlay quad to preview depth map

				tex_preview_overlay_ob =  new OverlayObject();

				tex_preview_overlay_ob->ob_to_world_matrix.setToUniformScaleMatrix(0.95f);

				tex_preview_overlay_ob->material.albedo_rgb = Colour3f(0.2f, 0.2f, 0.2f);
				tex_preview_overlay_ob->material.shader_prog = this->overlay_prog;

				tex_preview_overlay_ob->material.albedo_texture = shadow_mapping->depth_tex;

				tex_preview_overlay_ob->mesh_data = OpenGLEngine::makeOverlayQuadMesh();

				addOverlayObject(tex_preview_overlay_ob);
			}
		}

		// Outline stuff
		{
			outline_solid_fb = new FrameBuffer();
			outline_edge_fb = new FrameBuffer();
	
			outline_solid_mat.shader_prog = outline_prog;

			outline_edge_mat.albedo_rgb = Colour3f(0.2f, 0.2f, 0.2f);
			outline_edge_mat.shader_prog = this->overlay_prog;

			outline_quad_meshdata = OpenGLEngine::makeOverlayQuadMesh();
		}


		// Load diffuse irradiance maps
		const std::string processed_envmap_dir = data_dir + "/gl_data";
		try
		{
			std::vector<Map2DRef> face_maps(6);
			for(int i=0; i<6; ++i)
			{
				face_maps[i] = ImFormatDecoder::decodeImage(".", processed_envmap_dir + "/diffuse_sky_no_sun_" + toString(i) + ".exr");

				if(!face_maps[i].isType<ImageMapFloat>())
					throw Indigo::Exception("cosine env map Must be ImageMapFloat");
			}

			this->cosine_env_tex = loadCubeMap(face_maps, OpenGLTexture::Filtering_Bilinear);
		}
		catch(ImFormatExcep& e)
		{
			throw Indigo::Exception(e.what());
		}

		// Load specular-reflection env tex
		try
		{
			Map2DRef specular_env = ImFormatDecoder::decodeImage(".", processed_envmap_dir + "/specular_refl_sky_no_sun_combined.exr");

			if(!specular_env.isType<ImageMapFloat>())
				throw Indigo::Exception("specular env map Must be ImageMapFloat");

			this->specular_env_tex = getOrLoadOpenGLTexture(*specular_env, OpenGLTexture::Filtering_Bilinear);
		}
		catch(ImFormatExcep& e)
		{
			throw Indigo::Exception(e.what());
		}


		init_succeeded = true;
	}
	catch(Indigo::Exception& e)
	{
		conPrint(e.what());
		this->initialisation_error_msg = e.what();
		init_succeeded = false;
	}
}


// We want the outline textures to have the same resolution as the viewport.
void OpenGLEngine::buildOutlineTexturesForViewport()
{
	outline_tex_w = myMax(16, viewport_w);
	outline_tex_h = myMax(16, viewport_h);

	outline_solid_tex = new OpenGLTexture();
	outline_solid_tex->load(outline_tex_w, outline_tex_h, ArrayRef<uint8>(NULL, 0), NULL, 
		OpenGLTexture::Format_RGB_Linear_Uint8,
		OpenGLTexture::Filtering_Bilinear,
		OpenGLTexture::Wrapping_Clamp // Clamp texture reads otherwise edge outlines will wrap around to other side of frame.
	);

	outline_edge_tex = new OpenGLTexture();
	outline_edge_tex->load(outline_tex_w, outline_tex_h, ArrayRef<uint8>(NULL, 0), NULL,
		OpenGLTexture::Format_RGBA_Linear_Uint8,
		OpenGLTexture::Filtering_Bilinear
	);

	outline_edge_mat.albedo_texture = outline_edge_tex;

	if(false)
	{
		this->overlay_objects.clear();

		// TEMP: Add overlay quad to preview texture
		Reference<OverlayObject> preview_overlay_ob = new OverlayObject();
		preview_overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(-1.0,0,0);
		preview_overlay_ob->material.shader_prog = this->overlay_prog;
		preview_overlay_ob->material.albedo_texture = outline_solid_tex;
		preview_overlay_ob->mesh_data = OpenGLEngine::makeOverlayQuadMesh();
		addOverlayObject(preview_overlay_ob);

		preview_overlay_ob = new OverlayObject();
		preview_overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(0.0,0,0);
		preview_overlay_ob->material.shader_prog = this->overlay_prog;
		preview_overlay_ob->material.albedo_texture = outline_edge_tex;
		preview_overlay_ob->mesh_data = OpenGLEngine::makeOverlayQuadMesh();
		addOverlayObject(preview_overlay_ob);
	}

}


void OpenGLEngine::viewportChanged(int viewport_w_, int viewport_h_)
{
	viewport_w = viewport_w_;
	viewport_h = viewport_h_;
	viewport_aspect_ratio = (double)viewport_w_ / (double)viewport_h_;

	buildOutlineTexturesForViewport();
}


void OpenGLEngine::unloadAllData()
{
	this->objects.resize(0);
	this->transparent_objects.resize(0);

	this->selected_objects.clear();

	this->env_ob->materials[0] = OpenGLMaterial();
}


const js::AABBox OpenGLEngine::getAABBWSForObjectWithTransform(GLObject& object, const Matrix4f& to_world)
{
	return object.mesh_data->aabb_os.transformedAABB(to_world);
}


void OpenGLEngine::updateObjectTransformData(GLObject& object)
{
	const Matrix4f& to_world = object.ob_to_world_matrix;

	const bool invertible = to_world.getUpperLeftInverseTranspose(/*result=*/object.ob_to_world_inv_tranpose_matrix); // Compute inverse matrix
	if(!invertible)
		object.ob_to_world_inv_tranpose_matrix = object.ob_to_world_matrix; // If not invertible, just use to-world matrix.
	// Hopefully we won't encounter non-invertible matrices here anyway.

	object.aabb_ws = object.mesh_data->aabb_os.transformedAABBFast(to_world);
}


void OpenGLEngine::assignShaderProgToMaterial(OpenGLMaterial& material)
{
	// If the client code has already set a special non-basic shader program (like a grid shader), don't overwrite it.
	if(material.shader_prog.nonNull() && 
		!(material.shader_prog == transparent_prog  || material.shader_prog == phong_with_alpha_test_prog || material.shader_prog == phong_prog))
		return;

	if(material.transparent)
	{
		material.shader_prog = transparent_prog;
	}
	else
	{
		if(material.albedo_texture.nonNull() && material.albedo_texture->hasAlpha())
			material.shader_prog = phong_with_alpha_test_prog;
		else
			material.shader_prog = phong_prog;
	}
}


void OpenGLEngine::addObject(const Reference<GLObject>& object)
{
	// Compute world space AABB of object
	updateObjectTransformData(*object.getPointer());
	
	this->objects.push_back(object);

	// Build the mesh used by the object if it's not built already.
	//if(mesh_render_data.find(object->mesh) == mesh_render_data.end())
	//	this->buildMesh(object->mesh);

	// Build materials
	bool have_transparent_mat = false;
	for(size_t i=0; i<object->materials.size(); ++i)
	{
		assignShaderProgToMaterial(object->materials[i]);
		have_transparent_mat = have_transparent_mat || object->materials[i].transparent;
	//	buildMaterial(object->materials[i]);
	}

	if(have_transparent_mat)
		transparent_objects.push_back(object);
}


void OpenGLEngine::addOverlayObject(const Reference<OverlayObject>& object)
{
	this->overlay_objects.push_back(object);
	object->material.shader_prog = overlay_prog;
}


void OpenGLEngine::selectObject(const Reference<GLObject>& object)
{
	this->selected_objects.insert(object.getPointer());
}


void OpenGLEngine::deselectObject(const Reference<GLObject>& object)
{
	this->selected_objects.erase(object.getPointer());
}


void OpenGLEngine::removeObject(const Reference<GLObject>& object)
{
	// NOTE: linear time

	for(size_t i=0; i<objects.size(); ++i)
		if(objects[i].getPointer() == object.getPointer())
		{
			objects.erase(objects.begin() + i);
			break;
		}

	for(size_t i=0; i<transparent_objects.size(); ++i)
		if(transparent_objects[i].getPointer() == object.getPointer())
		{
			transparent_objects.erase(transparent_objects.begin() + i);
			break;
		}

	selected_objects.erase(object.getPointer());
}


void OpenGLEngine::newMaterialUsed(OpenGLMaterial& mat)
{
	assignShaderProgToMaterial(mat);
}


void OpenGLEngine::objectMaterialsUpdated(const Reference<GLObject>& object, TextureServer& texture_server)
{
	// Add this object to transparent_objects list if it has a transparent material and is not already in the list.

	bool have_transparent_mat = false;
	for(size_t i=0; i<object->materials.size(); ++i)
	{
		assignShaderProgToMaterial(object->materials[i]);
		have_transparent_mat = have_transparent_mat || object->materials[i].transparent;

		if(object->materials[i].albedo_tex_path.empty())
			object->materials[i].albedo_texture = NULL; // Delete OpenGL texture
		else
		{
			try
			{
				Reference<Map2D> map = texture_server.getTexForPath(".", object->materials[i].albedo_tex_path);
				object->materials[i].albedo_texture = this->getOrLoadOpenGLTexture(*map, OpenGLTexture::Filtering_Fancy, OpenGLTexture::Wrapping_Repeat);
			}
			catch(TextureServerExcep& e)
			{
				conPrint("Warning: failed to load texture: " + e.what());
			}
			catch(Indigo::Exception& e)
			{
				conPrint("Warning: failed to load texture: " + e.what());
			}
		}
	}

	if(have_transparent_mat)
	{
		// Is this object already in transparent_objects list?
		bool in_transparent_objects = false;
		for(size_t i=0; i<transparent_objects.size(); ++i)
			if(transparent_objects[i].getPointer() == object.getPointer())
				in_transparent_objects = true;

		if(!in_transparent_objects)
			transparent_objects.push_back(object);
	}
	else
	{
		// Remove from transparent material list if it is currently in there.  NOTE: SLOW linear time
		for(size_t i=0; i<transparent_objects.size(); ++i)
			if(transparent_objects[i].getPointer() == object.getPointer())
			{
				transparent_objects.erase(transparent_objects.begin() + i);
				break;
			}
	}
}


void OpenGLEngine::buildMaterial(OpenGLMaterial& opengl_mat)
{
}


// http://iquilezles.org/www/articles/frustumcorrect/frustumcorrect.htm
static bool AABBIntersectsFrustum(const Planef* frustum_clip_planes, int num_frustum_clip_planes, const js::AABBox& frustum_aabb, const js::AABBox& aabb_ws)
{
	const Vec4f min_ws = aabb_ws.min_;
	const Vec4f max_ws = aabb_ws.max_;

	// For each frustum plane, check if AABB is completely on front side of plane, if so, then the AABB
	// does not intersect the frustum.
	// We'll do this by checking each vertex of the AABB against the plane (in parallel with SSE).
	for(int z=0; z<num_frustum_clip_planes; ++z) 
	{
#ifndef NDEBUG
		float refdist;
		{
			const Vec4f normal = frustum_clip_planes[z].getNormal();
			float dist = std::numeric_limits<float>::infinity();
			dist = myMin(dist, dot(normal, Vec4f(min_ws[0], min_ws[1], min_ws[2], 1.f)));
			dist = myMin(dist, dot(normal, Vec4f(min_ws[0], min_ws[1], max_ws[2], 1.f)));
			dist = myMin(dist, dot(normal, Vec4f(min_ws[0], max_ws[1], min_ws[2], 1.f)));
			dist = myMin(dist, dot(normal, Vec4f(min_ws[0], max_ws[1], max_ws[2], 1.f)));
			dist = myMin(dist, dot(normal, Vec4f(max_ws[0], min_ws[1], min_ws[2], 1.f)));
			dist = myMin(dist, dot(normal, Vec4f(max_ws[0], min_ws[1], max_ws[2], 1.f)));
			dist = myMin(dist, dot(normal, Vec4f(max_ws[0], max_ws[1], min_ws[2], 1.f)));
			dist = myMin(dist, dot(normal, Vec4f(max_ws[0], max_ws[1], max_ws[2], 1.f)));
			//if(dist >= frustum_clip_planes[z].getD())
			//	return false;
			refdist = dist;
		}
		const float refD = frustum_clip_planes[z].getD();
#endif
		
		const Vec4f normal = frustum_clip_planes[z].getNormal();
		const Vec4f normal_x = copyToAll<0>(normal);
		const Vec4f normal_y = copyToAll<1>(normal);
		const Vec4f normal_z = copyToAll<2>(normal);

		const Vec4f min_x(copyToAll<0>(min_ws)); // [min_ws[0], min_ws[0], min_ws[0], min_ws[0]]
		const Vec4f max_x(copyToAll<0>(max_ws)); // [max_ws[0], max_ws[0], max_ws[0], max_ws[0]]

		const Vec4f py(_mm_shuffle_ps(min_ws.v, max_ws.v, _MM_SHUFFLE(1, 1, 1, 1))); // [min_ws[1], min_ws[1], max_ws[1], max_ws[1]]
		const Vec4f pz_(_mm_shuffle_ps(min_ws.v, max_ws.v, _MM_SHUFFLE(2, 2, 2, 2))); // [min_ws[2], min_ws[2], max_ws[2], max_ws[2]]
		const Vec4f pz(_mm_shuffle_ps(pz_.v, pz_.v, _MM_SHUFFLE(2, 0, 2, 0))); // [min_ws[2], max_ws[2], min_ws[2], max_ws[2]]

		const Vec4f dot1 = mul(normal_x, min_x) + (mul(normal_y, py) + mul(normal_z, pz));
		const Vec4f dot2 = mul(normal_x, max_x) + (mul(normal_y, py) + mul(normal_z, pz));

		const Vec4f dist = min(dot1, dot2); // smallest distances
		const Vec4f greaterv = _mm_cmpge_ps(dist.v, Vec4f(frustum_clip_planes[z].getD()).v); // distances >= plane.D ?
		const int greater = _mm_movemask_ps(greaterv.v);
		assert((refdist >= refD) == (greater == 15));
		if(greater == 15) // if all distances >= plane.D:
			return false;
	}

	if(frustum_aabb.disjoint(aabb_ws) != 0) // If the frustum AABB is disjoint from the object AABB:
		return false;

	return true;
}


static void bindMeshData(const OpenGLMeshRenderData& mesh_data)
{
	mesh_data.vert_vao->bind();
	mesh_data.vert_indices_buf->bind();
}


static void unbindMeshData(const OpenGLMeshRenderData& mesh_data)
{
	mesh_data.vert_indices_buf->unbind();
	mesh_data.vert_vao->unbind();
}


// http://www.manpagez.com/man/3/glFrustum/
static const Matrix4f frustumMatrix(GLdouble left,
                       GLdouble right,
                       GLdouble bottom,
                       GLdouble top,
                       GLdouble zNear,
                       GLdouble zFar)
{
	float A = (right + left) / (right - left);
	float B = (top + bottom) / (top - bottom);
	float C = - (zFar + zNear) / (zFar - zNear);
	float D = - (2 * zFar * zNear) / (zFar - zNear);

	float e[16] = {
		(float)(2*zNear / (right - left)), 0, A, 0,
		0, (float)(2*zNear / (top - bottom)), B, 0,
		0, 0, C, D,
		0, 0, -1.f, 0 };
	Matrix4f t;
	Matrix4f(e).getTranspose(t);
	return t;
}


// https://msdn.microsoft.com/en-us/library/windows/desktop/dd373965(v=vs.85).aspx
static const Matrix4f orthoMatrix(GLdouble left,
                       GLdouble right,
                       GLdouble bottom,
                       GLdouble top,
                       GLdouble zNear,
                       GLdouble zFar)
{
	const float t_x = (float)(-(right + left) / (right - left));
	const float t_y = (float)(-(top + bottom) / (top - bottom));
	const float t_z = (float)(-(zFar + zNear) / (zFar - zNear));

	float e[16] = {
		(float)(2 / (right - left)),  0, 0, t_x,
		0, (float)(2 / (top - bottom)),  0, t_y,
		0, 0, (float)(-2 / (zFar - zNear)), t_z,
		0, 0, 0, 1 };
	Matrix4f t;
	Matrix4f(e).getTranspose(t);
	return t;
}


void OpenGLEngine::drawDebugPlane(const Vec3f& point_on_plane, const Vec3f& plane_normal, 
	const Matrix4f& view_matrix, const Matrix4f& proj_matrix, float plane_draw_half_width)
{
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE); // Disable writing to depth buffer.

	Matrix4f rot;
	rot.constructFromVector(plane_normal.toVec4fVector());

	// Draw a quad on the plane
	{
		outline_edge_mat.albedo_rgb = Colour3f(0.8f, 0.2f, 0.2f);
		outline_edge_mat.alpha = 0.5f;
		outline_edge_mat.shader_prog = this->transparent_prog;

		Matrix4f quad_to_world = Matrix4f::translationMatrix(point_on_plane.toVec4fPoint()) * rot *
			Matrix4f::uniformScaleMatrix(2*plane_draw_half_width) * Matrix4f::translationMatrix(Vec4f(-0.5f, -0.5f, 0.f, 1.f));

		GLObject ob;
		ob.ob_to_world_matrix = quad_to_world;

		bindMeshData(*outline_quad_meshdata); // Bind the mesh data, which is the same for all batches.
		drawBatch(ob, view_matrix, proj_matrix, outline_edge_mat, outline_edge_mat.shader_prog, *outline_quad_meshdata, outline_quad_meshdata->batches[0]);
		unbindMeshData(*outline_quad_meshdata);
	}

	glDepthMask(GL_TRUE); // Re-enable writing to depth buffer.
	glDisable(GL_BLEND);

	// Draw an arrow to show the plane normal
	if(debug_arrow_ob.isNull())
	{
		debug_arrow_ob = new GLObject();
		if(arrow_meshdata.isNull())
			arrow_meshdata = make3DArrowMesh(); // tip lies at (1,0,0).
		debug_arrow_ob->mesh_data = arrow_meshdata;
		debug_arrow_ob->materials.resize(1);
		debug_arrow_ob->materials[0].albedo_rgb = Colour3f(0.5f, 0.9f, 0.3f);
		debug_arrow_ob->materials[0].shader_prog = phong_prog;
	}

	Matrix4f arrow_to_world = Matrix4f::translationMatrix(point_on_plane.toVec4fPoint()) * rot *
		Matrix4f::scaleMatrix(2, 2, 2) * Matrix4f::rotationMatrix(Vec4f(0,1,0,0), -Maths::pi_2<float>()); // rot x axis to z axis
	
	debug_arrow_ob->ob_to_world_matrix = arrow_to_world;
	bindMeshData(*debug_arrow_ob->mesh_data); // Bind the mesh data, which is the same for all batches.
	drawBatch(*debug_arrow_ob, view_matrix, proj_matrix, debug_arrow_ob->materials[0], debug_arrow_ob->materials[0].shader_prog, *debug_arrow_ob->mesh_data, debug_arrow_ob->mesh_data->batches[0]);
	unbindMeshData(*debug_arrow_ob->mesh_data);
}


static inline float largestDim(const js::AABBox& aabb)
{
	return horizontalMax((aabb.max_ - aabb.min_).v);
}


// Draws a quad, with z value at far clip plane.
void OpenGLEngine::partiallyClearBuffer(const Vec2f& begin, const Vec2f& end)
{
	clear_buf_overlay_ob->ob_to_world_matrix =
		Matrix4f::translationMatrix(-1 + begin.x, -1 + begin.y, 1.f) * Matrix4f::scaleMatrix(2 * (end.x - begin.x), 2 * (end.y - begin.y), 1.f);

	glDepthFunc(GL_ALWAYS); // Do this to effectively enable z-test, but still have z writes.

	const OpenGLMeshRenderData& mesh_data = *clear_buf_overlay_ob->mesh_data;
	bindMeshData(mesh_data); // Bind the mesh data, which is the same for all batches.
	const OpenGLMaterial& opengl_mat = clear_buf_overlay_ob->material;
	assert(opengl_mat.shader_prog.getPointer() == this->overlay_prog.getPointer());
	opengl_mat.shader_prog->useProgram();
	glUniform4f(overlay_diffuse_colour_location, opengl_mat.albedo_rgb.r, opengl_mat.albedo_rgb.g, opengl_mat.albedo_rgb.b, opengl_mat.alpha);
	glUniformMatrix4fv(opengl_mat.shader_prog->model_matrix_loc, 1, false, clear_buf_overlay_ob->ob_to_world_matrix.e);
	glUniform1i(this->overlay_have_texture_location, 0);
	glDrawElements(GL_TRIANGLES, (GLsizei)mesh_data.batches[0].num_indices, mesh_data.index_type, (void*)(uint64)mesh_data.batches[0].prim_start_offset);
	opengl_mat.shader_prog->useNoPrograms();
	unbindMeshData(mesh_data);

	glDepthFunc(GL_LESS); // restore
}


void OpenGLEngine::draw()
{
	if(!init_succeeded)
		return;

	this->num_indices_submitted = 0;
	this->num_face_groups_submitted = 0;
	this->num_aabbs_submitted = 0;
	Timer profile_timer;

	this->draw_time = draw_timer.elapsed();
	uint64 shadow_depth_drawing_elapsed_ns = 0;

	//=============== Render to shadow map depth buffer if needed ===========
	if(shadow_mapping.nonNull())
	{
#if !defined(OSX)
		if(PROFILE) glBeginQuery(GL_TIME_ELAPSED, timer_query_id);
#endif
		//-------------------- Draw dynamic depth textures ----------------
		shadow_mapping->bindDepthTexAsTarget();

		glClear(GL_DEPTH_BUFFER_BIT);

		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);

		const int per_map_h = shadow_mapping->dynamic_h / shadow_mapping->numDynamicDepthTextures();

		for(int ti=0; ti<shadow_mapping->numDynamicDepthTextures(); ++ti)
		{
			glViewport(0, ti*per_map_h, shadow_mapping->dynamic_w, per_map_h);

			// Compute the 8 points making up this slice of the view frustum
			float near_dist = pow(shadow_mapping->getDynamicDepthTextureScaleMultiplier(), ti);
			float far_dist = near_dist * shadow_mapping->getDynamicDepthTextureScaleMultiplier();
			if(ti == 0)
				near_dist = 0.01;

			Vec4f frustum_verts_ws[8];
			calcCamFrustumVerts(near_dist, far_dist, frustum_verts_ws);

			const Vec4f up(0, 0, 1, 0);
			const Vec4f k = sun_dir;
			const Vec4f i = normalise(crossProduct(up, sun_dir)); // right 
			const Vec4f j = crossProduct(k, i); // up

			assert(k.isUnitLength());

			// Get bounds along i, j, k vectors
			float min_i = std::numeric_limits<float>::max();
			float min_j = std::numeric_limits<float>::max();
			float min_k = std::numeric_limits<float>::max();
			float max_i = -std::numeric_limits<float>::max();
			float max_j = -std::numeric_limits<float>::max();
			float max_k = -std::numeric_limits<float>::max();
			
			for(int z=0; z<8; ++z)
			{
				float dot_i = dot(i, maskWToZero(frustum_verts_ws[z]));
				float dot_j = dot(j, maskWToZero(frustum_verts_ws[z]));
				float dot_k = dot(k, maskWToZero(frustum_verts_ws[z]));
				min_i = myMin(min_i, dot_i);
				min_j = myMin(min_j, dot_j);
				min_k = myMin(min_k, dot_k);
				max_i = myMax(max_i, dot_i);
				max_j = myMax(max_j, dot_j);
				max_k = myMax(max_k, dot_k);
			}

			const float max_shadowing_dist = 300.0f;

			float use_max_k = myMax(max_k - min_k, min_k + max_shadowing_dist);

			float near_signed_dist = -use_max_k;
			float far_signed_dist = -min_k;

			Matrix4f proj_matrix = orthoMatrix(
				min_i, max_i, // left, right
				min_j, max_j, // bottom, top
				near_signed_dist, far_signed_dist // near, far
			);

			
			// TEMP: compute verts of shadow volume
			Vec4f shadow_vol_verts[8];
			shadow_vol_verts[0] = Vec4f(0,0,0,1) + i*min_i + j*max_j + k*use_max_k;
			shadow_vol_verts[1] = Vec4f(0,0,0,1) + i*max_i + j*max_j + k*use_max_k;
			shadow_vol_verts[2] = Vec4f(0,0,0,1) + i*max_i + j*min_j + k*use_max_k;
			shadow_vol_verts[3] = Vec4f(0,0,0,1) + i*min_i + j*min_j + k*use_max_k;
			shadow_vol_verts[4] = Vec4f(0,0,0,1) + i*min_i + j*max_j + k*min_k;
			shadow_vol_verts[5] = Vec4f(0,0,0,1) + i*max_i + j*max_j + k*min_k;
			shadow_vol_verts[6] = Vec4f(0,0,0,1) + i*max_i + j*min_j + k*min_k;
			shadow_vol_verts[7] = Vec4f(0,0,0,1) + i*min_i + j*min_j + k*min_k;

			Matrix4f view_matrix;
			view_matrix.setRow(0, i);
			view_matrix.setRow(1, j);
			view_matrix.setRow(2, k);
			view_matrix.setRow(3, Vec4f(0, 0, 0, 1));

			// Compute clipping planes for shadow mapping
			shadow_clip_planes[0] = Planef( i, max_i);
			shadow_clip_planes[1] = Planef(-i, -min_i);
			shadow_clip_planes[2] = Planef( j, max_j);
			shadow_clip_planes[3] = Planef(-j, -min_j);
			shadow_clip_planes[4] = Planef( k, use_max_k);
			shadow_clip_planes[5] = Planef(-k, -min_k);

			js::AABBox shadow_vol_aabb = js::AABBox::emptyAABBox(); // AABB of shadow rendering volume.
			for(int z=0; z<8; ++z)
				shadow_vol_aabb.enlargeToHoldPoint(shadow_vol_verts[z]);

			
			// We need to a texcoord bias matrix to go from [-1, 1] to [0, 1] coord range.
			const Matrix4f texcoord_bias(
				Vec4f(0.5f, 0, 0, 0), // col 0
				Vec4f(0, 0.5f, 0, 0), // col 1
				Vec4f(0, 0, 0.5f, 0), // col 2
				Vec4f(0.5f, 0.5f, 0.5f, 1) // col 3
			);
			// Save shadow_tex_matrix that the shaders like phong will use.
			shadow_mapping->shadow_tex_matrix[ti] = texcoord_bias * proj_matrix * view_matrix;

			// Draw non-transparent batches from objects.
			//uint64 num_drawn = 0;
			//uint64 num_in_frustum = 0;
			for(size_t q=0; q<objects.size(); ++q)
			{
				const GLObject* const ob = objects[q].getPointer();

				if(AABBIntersectsFrustum(shadow_clip_planes, /*num clip planes=*/6, shadow_vol_aabb, ob->aabb_ws))
				{
					//num_in_frustum++;

					if(largestDim(ob->aabb_ws) < (max_i - min_i) * 0.002f)
						continue;

					const OpenGLMeshRenderData& mesh_data = *ob->mesh_data;
					bindMeshData(mesh_data); // Bind the mesh data, which is the same for all batches.
					for(uint32 z = 0; z < mesh_data.batches.size(); ++z)
					{
						const uint32 mat_index = mesh_data.batches[z].material_index;
						// Draw primitives for the given material
						if(!ob->materials[mat_index].transparent)
						{
							const bool use_alpha_test = ob->materials[mat_index].albedo_texture.nonNull() && ob->materials[mat_index].albedo_texture->hasAlpha();
							OpenGLMaterial& use_mat = use_alpha_test ? depth_draw_with_alpha_test_mat : depth_draw_mat;

							drawBatch(*ob, view_matrix, proj_matrix,
								ob->materials[mat_index], // Use tex matrix etc.. from original material
								use_mat.shader_prog, mesh_data, mesh_data.batches[z]); // Draw object with depth_draw_mat.
						}
					}
					unbindMeshData(mesh_data);

					//num_drawn++;
				}
			}
			//conPrint("Level " + toString(ti) + ": " + toString(num_drawn) + " / " + toString(num_in_frustum) + " drawn.");
		}

		shadow_mapping->unbindDepthTex();
		//-------------------- End draw dynamic depth textures ----------------

		//-------------------- Draw static depth textures ----------------
		// We will update the different static cascades only every N frames, and in a staggered fashion.
		if(frame_num % 4 == 0)
		{
			shadow_mapping->bindStaticDepthTexAsTarget();

			for(int ti=0; ti<shadow_mapping->numStaticDepthTextures(); ++ti)
			{
				const int frame_offset = ti * 4;
				if(frame_num % 16 == frame_offset)
				{
					//conPrint("At frame " + toString(frame_num) + ", drawing static cascade " + toString(ti));

					const int static_per_map_h = shadow_mapping->static_h / shadow_mapping->numStaticDepthTextures();
					glViewport(0, ti*static_per_map_h, shadow_mapping->static_w, static_per_map_h);

					glDisable(GL_CULL_FACE);
					partiallyClearBuffer(Vec2f(0, 0), Vec2f(1, 1));
					glEnable(GL_CULL_FACE);

					float w;
					if(ti == 0)
						w = 64;
					else if(ti == 1)
						w = 256;
					else
						w = 1024;

					const float h = 256;

					// Get a bounding AABB centered on camera
					Vec4f campos_ws = cam_to_world * Vec4f(0, 0, 0, 1);
					Vec4f frustum_verts_ws[8];
					frustum_verts_ws[0] = campos_ws + Vec4f(-w, -w, -h, 0);
					frustum_verts_ws[1] = campos_ws + Vec4f( w, -w, -h, 0);
					frustum_verts_ws[2] = campos_ws + Vec4f(-w,  w, -h, 0);
					frustum_verts_ws[3] = campos_ws + Vec4f( w,  w, -h, 0);
					frustum_verts_ws[4] = campos_ws + Vec4f(-w, -w,  h, 0);
					frustum_verts_ws[5] = campos_ws + Vec4f( w, -w,  h, 0);
					frustum_verts_ws[6] = campos_ws + Vec4f(-w,  w,  h, 0);
					frustum_verts_ws[7] = campos_ws + Vec4f( w,  w,  h, 0);

					const Vec4f up(0, 0, 1, 0);
					const Vec4f k = sun_dir;
					const Vec4f i = normalise(crossProduct(up, sun_dir)); // right 
					const Vec4f j = crossProduct(k, i); // up

					assert(k.isUnitLength());

					// Get bounds along i, j, k vectors
					float min_i = std::numeric_limits<float>::max();
					float min_j = std::numeric_limits<float>::max();
					float min_k = std::numeric_limits<float>::max();
					float max_i = -std::numeric_limits<float>::max();
					float max_j = -std::numeric_limits<float>::max();
					float max_k = -std::numeric_limits<float>::max();

					for(int z=0; z<8; ++z)
					{
						float dot_i = dot(i, maskWToZero(frustum_verts_ws[z]));
						float dot_j = dot(j, maskWToZero(frustum_verts_ws[z]));
						float dot_k = dot(k, maskWToZero(frustum_verts_ws[z]));
						min_i = myMin(min_i, dot_i);
						min_j = myMin(min_j, dot_j);
						min_k = myMin(min_k, dot_k);
						max_i = myMax(max_i, dot_i);
						max_j = myMax(max_j, dot_j);
						max_k = myMax(max_k, dot_k);
					}

					const float max_shadowing_dist = 300.0f;

					float use_max_k = myMax(max_k - min_k, min_k + max_shadowing_dist);

					float near_signed_dist = -use_max_k;
					float far_signed_dist = -min_k;

					Matrix4f proj_matrix = orthoMatrix(
						min_i, max_i, // left, right
						min_j, max_j, // bottom, top
						near_signed_dist, far_signed_dist // near, far
					);


					// TEMP: compute verts of shadow volume
					Vec4f shadow_vol_verts[8];
					shadow_vol_verts[0] = Vec4f(0, 0, 0, 1) + i*min_i + j*max_j + k*use_max_k;
					shadow_vol_verts[1] = Vec4f(0, 0, 0, 1) + i*max_i + j*max_j + k*use_max_k;
					shadow_vol_verts[2] = Vec4f(0, 0, 0, 1) + i*max_i + j*min_j + k*use_max_k;
					shadow_vol_verts[3] = Vec4f(0, 0, 0, 1) + i*min_i + j*min_j + k*use_max_k;
					shadow_vol_verts[4] = Vec4f(0, 0, 0, 1) + i*min_i + j*max_j + k*min_k;
					shadow_vol_verts[5] = Vec4f(0, 0, 0, 1) + i*max_i + j*max_j + k*min_k;
					shadow_vol_verts[6] = Vec4f(0, 0, 0, 1) + i*max_i + j*min_j + k*min_k;
					shadow_vol_verts[7] = Vec4f(0, 0, 0, 1) + i*min_i + j*min_j + k*min_k;

					Matrix4f view_matrix;
					view_matrix.setRow(0, i);
					view_matrix.setRow(1, j);
					view_matrix.setRow(2, k);
					view_matrix.setRow(3, Vec4f(0, 0, 0, 1));

					// Compute clipping planes for shadow mapping
					shadow_clip_planes[0] = Planef( i, max_i);
					shadow_clip_planes[1] = Planef(-i, -min_i);
					shadow_clip_planes[2] = Planef( j, max_j);
					shadow_clip_planes[3] = Planef(-j, -min_j);
					shadow_clip_planes[4] = Planef( k, use_max_k);
					shadow_clip_planes[5] = Planef(-k, -min_k);

					js::AABBox shadow_vol_aabb = js::AABBox::emptyAABBox(); // AABB of shadow rendering volume.
					for(int z=0; z<8; ++z)
						shadow_vol_aabb.enlargeToHoldPoint(shadow_vol_verts[z]);

					// We need to a texcoord bias matrix to go from [-1, 1] to [0, 1] coord range.
					const Matrix4f texcoord_bias(
						Vec4f(0.5f, 0, 0, 0), // col 0
						Vec4f(0, 0.5f, 0, 0), // col 1
						Vec4f(0, 0, 0.5f, 0), // col 2
						Vec4f(0.5f, 0.5f, 0.5f, 1) // col 3
					);
					// Save shadow_tex_matrix that the shaders like phong will use.
					shadow_mapping->shadow_tex_matrix[shadow_mapping->numDynamicDepthTextures() + ti] = texcoord_bias * proj_matrix * view_matrix;

					// Draw non-transparent batches from objects.
					//uint64 num_drawn = 0;
					//uint64 num_in_frustum = 0;
					for(size_t q=0; q<objects.size(); ++q)
					{
						const GLObject* const ob = objects[q].getPointer();

						if(AABBIntersectsFrustum(shadow_clip_planes, /*num clip planes=*/6, shadow_vol_aabb, ob->aabb_ws))
						{
							//num_in_frustum++;

							if(largestDim(ob->aabb_ws) < (max_i - min_i) * 0.001f)
								continue;

							const OpenGLMeshRenderData& mesh_data = *ob->mesh_data;
							bindMeshData(mesh_data); // Bind the mesh data, which is the same for all batches.
							for(uint32 z = 0; z < mesh_data.batches.size(); ++z)
							{
								const uint32 mat_index = mesh_data.batches[z].material_index;
								// Draw primitives for the given material
								if(!ob->materials[mat_index].transparent)
								{
									const bool use_alpha_test = ob->materials[mat_index].albedo_texture.nonNull() && ob->materials[mat_index].albedo_texture->hasAlpha();
									OpenGLMaterial& use_mat = use_alpha_test ? depth_draw_with_alpha_test_mat : depth_draw_mat;

									drawBatch(*ob, view_matrix, proj_matrix,
										ob->materials[mat_index], // Use tex matrix etc.. from original material
										use_mat.shader_prog, mesh_data, mesh_data.batches[z]); // Draw object with depth_draw_mat.
								}
							}
							unbindMeshData(mesh_data);

							//num_drawn++;
						}
					}
					//conPrint("Level " + toString(ti) + ": " + toString(num_drawn) + " / " + toString(num_in_frustum) + " drawn.");
				}
			}

			shadow_mapping->unbindDepthTex();
		}
		//-------------------- End draw static depth textures ----------------

		// Restore viewport
		glViewport(0, 0, viewport_w, viewport_h);

		glDisable(GL_CULL_FACE);

#if !defined(OSX)
		if(PROFILE)
		{
			glEndQuery(GL_TIME_ELAPSED);
			glGetQueryObjectui64v(timer_query_id, GL_QUERY_RESULT, &shadow_depth_drawing_elapsed_ns); // Blocks
		}
#endif
	}

#if !defined(OSX)
	if(PROFILE) glBeginQuery(GL_TIME_ELAPSED, timer_query_id);
#endif


	
	// NOTE: We want to clear here first, even if the scene node is null.
	// Clearing here fixes the bug with the OpenGL widget buffer not being initialised properly and displaying garbled mem on OS X.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	glLineWidth(1);

	const double unit_shift_up    = this->lens_shift_up_distance    / this->lens_sensor_dist;
	const double unit_shift_right = this->lens_shift_right_distance / this->lens_sensor_dist;

	Matrix4f proj_matrix;
	
	// Initialise projection matrix from Indigo camera settings
	const double z_far  = max_draw_dist;
	const double z_near = max_draw_dist * 2e-5;

	if(camera_type == CameraType_Perspective)
	{
		double w, h;
		if(viewport_aspect_ratio > render_aspect_ratio)
		{
			h = 0.5 * (this->sensor_width / this->lens_sensor_dist) / render_aspect_ratio;
			w = h * viewport_aspect_ratio;
		}
		else
		{
			w = 0.5 * this->sensor_width / this->lens_sensor_dist;
			h = w / viewport_aspect_ratio;
		}

		const double x_min = z_near * (-w + unit_shift_right);
		const double x_max = z_near * ( w + unit_shift_right);
		const double y_min = z_near * (-h + unit_shift_up);
		const double y_max = z_near * ( h + unit_shift_up);
		proj_matrix = frustumMatrix(x_min, x_max, y_min, y_max, z_near, z_far);
	}
	else if(camera_type == CameraType_Orthographic)
	{
		const double sensor_height = this->sensor_width / render_aspect_ratio;

		if(viewport_aspect_ratio > render_aspect_ratio)
		{
			// Match on the vertical clip planes.
			proj_matrix = orthoMatrix(
				-sensor_height * 0.5 * viewport_aspect_ratio, // left
				sensor_height * 0.5 * viewport_aspect_ratio, // right
				-sensor_height * 0.5, // bottom
				sensor_height * 0.5, // top
				z_near,
				z_far
			);
		}
		else
		{
			// Match on the horizontal clip planes.
			proj_matrix = orthoMatrix(
				-this->sensor_width * 0.5, // left
				this->sensor_width * 0.5, // right
				-this->sensor_width * 0.5 / viewport_aspect_ratio, // bottom
				this->sensor_width * 0.5 / viewport_aspect_ratio, // top
				z_near,
				z_far
			);
		}
	}

	const float e[16] = { 1, 0, 0, 0,	0, 0, -1, 0,	0, 1, 0, 0,		0, 0, 0, 1 };
	const Matrix4f indigo_to_opengl_cam_matrix(e);

	Matrix4f view_matrix;
	mul(indigo_to_opengl_cam_matrix, world_to_camera_space_matrix, view_matrix);

	this->sun_dir_cam_space = indigo_to_opengl_cam_matrix * (world_to_camera_space_matrix * sun_dir);

	// Draw solid polygons
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);


	
	// Draw outlines around selected object(s).
	// * Draw object with flat uniform shading to a render frame buffer.
	// * Make render buffer a texture
	// * Draw a quad over the main viewport, using a shader that does a Sobel filter, to detect the edges.  write to another frame buffer.
	// * Draw another quad using the edge texture, blending a green colour into the final frame buffer on the edge.

	//================= Generate outline texture =================
	if(!selected_objects.empty())
	{
		// -------------------------- Stage 1: draw flat selected objects. --------------------
		outline_solid_fb->bind();
		glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, outline_solid_tex->texture_handle, 0);
		glViewport(0, 0, (GLsizei)outline_tex_w, (GLsizei)outline_tex_h); // Make viewport same size as texture.
		glClearColor(0.f, 0.f, 0.f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		for(auto i = selected_objects.begin(); i != selected_objects.end(); ++i)
		{
			const GLObject* const ob = *i;
			if(AABBIntersectsFrustum(frustum_clip_planes, num_frustum_clip_planes, frustum_aabb, ob->aabb_ws))
			{
				const OpenGLMeshRenderData& mesh_data = *ob->mesh_data;
				bindMeshData(mesh_data); // Bind the mesh data, which is the same for all batches.
				for(uint32 z = 0; z < mesh_data.batches.size(); ++z)
					drawBatch(*ob, view_matrix, proj_matrix, outline_solid_mat, outline_solid_mat.shader_prog, mesh_data, mesh_data.batches[z]); // Draw object with outline_mat.
				unbindMeshData(mesh_data);
			}
		}

		outline_solid_fb->unbind();
	
		// ------------------- Stage 2: Extract edges with Sobel filter---------------------
		// Shader reads from outline_solid_tex, writes to outline_edge_tex.
	
		outline_edge_fb->bind();
		glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, outline_edge_tex->texture_handle, 0);
		glDepthMask(GL_FALSE); // Don't write to z-buffer, depth not needed.

		edge_extract_prog->useProgram();

		// Position quad to cover viewport
		const Matrix4f ob_to_world_matrix = Matrix4f::scaleMatrix(2.f, 2.f, 1.f) * Matrix4f::translationMatrix(Vec4f(-0.5, -0.5, 0, 0));

		bindMeshData(*outline_quad_meshdata); // Bind the mesh data, which is the same for all batches.
		for(uint32 z = 0; z < outline_quad_meshdata->batches.size(); ++z)
		{
			glUniformMatrix4fv(edge_extract_prog->model_matrix_loc, 1, false, ob_to_world_matrix.e);

			glActiveTexture(GL_TEXTURE0 + 0);
			glBindTexture(GL_TEXTURE_2D, outline_solid_tex->texture_handle);
			glUniform1i(edge_extract_tex_location, 0);
				
			glDrawElements(GL_TRIANGLES, (GLsizei)outline_quad_meshdata->batches[0].num_indices, outline_quad_meshdata->index_type, (void*)(uint64)outline_quad_meshdata->batches[0].prim_start_offset);
		}
		unbindMeshData(*outline_quad_meshdata);

		edge_extract_prog->useNoPrograms();

		glDepthMask(GL_TRUE); // Restore writing to z-buffer.
		outline_edge_fb->unbind();

		glViewport(0, 0, viewport_w, viewport_h); // Restore viewport
	}


	// Draw background env map if there is one.
	if(this->env_ob->materials[0].albedo_texture.nonNull())
	{
		Matrix4f world_to_camera_space_no_translation = view_matrix;
		world_to_camera_space_no_translation.e[12] = 0;
		world_to_camera_space_no_translation.e[13] = 0;
		world_to_camera_space_no_translation.e[14] = 0;

		glDepthMask(GL_FALSE); // Disable writing to depth buffer.

		Matrix4f use_proj_mat;
		if(camera_type == CameraType_Orthographic)
		{
			// Use a perpective transformation for rendering the env sphere, with a few narrow field of view, to provide just a hint of texture detail.
			const float w = 0.01;
			use_proj_mat = frustumMatrix(-w, w, -w, w, 0.5, 100);
		}
		else
			use_proj_mat = proj_matrix;

		bindMeshData(*env_ob->mesh_data);
		drawBatch(*env_ob, world_to_camera_space_no_translation, use_proj_mat, env_ob->materials[0], env_ob->materials[0].shader_prog, *env_ob->mesh_data, env_ob->mesh_data->batches[0]);
		unbindMeshData(*env_ob->mesh_data);
			
		glDepthMask(GL_TRUE); // Re-enable writing to depth buffer.
	}
	

	// Draw non-transparent batches from objects.
	uint64 num_frustum_culled = 0;
	for(size_t i=0; i<objects.size(); ++i)
	{
		const GLObject* const ob = objects[i].getPointer();
		if(AABBIntersectsFrustum(frustum_clip_planes, num_frustum_clip_planes, frustum_aabb, ob->aabb_ws))
		{
			const OpenGLMeshRenderData& mesh_data = *ob->mesh_data;
			bindMeshData(mesh_data); // Bind the mesh data, which is the same for all batches.
			for(uint32 z = 0; z < mesh_data.batches.size(); ++z)
			{
				const uint32 mat_index = mesh_data.batches[z].material_index;
				if(mat_index < ob->materials.size()) // This can happen for some dodgy meshes.  TODO: Filter such batches out in mesh creation stage.
				{
					// Draw primitives for the given material
					if(!ob->materials[mat_index].transparent)
						drawBatch(*ob, view_matrix, proj_matrix, ob->materials[mat_index], ob->materials[mat_index].shader_prog, mesh_data, mesh_data.batches[z]);
				}
			}
			unbindMeshData(mesh_data);
		}
		else
			num_frustum_culled++;
	}

	// Draw wireframes if required.
	if(draw_wireframes)
	{
		// Use outline shaders for now as they just generate white fragments, which is what we want.
		OpenGLMaterial wire_mat;
		wire_mat.shader_prog = outline_prog;

		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		
		// Offset the lines so they draw in front of the filled polygons.
		glEnable(GL_POLYGON_OFFSET_LINE);
		glPolygonOffset(0.f, -1.0f);

		for(size_t i=0; i<objects.size(); ++i)
		{
			const GLObject* const ob = objects[i].getPointer();
			if(AABBIntersectsFrustum(frustum_clip_planes, num_frustum_clip_planes, frustum_aabb, ob->aabb_ws))
			{
				const OpenGLMeshRenderData& mesh_data = *ob->mesh_data;
				bindMeshData(mesh_data); // Bind the mesh data, which is the same for all batches.
				for(uint32 z = 0; z < mesh_data.batches.size(); ++z)
					drawBatch(*ob, view_matrix, proj_matrix, wire_mat, wire_mat.shader_prog, mesh_data, mesh_data.batches[z]);
				unbindMeshData(mesh_data);
			}
		}

		glDisable(GL_POLYGON_OFFSET_LINE);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	// Draw transparent batches
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE); // Disable writing to depth buffer.

	for(size_t i=0; i<transparent_objects.size(); ++i)
	{
		const GLObject* const ob = transparent_objects[i].getPointer();
		if(AABBIntersectsFrustum(frustum_clip_planes, num_frustum_clip_planes, frustum_aabb, ob->aabb_ws))
		{
			const OpenGLMeshRenderData& mesh_data = *ob->mesh_data;
			bindMeshData(mesh_data); // Bind the mesh data, which is the same for all batches.
			for(uint32 z = 0; z < mesh_data.batches.size(); ++z)
			{
				const uint32 mat_index = mesh_data.batches[z].material_index;
				if(mat_index < ob->materials.size()) // This can happen for some dodgy meshes.  TODO: Filter such batches out in mesh creation stage.
				{
					// Draw primitives for the given material
					if(ob->materials[mat_index].transparent)
						drawBatch(*ob, view_matrix, proj_matrix, ob->materials[mat_index], ob->materials[mat_index].shader_prog, mesh_data, mesh_data.batches[z]);
				}
			}
			unbindMeshData(mesh_data);
		}
	}
	glDepthMask(GL_TRUE); // Re-enable writing to depth buffer.
	glDisable(GL_BLEND);

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);


	//================= Draw outlines around selected objects =================
	// At this stage the outline texture has been generated in outline_edge_tex.  So we will just blend it over the current frame.
	if(!selected_objects.empty())
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDepthMask(GL_FALSE); // Don't write to z-buffer

		// Position quad to cover viewport
		const Matrix4f ob_to_world_matrix = Matrix4f::scaleMatrix(2.f, 2.f, 1.f) * Matrix4f::translationMatrix(Vec4f(-0.5, -0.5, 0, 0));

		const OpenGLMeshRenderData& mesh_data = *outline_quad_meshdata;
		bindMeshData(mesh_data); // Bind the mesh data, which is the same for all batches.
		for(uint32 z = 0; z < mesh_data.batches.size(); ++z)
		{
			const OpenGLMaterial& opengl_mat = outline_edge_mat;

			assert(opengl_mat.shader_prog.getPointer() == this->overlay_prog.getPointer());

			opengl_mat.shader_prog->useProgram();

			glUniform4f(overlay_diffuse_colour_location, opengl_mat.albedo_rgb.r, opengl_mat.albedo_rgb.g, opengl_mat.albedo_rgb.b, opengl_mat.alpha);
			glUniformMatrix4fv(opengl_mat.shader_prog->model_matrix_loc, 1, false, /*ob->*/ob_to_world_matrix.e);
			glUniform1i(this->overlay_have_texture_location, opengl_mat.albedo_texture.nonNull() ? 1 : 0);

			glActiveTexture(GL_TEXTURE0 + 0);
			glBindTexture(GL_TEXTURE_2D, opengl_mat.albedo_texture->texture_handle);

			const Matrix3f identity = Matrix3f::identity();
			glUniformMatrix3fv(overlay_texture_matrix_location, /*count=*/1, /*transpose=*/false, identity.e);
			glUniform1i(overlay_diffuse_tex_location, 0);
				
			glDrawElements(GL_TRIANGLES, (GLsizei)mesh_data.batches[0].num_indices, mesh_data.index_type, (void*)(uint64)mesh_data.batches[0].prim_start_offset);

			opengl_mat.shader_prog->useNoPrograms();
		}
		unbindMeshData(mesh_data);

		glDepthMask(GL_TRUE); // Restore
		glDisable(GL_BLEND);
	}

	//================= Draw UI overlay objects =================
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDepthMask(GL_FALSE); // Don't write to z-buffer

		for(size_t i=0; i<overlay_objects.size(); ++i)
		{
			const OverlayObject* const ob = overlay_objects[i].getPointer();
			const OpenGLMeshRenderData& mesh_data = *ob->mesh_data;
			bindMeshData(mesh_data); // Bind the mesh data, which is the same for all batches.
			for(uint32 z = 0; z < mesh_data.batches.size(); ++z)
			{
				const OpenGLMaterial& opengl_mat = ob->material;

				assert(opengl_mat.shader_prog.getPointer() == this->overlay_prog.getPointer());

				opengl_mat.shader_prog->useProgram();

				glUniform4f(overlay_diffuse_colour_location, opengl_mat.albedo_rgb.r, opengl_mat.albedo_rgb.g, opengl_mat.albedo_rgb.b, opengl_mat.alpha);
				glUniformMatrix4fv(opengl_mat.shader_prog->model_matrix_loc, 1, false, ob->ob_to_world_matrix.e);
				glUniform1i(this->overlay_have_texture_location, opengl_mat.albedo_texture.nonNull() ? 1 : 0);

				if(opengl_mat.albedo_texture.nonNull())
				{
					glActiveTexture(GL_TEXTURE0 + 0);
					glBindTexture(GL_TEXTURE_2D, opengl_mat.albedo_texture->texture_handle);

					const GLfloat tex_elems[9] = {
						opengl_mat.tex_matrix.e[0], opengl_mat.tex_matrix.e[2], 0,
						opengl_mat.tex_matrix.e[1], opengl_mat.tex_matrix.e[3], 0,
						opengl_mat.tex_translation.x, opengl_mat.tex_translation.y, 1
					};
					glUniformMatrix3fv(overlay_texture_matrix_location, /*count=*/1, /*transpose=*/false, tex_elems);
					glUniform1i(overlay_diffuse_tex_location, 0);
				}
				
				glDrawElements(GL_TRIANGLES, (GLsizei)mesh_data.batches[0].num_indices, mesh_data.index_type, (void*)(uint64)mesh_data.batches[0].prim_start_offset);

				opengl_mat.shader_prog->useNoPrograms();
			}
			unbindMeshData(mesh_data);
		}

		glDepthMask(GL_TRUE); // Restore
		glDisable(GL_BLEND);
	}


	if(PROFILE)
	{
		const double cpu_time = profile_timer.elapsed();
		uint64 elapsed_ns = 0;
#if !defined(OSX)
		glEndQuery(GL_TIME_ELAPSED);
		glGetQueryObjectui64v(timer_query_id, GL_QUERY_RESULT, &elapsed_ns); // Blocks
#endif

		conPrint("Frame times: CPU: " + doubleToStringNDecimalPlaces(cpu_time * 1.0e3, 4) + 
			" ms, depth map gen on GPU: " + doubleToStringNDecimalPlaces(shadow_depth_drawing_elapsed_ns * 1.0e-6, 4) + 
			" ms, GPU: " + doubleToStringNDecimalPlaces(elapsed_ns * 1.0e-6, 4) + " ms.");
		conPrint("Submitted: face groups: " + toString(num_face_groups_submitted) + ", faces: " + toString(num_indices_submitted / 3) + ", aabbs: " + toString(num_aabbs_submitted) + ", " + 
			toString(objects.size() - num_frustum_culled) + "/" + toString(objects.size()) + " obs");
	}

	frame_num++;
}


// KVP == (key, value) pair
struct uint32KVP
{
	inline bool operator() (const std::pair<uint32, uint32>& lhs, const std::pair<uint32, uint32>& rhs) const { return lhs.first < rhs.first; }
};



//struct UniqueVert
//{
//	Vec3f p;
//	Vec3f n;
//	Vec2f uv;
//
//	inline bool operator < (const UniqueVert& b) const
//	{
//		if(p < b.p)
//			return true;
//		else if(b.p < p)
//			return false;
//		else
//		{
//			if(n < b.n)
//				return true;
//			else if(b.n < n)
//				return false;
//			else
//				return uv < b.uv;
//		}
//	}
//
//	inline bool operator == (const UniqueVert& b) const
//	{
//		return p == b.p && n == b.n && uv == b.uv;
//	}
//};
//
//
//class UniqueVertHash
//{
//public:
//	// hash _Keyval to size_t value by pseudorandomizing transform.
//	inline size_t operator()(const UniqueVert& v) const { return bitCast<uint32>(v.p.x); }
//};


// This is used to combine vertices with the same position, normal, and uv.
// Note that there is a tradeoff here - we could combine with the full position vector, normal, and uvs, but then the keys would be slow to compare.
// Or we could compare with the existing indices.  This will combine vertices effectively only if there are merged (not duplicated) in the Indigo mesh.
// In practice positions are usually combined (subdiv relies on it etc..), but UVs often aren't.  So we will use the index for positions, and the actual UV (0) value
// for the UVs.
/*struct UniqueVertKey
{
	Vec3f p;
	Vec3f n;
	Vec2f uv;

	inline bool operator < (const UniqueVertKey& other) const
	{
		if(p < other.p)
			return true;
		if(other.p < p)
			return false;
		if(n < other.n)
			return true;
		if(other.n < n)
			return false;
		return uv < other.uv;
	}
	inline bool operator == (const UniqueVertKey& b) const
	{
		return p == b.p && n == b.n && uv == b.uv;
	}
};


// Hash function for UniqueVert
struct UniqueVertKeyHash
{
	inline size_t operator()(const UniqueVertKey& v) const { return (uint64)bitCast<uint32>(v.p.x); }
};*/


struct UniqueVertKey
{
	unsigned int pos_i; // Index for position and normal for vert
	//unsigned int uv_i;
	Vec2f uv;

	inline bool operator < (const UniqueVertKey& b) const
	{
		if(pos_i == b.pos_i)
			return uv < b.uv; //uv_i < b.uv_i;
		else
			return pos_i < b.pos_i;
	}
	inline bool operator == (const UniqueVertKey& b) const
	{
		return pos_i == b.pos_i && uv == b.uv; // uv_i == b.uv_i;
	}
	inline bool operator != (const UniqueVertKey& b) const
	{
		return pos_i != b.pos_i || uv != b.uv;
	}
};


// From http://burtleburtle.net/bob/hash/integer.html
static inline uint32_t uint32Hash(uint32_t a)
{
	a = (a ^ 61) ^ (a >> 16);
	a = a + (a << 3);
	a = a ^ (a >> 4);
	a = a * 0x27d4eb2d;
	a = a ^ (a >> 15);
	return a;
}

// Hash function for UniqueVert
struct UniqueVertKeyHash
{
	inline size_t operator()(const UniqueVertKey& v) const
	{
		return uint32Hash(v.pos_i);
	}
};


//struct UniqueVert
//{
//	Vec3f p;
//	Vec3f n;
//	Vec2f uv;
//	uint32 mat_index;
//};



Reference<OpenGLMeshRenderData> OpenGLEngine::buildIndigoMesh(const Reference<Indigo::Mesh>& mesh_, bool skip_opengl_calls)
{
	//-------------------------------------------------------------------------------------------------------
#if USE_INDIGO_MESH_INDICES
	if(!mesh_->indices.empty())
	{
		Timer timer;
		const Indigo::Mesh* const mesh = mesh_.getPointer();
		const bool mesh_has_shading_normals = !mesh->vert_normals.empty();
		const bool mesh_has_uvs = mesh->num_uv_mappings > 0;
		const uint32 num_uv_sets = mesh->num_uv_mappings;

		Reference<OpenGLMeshRenderData> opengl_render_data = new OpenGLMeshRenderData();

		// If UVs are somewhat small in magnitude, use GL_HALF_FLOAT instead of GL_FLOAT.
		// If the magnitude is too high we can get articacts if we just use half precision.
		const float max_use_half_range = 10.f;
		bool use_half_uvs = true;
		for(size_t i=0; i<mesh->uv_pairs.size(); ++i)
			if(std::fabs(mesh->uv_pairs[i].x) > max_use_half_range || std::fabs(mesh->uv_pairs[i].y) > max_use_half_range)
				use_half_uvs = false;

		js::Vector<uint8, 16> vert_data;
#ifdef OSX // GL_INT_2_10_10_10_REV is not present in our OS X header files currently.
		const size_t packed_normal_size = sizeof(float)*3;
#else
		const size_t packed_normal_size = 4; // 4 bytes since we are using GL_INT_2_10_10_10_REV format.
#endif
		const size_t packed_uv_size = use_half_uvs ? sizeof(half)*2 : sizeof(float)*2;
		const size_t num_bytes_per_vert = sizeof(float)*3 + (mesh_has_shading_normals ? packed_normal_size : 0) + (mesh_has_uvs ? packed_uv_size : 0);
		const size_t normal_offset      = sizeof(float)*3;
		const size_t uv_offset          = sizeof(float)*3 + (mesh_has_shading_normals ? packed_normal_size : 0);
		vert_data.resizeNoCopy(mesh->vert_positions.size() * num_bytes_per_vert);


		const size_t vert_positions_size = mesh->vert_positions.size();
		const size_t uvs_size = mesh->uv_pairs.size();

		for(size_t i=0; i<vert_positions_size; ++i)
		{
			const size_t offset = num_bytes_per_vert * i;

			std::memcpy(&vert_data[offset], &mesh->vert_positions[i].x, sizeof(Indigo::Vec3f));

			// Copy vertex data
			if(mesh_has_shading_normals)
			{
#ifdef OSX
				std::memcpy(&vert_data[offset + normal_offset], &mesh->vert_normals[i].x, sizeof(Indigo::Vec3f));
#else
				// Pack normal into GL_INT_2_10_10_10_REV format.
				int x = (int)((mesh->vert_normals[i].x) * 511.f);
				int y = (int)((mesh->vert_normals[i].y) * 511.f);
				int z = (int)((mesh->vert_normals[i].z) * 511.f);
				// ANDing with 1023 isolates the bottom 10 bits.
				uint32 n = (x & 1023) | ((y & 1023) << 10) | ((z & 1023) << 20);
				std::memcpy(&vert_data[offset + normal_offset], &n, 4);
#endif
			}

			if(mesh_has_uvs)
			{
				if(use_half_uvs)
				{
					half uv[2];
					uv[0] = half(mesh->uv_pairs[i].x);
					uv[1] = half(mesh->uv_pairs[i].y);
					std::memcpy(&vert_data[offset + uv_offset], uv, 4);
				}
				else
					std::memcpy(&vert_data[offset + uv_offset], &mesh->uv_pairs[i].x, sizeof(Indigo::Vec2f));
			}
		}


		// Build batches
		for(size_t i=0; i<mesh->chunks.size(); ++i)
		{
			opengl_render_data->batches.push_back(OpenGLBatch());
			OpenGLBatch& batch = opengl_render_data->batches.back();
			batch.material_index = mesh->chunks[i].mat_index;
			batch.prim_start_offset = mesh->chunks[i].indices_start * sizeof(uint32);
			batch.num_indices = mesh->chunks[i].num_indices;
			

			// If subsequent batches use the same material, combine them
			for(size_t z=i + 1; z<mesh->chunks.size(); ++z)
			{
				if(mesh->chunks[z].mat_index == batch.material_index)
				{
					// Combine:
					batch.num_indices += mesh->chunks[z].num_indices;
					i++;
				}
				else
					break;
			}
		}


		if(skip_opengl_calls)
			return opengl_render_data;

		opengl_render_data->vert_vbo = new VBO(&vert_data[0], vert_data.dataSizeBytes());

		VertexSpec spec;

		VertexAttrib pos_attrib;
		pos_attrib.enabled = true;
		pos_attrib.num_comps = 3;
		pos_attrib.type = GL_FLOAT;
		pos_attrib.normalised = false;
		pos_attrib.stride = (uint32)num_bytes_per_vert;
		pos_attrib.offset = 0;
		spec.attributes.push_back(pos_attrib);

		VertexAttrib normal_attrib;
		normal_attrib.enabled = mesh_has_shading_normals;
#ifdef OSX
		normal_attrib.num_comps = 3;
		normal_attrib.type = GL_FLOAT;
		normal_attrib.normalised = false;
#else
		normal_attrib.num_comps = 4;
		normal_attrib.type = GL_INT_2_10_10_10_REV;
		normal_attrib.normalised = true;
#endif
		normal_attrib.stride = (uint32)num_bytes_per_vert;
		normal_attrib.offset = (uint32)normal_offset;
		spec.attributes.push_back(normal_attrib);

		VertexAttrib uv_attrib;
		uv_attrib.enabled = mesh_has_uvs;
		uv_attrib.num_comps = 2;
		uv_attrib.type = use_half_uvs ? GL_HALF_FLOAT : GL_FLOAT;
		uv_attrib.normalised = false;
		uv_attrib.stride = (uint32)num_bytes_per_vert;
		uv_attrib.offset = (uint32)uv_offset;
		spec.attributes.push_back(uv_attrib);

		opengl_render_data->vert_vao = new VAO(opengl_render_data->vert_vbo, spec);


		opengl_render_data->has_uvs				= mesh_has_uvs;
		opengl_render_data->has_shading_normals = mesh_has_shading_normals;

		//assert(!vert_index_buffer.empty());
		//const size_t vert_index_buffer_size = vert_index_buffer.size();
		const size_t vert_index_buffer_size = mesh->indices.size();

		if(mesh->vert_positions.size() < 256)
		{
			js::Vector<uint8, 16> index_buf; index_buf.resize(vert_index_buffer_size);
			for(size_t i=0; i<vert_index_buffer_size; ++i)
			{
				assert(mesh->indices[i] < 256);
				index_buf[i] = (uint8)mesh->indices[i];
			}
			opengl_render_data->vert_indices_buf = new VBO(&index_buf[0], index_buf.dataSizeBytes(), GL_ELEMENT_ARRAY_BUFFER);
			opengl_render_data->index_type = GL_UNSIGNED_BYTE;
			// Go through the batches and adjust the start offset to take into account we're using uint8s.
			for(size_t i=0; i<opengl_render_data->batches.size(); ++i)
				opengl_render_data->batches[i].prim_start_offset /= 4;
		}
		else if(mesh->vert_positions.size() < 65536)
		{
			js::Vector<uint16, 16> index_buf; index_buf.resize(vert_index_buffer_size);
			for(size_t i=0; i<vert_index_buffer_size; ++i)
			{
				assert(mesh->indices[i] < 65536);
				index_buf[i] = (uint16)mesh->indices[i];
			}
			opengl_render_data->vert_indices_buf = new VBO(&index_buf[0], index_buf.dataSizeBytes(), GL_ELEMENT_ARRAY_BUFFER);
			opengl_render_data->index_type = GL_UNSIGNED_SHORT;
			// Go through the batches and adjust the start offset to take into account we're using uint16s.
			for(size_t i=0; i<opengl_render_data->batches.size(); ++i)
				opengl_render_data->batches[i].prim_start_offset /= 2;
		}
		else
		{
			opengl_render_data->vert_indices_buf = new VBO(mesh->indices.data(), mesh->indices.size() * sizeof(uint32), GL_ELEMENT_ARRAY_BUFFER);
			opengl_render_data->index_type = GL_UNSIGNED_INT;
		}

#ifndef NDEBUG
		for(size_t i=0; i<opengl_render_data->batches.size(); ++i)
		{
			const OpenGLBatch& batch = opengl_render_data->batches[i];
			assert(batch.material_index < mesh->num_materials_referenced);
			assert(batch.num_indices > 0);
			assert(batch.prim_start_offset < opengl_render_data->vert_indices_buf->getSize());
			const uint32 bytes_per_index = opengl_render_data->index_type == GL_UNSIGNED_INT ? 4 : (opengl_render_data->index_type == GL_UNSIGNED_SHORT ? 2 : 1);
			assert(batch.prim_start_offset + batch.num_indices*bytes_per_index <= opengl_render_data->vert_indices_buf->getSize());
		}
#endif

		if(MEM_PROFILE)
		{
			const int pad_w = 8;
			conPrint("Src num verts:     " + toString(mesh->vert_positions.size()) + ", num tris: " + toString(mesh->triangles.size()) + ", num quads: " + toString(mesh->quads.size()));
			if(use_half_uvs) conPrint("Using half UVs.");
			conPrint("verts:             " + rightPad(toString(mesh->vert_positions.size()), ' ', pad_w) + "(" + getNiceByteSize(vert_data.dataSizeBytes()) + ")");
			//conPrint("merged_positions:  " + rightPad(toString(num_merged_verts),         ' ', pad_w) + "(" + getNiceByteSize(merged_positions.dataSizeBytes()) + ")");
			//conPrint("merged_normals:    " + rightPad(toString(merged_normals.size()),    ' ', pad_w) + "(" + getNiceByteSize(merged_normals.dataSizeBytes()) + ")");
			//conPrint("merged_uvs:        " + rightPad(toString(merged_uvs.size()),        ' ', pad_w) + "(" + getNiceByteSize(merged_uvs.dataSizeBytes()) + ")");
			conPrint("vert_index_buffer: " + rightPad(toString(mesh->indices.size()), ' ', pad_w) + "(" + getNiceByteSize(opengl_render_data->vert_indices_buf->getSize()) + ")");
		}
		if(PROFILE)
		{
			conPrint("buildIndigoMesh took " + timer.elapsedStringNPlaces(4));
		}

		opengl_render_data->aabb_os.min_ = Vec4f(mesh_->aabb_os.bound[0].x, mesh_->aabb_os.bound[0].y, mesh_->aabb_os.bound[0].z, 1.f);
		opengl_render_data->aabb_os.max_ = Vec4f(mesh_->aabb_os.bound[1].x, mesh_->aabb_os.bound[1].y, mesh_->aabb_os.bound[1].z, 1.f);
		return opengl_render_data;
	}
#endif
	//-------------------------------------------------------------------------------------------------------

	Timer timer;

	const Indigo::Mesh* const mesh = mesh_.getPointer();
	const bool mesh_has_shading_normals = !mesh->vert_normals.empty();
	const bool mesh_has_uvs = mesh->num_uv_mappings > 0;
	const uint32 num_uv_sets = mesh->num_uv_mappings;

	Reference<OpenGLMeshRenderData> opengl_render_data = new OpenGLMeshRenderData();

	UniqueVertKey empty_key;
	empty_key.pos_i = std::numeric_limits<unsigned int>::max();
	empty_key.uv = Vec2f(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());
	HashMapInsertOnly2<UniqueVertKey, uint32, UniqueVertKeyHash> index_for_vert(empty_key, mesh->vert_normals.size());

	// If UVs are somewhat small in magnitude, use GL_HALF_FLOAT instead of GL_FLOAT.
	// If the magnitude is too high we can get articacts if we just use half precision.
	const float max_use_half_range = 10.f;
	bool use_half_uvs = true;
	for(size_t i=0; i<mesh->uv_pairs.size(); ++i)
		if(std::fabs(mesh->uv_pairs[i].x) > max_use_half_range || std::fabs(mesh->uv_pairs[i].y) > max_use_half_range)
			use_half_uvs = false;

	js::Vector<uint8, 16> vert_data;
#ifdef OSX // GL_INT_2_10_10_10_REV is not present in our OS X header files currently.
	const size_t packed_normal_size = sizeof(float)*3;
#else
	const size_t packed_normal_size = 4; // 4 bytes since we are using GL_INT_2_10_10_10_REV format.
#endif
	const size_t packed_uv_size = use_half_uvs ? sizeof(half)*2 : sizeof(float)*2;
	const size_t num_bytes_per_vert = sizeof(float)*3 + (mesh_has_shading_normals ? packed_normal_size : 0) + (mesh_has_uvs ? packed_uv_size : 0);
	const size_t normal_offset      = sizeof(float)*3;
	const size_t uv_offset          = sizeof(float)*3 + (mesh_has_shading_normals ? packed_normal_size : 0);
	vert_data.reserve(mesh->vert_positions.size() * num_bytes_per_vert);
	size_t next_merged_vert_i = 0;
	

	js::Vector<uint32, 16> vert_index_buffer;
	vert_index_buffer.resizeNoCopy(mesh->triangles.size()*3 + mesh->quads.size()*6); // Quads are rendered as two tris.
	uint32 vert_index_buffer_i = 0; // Current write index into vert_index_buffer


	const size_t vert_positions_size = mesh->vert_positions.size();
	const size_t uvs_size = mesh->uv_pairs.size();

	// Create per-material OpenGL triangle indices
	if(mesh->triangles.size() > 0)
	{
		// Create list of triangle references sorted by material index
		std::vector<std::pair<uint32, uint32> > tri_indices(mesh->triangles.size());
				
		assert(mesh->num_materials_referenced > 0);
				
		for(uint32 t = 0; t < mesh->triangles.size(); ++t)
			tri_indices[t] = std::make_pair(mesh->triangles[t].tri_mat_index, t);

		std::sort(tri_indices.begin(), tri_indices.end(), uint32KVP());

		uint32 current_mat_index = std::numeric_limits<uint32>::max();
		uint32 last_pass_start_tri_i = 0;
		for(uint32 t = 0; t < tri_indices.size(); ++t)
		{
			// If we've switched to a new material then start a new triangle range
			if(tri_indices[t].first != current_mat_index)
			{
				// Add last pass data
				if(t > last_pass_start_tri_i) // Don't add zero-length passes.
				{
					OpenGLBatch batch;
					batch.material_index = current_mat_index;
					batch.prim_start_offset = last_pass_start_tri_i * sizeof(uint32) * 3;
					batch.num_indices = (t - last_pass_start_tri_i)*3;
					opengl_render_data->batches.push_back(batch);
				}

				last_pass_start_tri_i = t;
				current_mat_index = tri_indices[t].first;
			}
			
			const Indigo::Triangle& tri = mesh->triangles[tri_indices[t].second];
			for(uint32 i = 0; i < 3; ++i) // For each vert in tri:
			{
				const uint32 pos_i		= tri.vertex_indices[i];
				const uint32 base_uv_i	= tri.uv_indices[i];
				const uint32 uv_i = base_uv_i * num_uv_sets; // Index of UV for UV set 0.
				if(pos_i >= vert_positions_size)
					throw Indigo::Exception("vert index out of bounds");
				if(mesh_has_uvs && uv_i >= uvs_size)
					throw Indigo::Exception("UV index out of bounds");

				// Look up merged vertex
				UniqueVertKey key; key.pos_i = pos_i; key.uv = mesh_has_uvs ? Vec2f(mesh->uv_pairs[uv_i].x, mesh->uv_pairs[uv_i].y) : Vec2f(0.f); // key.uv_i = uv_i;
				//UniqueVertKey key; 
				//key.p = Vec3f(mesh->vert_positions[pos_i].x, mesh->vert_positions[pos_i].y, mesh->vert_positions[pos_i].z); 
				//key.n = mesh_has_shading_normals ? Vec3f(mesh->vert_normals[pos_i].x, mesh->vert_normals[pos_i].y, mesh->vert_normals[pos_i].z) : Vec3f(0.f);
				//key.uv = mesh_has_uvs ? Vec2f(mesh->uv_pairs[uv_i].x, mesh->uv_pairs[uv_i].y) : Vec2f(0.f);

				auto res = index_for_vert.find(key); // Have we created this unique vert yet?
				uint32 merged_v_index;
				if(res == index_for_vert.end()) // Not created yet:
				{
					merged_v_index = (uint32)next_merged_vert_i++;
					index_for_vert.insert(std::make_pair(key, merged_v_index));

					const size_t cur_size = vert_data.size();
					vert_data.resize(cur_size + num_bytes_per_vert);
					std::memcpy(&vert_data[cur_size], &mesh->vert_positions[pos_i].x, sizeof(Indigo::Vec3f));
					if(mesh_has_shading_normals)
					{
#ifdef OSX
						std::memcpy(&vert_data[cur_size + normal_offset], &mesh->vert_normals[pos_i].x, sizeof(Indigo::Vec3f));
#else
						// Pack normal into GL_INT_2_10_10_10_REV format.
						int x = (int)((mesh->vert_normals[pos_i].x) * 511.f);
						int y = (int)((mesh->vert_normals[pos_i].y) * 511.f);
						int z = (int)((mesh->vert_normals[pos_i].z) * 511.f);
						// ANDing with 1023 isolates the bottom 10 bits.
						uint32 n = (x & 1023) | ((y & 1023) << 10) | ((z & 1023) << 20); 
						std::memcpy(&vert_data[cur_size + normal_offset], &n, 4);
#endif
					}

					if(mesh_has_uvs)
					{
						if(use_half_uvs)
						{
							half uv[2];
							uv[0] = half(mesh->uv_pairs[uv_i].x);
							uv[1] = half(mesh->uv_pairs[uv_i].y);
							std::memcpy(&vert_data[cur_size + uv_offset], uv, 4);
						}
						else
							std::memcpy(&vert_data[cur_size + uv_offset], &mesh->uv_pairs[uv_i].x, sizeof(Indigo::Vec2f));
					}
				}
				else
					merged_v_index = res->second;

				vert_index_buffer[vert_index_buffer_i++] = merged_v_index;
			}
		}

		// Build last pass data that won't have been built yet.
		{
			OpenGLBatch batch;
			batch.material_index = current_mat_index;
			batch.prim_start_offset = last_pass_start_tri_i * sizeof(uint32) * 3;
			batch.num_indices = ((uint32)tri_indices.size() - last_pass_start_tri_i)*3;
			opengl_render_data->batches.push_back(batch);
		}
	}

	if(mesh->quads.size() > 0)
	{
		// Create list of quad references sorted by material index
		std::vector<std::pair<uint32, uint32> > quad_indices(mesh->quads.size());
				
		assert(mesh->num_materials_referenced > 0);
				
		for(uint32 q = 0; q < mesh->quads.size(); ++q)
			quad_indices[q] = std::make_pair(mesh->quads[q].mat_index, q);
		
		std::sort(quad_indices.begin(), quad_indices.end(), uint32KVP());

		uint32 current_mat_index = std::numeric_limits<uint32>::max();
		uint32 last_pass_start_quad_i = 0;
		for(uint32 q = 0; q < quad_indices.size(); ++q)
		{
			// If we've switched to a new material then start a new quad range
			if(quad_indices[q].first != current_mat_index)
			{
				// Add last pass data
				if(q > last_pass_start_quad_i) // Don't add zero-length passes.
				{
					OpenGLBatch batch;
					batch.material_index = current_mat_index;
					batch.prim_start_offset = (uint32)mesh->triangles.size()*sizeof(uint32)*3 + last_pass_start_quad_i*sizeof(uint32)*6;
					batch.num_indices = (q - last_pass_start_quad_i)*6;
					opengl_render_data->batches.push_back(batch);
				}

				last_pass_start_quad_i = q;
				current_mat_index = quad_indices[q].first;
			}
			
			const Indigo::Quad& quad = mesh->quads[quad_indices[q].second];
			uint32 vert_merged_index[4];
			for(uint32 i = 0; i < 4; ++i) // For each vert in quad:
			{
				const uint32 pos_i  = quad.vertex_indices[i];
				const uint32 uv_i   = quad.uv_indices[i];
				if(pos_i >= vert_positions_size)
					throw Indigo::Exception("vert index out of bounds");
				if(mesh_has_uvs && uv_i >= uvs_size)
					throw Indigo::Exception("UV index out of bounds");

				// Look up merged vertex
				UniqueVertKey key; key.pos_i = pos_i; key.uv = mesh_has_uvs ? Vec2f(mesh->uv_pairs[uv_i].x, mesh->uv_pairs[uv_i].y) : Vec2f(0.f); // key.uv_i = uv_i;
				//UniqueVertKey key; 
				//key.p = Vec3f(mesh->vert_positions[pos_i].x, mesh->vert_positions[pos_i].y, mesh->vert_positions[pos_i].z); 
				//key.n = mesh_has_shading_normals ? Vec3f(mesh->vert_normals[pos_i].x, mesh->vert_normals[pos_i].y, mesh->vert_normals[pos_i].z) : Vec3f(0.f);
				//key.uv = mesh_has_uvs ? Vec2f(mesh->uv_pairs[uv_i].x, mesh->uv_pairs[uv_i].y) : Vec2f(0.f);

				auto res = index_for_vert.find(key); // Have we created this unique vert yet?
				uint32 merged_v_index;
				if(res == index_for_vert.end()) // Not created yet:
				{
					merged_v_index = (uint32)next_merged_vert_i++;
					index_for_vert.insert(std::make_pair(key, merged_v_index));

					const size_t cur_size = vert_data.size();
					vert_data.resize(cur_size + num_bytes_per_vert);
					std::memcpy(&vert_data[cur_size], &mesh->vert_positions[pos_i].x, sizeof(Indigo::Vec3f));
					if(mesh_has_shading_normals)
					{
#ifdef OSX
						std::memcpy(&vert_data[cur_size + normal_offset], &mesh->vert_normals[pos_i].x, sizeof(Indigo::Vec3f));
#else
						// Pack normal into GL_INT_2_10_10_10_REV format.
						int x = (int)((mesh->vert_normals[pos_i].x) * 511.f);
						int y = (int)((mesh->vert_normals[pos_i].y) * 511.f);
						int z = (int)((mesh->vert_normals[pos_i].z) * 511.f);
						// ANDing with 1023 isolates the bottom 10 bits.
						uint32 n = (x & 1023) | ((y & 1023) << 10) | ((z & 1023) << 20); 
						std::memcpy(&vert_data[cur_size + normal_offset], &n, 4);
#endif
					}
					if(mesh_has_uvs)
					{
						if(use_half_uvs)
						{
							half uv[2];
							uv[0] = half(mesh->uv_pairs[uv_i].x);
							uv[1] = half(mesh->uv_pairs[uv_i].y);
							std::memcpy(&vert_data[cur_size + uv_offset], uv, 4);
						}
						else
							std::memcpy(&vert_data[cur_size + uv_offset], &mesh->uv_pairs[uv_i].x, sizeof(Indigo::Vec2f));
					}
				}
				else
					merged_v_index = res->second;

				vert_merged_index[i] = merged_v_index;
			} // End for each vert in quad:

			// Tri 1 of quad
			vert_index_buffer[vert_index_buffer_i + 0] = vert_merged_index[0];
			vert_index_buffer[vert_index_buffer_i + 1] = vert_merged_index[1];
			vert_index_buffer[vert_index_buffer_i + 2] = vert_merged_index[2];
			// Tri 2 of quad
			vert_index_buffer[vert_index_buffer_i + 3] = vert_merged_index[0];
			vert_index_buffer[vert_index_buffer_i + 4] = vert_merged_index[2];
			vert_index_buffer[vert_index_buffer_i + 5] = vert_merged_index[3];

			vert_index_buffer_i += 6;
		}

		// Build last pass data that won't have been built yet.
		OpenGLBatch batch;
		batch.material_index = current_mat_index;
		batch.prim_start_offset = (uint32)mesh->triangles.size()*sizeof(uint32)*3 + last_pass_start_quad_i*sizeof(uint32)*6;
		batch.num_indices = ((uint32)quad_indices.size() - last_pass_start_quad_i)*6;
		opengl_render_data->batches.push_back(batch);
	}

	assert(vert_index_buffer_i == (uint32)vert_index_buffer.size());

	if(skip_opengl_calls)
		return opengl_render_data;

	opengl_render_data->vert_vbo = new VBO(&vert_data[0], vert_data.dataSizeBytes());

	const size_t num_merged_verts = next_merged_vert_i;

	VertexSpec spec;

	VertexAttrib pos_attrib;
	pos_attrib.enabled = true;
	pos_attrib.num_comps = 3;
	pos_attrib.type = GL_FLOAT;
	pos_attrib.normalised = false;
	pos_attrib.stride = (uint32)num_bytes_per_vert;
	pos_attrib.offset = 0;
	spec.attributes.push_back(pos_attrib);

	VertexAttrib normal_attrib;
	normal_attrib.enabled = mesh_has_shading_normals;
#ifdef OSX
	normal_attrib.num_comps = 3;
	normal_attrib.type = GL_FLOAT;
	normal_attrib.normalised = false;
#else
	normal_attrib.num_comps = 4;
	normal_attrib.type = GL_INT_2_10_10_10_REV;
	normal_attrib.normalised = true;
#endif
	normal_attrib.stride = (uint32)num_bytes_per_vert;
	normal_attrib.offset = (uint32)normal_offset;
	spec.attributes.push_back(normal_attrib);

	VertexAttrib uv_attrib;
	uv_attrib.enabled = mesh_has_uvs;
	uv_attrib.num_comps = 2;
	uv_attrib.type = use_half_uvs ? GL_HALF_FLOAT : GL_FLOAT;
	uv_attrib.normalised = false;
	uv_attrib.stride = (uint32)num_bytes_per_vert;
	uv_attrib.offset = (uint32)uv_offset;
	spec.attributes.push_back(uv_attrib);

	opengl_render_data->vert_vao = new VAO(opengl_render_data->vert_vbo, spec);


	opengl_render_data->has_uvs				= mesh_has_uvs;
	opengl_render_data->has_shading_normals = mesh_has_shading_normals;
	
	assert(!vert_index_buffer.empty());
	const size_t vert_index_buffer_size = vert_index_buffer.size();

	if(num_merged_verts < 256)
	{
		js::Vector<uint8, 16> index_buf; index_buf.resize(vert_index_buffer_size);
		for(size_t i=0; i<vert_index_buffer_size; ++i)
		{
			assert(vert_index_buffer[i] < 256);
			index_buf[i] = (uint8)vert_index_buffer[i];
		}
		opengl_render_data->vert_indices_buf = new VBO(&index_buf[0], index_buf.dataSizeBytes(), GL_ELEMENT_ARRAY_BUFFER);
		opengl_render_data->index_type = GL_UNSIGNED_BYTE;
		// Go through the batches and adjust the start offset to take into account we're using uint8s.
		for(size_t i=0; i<opengl_render_data->batches.size(); ++i)
			opengl_render_data->batches[i].prim_start_offset /= 4;
	}
	else if(num_merged_verts < 65536)
	{
		js::Vector<uint16, 16> index_buf; index_buf.resize(vert_index_buffer_size);
		for(size_t i=0; i<vert_index_buffer_size; ++i)
		{
			assert(vert_index_buffer[i] < 65536);
			index_buf[i] = (uint16)vert_index_buffer[i];
		}
		opengl_render_data->vert_indices_buf = new VBO(&index_buf[0], index_buf.dataSizeBytes(), GL_ELEMENT_ARRAY_BUFFER);
		opengl_render_data->index_type = GL_UNSIGNED_SHORT;
		// Go through the batches and adjust the start offset to take into account we're using uint16s.
		for(size_t i=0; i<opengl_render_data->batches.size(); ++i)
			opengl_render_data->batches[i].prim_start_offset /= 2;
	}
	else
	{
		opengl_render_data->vert_indices_buf = new VBO(&vert_index_buffer[0], vert_index_buffer.dataSizeBytes(), GL_ELEMENT_ARRAY_BUFFER);
		opengl_render_data->index_type = GL_UNSIGNED_INT;
	}

#ifndef NDEBUG
	for(size_t i=0; i<opengl_render_data->batches.size(); ++i)
	{
		const OpenGLBatch& batch = opengl_render_data->batches[i];
		assert(batch.material_index < mesh->num_materials_referenced);
		assert(batch.num_indices > 0);
		assert(batch.prim_start_offset < opengl_render_data->vert_indices_buf->getSize());
		const uint32 bytes_per_index = opengl_render_data->index_type == GL_UNSIGNED_INT ? 4 : (opengl_render_data->index_type == GL_UNSIGNED_SHORT ? 2 : 1);
		assert(batch.prim_start_offset + batch.num_indices*bytes_per_index <= opengl_render_data->vert_indices_buf->getSize());
	}

	// Check indices
	for(size_t i=0; i<vert_index_buffer.size(); ++i)
		assert(vert_index_buffer[i] < num_merged_verts);
#endif

	if(MEM_PROFILE)
	{
		const int pad_w = 8;
		conPrint("Src num verts:     " + toString(mesh->vert_positions.size()) + ", num tris: " + toString(mesh->triangles.size()) + ", num quads: " + toString(mesh->quads.size()));
		if(use_half_uvs) conPrint("Using half UVs.");
		conPrint("verts:             " + rightPad(toString(num_merged_verts),         ' ', pad_w) + "(" + getNiceByteSize(vert_data.dataSizeBytes()) + ")");
		//conPrint("merged_positions:  " + rightPad(toString(num_merged_verts),         ' ', pad_w) + "(" + getNiceByteSize(merged_positions.dataSizeBytes()) + ")");
		//conPrint("merged_normals:    " + rightPad(toString(merged_normals.size()),    ' ', pad_w) + "(" + getNiceByteSize(merged_normals.dataSizeBytes()) + ")");
		//conPrint("merged_uvs:        " + rightPad(toString(merged_uvs.size()),        ' ', pad_w) + "(" + getNiceByteSize(merged_uvs.dataSizeBytes()) + ")");
		conPrint("vert_index_buffer: " + rightPad(toString(vert_index_buffer.size()), ' ', pad_w) + "(" + getNiceByteSize(opengl_render_data->vert_indices_buf->getSize()) + ")");
	}
	if(PROFILE)
	{
		conPrint("buildIndigoMesh took " + timer.elapsedStringNPlaces(4));
	}

	opengl_render_data->aabb_os.min_ = Vec4f(mesh_->aabb_os.bound[0].x, mesh_->aabb_os.bound[0].y, mesh_->aabb_os.bound[0].z, 1.f);
	opengl_render_data->aabb_os.max_ = Vec4f(mesh_->aabb_os.bound[1].x, mesh_->aabb_os.bound[1].y, mesh_->aabb_os.bound[1].z, 1.f);
	return opengl_render_data;
}


void OpenGLEngine::setUniformsForPhongProg(const OpenGLMaterial& opengl_mat, const OpenGLMeshRenderData& mesh_data, 
	const PhongUniformLocations& use_phong_locations)
{
	const Colour4f col_nonlinear(opengl_mat.albedo_rgb.r, opengl_mat.albedo_rgb.g, opengl_mat.albedo_rgb.b, 1.f);
	const Colour4f col_linear = fastApproxSRGBToLinearSRGB(col_nonlinear);

	glUniform4fv(use_phong_locations.sundir_cs_location, /*count=*/1, this->sun_dir_cam_space.x);
	glUniform4f(use_phong_locations.diffuse_colour_location, col_linear[0], col_linear[1], col_linear[2], 1.f);
	glUniform1i(use_phong_locations.have_shading_normals_location, mesh_data.has_shading_normals ? 1 : 0);
	glUniform1i(use_phong_locations.have_texture_location, opengl_mat.albedo_texture.nonNull() ? 1 : 0);
	glUniform1f(use_phong_locations.roughness_location, opengl_mat.roughness);
	glUniform1f(use_phong_locations.fresnel_scale_location, opengl_mat.fresnel_scale);
	glUniform1f(use_phong_locations.metallic_frac_location, opengl_mat.metallic_frac);

	if(this->cosine_env_tex.nonNull())
	{
		glActiveTexture(GL_TEXTURE0 + 3);
		glBindTexture(GL_TEXTURE_CUBE_MAP, this->cosine_env_tex->texture_handle);
		glUniform1i(use_phong_locations.cosine_env_tex_location, 3);
	}
			
	if(this->specular_env_tex.nonNull())
	{
		glActiveTexture(GL_TEXTURE0 + 4);
		glBindTexture(GL_TEXTURE_2D, this->specular_env_tex->texture_handle);
		glUniform1i(use_phong_locations.specular_env_tex_location, 4);
	}

	if(opengl_mat.albedo_texture.nonNull())
	{
		glActiveTexture(GL_TEXTURE0 + 0);
		glBindTexture(GL_TEXTURE_2D, opengl_mat.albedo_texture->texture_handle);

		const GLfloat tex_elems[9] ={
			opengl_mat.tex_matrix.e[0], opengl_mat.tex_matrix.e[2], 0,
			opengl_mat.tex_matrix.e[1], opengl_mat.tex_matrix.e[3], 0,
			opengl_mat.tex_translation.x, opengl_mat.tex_translation.y, 1
		};
		glUniformMatrix3fv(use_phong_locations.texture_matrix_location, /*count=*/1, /*transpose=*/false, tex_elems);
		glUniform1i(use_phong_locations.diffuse_tex_location, 0);
	}

	// Set shadow mapping uniforms
	if(shadow_mapping.nonNull())
	{
		glActiveTexture(GL_TEXTURE0 + 1);

		glBindTexture(GL_TEXTURE_2D, this->shadow_mapping->depth_tex->texture_handle);
		glUniform1i(use_phong_locations.dynamic_depth_tex_location, 1); // Texture unit 1 is for shadow maps

		glActiveTexture(GL_TEXTURE0 + 2);

		glBindTexture(GL_TEXTURE_2D, this->shadow_mapping->static_depth_tex->texture_handle);
		glUniform1i(use_phong_locations.static_depth_tex_location, 2);


		glUniformMatrix4fv(use_phong_locations.shadow_texture_matrix_location, 
			/*count=*/shadow_mapping->numDynamicDepthTextures() + shadow_mapping->numStaticDepthTextures(), 
			/*transpose=*/false, shadow_mapping->shadow_tex_matrix[0].e);
	}

	const Vec4f campos_ws = cam_to_world.getColumn(3);
	glUniform3fv(use_phong_locations.campos_ws_location, 1, campos_ws.x);
}


void OpenGLEngine::drawBatch(const GLObject& ob, const Matrix4f& view_mat, const Matrix4f& proj_mat, 
	const OpenGLMaterial& opengl_mat, const Reference<OpenGLProgram>& shader_prog, const OpenGLMeshRenderData& mesh_data, const OpenGLBatch& batch)
{
	if(shader_prog.nonNull())
	{
		shader_prog->useProgram();

		// Set uniforms.  NOTE: Setting the uniforms manually in this way (switching on shader program) is obviously quite hacky.  Improve.
		if(shader_prog.getPointer() == this->depth_draw_mat.shader_prog.getPointer())
		{
			const Matrix4f proj_view_model_matrix = proj_mat * view_mat * ob.ob_to_world_matrix;
			glUniformMatrix4fv(this->depth_proj_view_model_matrix_location, 1, false, proj_view_model_matrix.e);
		}
		else if(shader_prog.getPointer() == this->depth_draw_with_alpha_test_mat.shader_prog.getPointer())
		{
			const Matrix4f proj_view_model_matrix = proj_mat * view_mat * ob.ob_to_world_matrix;
			glUniformMatrix4fv(this->depth_with_alpha_proj_view_model_matrix_location, 1, false, proj_view_model_matrix.e);
		}
		else
		{
			glUniformMatrix4fv(shader_prog->model_matrix_loc, 1, false, ob.ob_to_world_matrix.e);
			glUniformMatrix4fv(shader_prog->view_matrix_loc, 1, false, view_mat.e);
			glUniformMatrix4fv(shader_prog->proj_matrix_loc, 1, false, proj_mat.e);
			glUniformMatrix4fv(shader_prog->normal_matrix_loc, 1, false, ob.ob_to_world_inv_tranpose_matrix.e); // inverse transpose model matrix
		}

		
		if(shader_prog.getPointer() == this->phong_prog.getPointer())
		{
			setUniformsForPhongProg(opengl_mat, mesh_data, phong_locations);
		}
		else if(shader_prog.getPointer() == this->phong_with_alpha_test_prog.getPointer())
		{
			setUniformsForPhongProg(opengl_mat, mesh_data, phong_with_alpha_test_locations);
		}
		else if(shader_prog.getPointer() == this->transparent_prog.getPointer())
		{
			const Colour4f col_nonlinear(opengl_mat.albedo_rgb.r, opengl_mat.albedo_rgb.g, opengl_mat.albedo_rgb.b, 1.f);
			const Colour4f col_linear = fastApproxSRGBToLinearSRGB(col_nonlinear);

			glUniform4fv(this->transparent_sundir_cs_location, /*count=*/1, this->sun_dir_cam_space.x);
			glUniform4f(this->transparent_colour_location, col_linear[0], col_linear[1], col_linear[2], opengl_mat.alpha);
			glUniform1i(this->transparent_have_shading_normals_location, mesh_data.has_shading_normals ? 1 : 0);
			if(this->specular_env_tex.nonNull())
			{
				glActiveTexture(GL_TEXTURE0 + 4);
				glBindTexture(GL_TEXTURE_2D, this->specular_env_tex->texture_handle);
				glUniform1i(transparent_specular_env_tex_location, 4);
			}
			const Vec4f campos_ws = cam_to_world.getColumn(3);
			glUniform3fv(this->transparent_campos_ws_location, 1, campos_ws.x);
		}
		else if(shader_prog.getPointer() == this->env_prog.getPointer())
		{
			glUniform4fv(this->env_sundir_cs_location, /*count=*/1, this->sun_dir_cam_space.x);
			glUniform4f(this->env_diffuse_colour_location, opengl_mat.albedo_rgb.r, opengl_mat.albedo_rgb.g, opengl_mat.albedo_rgb.b, 1.f);
			glUniform1i(this->env_have_texture_location, opengl_mat.albedo_texture.nonNull() ? 1 : 0);
			
			if(opengl_mat.albedo_texture.nonNull())
			{
				glActiveTexture(GL_TEXTURE0 + 0);
				glBindTexture(GL_TEXTURE_2D, opengl_mat.albedo_texture->texture_handle);

				const GLfloat tex_elems[9] = {
					opengl_mat.tex_matrix.e[0], opengl_mat.tex_matrix.e[2], 0,
					opengl_mat.tex_matrix.e[1], opengl_mat.tex_matrix.e[3], 0,
					opengl_mat.tex_translation.x, opengl_mat.tex_translation.y, 1
				};
				glUniformMatrix3fv(this->env_texture_matrix_location, /*count=*/1, /*transpose=*/false, tex_elems);
				glUniform1i(this->env_diffuse_tex_location, 0);
			}
		}
		else if(shader_prog.getPointer() == this->depth_draw_mat.shader_prog.getPointer())
		{

		}
		else if(shader_prog.getPointer() == this->depth_draw_with_alpha_test_mat.shader_prog.getPointer())
		{
			assert(opengl_mat.albedo_texture.nonNull()); // We should only be using the depth shader with alpha test if there is a texture with alpha.
			if(opengl_mat.albedo_texture.nonNull())
			{
				glActiveTexture(GL_TEXTURE0 + 0);
				glBindTexture(GL_TEXTURE_2D, opengl_mat.albedo_texture->texture_handle);

				const GLfloat tex_elems[9] ={
					opengl_mat.tex_matrix.e[0], opengl_mat.tex_matrix.e[2], 0,
					opengl_mat.tex_matrix.e[1], opengl_mat.tex_matrix.e[3], 0,
					opengl_mat.tex_translation.x, opengl_mat.tex_translation.y, 1
				};
				glUniformMatrix3fv(this->depth_texture_matrix_location, /*count=*/1, /*transpose=*/false, tex_elems);
				glUniform1i(this->depth_diffuse_tex_location, 0);
			}
		}
		else if(shader_prog.getPointer() == this->outline_prog.getPointer())
		{

		}
		else // Else shader created by user code:
		{
			if(shader_prog->campos_ws_loc >= 0)
			{
				const Vec4f campos_ws = cam_to_world.getColumn(3);
				glUniform3fv(shader_prog->campos_ws_loc, 1, campos_ws.x);
			}
			if(shader_prog->time_loc >= 0)
				glUniform1f(shader_prog->time_loc, this->current_time);
			if(shader_prog->colour_loc >= 0)
				glUniform3fv(shader_prog->colour_loc, 1, &opengl_mat.albedo_rgb.r);
		}
		
		glDrawElements(GL_TRIANGLES, (GLsizei)batch.num_indices, mesh_data.index_type, (void*)(uint64)batch.prim_start_offset);

		shader_prog->useNoPrograms();
	}

	this->num_indices_submitted += batch.num_indices;
	this->num_face_groups_submitted++;
}


// glEnableClientState(GL_VERTEX_ARRAY); should be called before this function is called.
void OpenGLEngine::drawBatchWireframe(const OpenGLBatch& batch, int num_verts_per_primitive)
{
	//assert(glIsEnabled(GL_VERTEX_ARRAY));

	//// Bind vertices
	//pass_data.vertices_vbo->bind();
	//glVertexPointer(3, GL_FLOAT, 0, NULL);
	//pass_data.vertices_vbo->unbind();

	//// Draw the triangles or quads
	//if(num_verts_per_primitive == 3)
	//	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)pass_data.num_prims * 3);
	//else
	//	glDrawArrays(GL_QUADS, 0, (GLsizei)pass_data.num_prims * 4);
}


GLObjectRef OpenGLEngine::makeArrowObject(const Vec4f& startpos, const Vec4f& endpos, const Colour4f& col, float radius_scale)
{
	GLObjectRef ob = new GLObject();

	// We want to map the vector (1,0,0) to endpos-startpos.
	// We want to map the point (0,0,0) to startpos.

	const Vec4f dir = endpos - startpos;
	const Vec4f col1 = normalise(std::fabs(dir[2]) > 0.5f ? crossProduct(dir, Vec4f(1,0,0,0)) : crossProduct(dir, Vec4f(0,0,1,0))) * radius_scale;
	const Vec4f col2 = normalise(crossProduct(dir, col1)) * radius_scale;
	//ob->ob_to_world_matrix = Matrix4f::identity();
	ob->ob_to_world_matrix.setColumn(0, dir);
	ob->ob_to_world_matrix.setColumn(1, col1);
	ob->ob_to_world_matrix.setColumn(2, col2);
	ob->ob_to_world_matrix.setColumn(3, startpos);

	if(arrow_meshdata.isNull())
		arrow_meshdata = make3DArrowMesh();
	ob->mesh_data = arrow_meshdata;
	ob->materials.resize(1);
	ob->materials[0].albedo_rgb = Colour3f(col[0], col[1], col[2]);
	return ob;
}


GLObjectRef OpenGLEngine::makeAABBObject(const Vec4f& min_, const Vec4f& max_, const Colour4f& col)
{
	GLObjectRef ob = new GLObject();

	const Vec4f span = max_ - min_;

	ob->ob_to_world_matrix.setColumn(0, Vec4f(span[0], 0, 0, 0));
	ob->ob_to_world_matrix.setColumn(1, Vec4f(0, span[1], 0, 0));
	ob->ob_to_world_matrix.setColumn(2, Vec4f(0, 0, span[2], 0));
	ob->ob_to_world_matrix.setColumn(3, min_); // set origin

	if(cube_meshdata.isNull())
		cube_meshdata = makeCubeMesh();
	ob->mesh_data = cube_meshdata;
	ob->materials.resize(1);
	ob->materials[0].albedo_rgb = Colour3f(col[0], col[1], col[2]);
	ob->materials[0].alpha = col[3];
	ob->materials[0].transparent = col[3] < 1.f;
	return ob;
}


// Make a cylinder from (0,0,0), to (0,0,1) with radius 1.
Reference<OpenGLMeshRenderData> OpenGLEngine::makeCylinderMesh()
{
	const Vec3f endpoint_a(0, 0, 0);
	const Vec3f endpoint_b(0, 0, 1);
	Reference<OpenGLMeshRenderData> mesh_data = new OpenGLMeshRenderData();

	const int res = 16;

	js::Vector<Vec3f, 16> verts;
	verts.resize(res * 4 + 2);
	js::Vector<Vec3f, 16> normals;
	normals.resize(res * 4 + 2);
	js::Vector<Vec2f, 16> uvs;
	uvs.resize(res * 4 + 2);
	js::Vector<uint32, 16> indices;
	indices.resize(res * 12); // 4 tris * res

	const Vec3f dir(0, 0, 1);
	const Vec3f basis_i(1, 0, 0);
	const Vec3f basis_j(0, 1, 0);

	// Make cylinder sides
	for(int i=0; i<res; ++i)
	{
		const float angle = i * Maths::get2Pi<float>() / res;

		// Set verts
		{
			//v[i*2 + 0] is top of side edge, facing outwards.
			//v[i*2 + 1] is bottom of side edge, facing outwards.
			const Vec3f normal = basis_i * cos(angle) + basis_j * sin(angle);
			const Vec3f bottom_pos = endpoint_a + normal;
			const Vec3f top_pos    = endpoint_b + normal;
			
			normals[i*2 + 0] = normal;
			normals[i*2 + 1] = normal;
			verts[i*2 + 0] = bottom_pos;
			verts[i*2 + 1] = top_pos;
			uvs[i*2 + 0] = Vec2f(0.f);
			uvs[i*2 + 1] = Vec2f(0.f);

			//v[2*r + i] is top of side edge, facing upwards
			normals[2*res + i] = dir;
			verts[2*res + i] = top_pos;
			uvs[2*res + i] = Vec2f(0.f);

			//v[3*r + i] is bottom of side edge, facing down
			normals[3*res + i] = -dir;
			verts[3*res + i] = bottom_pos;
			uvs[3*res + i] = Vec2f(0.f);
		}

		// top centre vert
		normals[4*res + 0] = dir;
		verts[4*res + 0] = endpoint_b;
		uvs[4*res + 0] = Vec2f(0.f);

		// bottom centre vert
		normals[4*res + 1] = -dir;
		verts[4*res + 1] = endpoint_a;
		uvs[4*res + 1] = Vec2f(0.f);


		// Set tris

		// Side face triangles
		indices[i*12 + 0] = i*2 + 1; // top
		indices[i*12 + 1] = i*2 + 0; // bottom 
		indices[i*12 + 2] = (i*2 + 2) % (2*res); // bottom on next edge
		indices[i*12 + 3] = i*2 + 1; // top
		indices[i*12 + 4] = (i*2 + 2) % (2*res); // bottom on next edge
		indices[i*12 + 5] = (i*2 + 3) % (2*res); // top on next edge

		// top triangle
		indices[i*12 + 6] = res*2 + i + 0;
		indices[i*12 + 7] = res*2 + (i + 1) % res;
		indices[i*12 + 8] = res*4; // top centre vert

		// bottom triangle
		indices[i*12 + 9]  = res*3 + (i + 1) % res;
		indices[i*12 + 10] = res*3 + i + 0;
		indices[i*12 + 11] = res*4 + 1; // bottom centre vert
	}

	buildMeshRenderData(*mesh_data, verts, normals, uvs, indices);
	return mesh_data;
}


Reference<OpenGLMeshRenderData> OpenGLEngine::getCylinderMesh() // A cylinder from (0,0,0), to (0,0,1) with radius 1;
{
	if(cylinder_meshdata.isNull())
		cylinder_meshdata = makeCylinderMesh();
	return cylinder_meshdata;
}


// Base will be at origin, tip will lie at (1, 0, 0)
Reference<OpenGLMeshRenderData> OpenGLEngine::make3DArrowMesh()
{
	Reference<OpenGLMeshRenderData> mesh_data = new OpenGLMeshRenderData();
	
	const int res = 20;

	js::Vector<Vec3f, 16> verts;
	verts.resize(res * 4 * 2);
	js::Vector<Vec3f, 16> normals;
	normals.resize(res * 4 * 2);
	js::Vector<Vec2f, 16> uvs;
	uvs.resize(res * 4 * 2);
	js::Vector<uint32, 16> indices;
	indices.resize(res * 6 * 2); // two tris per quad

	const Vec3f dir(1,0,0);
	const Vec3f basis_i(0,1,0);
	const Vec3f basis_j(0,0,1);

	const float length = 1;
	const float shaft_r = length * 0.02f;
	const float shaft_len = length * 0.8f;
	const float head_r = length * 0.04f;

	// Draw cylinder for shaft of arrow
	for(int i=0; i<res; ++i)
	{
		const float angle      = i       * Maths::get2Pi<float>() / res;
		const float next_angle = (i + 1) * Maths::get2Pi<float>() / res;

		// Define quad
		{
		Vec3f normal1(basis_i * cos(angle     ) + basis_j * sin(angle     ));
		Vec3f normal2(basis_i * cos(next_angle) + basis_j * sin(next_angle));

		normals[i*8 + 0] = normal1;
		normals[i*8 + 1] = normal2;
		normals[i*8 + 2] = normal2;
		normals[i*8 + 3] = normal1;

		Vec3f v0((basis_i * cos(angle     ) + basis_j * sin(angle     )) * shaft_r);
		Vec3f v1((basis_i * cos(next_angle) + basis_j * sin(next_angle)) * shaft_r);
		Vec3f v2((basis_i * cos(next_angle) + basis_j * sin(next_angle)) * shaft_r + dir * shaft_len);
		Vec3f v3((basis_i * cos(angle     ) + basis_j * sin(angle     )) * shaft_r + dir * shaft_len);

		verts[i*8 + 0] = v0;
		verts[i*8 + 1] = v1;
		verts[i*8 + 2] = v2;
		verts[i*8 + 3] = v3;

		uvs[i*8 + 0] = Vec2f(0.f);
		uvs[i*8 + 1] = Vec2f(0.f);
		uvs[i*8 + 2] = Vec2f(0.f);
		uvs[i*8 + 3] = Vec2f(0.f);

		indices[i*12 + 0] = i*8 + 0; 
		indices[i*12 + 1] = i*8 + 1; 
		indices[i*12 + 2] = i*8 + 2; 
		indices[i*12 + 3] = i*8 + 0;
		indices[i*12 + 4] = i*8 + 2;
		indices[i*12 + 5] = i*8 + 3;
		}

		// Define arrow head
		{
		//Vec3f normal(basis_i * cos(angle     ) + basis_j * sin(angle     ));
		// NOTE: this normal is somewhat wrong.
		Vec3f normal1(basis_i * cos(angle     ) + basis_j * sin(angle     ));
		Vec3f normal2(basis_i * cos(next_angle) + basis_j * sin(next_angle));

		normals[i*8 + 4] = normal1;
		normals[i*8 + 5] = normal2;
		normals[i*8 + 6] = normal2;
		normals[i*8 + 7] = normal1;

		Vec3f v0((basis_i * cos(angle     ) + basis_j * sin(angle     )) * head_r + dir * shaft_len);
		Vec3f v1((basis_i * cos(next_angle) + basis_j * sin(next_angle)) * head_r + dir * shaft_len);
		Vec3f v2(dir * length);
		Vec3f v3(dir * length);

		verts[i*8 + 4] = v0;
		verts[i*8 + 5] = v1;
		verts[i*8 + 6] = v2;
		verts[i*8 + 7] = v3;

		uvs[i*8 + 4] = Vec2f(0.f);
		uvs[i*8 + 5] = Vec2f(0.f);
		uvs[i*8 + 6] = Vec2f(0.f);
		uvs[i*8 + 7] = Vec2f(0.f);

		indices[i*12 +  6] = i*8 + 4; 
		indices[i*12 +  7] = i*8 + 5; 
		indices[i*12 +  8] = i*8 + 6; 
		indices[i*12 +  9] = i*8 + 4;
		indices[i*12 + 10] = i*8 + 6;
		indices[i*12 + 11] = i*8 + 7;
		}
	}

	buildMeshRenderData(*mesh_data, verts, normals, uvs, indices);
	return mesh_data;
}


// Base will be at origin, tip will lie at (1, 0, 0)
Reference<OpenGLMeshRenderData> OpenGLEngine::makeCapsuleMesh(const Vec3f& bottom_spans, const Vec3f& top_spans)
{
	Reference<OpenGLMeshRenderData> mesh_data = new OpenGLMeshRenderData();
	
	const int phi_res = 20;
//	const int theta_res = 10;

	js::Vector<Vec3f, 16> verts;
	verts.resize(phi_res * 2 * 2);
	js::Vector<Vec3f, 16> normals;
	normals.resize(phi_res * 2 * 2);
	js::Vector<Vec2f, 16> uvs;
	uvs.resize(phi_res * 2 * 2);
	js::Vector<uint32, 16> indices;
	indices.resize(phi_res * 3 * 2); // two tris per quad * 3 indices per tri

#if 0
	const Vec4f dir(0,0,1,0);
	const Vec4f bot_basis_i(bottom_spans[0],0,0,0);
	const Vec4f bot_basis_j(0,bottom_spans[1],0,0);
	const Vec4f top_basis_i(top_spans[0],0,0,0);
	const Vec4f top_basis_j(0,top_spans[1],0,0);

	// Create cylinder
	for(int i=0; i<phi_res; ++i)
	{
		const float angle      = i       * Maths::get2Pi<float>() / phi_res;
		const float next_angle = (i + 1) * Maths::get2Pi<float>() / phi_res;

		Vec3f normal1(bot_basis_i * cos(angle     ) + bot_basis_j * sin(angle     ));
		Vec3f normal2(bot_basis_i * cos(next_angle) + bot_basis_j * sin(next_angle));

		normals[i*4 + 0] = normal1;
		normals[i*4 + 1] = normal2;
		normals[i*4 + 2] = normal2;
		normals[i*4 + 3] = normal1;

		Vec3f v0((bot_basis_i * cos(angle     ) + bot_basis_j * sin(angle     )));
		Vec3f v1((bot_basis_i * cos(next_angle) + bot_basis_j * sin(next_angle)));

		Vec3f v2((top_basis_i * cos(next_angle) + top_basis_j * sin(next_angle)) + dir);
		Vec3f v3((top_basis_i * cos(angle     ) + top_basis_j * sin(angle     )) + dir);

		verts[i*4 + 0] = v0;
		verts[i*4 + 1] = v1;
		verts[i*4 + 2] = v2;
		verts[i*4 + 3] = v3;

		uvs[i*4 + 0] = Vec2f(0.f);
		uvs[i*4 + 1] = Vec2f(0.f);
		uvs[i*4 + 2] = Vec2f(0.f);
		uvs[i*4 + 3] = Vec2f(0.f);

		indices[i*6 + 0] = i*4 + 0; 
		indices[i*6 + 1] = i*4 + 1; 
		indices[i*6 + 2] = i*4 + 2; 
		indices[i*6 + 3] = i*4 + 0;
		indices[i*6 + 4] = i*4 + 2;
		indices[i*6 + 5] = i*4 + 3;
	}

	int normals_offset = phi_res * 4;
	int uvs_offset = phi_res * 4;
	int verts_offset = phi_res * 4;
	int indices_offset = phi_res * 6;

	//Matrix4f to_basis(Vec4f(0,0,0,1), 

	// Create top hemisphere cap
	/*for(int theta_i=0; theta_i<theta_res; ++theta_i)
	{
		for(int phi_i=0; phi_i<phi_res; ++phi_i)
		{
			const float phi_1 = Maths::get2Pi<float>() * phi_i / phi_res;
			const float phi_2 = Maths::get2Pi<float>() * (phi_i + 1) / phi_res;
			const float theta_1 = Maths::pi<float>() * theta_i / theta_res;
			const float theta_2 = Maths::pi<float>() * (theta_i + 1) / theta_res;

			const Vec4f p1 = GeometrySampling::dirForSphericalCoords(phi_1, theta_1);
			const Vec4f p2 = GeometrySampling::dirForSphericalCoords(phi_2, theta_1);
			const Vec4f p3 = GeometrySampling::dirForSphericalCoords(phi_2, theta_2);
			const Vec4f p4 = GeometrySampling::dirForSphericalCoords(phi_1, theta_2);


			//Vec3f normal1(bot_basis_i * cos(phi     ) + bot_basis_j * sin(phi     ));
			//Vec3f normal2(bot_basis_i * cos(next_phi) + bot_basis_j * sin(next_phi));

			normals[i*4 + 0] = p1;
			normals[i*4 + 1] = p2;
			normals[i*4 + 2] = p3;
			normals[i*4 + 3] = p4;

			Vec3f v0((bot_basis_i * cos(angle     ) + bot_basis_j * sin(angle     )));
			Vec3f v1((bot_basis_i * cos(next_angle) + bot_basis_j * sin(next_angle)));

			Vec3f v2((top_basis_i * cos(next_angle) + top_basis_j * sin(next_angle)) + dir);
			Vec3f v3((top_basis_i * cos(angle     ) + top_basis_j * sin(angle     )) + dir);

			verts[i*4 + 0] = v0;
			verts[i*4 + 1] = v1;
			verts[i*4 + 2] = v2;
			verts[i*4 + 3] = v3;

			uvs[i*4 + 0] = Vec2f(0.f);
			uvs[i*4 + 1] = Vec2f(0.f);
			uvs[i*4 + 2] = Vec2f(0.f);
			uvs[i*4 + 3] = Vec2f(0.f);

			indices[i*6 + 0] = i*4 + 0; 
			indices[i*6 + 1] = i*4 + 1; 
			indices[i*6 + 2] = i*4 + 2; 
			indices[i*6 + 3] = i*4 + 0;
			indices[i*6 + 4] = i*4 + 2;
			indices[i*6 + 5] = i*4 + 3;
		}
	}*/


#endif
	buildMeshRenderData(*mesh_data, verts, normals, uvs, indices);
	return mesh_data;
}


// Make a cube mesh.  Bottom left corner will be at origin, opposite corner will lie at (1, 1, 1)
Reference<OpenGLMeshRenderData> OpenGLEngine::makeCubeMesh()
{
	Reference<OpenGLMeshRenderData> mesh_data = new OpenGLMeshRenderData();
	
	js::Vector<Vec3f, 16> verts;
	verts.resize(24); // 6 faces * 4 verts/face
	js::Vector<Vec3f, 16> normals;
	normals.resize(24);
	js::Vector<Vec2f, 16> uvs;
	uvs.resize(24);
	js::Vector<uint32, 16> indices;
	indices.resize(6 * 6); // two tris per face, 6 faces

	for(int face = 0; face < 6; ++face)
	{
		indices[face*6 + 0] = face*4 + 0; 
		indices[face*6 + 1] = face*4 + 1; 
		indices[face*6 + 2] = face*4 + 2; 
		indices[face*6 + 3] = face*4 + 0;
		indices[face*6 + 4] = face*4 + 2;
		indices[face*6 + 5] = face*4 + 3;
	}
	
	int face = 0;

	// x = 0 face
	{
		Vec3f v0(0, 0, 0);
		Vec3f v1(0, 0, 1);
		Vec3f v2(0, 1, 1);
		Vec3f v3(0, 1, 0);
		
		verts[face*4 + 0] = v0;
		verts[face*4 + 1] = v1;
		verts[face*4 + 2] = v2;
		verts[face*4 + 3] = v3;

		uvs[face*4 + 0] = Vec2f(0, 0);
		uvs[face*4 + 1] = Vec2f(0, 1);
		uvs[face*4 + 2] = Vec2f(1, 1);
		uvs[face*4 + 3] = Vec2f(1, 0);

		for(int i=0; i<4; ++i)
			normals[face*4 + i] = Vec3f(-1, 0, 0);
		
		face++;
	}

	// x = 1 face
	{
		Vec3f v0(1, 0, 0);
		Vec3f v1(1, 1, 0);
		Vec3f v2(1, 1, 1);
		Vec3f v3(1, 0, 1);
		
		verts[face*4 + 0] = v0;
		verts[face*4 + 1] = v1;
		verts[face*4 + 2] = v2;
		verts[face*4 + 3] = v3;

		uvs[face*4 + 0] = Vec2f(0, 0);
		uvs[face*4 + 1] = Vec2f(1, 0);
		uvs[face*4 + 2] = Vec2f(1, 1);
		uvs[face*4 + 3] = Vec2f(0, 1);

		for(int i=0; i<4; ++i)
			normals[face*4 + i] = Vec3f(1, 0, 0);
		
		face++;
	}

	// y = 0 face
	{
		Vec3f v0(0, 0, 0);
		Vec3f v1(1, 0, 0);
		Vec3f v2(1, 0, 1);
		Vec3f v3(0, 0, 1);
		
		verts[face*4 + 0] = v0;
		verts[face*4 + 1] = v1;
		verts[face*4 + 2] = v2;
		verts[face*4 + 3] = v3;

		uvs[face*4 + 0] = Vec2f(0, 0);
		uvs[face*4 + 1] = Vec2f(1, 0);
		uvs[face*4 + 2] = Vec2f(1, 1);
		uvs[face*4 + 3] = Vec2f(0, 1);

		for(int i=0; i<4; ++i)
			normals[face*4 + i] = Vec3f(0, -1, 0);
		
		face++;
	}

	// y = 1 face
	{
		Vec3f v0(0, 1, 0);
		Vec3f v1(0, 1, 1);
		Vec3f v2(1, 1, 1);
		Vec3f v3(1, 1, 0);
		
		verts[face*4 + 0] = v0;
		verts[face*4 + 1] = v1;
		verts[face*4 + 2] = v2;
		verts[face*4 + 3] = v3;

		uvs[face*4 + 0] = Vec2f(0, 0);
		uvs[face*4 + 1] = Vec2f(0, 1);
		uvs[face*4 + 2] = Vec2f(1, 1);
		uvs[face*4 + 3] = Vec2f(1, 0);

		for(int i=0; i<4; ++i)
			normals[face*4 + i] = Vec3f(0, 1, 0);
		
		face++;
	}

	// z = 0 face
	{
		Vec3f v0(0, 0, 0);
		Vec3f v1(1, 0, 0);
		Vec3f v2(1, 1, 0);
		Vec3f v3(0, 1, 0);
		
		verts[face*4 + 0] = v0;
		verts[face*4 + 1] = v1;
		verts[face*4 + 2] = v2;
		verts[face*4 + 3] = v3;

		uvs[face*4 + 0] = Vec2f(0, 0);
		uvs[face*4 + 1] = Vec2f(1, 0);
		uvs[face*4 + 2] = Vec2f(1, 1);
		uvs[face*4 + 3] = Vec2f(0, 1);

		for(int i=0; i<4; ++i)
			normals[face*4 + i] = Vec3f(0, 0, -1);
		
		face++;
	}

	// z = 1 face
	{
		Vec3f v0(0, 0, 1);
		Vec3f v1(1, 0, 1);
		Vec3f v2(1, 1, 1);
		Vec3f v3(0, 1, 1);

		verts[face*4 + 0] = v0;
		verts[face*4 + 1] = v1;
		verts[face*4 + 2] = v2;
		verts[face*4 + 3] = v3;

		uvs[face*4 + 0] = Vec2f(0, 0);
		uvs[face*4 + 1] = Vec2f(1, 0);
		uvs[face*4 + 2] = Vec2f(1, 1);
		uvs[face*4 + 3] = Vec2f(0, 1);

		for(int i=0; i<4; ++i)
			normals[face*4 + i] = Vec3f(0, 0, 1);
		
		face++;
	}

	buildMeshRenderData(*mesh_data, verts, normals, uvs, indices);
	return mesh_data;
}


void OpenGLEngine::addDebugHexahedron(const Vec4f* verts_ws, const Colour4f& col)
{
	Reference<OpenGLMeshRenderData> mesh_data = new OpenGLMeshRenderData();
	
	js::Vector<Vec3f, 16> verts;
	verts.resize(24); // 6 faces * 4 verts/face
	js::Vector<Vec3f, 16> normals;
	normals.resize(24);
	js::Vector<Vec2f, 16> uvs(24, Vec2f(0.f));
	js::Vector<uint32, 16> indices;
	indices.resize(6 * 6); // two tris per face, 6 faces

	for(int face = 0; face < 6; ++face)
	{
		indices[face*6 + 0] = face*4 + 0; 
		indices[face*6 + 1] = face*4 + 1; 
		indices[face*6 + 2] = face*4 + 2; 
		indices[face*6 + 3] = face*4 + 0;
		indices[face*6 + 4] = face*4 + 2;
		indices[face*6 + 5] = face*4 + 3;
	}
	
	int face = 0;

	// x = 0 face
	verts[face*4 + 0] = toVec3f(verts_ws[0]);
	verts[face*4 + 1] = toVec3f(verts_ws[4]);
	verts[face*4 + 2] = toVec3f(verts_ws[7]);
	verts[face*4 + 3] = toVec3f(verts_ws[3]);
	for(int i=0; i<4; ++i)
		normals[face*4 + i] = crossProduct(verts[face*4 + 1] - verts[face*4 + 0], verts[face*4 + 3] - verts[face*4 + 0]);
	face++;

	// x = 1 face
	verts[face*4 + 0] = toVec3f(verts_ws[1]);
	verts[face*4 + 1] = toVec3f(verts_ws[2]);
	verts[face*4 + 2] = toVec3f(verts_ws[6]);
	verts[face*4 + 3] = toVec3f(verts_ws[5]);
	for(int i=0; i<4; ++i)
		normals[face*4 + i] = crossProduct(verts[face*4 + 1] - verts[face*4 + 0], verts[face*4 + 3] - verts[face*4 + 0]);
	face++;

	// y = 0 face
	verts[face*4 + 0] = toVec3f(verts_ws[0]);
	verts[face*4 + 1] = toVec3f(verts_ws[1]);
	verts[face*4 + 2] = toVec3f(verts_ws[5]);
	verts[face*4 + 3] = toVec3f(verts_ws[4]);
	for(int i=0; i<4; ++i)
		normals[face*4 + i] = crossProduct(verts[face*4 + 1] - verts[face*4 + 0], verts[face*4 + 3] - verts[face*4 + 0]);
	face++;

	// y = 1 face
	verts[face*4 + 0] = toVec3f(verts_ws[2]);
	verts[face*4 + 1] = toVec3f(verts_ws[3]);
	verts[face*4 + 2] = toVec3f(verts_ws[7]);
	verts[face*4 + 3] = toVec3f(verts_ws[6]);
	for(int i=0; i<4; ++i)
		normals[face*4 + i] = crossProduct(verts[face*4 + 1] - verts[face*4 + 0], verts[face*4 + 3] - verts[face*4 + 0]);
	face++;

	// z = 0 face
	verts[face*4 + 0] = toVec3f(verts_ws[0]);
	verts[face*4 + 1] = toVec3f(verts_ws[3]);
	verts[face*4 + 2] = toVec3f(verts_ws[2]);
	verts[face*4 + 3] = toVec3f(verts_ws[1]);
	for(int i=0; i<4; ++i)
		normals[face*4 + i] = crossProduct(verts[face*4 + 1] - verts[face*4 + 0], verts[face*4 + 3] - verts[face*4 + 0]);
	face++;

	// z = 1 face
	verts[face*4 + 0] = toVec3f(verts_ws[4]);
	verts[face*4 + 1] = toVec3f(verts_ws[5]);
	verts[face*4 + 2] = toVec3f(verts_ws[6]);
	verts[face*4 + 3] = toVec3f(verts_ws[7]);
	for(int i=0; i<4; ++i)
		normals[face*4 + i] = crossProduct(verts[face*4 + 1] - verts[face*4 + 0], verts[face*4 + 3] - verts[face*4 + 0]);
	face++;

	buildMeshRenderData(*mesh_data, verts, normals, uvs, indices);


	// Make the object
	GLObjectRef ob = new GLObject();
	ob->ob_to_world_matrix = Matrix4f::identity();
	ob->mesh_data = mesh_data;
	ob->materials.resize(1);
	ob->materials[0].albedo_rgb = Colour3f(col[0], col[1], col[2]);
	ob->materials[0].alpha = col[3];
	ob->materials[0].transparent = col[3] < 1.f;
	
	this->addObject(ob);
}


// Makes a quad with xspan = 1, yspan = 1, lying on the z = 0 plane.
Reference<OpenGLMeshRenderData> OpenGLEngine::makeOverlayQuadMesh()
{
	Reference<OpenGLMeshRenderData> mesh_data = new OpenGLMeshRenderData();
	
	js::Vector<Vec3f, 16> verts;
	verts.resize(4);
	js::Vector<Vec3f, 16> normals;
	normals.resize(4);
	js::Vector<Vec2f, 16> uvs;
	uvs.resize(4);
	js::Vector<uint32, 16> indices;
	indices.resize(6); // two tris per face

	indices[0] = 0; 
	indices[1] = 1; 
	indices[2] = 2; 
	indices[3] = 0;
	indices[4] = 2;
	indices[5] = 3;
	
	Vec3f v0(0, 0, 0); // bottom left
	Vec3f v1(1, 0, 0); // bottom right
	Vec3f v2(1, 1, 0); // top right
	Vec3f v3(0, 1, 0); // top left
		
	verts[0] = v0;
	verts[1] = v1;
	verts[2] = v2;
	verts[3] = v3;

	Vec2f uv0(0, 0);
	Vec2f uv1(1, 0);
	Vec2f uv2(1, 1);
	Vec2f uv3(0, 1);
		
	uvs[0] = uv0;
	uvs[1] = uv1;
	uvs[2] = uv2;
	uvs[3] = uv3;

	for(int i=0; i<4; ++i)
		normals[i] = Vec3f(0, 0, -1);

	buildMeshRenderData(*mesh_data, verts, normals, uvs, indices);
	return mesh_data;
}


Reference<OpenGLMeshRenderData> OpenGLEngine::makeQuadMesh(const Vec4f& i, const Vec4f& j)
{
	Reference<OpenGLMeshRenderData> mesh_data = new OpenGLMeshRenderData();

	js::Vector<Vec3f, 16> verts;
	verts.resize(4);
	js::Vector<Vec3f, 16> normals;
	normals.resize(4);
	js::Vector<Vec2f, 16> uvs;
	uvs.resize(4);
	js::Vector<uint32, 16> indices;
	indices.resize(6); // two tris per face

	indices[0] = 0;
	indices[1] = 1;
	indices[2] = 2;
	indices[3] = 0;
	indices[4] = 2;
	indices[5] = 3;

	Vec3f v0(0.f); // bottom left
	Vec3f v1 = toVec3f(i); // bottom right
	Vec3f v2 = toVec3f(i) + toVec3f(j); // top right
	Vec3f v3 = toVec3f(j); // top left

	verts[0] = v0;
	verts[1] = v1;
	verts[2] = v2;
	verts[3] = v3;

	Vec2f uv0(0, 0);
	Vec2f uv1(1, 0);
	Vec2f uv2(1, 1);
	Vec2f uv3(0, 1);

	uvs[0] = uv0;
	uvs[1] = uv1;
	uvs[2] = uv2;
	uvs[3] = uv3;

	for(int z=0; z<4; ++z)
		normals[z] = toVec3f(crossProduct(i, j));// Vec3f(0, 0, -1);

	buildMeshRenderData(*mesh_data, verts, normals, uvs, indices);
	return mesh_data;
}


Reference<OpenGLTexture> OpenGLEngine::loadCubeMap(const std::vector<Reference<Map2D> >& face_maps,
	OpenGLTexture::Filtering filtering, OpenGLTexture::Wrapping wrapping)
{
	Reference<OpenGLTexture> opengl_tex = new OpenGLTexture();

	if(dynamic_cast<const ImageMapFloat*>(face_maps[0].getPointer()))
	{
		std::vector<const void*> tex_data(6);
		for(int i=0; i<6; ++i)
		{
			const ImageMapFloat* imagemap = static_cast<const ImageMapFloat*>(face_maps[i].getPointer());

			if(imagemap->getN() != 3)
				throw Indigo::Exception("Texture has unhandled number of components: " + toString(imagemap->getN()));

			tex_data[i] = imagemap->getData();
		}

		unsigned int tex_xres = face_maps[0]->getMapWidth();
		unsigned int tex_yres = face_maps[0]->getMapHeight();
		opengl_tex->loadCubeMap(tex_xres, tex_yres, tex_data, this, OpenGLTexture::Format_RGB_Linear_Float,
			filtering, wrapping
		);

		//this->opengl_textures.insert(std::make_pair(key, opengl_tex)); // Store

		return opengl_tex;
	}
	/*else if(dynamic_cast<const ImageMap<half, HalfComponentValueTraits>*>(&map2d))
	{
		const ImageMap<half, HalfComponentValueTraits>* imagemap = static_cast<const ImageMap<half, HalfComponentValueTraits>*>(&map2d);

		// Compute hash of map
		const uint64 key = XXH64(imagemap->getData(), imagemap->getDataSize() * sizeof(half), 1);

		auto res = this->opengl_textures.find(key);
		if(res == this->opengl_textures.end())
		{
			// Load texture
			unsigned int tex_xres = map2d.getMapWidth();
			unsigned int tex_yres = map2d.getMapHeight();

			if(imagemap->getN() == 3)
			{
				Reference<OpenGLTexture> opengl_tex = new OpenGLTexture();
				opengl_tex->load(tex_xres, tex_yres, (uint8*)imagemap->getData(), this, target, GL_RGB, GL_RGB, GL_HALF_FLOAT,
					OpenGLTexture::Filtering_Fancy
				);

				this->opengl_textures.insert(std::make_pair(key, opengl_tex)); // Store

				return opengl_tex;
			}
			else
				throw Indigo::Exception("Texture has unhandled number of components: " + toString(imagemap->getN()));

		}
		else // Else if this map has already been loaded into an OpenGL Texture:
		{
			return res->second;
		}
	}*/
	else
	{
		throw Indigo::Exception("Unhandled texture type.");
	}
}


class DXTCompressTask : public Indigo::Task
{
public:
	virtual void run(size_t thread_index)
	{
		const unsigned int W = imagemap->getWidth();
		const unsigned int H = imagemap->getHeight();
		const unsigned int bytes_pp = imagemap->getBytesPerPixel();
		const unsigned int num_blocks_x = Maths::roundedUpDivide(W, 4u);
		uint8* const compressed_data = compressed;
		if(bytes_pp == 4)
		{
			size_t write_i = num_blocks_x * (begin_y / 4) * 16; // as DXT5 (with alpha) is 16 bytes / block

			// Copy to rgba block
			uint8 rgba_block[4*4*4];

			for(unsigned int by=begin_y; by<end_y; by += 4) // by = y coordinate of block
				for(unsigned int bx=0; bx<W; bx += 4)
				{
					int z = 0;
					for(unsigned int y=by; y<by+4; ++y)
						for(unsigned int x=bx; x<bx+4; ++x) // For each pixel in block:
						{
							if(x < W && y < H)
							{
								const uint8* const pixel = imagemap->getPixel(x, y);
								rgba_block[z + 0] = pixel[0];
								rgba_block[z + 1] = pixel[1];
								rgba_block[z + 2] = pixel[2];
								rgba_block[z + 3] = pixel[3];
							}
							else
							{
								rgba_block[z + 0] = 0;
								rgba_block[z + 1] = 0;
								rgba_block[z + 2] = 0;
								rgba_block[z + 3] = 0;
							}
							z += 4;
						}

					stb_compress_dxt_block(&(compressed_data[write_i]), rgba_block, /*alpha=*/1, /*mode=*/STB_DXT_HIGHQUAL);
					write_i += 16;
				}
		}
		else if(bytes_pp == 3)
		{
			size_t write_i = num_blocks_x * (begin_y / 4) * 8; // as DXT1 (no alpha) is 8 bytes / block

			// Copy to rgba block
			uint8 rgba_block[4*4*4];

			for(unsigned int by=begin_y; by<end_y; by += 4) // by = y coordinate of block
				for(unsigned int bx=0; bx<W; bx += 4)
				{
					int z = 0;
					for(unsigned int y=by; y<by+4; ++y)
						for(unsigned int x=bx; x<bx+4; ++x) // For each pixel in block:
						{
							if(x < W && y < H)
							{
								const uint8* const pixel = imagemap->getPixel(x, y);
								rgba_block[z + 0] = pixel[0];
								rgba_block[z + 1] = pixel[1];
								rgba_block[z + 2] = pixel[2];
								rgba_block[z + 3] = 0;
							}
							else
							{
								rgba_block[z + 0] = 0;
								rgba_block[z + 1] = 0;
								rgba_block[z + 2] = 0;
								rgba_block[z + 3] = 0;
							}
							z += 4;
						}

					stb_compress_dxt_block(&(compressed_data[write_i]), rgba_block, /*alpha=*/0, /*mode=*/STB_DXT_HIGHQUAL);
					write_i += 8;
				}
		}
	}
	unsigned int begin_y, end_y;
	uint8* compressed;
	const ImageMapUInt8* imagemap;
};


Reference<OpenGLTexture> OpenGLEngine::getOrLoadOpenGLTexture(/*Indigo::TaskManager& task_manager, */const Map2D& map2d,
	OpenGLTexture::Filtering filtering, OpenGLTexture::Wrapping wrapping)
{
	if(dynamic_cast<const ImageMapUInt8*>(&map2d))
	{
		const ImageMapUInt8* imagemap = static_cast<const ImageMapUInt8*>(&map2d);

		// Compute hash of map
		const uint64 key = XXH64(imagemap->getData(), imagemap->getDataSize(), 1);

		//conPrint("key: " + toString(key));
		//conPrint("&map2d: " + toString((uint64)&map2d));
		auto res = this->opengl_textures.find(key);
		if(res == this->opengl_textures.end())
		{
			// Load texture

			// conPrint("Creating new OpenGL texture.");
			Reference<OpenGLTexture> opengl_tex = new OpenGLTexture();

			// Try and load as a DXT texture compression
			const bool compressed_sRGB_support = GL_EXT_texture_sRGB_support && GL_EXT_texture_compression_s3tc_support;
			const unsigned int W = imagemap->getWidth();
			const unsigned int H = imagemap->getHeight();
			const unsigned int bytes_pp = imagemap->getBytesPerPixel();
			if(settings.compress_textures && compressed_sRGB_support && (bytes_pp == 3 || bytes_pp == 4))
			{
				if(!task_manager)
					task_manager = new Indigo::TaskManager();

				const unsigned int num_blocks_x = Maths::roundedUpDivide(W, 4u);
				const unsigned int num_blocks_y = Maths::roundedUpDivide(H, 4u);
				const unsigned int num_blocks = num_blocks_x * num_blocks_y;

				// Timer timer;
				js::Vector<uint8, 16> compressed((bytes_pp == 3) ? (num_blocks * 8) : (num_blocks * 16)); // DXT1 is 8 bytes per 4x4 block, DXT5 (with alpha) is 16 bytes per block
				
				compress_tasks.resize(myMax<size_t>(1, task_manager->getNumThreads()));
				for(size_t z=0; z<compress_tasks.size(); ++z)
				{
					if(compress_tasks[z].isNull())
						compress_tasks[z] = new DXTCompressTask();

					const size_t y_blocks_per_task = Maths::roundedUpDivide((size_t)num_blocks_y, compress_tasks.size());
					assert(y_blocks_per_task * compress_tasks.size() >= num_blocks_y);

					// Set compress_tasks fields
					assert(dynamic_cast<DXTCompressTask*>(compress_tasks[z].ptr()));
					DXTCompressTask* task = (DXTCompressTask*)compress_tasks[z].ptr();
					task->compressed = compressed.data();
					task->imagemap = imagemap;
					task->begin_y = (unsigned int)myMin((size_t)H, (z       * y_blocks_per_task) * 4);
					task->end_y   = (unsigned int)myMin((size_t)H, ((z + 1) * y_blocks_per_task) * 4);
					assert(task->begin_y >= 0 && task->begin_y <= H && task->end_y >= 0 && task->end_y <= H);
				}
				task_manager->addTasks(compress_tasks.data(), compress_tasks.size());
				task_manager->waitForTasksToComplete();

				// conPrint("DXT compression took " + timer.elapsedString());

				opengl_tex->load(W, H, compressed, this,
					/*format=*/(bytes_pp == 3) ? OpenGLTexture::Format_Compressed_SRGB_Uint8 : OpenGLTexture::Format_Compressed_SRGBA_Uint8, 
					filtering, wrapping
				);
			}
			else // Else if not using a compressed texture format:
			{
				OpenGLTexture::Format format;
				if(imagemap->getN() == 1)
					format = OpenGLTexture::Format_Greyscale_Uint8;
				else if(imagemap->getN() == 3)
					format = OpenGLTexture::Format_SRGB_Uint8;
				else if(imagemap->getN() == 4)
					format = OpenGLTexture::Format_SRGBA_Uint8;
				else
					throw Indigo::Exception("Texture has unhandled number of components: " + toString(imagemap->getN()));

				opengl_tex->load(W, H, ArrayRef<uint8>(imagemap->getData(), imagemap->getDataSize()), this,
					format, filtering, wrapping
				);
			}

			this->opengl_textures.insert(std::make_pair(key, opengl_tex)); // Store
			return opengl_tex;
		}
		else // Else if this map has already been loaded into an OpenGL Texture:
		{
			// conPrint("texture already loaded.");
			return res->second;
		}
	}
	else if(dynamic_cast<const ImageMapFloat*>(&map2d))
	{
		const ImageMapFloat* imagemap = static_cast<const ImageMapFloat*>(&map2d);

		// Compute hash of map
		const uint64 key = XXH64(imagemap->getData(), imagemap->getDataSize() * sizeof(float), 1);

		auto res = this->opengl_textures.find(key);
		if(res == this->opengl_textures.end())
		{
			// Load texture
			unsigned int tex_xres = map2d.getMapWidth();
			unsigned int tex_yres = map2d.getMapHeight();

			if(imagemap->getN() == 3)
			{
				Reference<OpenGLTexture> opengl_tex = new OpenGLTexture();
				opengl_tex->load(tex_xres, tex_yres, ArrayRef<uint8>((uint8*)imagemap->getData(), imagemap->getDataSize()), this, OpenGLTexture::Format_RGB_Linear_Float,
					filtering, wrapping
				);

				this->opengl_textures.insert(std::make_pair(key, opengl_tex)); // Store

				return opengl_tex;
			}
			else
				throw Indigo::Exception("Texture has unhandled number of components: " + toString(imagemap->getN()));

		}
		else // Else if this map has already been loaded into an OpenGL Texture:
		{
			return res->second;
		}
	}
	else if(dynamic_cast<const ImageMap<half, HalfComponentValueTraits>*>(&map2d))
	{
		const ImageMap<half, HalfComponentValueTraits>* imagemap = static_cast<const ImageMap<half, HalfComponentValueTraits>*>(&map2d);

		// Compute hash of map
		const uint64 key = XXH64(imagemap->getData(), imagemap->getDataSize() * sizeof(half), 1);

		auto res = this->opengl_textures.find(key);
		if(res == this->opengl_textures.end())
		{
			// Load texture
			unsigned int tex_xres = map2d.getMapWidth();
			unsigned int tex_yres = map2d.getMapHeight();

			if(imagemap->getN() == 3)
			{
				Reference<OpenGLTexture> opengl_tex = new OpenGLTexture();
				opengl_tex->load(tex_xres, tex_yres, ArrayRef<uint8>((uint8*)imagemap->getData(), imagemap->getDataSize()), this, OpenGLTexture::Format_RGB_Linear_Half,
					OpenGLTexture::Filtering_Fancy
				);

				this->opengl_textures.insert(std::make_pair(key, opengl_tex)); // Store

				return opengl_tex;
			}
			else
				throw Indigo::Exception("Texture has unhandled number of components: " + toString(imagemap->getN()));

		}
		else // Else if this map has already been loaded into an OpenGL Texture:
		{
			return res->second;
		}
	}
	else
	{
		throw Indigo::Exception("Unhandled texture type.");
	}
}


float OpenGLEngine::getPixelDepth(int pixel_x, int pixel_y)
{
	float depth;
	glReadPixels(pixel_x, pixel_y, 
		1, 1,  // width, height
		GL_DEPTH_COMPONENT, GL_FLOAT, 
		&depth);

	const double z_far  = max_draw_dist;
	const double z_near = max_draw_dist * 2e-5;

	// From http://learnopengl.com/#!Advanced-OpenGL/Depth-testing
	float z = depth * 2.0 - 1.0; 
	float linear_depth = (2.0 * z_near * z_far) / (z_far + z_near - z * (z_far - z_near));	
	return linear_depth;
}
