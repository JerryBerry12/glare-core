/*=====================================================================
DisplacementUtils.h
-------------------
Copyright Glare Technologies Limited 2015 -
File created by ClassTemplate on Thu May 15 20:31:26 2008
=====================================================================*/
#pragma once


#include "../simpleraytracer/raymesh.h"
#include "../maths/Matrix4f.h"
#include "../utils/Platform.h"
#include "../utils/Vector.h"
class ThreadContext;
class PrintOutput;
struct DUScratchInfo;
namespace Indigo { class TaskManager; }


class DUVertex
{
public:
	DUVertex(){}
	DUVertex(const Vec3f& pos_, const Vec3f& normal_) : pos(pos_), normal(normal_)
	{ 
		anchored = false; 
		uv_discontinuity = false;
	}

	Vec3f pos;
	Vec3f normal;
	int adjacent_vert_0, adjacent_vert_1; // Used when anchored = true.
	float displacement; // will be set after displace() is called.
	bool anchored; // If this vertex lies on a T-junction, it is 'anchored', and will be given a position which is the average of adjacent_vert_0 and adjacent_vert_0, in order to prevent gaps.
	bool uv_discontinuity; // Does this vertex lie on an edge with a UV discontinuity?
};


// Actually this represents either a triangle or a quad.
class DUQuad
{
public:
	DUQuad(){}
	DUQuad(	uint32_t v0_, uint32_t v1_, uint32_t v2_, uint32_t v3_,
			uint32_t mat_index_) : mat_index(mat_index_)
	{
		vertex_indices[0] = v0_;
		vertex_indices[1] = v1_;
		vertex_indices[2] = v2_;
		vertex_indices[3] = v3_;

		adjacent_quad_index[0] = -1;
		adjacent_quad_index[1] = -1;
		adjacent_quad_index[2] = -1;
		adjacent_quad_index[3] = -1;

		edge_midpoint_vert_index[0] = -1;
		edge_midpoint_vert_index[1] = -1;
		edge_midpoint_vert_index[2] = -1;
		edge_midpoint_vert_index[3] = -1;

		edge_uv_discontinuity[0] = false;
		edge_uv_discontinuity[1] = false;
		edge_uv_discontinuity[2] = false;
		edge_uv_discontinuity[3] = false;

		dead = false;
	}

	inline bool isTri() const { return vertex_indices[3] == std::numeric_limits<uint32>::max(); }
	inline bool isQuad() const { return vertex_indices[3] != std::numeric_limits<uint32>::max(); }
	inline int numSides() const { return vertex_indices[3] == std::numeric_limits<uint32>::max() ? 3 : 4; }

	uint32_t vertex_indices[4]; // Indices of the corner vertices.  If vertex_indices[3] == std::numeric_limits<uint32>::max(), then this is a triangle.
	//16 bytes

	int adjacent_quad_index[4]; // Index of adjacent quad along the given edge, or -1 if no adjacent quad. (border edge)
	int edge_midpoint_vert_index[4]; // Indices of the new vertices at the midpoint of each edge.
	// 48 bytes

	uint32_t mat_index; // material index

	int child_quads_index; // Index of the first child (result of subdivision) quad of this quad.

	bool edge_uv_discontinuity[4];
	
	bool dead; // A dead quad is a quad that does not need to be subdivided any more.
};


typedef js::Vector<DUQuad, 64> DUQuadVector;
typedef js::Vector<DUVertex, 16> DUVertexVector;
typedef js::Vector<Vec2f, 16> UVVector;


struct Polygons
{
	js::Vector<DUQuad, 64> quads;
};


struct VertsAndUVs
{
	js::Vector<DUVertex, 16> verts;
	js::Vector<Vec2f, 16> uvs;
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


	// Subdivide and displace the mesh.
	// All quads will be converted to triangles.
	static void subdivideAndDisplace(
		const std::string& mesh_name,
		Indigo::TaskManager& task_manager,
		PrintOutput& print_output,
		ThreadContext& context,
		const std::vector<Reference<Material> >& materials,
		bool subdivision_smoothing,
		RayMesh::TriangleVectorType& tris_in_out,
		const RayMesh::QuadVectorType& quads_in,
		RayMesh::VertexVectorType& verts_in_out,
		std::vector<Vec2f>& uvs_in_out,
		unsigned int num_uv_sets,
		const DUOptions& options,
		bool use_shading_normals
	);


	// Displace all vertices - updates verts_in_out.pos
	static void doDisplacementOnly(
		const std::string& mesh_name,
		Indigo::TaskManager& task_manager,
		PrintOutput& print_output,
		ThreadContext& context,
		const std::vector<Reference<Material> >& materials,
		const RayMesh::TriangleVectorType& tris_in,
		const RayMesh::QuadVectorType& quads_in,
		RayMesh::VertexVectorType& verts_in_out,
		const std::vector<Vec2f>& uvs_in,
		unsigned int num_uv_sets
	);
	


	static void test();

private:

	

	static void displace(
		Indigo::TaskManager& task_manager,
		ThreadContext& context,
		const std::vector<Reference<Material> >& materials,
		const DUQuadVector& quads,
		const DUVertexVector& verts_in,
		const UVVector& uvs,
		unsigned int num_uv_sets,
		DUVertexVector& verts_out
	);

	static void linearSubdivision(
		Indigo::TaskManager& task_manager,
		PrintOutput& print_output,
		ThreadContext& context,
		const std::vector<Reference<Material> >& materials,
		Polygons& polygons_in,
		const VertsAndUVs& verts_and_uvs_in,
		unsigned int num_uv_sets,
		unsigned int num_subdivs_done,
		bool subdivision_smoothing, // TODO: put in DUOptions.
		const DUOptions& options,
		DUScratchInfo& scratch_info,
		Polygons& polygons_out,
		VertsAndUVs& verts_and_uvs_out
	);

	static void draw(const Polygons& polygons, const VertsAndUVs& verts_and_uvs, int num_uv_sets, const std::string& image_path);
};
