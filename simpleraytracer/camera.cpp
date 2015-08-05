/*=====================================================================
camera.cpp
----------
File created by ClassTemplate on Sun Nov 14 04:06:01 2004
Code By Nicholas Chapman.
=====================================================================*/
#include "camera.h"


Camera::Camera(CameraType camera_type_)
:	Geometry(
		false // sub-elements curved
	),
	camera_type(camera_type_)
{}


Camera::~Camera()
{}


void Camera::setContainingMedia(const std::vector<const Medium*>& media_)
{
	containing_media = media_;
}


void Camera::getSubElementSurfaceAreas(const Matrix4f& to_parent, std::vector<float>& surface_areas_out) const
{
	assert(0);
}


void Camera::sampleSubElement(unsigned int sub_elem_index, const SamplePair& samples, Pos3Type& pos_out, Vec3Type& normal_out, HitInfo& hitinfo_out, 
										  /*float recip_sub_elem_area_ws, Real& p_out, */unsigned int& mat_index_out, Vec2f& uv0_out) const
{
	assert(0);
}


void Camera::getPartialDerivs(const HitInfo& hitinfo, Vec3Type& dp_du_out, Vec3Type& dp_dv_out) const
{
	assert(0);
}
