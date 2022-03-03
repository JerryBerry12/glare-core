/*=====================================================================
OpenGLProgram.cpp
----------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#include "OpenGLProgram.h"


#include "OpenGLShader.h"
#include "../utils/FileUtils.h"
#include "../utils/Exception.h"
#include "../utils/ConPrint.h"
#include "../utils/StringUtils.h"


static const std::string getLog(GLuint program)
{
	// Get log length including null terminator
	GLint log_length = 0;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
		
	std::string log;
	if(log_length > 0)
	{
		log.resize(log_length - 1);
		glGetProgramInfoLog(program, log_length, NULL, &log[0]);
	}
	return log;
}


OpenGLProgram::OpenGLProgram(const std::string& prog_name_, const Reference<OpenGLShader>& vert_shader_, const Reference<OpenGLShader>& frag_shader_)
:	program(0),
	prog_name(prog_name_),
	campos_ws_loc(-1),
	time_loc(-1),
	colour_loc(-1),
	albedo_texture_loc(-1),
	lightmap_tex_loc(-1),
	uses_phong_uniforms(false),
	is_transparent(false),
	is_depth_draw(false),
	is_depth_draw_with_alpha_test(false),
	uses_vert_uniform_buf_obs(false)
{
	vert_shader = vert_shader_;
	frag_shader = frag_shader_;

	program = glCreateProgram();
	if(program == 0)
		throw glare::Exception("Failed to create OpenGL program '" + prog_name + "'.");

	glAttachShader(program, vert_shader->shader);
	if(frag_shader.nonNull()) glAttachShader(program, frag_shader->shader);

	// Bind shader input variables.
	// This corresponds to the order we supply vertex attributes in our mesh VAOs.
	glBindAttribLocation(program, 0, "position_in");
	glBindAttribLocation(program, 1, "normal_in");
	glBindAttribLocation(program, 2, "texture_coords_0_in");
	glBindAttribLocation(program, 3, "vert_colours_in");
	glBindAttribLocation(program, 4, "lightmap_coords_in");
	glBindAttribLocation(program, 5, "instance_matrix_in"); // uses attribute indices 5, 6, 7, 8
	//glBindAttribLocation(program, 9, "instance_colour_in");
	glBindAttribLocation(program, 9, "joint");
	glBindAttribLocation(program, 10, "weight");

	glLinkProgram(program);

	const std::string log = getLog(program);

	if(!isAllWhitespace(log))
		conPrint("shader program '" + prog_name + "' log:\n" + log);

	GLint program_ok;
	glGetProgramiv(program, GL_LINK_STATUS, &program_ok);
	if(!program_ok)
		throw glare::Exception("Failed to link shader program '" + prog_name + "': " + log);

	model_matrix_loc   = glGetUniformLocation(program, "model_matrix");
	view_matrix_loc    = glGetUniformLocation(program, "view_matrix");
	proj_matrix_loc    = glGetUniformLocation(program, "proj_matrix");
	normal_matrix_loc  = glGetUniformLocation(program, "normal_matrix");
	joint_matrix_loc   = glGetUniformLocation(program, "joint_matrix");
					   
	campos_ws_loc      = glGetUniformLocation(program, "campos_ws");
	time_loc           = glGetUniformLocation(program, "time");
	colour_loc         = glGetUniformLocation(program, "colour");
	albedo_texture_loc = glGetUniformLocation(program, "albedo_texture");
	lightmap_tex_loc   = glGetUniformLocation(program, "lightmap_tex");
	texture_2_loc      = glGetUniformLocation(program, "texture_2");
}


OpenGLProgram::~OpenGLProgram()
{
	glDeleteProgram(program);
}


void OpenGLProgram::useProgram() const
{
	glUseProgram(program);
}


void OpenGLProgram::useNoPrograms()
{
	glUseProgram(0);
}


void OpenGLProgram::bindAttributeLocation(int index, const std::string& name)
{
	glBindAttribLocation(program, index, name.c_str());
}


int OpenGLProgram::getUniformLocation(const std::string& name)
{
	const int res = glGetUniformLocation(program, name.c_str());
#ifndef NDEBUG
	if(res < 0)
		conPrint("Warning: for program '" + prog_name + "': failed to get uniform '" + name + "'.");
#endif
	return res;
}


int OpenGLProgram::getAttributeLocation(const std::string& name)
{
	const GLint res = glGetAttribLocation(program, name.c_str());
	return res;
}


void OpenGLProgram::appendUserUniformInfo(UserUniformInfo::UniformType uniform_type, const std::string& name)
{
	const int index = (int)user_uniform_info.size();
	
	user_uniform_info.push_back(UserUniformInfo(
		getUniformLocation(name), // location
		index, // index in user_uniform_info
		uniform_type
	));
}
