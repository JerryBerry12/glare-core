/*=====================================================================
CameraController.cpp
--------------------
Copyright Glare Technologies Limited 2010 -
Generated at Thu Dec 09 17:24:15 +1300 2010
=====================================================================*/
#include "CameraController.h"

#include <math.h>


CameraController::CameraController()
{
	position = Vec3d(0.0);
	rotation = Vec2d(0.0);

	move_speed   =  0.035;
	rotate_speed = -0.005;

	invert_mouse = false;
}


CameraController::~CameraController()
{

}


void CameraController::update(const Vec3d& pos_delta, const Vec2d& rot_delta)
{
	// Accumulate rotation angles, taking into account mouse speed and invertedness.
	rotation.x += rot_delta.x * rotate_speed;
	rotation.y += rot_delta.y * rotate_speed * (invert_mouse ? -1 : 1);

	// Construct camera basis.
	Vec3d forwards(	sin(rotation.x) * cos(rotation.y),
					sin(rotation.x) * sin(rotation.y),
					cos(rotation.x));

	Vec3d up(0, 0, 1); up.removeComponentInDir(forwards); up.normalise();
	Vec3d right = ::crossProduct(forwards, up);

	position += right		* pos_delta.x * move_speed +
				forwards	* pos_delta.y * move_speed +
				up			* pos_delta.z * move_speed;
}


Vec3d CameraController::getPosition() const
{
	return position;
}


void CameraController::setPosition(const Vec3d& pos)
{
	position = pos;
}


void CameraController::getBasis(Vec3d& right_out, Vec3d& up_out, Vec3d& forward_out) const
{
	forward_out = Vec3d(sin(rotation.x) * cos(rotation.y),
						sin(rotation.x) * sin(rotation.y),
						cos(rotation.x));

	up_out = Vec3d(0, 0, 1);
	up_out.removeComponentInDir(forward_out);
	up_out.normalise();

	right_out = ::crossProduct(forward_out, up_out);
}


void CameraController::setBasis(const Vec3d &up, const Vec3d &forward)
{
	rotation.x = acos(forward.z / forward.length());
	rotation.y = atan2(forward.y, forward.x);
}
