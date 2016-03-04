/*=====================================================================
OpenGLEngine.cpp
----------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "IncludeOpenGL.h"
#include "OpenGLEngine.h"


#include "OpenGLProgram.h"
#include "OpenGLShader.h"
#include "../dll/include/IndigoMesh.h"
#include "../indigo/globals.h"
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
#include <half.h> // From OpenEXR
#include <unordered_map>
#include <algorithm>


static const bool PROFILE = false;
static const bool MEM_PROFILE = false;


OpenGLEngine::OpenGLEngine()
:	init_succeeded(false),
	anisotropic_filtering_supported(false),
	support_geom_normal_shader(false)
{
	viewport_aspect_ratio = 1;
	max_draw_dist = 1;
	render_aspect_ratio = 1;

	sun_dir = normalise(Vec4f(0.2,0.2,1,0));
	env_ob = new GLObject();
	env_ob->ob_to_world_matrix = Matrix4f::identity();
	env_ob->ob_to_world_inv_tranpose_matrix = Matrix4f::identity();
	env_ob->materials.resize(1);
}


OpenGLEngine::~OpenGLEngine()
{
}


static const Vec4f FORWARDS_OS(0.0f, 1.0f, 0.0f, 0.0f); // Forwards in local camera (object) space.
static const Vec4f UP_OS(0.0f, 0.0f, 1.0f, 0.0f);
static const Vec4f RIGHT_OS(1.0f, 0.0f, 0.0f, 0.0f);


void OpenGLEngine::setCameraTransform(const Matrix4f& world_to_camera_space_matrix_, float sensor_width_, float lens_sensor_dist_, float render_aspect_ratio_)
{
	this->sensor_width = sensor_width_;
	this->world_to_camera_space_matrix = world_to_camera_space_matrix_;
	this->lens_sensor_dist = lens_sensor_dist_;
	this->render_aspect_ratio = render_aspect_ratio_;

	float use_sensor_width;
	float use_sensor_height;
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
	const Vec4f lens_center(0,0,0,1);//TEMP
	const Vec4f sensor_center(0,-lens_sensor_dist,0,1);//TEMP

	const Vec4f sensor_bottom = (sensor_center - UP_OS    * (use_sensor_height * 0.5f));
	const Vec4f sensor_top    = (sensor_center + UP_OS    * (use_sensor_height * 0.5f));
	const Vec4f sensor_left   = (sensor_center - RIGHT_OS * (use_sensor_width  * 0.5f));
	const Vec4f sensor_right  = (sensor_center + RIGHT_OS * (use_sensor_width  * 0.5f));

	Planef planes_cs[5]; // Clipping planes in camera space

	// We won't bother with a culling plane at the near clip plane, since it near draw distance is so close to zero, so it's unlikely to cull any objects.

	planes_cs[0] = Planef(
		toVec3f(FORWARDS_OS * this->max_draw_dist), // pos.
		toVec3f(FORWARDS_OS) // normal
	); // far clip plane of frustum

	const Vec4f left_normal = normalise(crossProduct(UP_OS, lens_center - sensor_right));
	planes_cs[1] = Planef(
		toVec3f(lens_center),
		toVec3f(left_normal)
	); // left

	const Vec4f right_normal = normalise(crossProduct(lens_center - sensor_left, UP_OS));
	planes_cs[2] = Planef(
		toVec3f(lens_center),
		toVec3f(right_normal)
	); // right

	const Vec4f bottom_normal = normalise(crossProduct(lens_center - sensor_top, RIGHT_OS));
	planes_cs[3] = Planef(
		toVec3f(lens_center),
		toVec3f(bottom_normal)
	); // bottom

	const Vec4f top_normal = normalise(crossProduct(RIGHT_OS, lens_center - sensor_bottom));
	planes_cs[4] = Planef(
		toVec3f(lens_center),
		toVec3f(top_normal)
	); // top

	// Transform clipping planes to world space
	Matrix4f cam_to_world;
	world_to_camera_space_matrix.getInverseForRandTMatrix(cam_to_world);

	for(int i=0; i<5; ++i)
	{
		// Get point on plane and plane normal in Camera space.
		Vec4f plane_point_cs;
		planes_cs[i].getPointOnPlane().pointToVec4f(plane_point_cs);

		Vec4f plane_normal_cs;
		planes_cs[i].getNormal().vectorToVec4f(plane_normal_cs);

		// Transform from camera space -> world space, then world space -> object space.
		const Vec4f plane_point_ws = cam_to_world * plane_point_cs;
		const Vec4f normal_ws = normalise(cam_to_world * plane_normal_cs);

		frustum_clip_planes[i] = Plane<float>(
			toVec3f(plane_point_ws),
			toVec3f(normal_ws)
		);
	}


	// Calculate frustum verts
	Vec4f verts_cs[5];
	const float d_w = use_sensor_width  * max_draw_dist / (2 * lens_sensor_dist);
	const float d_h = use_sensor_height * max_draw_dist / (2 * lens_sensor_dist);
	verts_cs[0] = lens_center;
	// NOTE: probably wrong for shift lens
	verts_cs[1] = lens_center + FORWARDS_OS * max_draw_dist - RIGHT_OS * d_w - UP_OS * d_h; // bottom left
	verts_cs[2] = lens_center + FORWARDS_OS * max_draw_dist + RIGHT_OS * d_w - UP_OS * d_h; // bottom right
	verts_cs[3] = lens_center + FORWARDS_OS * max_draw_dist + RIGHT_OS * d_w + UP_OS * d_h; // top right
	verts_cs[4] = lens_center + FORWARDS_OS * max_draw_dist - RIGHT_OS * d_w + UP_OS * d_h; // top left

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


static void buildMeshRenderData(OpenGLMeshRenderData& meshdata, const js::Vector<Vec3f, 16>& vertices, const js::Vector<Vec3f, 16>& normals, const js::Vector<Vec2f, 16>& uvs, const js::Vector<uint32, 16>& indices)
{
	meshdata.has_uvs = !uvs.empty();
	meshdata.has_shading_normals = !normals.empty();
	meshdata.batches.resize(1);
	meshdata.batches[0].material_index = 0;
	meshdata.batches[0].num_indices = (uint32)indices.size();
	meshdata.batches[0].prim_start_offset = 0;

	js::Vector<float, 16> combined_data;
	const int NUM_COMPONENTS = 8;
	combined_data.resizeUninitialised(NUM_COMPONENTS * vertices.size());
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


void OpenGLEngine::initialise(const std::string& shader_dir_)
{
	shader_dir = shader_dir_;

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
		sphere_meshdata->aabb_os = js::AABBox(Vec4f(-1.f, -1.f, -1.f, 1.f), Vec4f(1.f, 1.f, 1.f, 1.f));
	}

	this->env_ob->mesh_data = sphere_meshdata;


	// Create VBO for unit AABB.
	/*{
		std::vector<Vec3f> corners;
		corners.push_back(Vec3f(0,0,0));
		corners.push_back(Vec3f(1,0,0));
		corners.push_back(Vec3f(0,1,0));
		corners.push_back(Vec3f(1,1,0));
		corners.push_back(Vec3f(0,0,1));
		corners.push_back(Vec3f(1,0,1));
		corners.push_back(Vec3f(0,1,1));
		corners.push_back(Vec3f(1,1,1));

		std::vector<Vec3f> verts;
		verts.push_back(corners[0]); verts.push_back(corners[1]); verts.push_back(corners[5]); verts.push_back(corners[4]); // front face
		verts.push_back(corners[4]); verts.push_back(corners[5]); verts.push_back(corners[7]); verts.push_back(corners[6]); // top face
		verts.push_back(corners[7]); verts.push_back(corners[5]); verts.push_back(corners[1]); verts.push_back(corners[3]); // right face
		verts.push_back(corners[6]); verts.push_back(corners[7]); verts.push_back(corners[3]); verts.push_back(corners[2]); // back face
		verts.push_back(corners[4]); verts.push_back(corners[6]); verts.push_back(corners[2]); verts.push_back(corners[0]); // left face
		verts.push_back(corners[0]); verts.push_back(corners[2]); verts.push_back(corners[3]); verts.push_back(corners[1]); // bottom face

		std::vector<Vec3f> normals;
		std::vector<Vec2f> uvs;

		aabb_passdata.num_prims = 6;
		aabb_passdata.material_index = 0;
		buildPassData(aabb_passdata, verts, normals, uvs);
	}*/

	support_geom_normal_shader = true;


	try
	{
		//const std::string use_shader_dir = TestUtils::getIndigoTestReposDir() + "/opengl/shaders"; // For local dev
		const std::string use_shader_dir = shader_dir;

		phong_untextured_prog = new OpenGLProgram(
			new OpenGLShader(use_shader_dir + "/phong_vert_shader.glsl", GL_VERTEX_SHADER),
			new OpenGLShader(use_shader_dir + "/phong_frag_shader.glsl", GL_FRAGMENT_SHADER)
		);
		diffuse_colour_location			= phong_untextured_prog->getUniformLocation("diffuse_colour");
		have_shading_normals_location	= phong_untextured_prog->getUniformLocation("have_shading_normals");
		have_texture_location			= phong_untextured_prog->getUniformLocation("have_texture");
		diffuse_tex_location			= phong_untextured_prog->getUniformLocation("diffuse_tex");
		texture_matrix_location			= phong_untextured_prog->getUniformLocation("texture_matrix");
		sundir_location					= phong_untextured_prog->getUniformLocation("sundir");
		exponent_location				= phong_untextured_prog->getUniformLocation("exponent");
		fresnel_scale_location			= phong_untextured_prog->getUniformLocation("fresnel_scale");

		transparent_prog = new OpenGLProgram(
			new OpenGLShader(use_shader_dir + "/transparent_vert_shader.glsl", GL_VERTEX_SHADER),
			new OpenGLShader(use_shader_dir + "/transparent_frag_shader.glsl", GL_FRAGMENT_SHADER)
		);
		transparent_colour_location		= transparent_prog->getUniformLocation("colour");
		transparent_have_shading_normals_location = transparent_prog->getUniformLocation("have_shading_normals");

		env_prog = new OpenGLProgram(
			new OpenGLShader(use_shader_dir + "/env_vert_shader.glsl", GL_VERTEX_SHADER),
			new OpenGLShader(use_shader_dir + "/env_frag_shader.glsl", GL_FRAGMENT_SHADER)
		);
		env_diffuse_colour_location		= env_prog->getUniformLocation("diffuse_colour");
		env_have_texture_location		= env_prog->getUniformLocation("have_texture");
		env_diffuse_tex_location		= env_prog->getUniformLocation("diffuse_tex");
		env_texture_matrix_location		= env_prog->getUniformLocation("texture_matrix");

		init_succeeded = true;
	}
	catch(Indigo::Exception& e)
	{
		conPrint(e.what());
		this->initialisation_error_msg = e.what();
		init_succeeded = false;
	}
}


void OpenGLEngine::unloadAllData()
{
	this->objects.resize(0);
	this->transparent_objects.resize(0);

	this->env_ob->materials[0] = OpenGLMaterial();
}


void OpenGLEngine::updateObjectTransformData(GLObject& object)
{
	const Vec4f min_os = object.mesh_data->aabb_os.min_;
	const Vec4f max_os = object.mesh_data->aabb_os.max_;
	const Matrix4f& to_world = object.ob_to_world_matrix;

	to_world.getUpperLeftInverseTranspose(object.ob_to_world_inv_tranpose_matrix);

	js::AABBox bbox_ws = js::AABBox::emptyAABBox();
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(min_os.x[0], min_os.x[1], min_os.x[2], 1.0f));
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(min_os.x[0], min_os.x[1], max_os.x[2], 1.0f));
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(min_os.x[0], max_os.x[1], min_os.x[2], 1.0f));
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(min_os.x[0], max_os.x[1], max_os.x[2], 1.0f));
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(max_os.x[0], min_os.x[1], min_os.x[2], 1.0f));
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(max_os.x[0], min_os.x[1], max_os.x[2], 1.0f));
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(max_os.x[0], max_os.x[1], min_os.x[2], 1.0f));
	bbox_ws.enlargeToHoldPoint(to_world * Vec4f(max_os.x[0], max_os.x[1], max_os.x[2], 1.0f));
	object.aabb_ws = bbox_ws;
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
		if(object->materials[i].transparent)
		{
			object->materials[i].shader_prog = transparent_prog;
			have_transparent_mat = true;
		}
		else
			object->materials[i].shader_prog = phong_untextured_prog;//TEMP
	//	buildMaterial(object->materials[i]);
	}

	if(have_transparent_mat)
		transparent_objects.push_back(object);
}


void OpenGLEngine::newMaterialUsed(OpenGLMaterial& mat)
{
	if(mat.transparent)
		mat.shader_prog = this->transparent_prog;
	else
		mat.shader_prog = this->phong_untextured_prog;
}


void OpenGLEngine::objectMaterialsUpdated(const Reference<GLObject>& object)
{
	// Add this object to transparent_objects list if it has a transparent material and is not already in the list.

	bool have_transparent_mat = false;
	for(size_t i=0; i<object->materials.size(); ++i)
		if(object->materials[i].transparent)
			have_transparent_mat = true;

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
}


void OpenGLEngine::buildMaterial(OpenGLMaterial& opengl_mat)
{
}


// http://iquilezles.org/www/articles/frustumcorrect/frustumcorrect.htm
static bool AABBIntersectsFrustum(const Plane<float>* frustum_clip_planes, const js::AABBox& frustum_aabb, const js::AABBox& aabb_ws)
{
	const Vec4f min_ws = aabb_ws.min_;
	const Vec4f max_ws = aabb_ws.max_;

	for(int z=0; z<5; ++z) // For each frustum plane:
	{
		const Vec3f normal = frustum_clip_planes[z].getNormal();
		float dist = std::numeric_limits<float>::infinity();
		dist = myMin(dist, dot(normal, Vec3f(min_ws[0], min_ws[1], min_ws[2])));
		dist = myMin(dist, dot(normal, Vec3f(min_ws[0], min_ws[1], max_ws[2])));
		dist = myMin(dist, dot(normal, Vec3f(min_ws[0], max_ws[1], min_ws[2])));
		dist = myMin(dist, dot(normal, Vec3f(min_ws[0], max_ws[1], max_ws[2])));
		dist = myMin(dist, dot(normal, Vec3f(max_ws[0], min_ws[1], min_ws[2])));
		dist = myMin(dist, dot(normal, Vec3f(max_ws[0], min_ws[1], max_ws[2])));
		dist = myMin(dist, dot(normal, Vec3f(max_ws[0], max_ws[1], min_ws[2])));
		dist = myMin(dist, dot(normal, Vec3f(max_ws[0], max_ws[1], max_ws[2])));
		if(dist >= frustum_clip_planes[z].getD())
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


void OpenGLEngine::draw()
{
	if(!init_succeeded)
		return;
#if !defined(OSX)
	if(PROFILE) glBeginQuery(GL_TIME_ELAPSED, timer_query_id);
#endif
	this->num_indices_submitted = 0;
	this->num_face_groups_submitted = 0;
	this->num_aabbs_submitted = 0;
	Timer profile_timer;
	
	// NOTE: We want to clear here first, even if the scene node is null.
	// Clearing here fixes the bug with the OpenGL widget buffer not being initialised properly and displaying garbled mem on OS X.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	glLineWidth(1);

	Matrix4f proj_matrix;
	
	// Initialise projection matrix from Indigo camera settings
	{
		const double z_far  = max_draw_dist;
		const double z_near = max_draw_dist * 2e-5;
		
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

		const double x_min = z_near * (-w);
		const double x_max = z_near * ( w);
		const double y_min = z_near * (-h);
		const double y_max = z_near * ( h);
		proj_matrix = frustumMatrix(x_min, x_max, y_min, y_max, z_near, z_far);
	}

	const float e[16] = { 1, 0, 0, 0,	0, 0, -1, 0,	0, 1, 0, 0,		0, 0, 0, 1 };
	const Matrix4f indigo_to_opengl_cam_matrix(e);

	Matrix4f view_matrix;
	mul(indigo_to_opengl_cam_matrix, world_to_camera_space_matrix, view_matrix);

	this->sun_dir_cam_space = indigo_to_opengl_cam_matrix * (world_to_camera_space_matrix * sun_dir);

	// Draw solid polygons
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	// Draw background env map if there is one.
	if(this->env_ob->materials[0].albedo_texture.nonNull())
	{
		Matrix4f world_to_camera_space_no_translation = view_matrix;
		world_to_camera_space_no_translation.e[12] = 0;
		world_to_camera_space_no_translation.e[13] = 0;
		world_to_camera_space_no_translation.e[14] = 0;

		glDepthMask(GL_FALSE); // Disable writing to depth buffer.

		bindMeshData(*env_ob->mesh_data);
		drawBatch(*env_ob, world_to_camera_space_no_translation, proj_matrix, env_ob->materials[0], *env_ob->mesh_data, env_ob->mesh_data->batches[0]);
		unbindMeshData(*env_ob->mesh_data);
			
		glDepthMask(GL_TRUE); // Re-enable writing to depth buffer.
	}
	

	// Draw non-transparent batches from objects.
	uint64 num_frustum_culled = 0;
	for(size_t i=0; i<objects.size(); ++i)
	{
		const GLObject* const ob = objects[i].getPointer();
		if(AABBIntersectsFrustum(frustum_clip_planes, frustum_aabb, ob->aabb_ws))
		{
			const OpenGLMeshRenderData& mesh_data = *ob->mesh_data;
			bindMeshData(mesh_data); // Bind the mesh data, which is the same for all batches.
			for(uint32 z = 0; z < mesh_data.batches.size(); ++z)
			{
				const uint32 mat_index = mesh_data.batches[z].material_index;
				// Draw primitives for the given material
				if(!ob->materials[mat_index].transparent)
					drawBatch(*ob, view_matrix, proj_matrix, ob->materials[mat_index], mesh_data, mesh_data.batches[z]);
			}
			unbindMeshData(mesh_data);
		}
		else
			num_frustum_culled++;
	}


	// Draw transparent batches
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE); // Disable writing to depth buffer.

	for(size_t i=0; i<transparent_objects.size(); ++i)
	{
		const GLObject* const ob = transparent_objects[i].getPointer();
		if(AABBIntersectsFrustum(frustum_clip_planes, frustum_aabb, ob->aabb_ws)) 
		{
			const OpenGLMeshRenderData& mesh_data = *ob->mesh_data;
			bindMeshData(mesh_data); // Bind the mesh data, which is the same for all batches.
			for(uint32 z = 0; z < mesh_data.batches.size(); ++z)
			{
				const uint32 mat_index = mesh_data.batches[z].material_index;
				// Draw primitives for the given material
				if(ob->materials[mat_index].transparent)
					drawBatch(*ob, view_matrix, proj_matrix, ob->materials[mat_index], mesh_data, mesh_data.batches[z]);
			}
			unbindMeshData(mesh_data);
		}
	}
	glDepthMask(GL_TRUE); // Re-enable writing to depth buffer.
	glDisable(GL_BLEND);


	// Draw camera frustum for debugging purposes.
	if(false)
	{
#if 0
		glDisable(GL_LIGHTING);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glColor3f(1.f, 0.f, 0.f);
		glLineWidth(2.0f);
		glBegin(GL_LINES);
		
		glVertex3fv(frustum_verts[0].x);
		glVertex3fv(frustum_verts[1].x);

		glVertex3fv(frustum_verts[0].x);
		glVertex3fv(frustum_verts[2].x);

		glVertex3fv(frustum_verts[0].x);
		glVertex3fv(frustum_verts[3].x);

		glVertex3fv(frustum_verts[0].x);
		glVertex3fv(frustum_verts[4].x);


		glVertex3fv(frustum_verts[1].x);
		glVertex3fv(frustum_verts[2].x);

		glVertex3fv(frustum_verts[2].x);
		glVertex3fv(frustum_verts[3].x);

		glVertex3fv(frustum_verts[3].x);
		glVertex3fv(frustum_verts[4].x);

		glVertex3fv(frustum_verts[4].x);
		glVertex3fv(frustum_verts[1].x);
		glEnd();
#endif
	}


	// Draw alpha-blended grey quads on the outside of 'render viewport'.
	// Also draw some blue lines on the edge of the quads.
	if(false) // Disable for now until we get rid of the fixed-function pipeline usage
	{
#if 0
		// Reset transformations
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		glDisable(GL_DEPTH_TEST); // Disable depth test, so the viewport lines are always drawn over existing fragments.
		glDisable(GL_LIGHTING);

		// Set quad colour
		const float grey = 0.5f;
		glColor4d(grey, grey, grey, 0.5f);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		const Vec3d line_colour(0.094, 0.41960784313725490196078431372549, 0.54901960784313725490196078431373);

		const float z = 0.02f;

		if(viewport_aspect_ratio > render_aspect_ratio)
		{
			const float x = render_aspect_ratio / viewport_aspect_ratio;

			// Draw quads
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glBegin(GL_QUADS);

			// Left quad
			glVertex3f(-1, -1, z);
			glVertex3f(-x, -1, z);
			glVertex3f(-x, 1, z);
			glVertex3f(-1, 1, z);

			// Right quad
			glVertex3f(x, -1, z);
			glVertex3f(1, -1, z);
			glVertex3f(1, 1, z);
			glVertex3f(x, 1, z);

			glEnd(); // GL_QUADS
			glDisable(GL_BLEND);

			// Draw vertical lines
			glColor3dv(&line_colour.x);
			glBegin(GL_LINES);

			glVertex3f(x, -1, z); // origin of the line
			glVertex3f(x, 1, z); // ending point of the line

			glVertex3f(-x, -1, z); // origin of the line
			glVertex3f(-x, 1, z); // ending point of the line

			glEnd(); // GL_LINES
		}
		else
		{
			const float y = viewport_aspect_ratio / render_aspect_ratio;

			// Draw quads
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glBegin(GL_QUADS);

			// Bottom quad
			glVertex3f(-1, -1, z);
			glVertex3f(1, -1, z);
			glVertex3f(1, -y, z);
			glVertex3f(-1,-y, z);

			// Top quad
			glVertex3f(-1, y, z);
			glVertex3f(1, y, z);
			glVertex3f(1, 1, z);
			glVertex3f(-1,1, z);

			glEnd(); // GL_QUADS
			glDisable(GL_BLEND);

			// Draw vertical lines
			glColor3dv(&line_colour.x);
			glBegin(GL_LINES);

			glVertex3f(-1, y, z); // origin of the line
			glVertex3f( 1, y, z); // ending point of the line

			glVertex3f(-1, -y, z); // origin of the line
			glVertex3f( 1, -y, z); // ending point of the line

			glEnd(); // GL_LINES
		}
	
		glEnable(GL_DEPTH_TEST);
#endif
	}

	if(PROFILE)
	{
		const double cpu_time = profile_timer.elapsed();
		uint64 elapsed_ns = 0;
#if !defined(OSX)
		glEndQuery(GL_TIME_ELAPSED);
		glGetQueryObjectui64v(timer_query_id, GL_QUERY_RESULT, &elapsed_ns); // Blocks
#endif

		conPrint("Frame times: CPU: " + doubleToStringNDecimalPlaces(cpu_time * 1.0e3, 4) + " ms, GPU: " + doubleToStringNDecimalPlaces(elapsed_ns * 1.0e-6, 4) + " ms.");
		conPrint("Submitted: face groups: " + toString(num_face_groups_submitted) + ", faces: " + toString(num_indices_submitted / 3) + ", aabbs: " + toString(num_aabbs_submitted) + ", " + 
			toString(objects.size() - num_frustum_culled) + "/" + toString(objects.size()) + " obs");
	}
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
};


// Hash function for UniqueVert
struct UniqueVertKeyHash
{
	inline size_t operator()(const UniqueVertKey& v) const { return (size_t)v.pos_i; }
};


//struct UniqueVert
//{
//	Vec3f p;
//	Vec3f n;
//	Vec2f uv;
//	uint32 mat_index;
//};


Reference<OpenGLMeshRenderData> OpenGLEngine::buildIndigoMesh(const Reference<Indigo::Mesh>& mesh_)
{
	Timer timer;

	const Indigo::Mesh* const mesh = mesh_.getPointer();
	const bool mesh_has_shading_normals = !mesh->vert_normals.empty();
	const bool mesh_has_uvs = mesh->num_uv_mappings > 0;

	Reference<OpenGLMeshRenderData> opengl_render_data = new OpenGLMeshRenderData();

	js::AABBox aabb_os = js::AABBox::emptyAABBox();

	std::unordered_map<UniqueVertKey, uint32, UniqueVertKeyHash> index_for_vert;

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
	vert_index_buffer.resizeUninitialised(mesh->triangles.size()*3 + mesh->quads.size()*6); // Quads are rendered as two tris.
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
				const uint32 pos_i  = tri.vertex_indices[i];
				const uint32 uv_i   = tri.uv_indices[i];
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

				std::unordered_map<UniqueVertKey, uint32, UniqueVertKeyHash>::iterator res = index_for_vert.find(key); // Have we created this unique vert yet?
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

					aabb_os.enlargeToHoldPoint(Vec4f(mesh->vert_positions[pos_i].x, mesh->vert_positions[pos_i].y, mesh->vert_positions[pos_i].z, 1.f));
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

				std::unordered_map<UniqueVertKey, uint32, UniqueVertKeyHash>::iterator res = index_for_vert.find(key); // Have we created this unique vert yet?
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

					aabb_os.enlargeToHoldPoint(Vec4f(mesh->vert_positions[pos_i].x, mesh->vert_positions[pos_i].y, mesh->vert_positions[pos_i].z, 1.f));
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

	opengl_render_data->aabb_os = aabb_os;
	return opengl_render_data;
}


void OpenGLEngine::drawBatch(const GLObject& ob, const Matrix4f& view_mat, const Matrix4f& proj_mat, const OpenGLMaterial& opengl_mat, const OpenGLMeshRenderData& mesh_data, const OpenGLBatch& batch)
{
	if(opengl_mat.shader_prog.nonNull())
	{
		opengl_mat.shader_prog->useProgram();

		glUniformMatrix4fv(opengl_mat.shader_prog->model_matrix_loc, 1, false, ob.ob_to_world_matrix.e);
		glUniformMatrix4fv(opengl_mat.shader_prog->view_matrix_loc, 1, false, view_mat.e);
		glUniformMatrix4fv(opengl_mat.shader_prog->proj_matrix_loc, 1, false, proj_mat.e);
		glUniformMatrix4fv(opengl_mat.shader_prog->normal_matrix_loc, 1, false, ob.ob_to_world_inv_tranpose_matrix.e); // inverse transpose model matrix

		// Set uniforms.  NOTE: Setting the uniforms manually in this way is obviously quite hacky.  Improve.
		if(opengl_mat.shader_prog.getPointer() == this->phong_untextured_prog.getPointer())
		{
			glUniform4fv(this->sundir_location, /*count=*/1, this->sun_dir_cam_space.x);
			glUniform4f(this->diffuse_colour_location, opengl_mat.albedo_rgb.r, opengl_mat.albedo_rgb.g, opengl_mat.albedo_rgb.b, 1.f);
			glUniform1i(this->have_shading_normals_location, mesh_data.has_shading_normals ? 1 : 0);
			glUniform1i(this->have_texture_location, opengl_mat.albedo_texture.nonNull() ? 1 : 0);
			glUniform1f(this->exponent_location, opengl_mat.phong_exponent);
			glUniform1f(this->fresnel_scale_location, opengl_mat.fresnel_scale);

			if(opengl_mat.albedo_texture.nonNull())
			{
				glBindTexture(GL_TEXTURE_2D, opengl_mat.albedo_texture->texture_handle);

				const GLfloat tex_elems[16] = {
					opengl_mat.tex_matrix.e[0], opengl_mat.tex_matrix.e[2], 0, 0,
					opengl_mat.tex_matrix.e[1], opengl_mat.tex_matrix.e[3], 0, 0,
					0, 0, 1, 0,
					opengl_mat.tex_translation.x, opengl_mat.tex_translation.y, 0, 1
				};
				glUniformMatrix4fv(this->texture_matrix_location, /*count=*/1, /*transpose=*/false, tex_elems);
				glUniform1i(this->diffuse_tex_location, 0);
			}
		}
		else if(opengl_mat.shader_prog.getPointer() == this->transparent_prog.getPointer())
		{
			glUniform4f(this->transparent_colour_location, opengl_mat.albedo_rgb.r, opengl_mat.albedo_rgb.g, opengl_mat.albedo_rgb.b, opengl_mat.alpha);
			glUniform1i(this->transparent_have_shading_normals_location, mesh_data.has_shading_normals ? 1 : 0);
		}
		else
		{
			glUniform4f(this->env_diffuse_colour_location, opengl_mat.albedo_rgb.r, opengl_mat.albedo_rgb.g, opengl_mat.albedo_rgb.b, 1.f);
			glUniform1i(this->env_have_texture_location, opengl_mat.albedo_texture.nonNull() ? 1 : 0);
			
			if(opengl_mat.albedo_texture.nonNull())
			{
				glBindTexture(GL_TEXTURE_2D, opengl_mat.albedo_texture->texture_handle);

				const GLfloat tex_elems[16] = {
					opengl_mat.tex_matrix.e[0], opengl_mat.tex_matrix.e[2], 0, 0,
					opengl_mat.tex_matrix.e[1], opengl_mat.tex_matrix.e[3], 0, 0,
					0, 0, 1, 0,
					opengl_mat.tex_translation.x, opengl_mat.tex_translation.y, 0, 1
				};
				glUniformMatrix4fv(this->env_texture_matrix_location, /*count=*/1, /*transpose=*/false, tex_elems);
				glUniform1i(this->env_diffuse_tex_location, 0);
			}
		}
		

		glDrawElements(GL_TRIANGLES, (GLsizei)batch.num_indices, mesh_data.index_type, (void*)batch.prim_start_offset);

		opengl_mat.shader_prog->useNoPrograms();
	}
	else
	{
#if 0
		if(opengl_mat.transparent)
			return; // Don't render transparent materials in the solid pass

		// Set OpenGL material state
		if(opengl_mat.albedo_texture.nonNull())// && mesh_data.uvs_vbo.nonNull())
		{
			GLfloat mat_amb_diff[] = { 1.0f, 1.0f, 1.0f, 1.0f };
			glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat_amb_diff);

			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, opengl_mat.albedo_texture->texture_handle);

			// Set texture matrix
			glMatrixMode(GL_TEXTURE); // Enter texture matrix mode
			const GLfloat tex_elems[16] = {
				opengl_mat.tex_matrix.e[0], opengl_mat.tex_matrix.e[2], 0, 0,
				opengl_mat.tex_matrix.e[1], opengl_mat.tex_matrix.e[3], 0, 0,
				0, 0, 1, 0,
				opengl_mat.tex_translation.x, opengl_mat.tex_translation.y, 0, 1
			};
			glLoadMatrixf(tex_elems);
			glMatrixMode(GL_MODELVIEW); // Back to modelview mode
		}
		else
		{
			GLfloat mat_amb_diff[] = { opengl_mat.albedo_rgb[0], opengl_mat.albedo_rgb[1], opengl_mat.albedo_rgb[2], 1.0f };
			glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat_amb_diff);
		}

		GLfloat mat_spec[] = { opengl_mat.specular_rgb[0], opengl_mat.specular_rgb[1], opengl_mat.specular_rgb[2], 1.0f };
		glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_spec);
		GLfloat shininess[] = { opengl_mat.shininess };
		glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, shininess);


		if(batch.num_verts_per_prim == 3)
			glDrawElements(GL_TRIANGLES, (GLsizei)batch.num_prims*3, GL_UNSIGNED_INT, (void*)batch.prim_start_offset);
		else
			glDrawElements(GL_QUADS,     (GLsizei)batch.num_prims*4, GL_UNSIGNED_INT, (void*)batch.prim_start_offset);


		if(opengl_mat.albedo_texture.nonNull())
			glDisable(GL_TEXTURE_2D);
#endif
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
