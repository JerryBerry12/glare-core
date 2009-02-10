/*===================================================================

  
  digital liberation front 2002
  
  _______    ______      _______
 /______/\  |______|    /\______\  
|       \ \ |      |   / /       |    
|        \| |      |  |/         |  
|_____    \ |      |_ /    ______|       
 ____|    | |      |_||    |_____          
     |____| |________||____|                
           



Code by Nicholas Chapman[/ Ono-Sendai]
nickamy@paradise.net.nz

You may use this code for any non-commercial project,
as long as you do not remove this description.

You may not use this code for any commercial project.
====================================================================*/
#ifndef __RAYSPHERE_H__
#define __RAYSPHERE_H__

#include "geometry.h"
#include "../maths/vec3.h"
#include "../physics/jscol_aabbox.h"


class RaySphere : public Geometry
{
public:
	RaySphere(const Vec3d& pos_, double radius_);
	virtual ~RaySphere();

	
	////////////////////// Geometry interface ///////////////////
	virtual double traceRay(const Ray& ray, double max_t, ThreadContext& thread_context, js::ObjectTreePerThreadData& context, const Object* object, HitInfo& hitinfo_out) const;
	virtual void getAllHits(const Ray& ray, ThreadContext& thread_context, js::ObjectTreePerThreadData& context, const Object* object, std::vector<DistanceHitInfo>& hitinfos_out) const;
	virtual bool doesFiniteRayHit(const Ray& ray, double raylength, ThreadContext& thread_context, js::ObjectTreePerThreadData& context, const Object* object) const;
	virtual const js::AABBox& getAABBoxWS() const;
	//virtual const std::string debugName() const;
	
	virtual const Vec3Type getShadingNormal(const HitInfo& hitinfo) const;
	virtual const Vec3Type getGeometricNormal(const HitInfo& hitinfo) const;
	const TexCoordsType getTexCoords(const HitInfo& hitinfo, unsigned int texcoords_set) const;
	virtual unsigned int getNumTexCoordSets() const;
	virtual void getPartialDerivs(const HitInfo& hitinfo, Vec3Type& dp_du_out, Vec3Type& dp_dv_out, Vec3Type& dNs_du_out, Vec3Type& dNs_dv_out) const;
	virtual void getTexCoordPartialDerivs(const HitInfo& hitinfo, unsigned int texcoord_set, TexCoordsRealType& ds_du_out, TexCoordsRealType& ds_dv_out, TexCoordsRealType& dt_du_out, TexCoordsRealType& dt_dv_out) const;
	virtual unsigned int getMaterialIndexForTri(unsigned int tri_index) const;
	
	virtual void getSubElementSurfaceAreas(const Matrix3<Vec3RealType>& to_parent, std::vector<double>& surface_areas_out) const;
	virtual void sampleSubElement(unsigned int sub_elem_index, const SamplePair& samples, Pos3Type& pos_out, Vec3Type& normal_out, HitInfo& hitinfo_out) const;
	virtual double subElementSamplingPDF(unsigned int sub_elem_index, const Pos3Type& pos, double sub_elem_area_ws) const;

	virtual void subdivideAndDisplace(ThreadContext& context, const Object& object, const CoordFramed& camera_coordframe_os, double pixel_height_at_dist_one,
		const std::vector<Plane<double> >& camera_clip_planes);
	virtual void build(const std::string& indigo_base_dir_path, const RendererSettings& settings, PrintOutput& print_output); // throws GeometryExcep
	virtual const std::string getName() const;
	virtual bool isEnvSphereGeometry() const;
	//////////////////////////////////////////////////////////


	//intersectable interface
	/*virtual double traceRay(const Ray& ray, double max_t, ThreadContext& thread_context, js::ObjectTreePerThreadData& context, const Object* object, HitInfo& hitinfo_out) const;
	virtual const js::AABBox& getAABBoxWS() const { return aabbox; }

	virtual void getAllHits(const Ray& ray, ThreadContext& thread_context, js::ObjectTreePerThreadData& context, const Object* object, std::vector<DistanceHitInfo>& hitinfos_out) const;
	virtual bool doesFiniteRayHit(const Ray& ray, double raylength, ThreadContext& thread_context, js::ObjectTreePerThreadData& context, const Object* object) const;

	virtual const Vec3d getShadingNormal(const HitInfo& hitinfo) const;
	virtual const Vec3d getGeometricNormal(const HitInfo& hitinfo) const;
	virtual const Vec2d getTexCoords(const HitInfo& hitinfo, unsigned int texcoords_set) const;
	virtual unsigned int getNumTexCoordSets() const { return 1; }

	virtual void getPartialDerivs(const HitInfo& hitinfo, Vec3d& dp_du_out, Vec3d& dp_dv_out, Vec3d& dNs_du_out, Vec3d& dNs_dv_out) const;
	virtual void getTexCoordPartialDerivs(const HitInfo& hitinfo, unsigned int texcoord_set, double& ds_du_out, double& ds_dv_out, double& dt_du_out, double& dt_dv_out) const;
	//returns true if could construct a suitable basis
	//virtual bool getTangents(const HitInfo& hitinfo, unsigned int texcoord_set, Vec3d& tangent_out, Vec3d& bitangent_out) const;

	//virtual void emitterInit();
	//virtual const Vec3d sampleSurface(const SamplePair& samples, const Vec3d& viewer_point, Vec3d& normal_out, HitInfo& hitinfo_out) const;
	//virtual double surfacePDF(const Vec3d& pos, const Vec3d& normal, const Matrix3d& to_parent) const;
	//virtual double surfaceArea(const Matrix3d& to_parent) const;
	virtual void getSubElementSurfaceAreas(const Matrix3d& to_parent, std::vector<double>& surface_areas_out) const;
	virtual void sampleSubElement(unsigned int sub_elem_index, const SamplePair& samples, Pos3Type& pos_out, Vec3Type& normal_out, HitInfo& hitinfo_out) const;
	virtual double subElementSamplingPDF(unsigned int sub_elem_index, const Pos3Type& pos, double sub_elem_area_ws) const;

	virtual int UVSetIndexForName(const std::string& uvset_name) const { return 0; }

	

	virtual const std::string getName() const { return "RaySphere"; }

	virtual void subdivideAndDisplace(ThreadContext& context, const Object& object, const CoordFramed& camera_coordframe_os, double pixel_height_at_dist_one, 
		const std::vector<Plane<double> >& camera_clip_planes){}

	virtual void build(const std::string& indigo_base_dir_path, const RendererSettings& settings) {} // throws GeometryExcep*/

	static void test();

private:
	Vec3d centerpos;

	double radius;

	//stuff below is precomputed for efficiency
	double radius_squared;
	double recip_radius;
	js::AABBox aabbox;
};





#endif //__RAYSPHERE_H__
