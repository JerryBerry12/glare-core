/*=====================================================================
camera.h
--------
File created by ClassTemplate on Sun Nov 14 04:06:01 2004
Code By Nicholas Chapman.
=====================================================================*/
#pragma once


#include "ray.h"
#include "geometry.h"
#include "../indigo/SampleTypes.h"
#include "../indigo/TransformPath.h"
#include "../maths/vec2.h"
#include "../maths/vec3.h"
#include "../physics/jscol_aabbox.h"
#include "../utils/MemAlloc.h"
#include "../utils/Reference.h"
#include <vector>
#include <string>
class ColourSpaceConverter;
class HitInfo;
class FullHitInfo;
class Image;
class Medium;
class DiffractionFilter;
class Distribution2;
class Aperture;
class FFTPlan;
class WavelengthSamples;
struct EmitWinterCodeArgs;
class SpectralVector;


class CameraExcep
{
public:
	CameraExcep(const std::string& text_) : text(text_) {}
	~CameraExcep(){}

	const std::string& what() const { return text; }
private:
	std::string text;
};


/*=====================================================================
Camera
------

=====================================================================*/
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable:4324) // Disable 'structure was padded due to __declspec(align())' warning.
#endif
class Camera : public Geometry
{
public:
	GLARE_ALIGNED_16_NEW_DELETE

	enum CameraType
	{
		CameraType_ThinLens,
		CameraType_Orthographic,
		CameraType_Spherical
	};


	Camera(CameraType camera_type);
	virtual ~Camera();

	typedef float PDType;

	virtual void init(
		const ArrayRef<TransformKeyFrame>& frames,
		const Vec3d& ws_updir, const Vec3d& forwards, 
		double lens_radius, double focus_distance, 
		double original_sensor_width, double original_sensor_height, 
		double sensor_width, double sensor_height, 
		double lens_sensor_dist,
		double exposure_duration,
		Reference<Aperture>& aperture,
		double lens_shift_up_distance,
		double lens_shift_right_distance,
		bool omnidirectional_stereo,
		Real IPD,
		bool left_eye,
		bool top_merge,
		bool bottom_merge
	) = 0;

	// See PathTracingUtils::computeUseSensorWidthAndHeightFactor()
	// This method will change the camera sensor size.
	virtual void setSensorDimFactor(const Vec2f& sensor_dim_factor) = 0;

	struct SampleLensResults
	{
		Vec3Type forwards_dir_ws;
		Vec3Type sensorpos_ws;
		Vec3Type lenspos_ws;
		Vec3Type lens_exit_dir_ws;
		const std::vector<const Medium*>* containing_media;
		Real sensorpos_p;
		Real recip_lenspos_p;
		Real aperture_vis;
		Real F; // camera contribution
		Real exposure_duration;
		Real sensitivity_scale;
		//Real sensorpos_to_lenspos_dist;
		Real alpha_splat_weight;
		Real non_vignetting_splat_weight;
	};


	virtual SampleLensResults sampleLens(
		const Vec2f& imagepos,
		const SamplePair& samples,
		float time,
		bool vignetting,
		Real recip_normed_image_rect_area,
		bool compute_alpha_weight
	) const = 0;


	virtual PDType sensitivityScale() const = 0;

	virtual PDType sensorPDF() const = 0; // used

	virtual void sampleLensPos(const SamplePair& samples, float time, Vec3Type& pos_os_out, Vec3Type& pos_ws_out) const = 0; // used

	virtual const Vec2f imCoordsForSensorPos(const Vec3Type& sensorpos_os, float time) const = 0;

	virtual const Vec4f getForwardsDirF(float time) const = 0;

	virtual const Vec4f getPosWS(float time) const = 0;
	
	virtual Real getExposureDuration() const = 0;

	virtual const Vec3Type diffractRay(const SamplePair& samples, const Vec3Type& dir, const WavelengthSamples& wavelengths, float direction_sign, float time, SpectralVector& weights_out) const = 0;

	virtual void prepareForDiffractionFilter(int main_buffer_width, int main_buffer_height, int ssf_) = 0;

	virtual void buildDiffractionFilter() = 0;

	virtual const std::vector<Planef>& getViewVolumeClippingPlanesCameraSpace() const = 0;

	virtual bool isAutoFocus() const = 0;

	virtual void setFocusDistance(double fd) = 0; // NOTE: non-const

	virtual double imageHeightAtDistanceOne() const = 0;

	// Used in IndigoDriver::traceRay() for picking.
	virtual void getRayForImagePos(const Vec2f& image_coordinates, float time, Vec4f& pos_ws_out, Vec4f& dir_ws_out) const = 0;


	/////////////// Geometry interface //////////////
	virtual Real meanCurvature(const HitInfo& hitinfo) const { return 0; }

	virtual const std::string emitWinterCode(EmitWinterCodeArgs& args) const { assert(0); return ""; }

	virtual void getInfoForHit(const HitInfo& hitinfo, Vec3Type& N_g_os_out, Vec3Type& N_s_os_out, unsigned int& mat_index_out, Vec3Type& pos_os_out, Real& pos_os_abs_error_out, Vec2f& uv0_out) const;

	virtual void getPartialDerivs(const HitInfo& hitinfo, Vec3Type& dp_du_out, Vec3Type& dp_dv_out) const;
	virtual void getIntrinsicCoordsPartialDerivs(const HitInfo& hitinfo, Vec3Type& dp_dalpha_out, Vec3Type& dp_dbeta_out) const;

	virtual const UVCoordsType getUVCoordsAndPartialDerivs(const HitInfo& hitinfo, unsigned int texcoord_set, Matrix2f& duv_dalphabeta_out) const;


	virtual void getSubElementSurfaceAreas(const Matrix4f& to_parent, std::vector<float>& surface_areas_out) const;
	
	// Sample the surface of the given sub-element.
	virtual void sampleSubElement(unsigned int sub_elem_index, const SamplePair& samples, Pos3Type& pos_out, Vec3Type& normal_out, HitInfo& hitinfo_out, 
		unsigned int& mat_index_out, Vec2f& uv0_out) const;

	// Returns true if possibly clipped by section planes, false otherwise.
	virtual bool subdivideAndDisplace(glare::TaskManager& task_manager, const ArrayRef<Reference<Material> >& materials, /*const Object& object, */const Matrix4f& object_to_camera, double pixel_height_at_dist_one, 
		const std::vector<Planef>& camera_clip_planes_os, const std::vector<Planef>& section_planes_os, const WorldParams& world_params,
		PrintOutput& print_output, bool verbose, ShouldCancelCallback* should_cancel_callback
		);

	virtual void build(const BuildOptions& options, ShouldCancelCallback& should_cancel_callback, PrintOutput& print_output, bool verbose, glare::TaskManager& task_manager);

	virtual unsigned int getNumUVCoordSets() const;
	virtual unsigned int getMaterialIndexForTri(unsigned int tri_index) const;

	virtual Aperture* getAperture() const = 0; // May return a NULL pointer.
	virtual bool isPlanar(Vec4f& normal_out) const { return true; }

	/////////////// Non-virtual methods //////////////
	inline CameraType getCameraType() const { return camera_type; }

	void setContainingMedia(const std::vector<const Medium*>& media);
	inline const std::vector<const Medium*>& getContainingMedia() const { return containing_media; }

	inline const TransformPath& getTransformPath() const { return transform_path; }


	double originalSensorWidth() const { return original_sensor_width; }
	double originalSensorHeight() const { return original_sensor_height; }
protected:
	TransformPath transform_path; // SSE Aligned
	js::AABBox bbox_ws; // SSE Aligned

private:
	CameraType camera_type;

protected:
	std::vector<const Medium*> containing_media;
	double original_sensor_width;
	double original_sensor_height;
};

#ifdef _WIN32
#pragma warning(pop)
#endif
