/*=====================================================================
GLUIImage.cpp
-------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "GLUIImage.h"


#include <graphics/SRGBUtils.h>
#include "../OpenGLMeshRenderData.h"
#include "../utils/FileUtils.h"
#include "../utils/Exception.h"
#include "../utils/ConPrint.h"
#include "../utils/StringUtils.h"



GLUIImage::GLUIImage()
{
}


GLUIImage::~GLUIImage()
{
	destroy();
}


static const Colour3f default_colour(1.f);
static const Colour3f default_mouseover_colour = toLinearSRGB(Colour3f(0.9f));


void GLUIImage::create(GLUI& glui, Reference<OpenGLEngine>& opengl_engine_, const std::string& tex_path, const Vec2f& botleft, const Vec2f& dims,
	const std::string& tooltip_)
{
	opengl_engine = opengl_engine_;
	tooltip = tooltip_;

	colour = default_colour;
	mouseover_colour = default_mouseover_colour;

	overlay_ob = new OverlayObject();
	overlay_ob->mesh_data = opengl_engine->getUnitQuadMeshData();
	overlay_ob->material.albedo_linear_rgb = colour;
	if(!tex_path.empty())
	{
		TextureParams tex_params;
		tex_params.allow_compression = false;
		overlay_ob->material.albedo_texture = opengl_engine->getTexture(tex_path, tex_params);
	}
	overlay_ob->material.tex_matrix = Matrix2f(1,0,0,-1);


	rect = Rect2f(botleft, botleft + dims);


	const float y_scale = opengl_engine->getViewPortAspectRatio();

	const float z = -0.999f;
	overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(botleft.x, botleft.y * y_scale, z) * Matrix4f::scaleMatrix(dims.x, dims.y * y_scale, 1);

	opengl_engine->addOverlayObject(overlay_ob);
}


void GLUIImage::setColour(Colour3f colour_)
{
	colour = colour_;
	if(overlay_ob.nonNull())
		overlay_ob->material.albedo_linear_rgb = colour_;
}


void GLUIImage::setMouseOverColour(Colour3f colour_)
{
	mouseover_colour = colour_;
}


void GLUIImage::destroy()
{
	if(overlay_ob.nonNull())
		opengl_engine->removeOverlayObject(overlay_ob);
	overlay_ob = NULL;
	opengl_engine = NULL;
}


bool GLUIImage::doHandleMouseMoved(const Vec2f& coords)
{
	if(overlay_ob.nonNull())
	{
		if(rect.inOpenRectangle(coords)) // If mouse over widget:
		{
			overlay_ob->material.albedo_linear_rgb = mouseover_colour;
			return true;
		}
		else
		{
			overlay_ob->material.albedo_linear_rgb = colour;
		}
	}
	return false;
}


void GLUIImage::setPosAndDims(const Vec2f& botleft, const Vec2f& dims, float z)
{
	rect = Rect2f(botleft, botleft + dims);

	const float y_scale = opengl_engine->getViewPortAspectRatio();

	//const float z = -0.9f;
	overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(botleft.x, botleft.y * y_scale, z) * Matrix4f::scaleMatrix(dims.x, dims.y * y_scale, 1);
}


void GLUIImage::setTransform(const Vec2f& botleft, const Vec2f& dims, float rotation, float z)
{
	rect = Rect2f(botleft, botleft + dims); // NOTE: rectangle-based mouse-over detection will be wrong for non-zero rotation.

	const float y_scale = opengl_engine->getViewPortAspectRatio();

	//const float z = -0.9f;
	overlay_ob->ob_to_world_matrix = Matrix4f::translationMatrix(botleft.x, botleft.y * y_scale, z) * 
		Matrix4f::scaleMatrix(dims.x, dims.y * y_scale, 1) * Matrix4f::translationMatrix(0.5f, 0.5f, 0) * Matrix4f::rotationAroundZAxis(rotation) * 
		Matrix4f::translationMatrix(-0.5f, -0.5f, 0); // Transform so that rotation rotates around centre of object.
}


void GLUIImage::setVisible(bool visible)
{
	if(overlay_ob.nonNull())
		overlay_ob->draw = visible;
}
