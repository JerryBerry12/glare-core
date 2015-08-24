/*=====================================================================
DisplacementUtils.h
-------------------
File created by ClassTemplate on Thu May 15 20:31:26 2008
Code By Nicholas Chapman.
=====================================================================*/
#pragma once


#include "../utils/Platform.h"
#include "../maths/Matrix4f.h"
#include "../simpleraytracer/raymesh.h"
class ThreadContext;
class PrintOutput;
struct DUScratchInfo;
namespace Indigo { class TaskManager; }


class DUVertex
{
public:
	DUVertex(){ anchored = false; }
	DUVertex(const Vec3f& pos_, const Vec3f& normal_) : pos(pos_), normal(normal_) { 
		//texcoords[0] = texcoords[1] = texcoords[2] = texcoords[3] = Vec2f(0.f, 0.f); 
		anchored = false; }
	Vec3f pos;
	Vec3f normal;
	//Vec2f texcoords[4];
	//int adjacent_subdivided_tris;
	int adjacent_vert_0, adjacent_vert_1;
	float displacement; // will be set after displace() is called.
	bool anchored;

	//Vec2f texcoords; // TEMP
	
	//static const unsigned int MAX_NUM_UV_SET_INDICES = 8;
	//unsigned int uv_set_indices[MAX_NUM_UV_SET_INDICES];
	//unsigned int num_uv_set_indices;
};


class DUVertexPolygon
{
public:
	unsigned int vertex_index;
	unsigned int uv_index;
};


// dimension 1
class DUEdge
{
public:
	DUEdge(){}
	DUEdge(uint32 v0, uint32 v1, uint32 uv0, uint32 uv1)
	{
		vertex_indices[0] = v0;
		vertex_indices[1] = v1;
		uv_indices[0] = uv0;
		uv_indices[1] = uv1;
	}
	unsigned int vertex_indices[2];
	unsigned int uv_indices[2];
};


class DUTriangle
{
public:
	DUTriangle(){}
	DUTriangle(unsigned int v0_, unsigned int v1_, unsigned int v2_, unsigned int uv0, unsigned int uv1, unsigned int uv2, unsigned int matindex/*, unsigned int dimension_*/) : tri_mat_index(matindex)/*, dimension(dimension_)*///, num_subdivs(num_subdivs_)
	{
		vertex_indices[0] = v0_;
		vertex_indices[1] = v1_;
		vertex_indices[2] = v2_;

		uv_indices[0] = uv0;
		uv_indices[1] = uv1;
		uv_indices[2] = uv2;

		edge_uv_discontinuity[0] = false;
		edge_uv_discontinuity[1] = false;
		edge_uv_discontinuity[2] = false;

		dead = false;
	}
	unsigned int vertex_indices[3];
	unsigned int uv_indices[3];
	unsigned int tri_mat_index;

	bool edge_uv_discontinuity[3];

	bool dead; // Have we stopped subdividing this tri?  If so, mark it as dead so we can easily choose to not subdivide it in subsequent passes.
};


class DUQuad
{
public:
	DUQuad(){}
	DUQuad(	uint32_t v0_, uint32_t v1_, uint32_t v2_, uint32_t v3_,
			uint32_t uv0, uint32_t uv1, uint32_t uv2, uint32_t uv3,
			uint32_t mat_index_) : mat_index(mat_index_)
	{
		vertex_indices[0] = v0_;
		vertex_indices[1] = v1_;
		vertex_indices[2] = v2_;
		vertex_indices[3] = v3_;

		uv_indices[0] = uv0;
		uv_indices[1] = uv1;
		uv_indices[2] = uv2;
		uv_indices[3] = uv3;

		edge_uv_discontinuity[0] = false;
		edge_uv_discontinuity[1] = false;
		edge_uv_discontinuity[2] = false;
		edge_uv_discontinuity[3] = false;

		dead = false;
	}
	uint32_t vertex_indices[4];
	uint32_t uv_indices[4];
	uint32_t mat_index;

	bool edge_uv_discontinuity[4];

	//uint32_t padding[2];
	bool dead;
};


struct Polygons
{
	std::vector<DUVertexPolygon> vert_polygons;
	std::vector<DUEdge> edges;  // Boundary and crease edges.
	std::vector<DUTriangle> tris;
	std::vector<DUQuad> quads;
};


struct VertsAndUVs
{
	std::vector<DUVertex> verts;
	std::vector<Vec2f> uvs;
};


SSE_CLASS_ALIGN DUOptions
{
public:
	INDIGO_ALIGNED_NEW_DELETE

	Matrix4f object_to_camera;

	bool view_dependent_subdivision;
	double pixel_height_at_dist_one;
	double subdivide_pixel_threshold;
	double subdivide_curvature_threshold;
	double displacement_error_threshold;
	unsigned int max_num_subdivisions;

	std::vector<Plane<float> > camera_clip_planes_os; // Camera clip planes in object space.
};


/*=====================================================================
DisplacementUtils
-----------------

=====================================================================*/
class DisplacementUtils
{
public:
	DisplacementUtils();
	~DisplacementUtils();


	static void subdivideAndDisplace(
		const std::string& mesh_name,
		Indigo::TaskManager& task_manager,
		PrintOutput& print_output,
		ThreadContext& context,
		const std::vector<Reference<Material> >& materials,
		bool subdivision_smoothing,
		const RayMesh::TriangleVectorType& tris_in,
		const RayMesh::QuadVectorType& quads_in,
		const RayMesh::VertexVectorType& verts_in,
		const std::vector<Vec2f>& uvs_in,
		unsigned int num_uv_sets,
		const DUOptions& options,
		bool use_shading_normals,
		RayMesh::TriangleVectorType& tris_out,
		RayMesh::VertexVectorType& verts_out,
		std::vector<Vec2f>& uvs_out
	);


	static void test();

private:
	static void displace(
		Indigo::TaskManager& task_manager,
		ThreadContext& context,
		const std::vector<Reference<Material> >& materials,
		bool use_anchoring,
		const std::vector<DUTriangle>& tris,
		const std::vector<DUQuad>& quads,
		const std::vector<DUVertex>& verts_in,
		const std::vector<Vec2f>& uvs,
		unsigned int num_uv_sets,
		std::vector<DUVertex>& verts_out
		);

	static void linearSubdivision(
		Indigo::TaskManager& task_manager,
		PrintOutput& print_output,
		ThreadContext& context,
		const std::vector<Reference<Material> >& materials,
		const Polygons& polygons_in,
		const VertsAndUVs& verts_and_uvs_in,
		unsigned int num_uv_sets,
		unsigned int num_subdivs_done,
		const DUOptions& options,
		DUScratchInfo& scratch_info,
		Polygons& polygons_out,
		VertsAndUVs& verts_and_uvs_out
		);

	static void averagePass(
		Indigo::TaskManager& task_manager,
		const Polygons& polygons_in,
		const VertsAndUVs& verts_and_uvs_in,
		unsigned int num_uv_sets,
		const DUOptions& options,
		VertsAndUVs& verts_and_uvs_out
		);
};
