/*=====================================================================
BVH.cpp
-------
File created by ClassTemplate on Sun Oct 26 17:19:14 2008
Code By Nicholas Chapman.
=====================================================================*/
#include "BVH.h"


#include "jscol_aabbox.h"
#include "../indigo/globals.h"
#include "../utils/timer.h"
#include "../simpleraytracer/raymesh.h"
#include "TreeUtils.h"
#include "BVHImpl.h"
#include "MollerTrumboreTri.h"


namespace js
{


BVH::BVH(RayMesh* raymesh_)
:	raymesh(raymesh_),
	num_intersect_tris(0),
	intersect_tris(NULL),
	root_aabb(NULL),
	tri_aabbs(NULL)
{
	assert(raymesh);

	BVHNode node;
	node.setLeftChildIndex(45654);
	node.setRightChildIndex(456456);

	assert(node.getLeftChildIndex() == 45654);
	assert(node.getRightChildIndex() == 456456);

	node.setToInterior();
	node.setLeftToLeaf();
	node.setRightToLeaf();
	
	node.setLeftGeomIndex(3);
	node.setLeftNumGeom(7);
	node.setRightGeomIndex(567);
	node.setRightNumGeom(4565);

	
	assert(node.isLeftLeaf());
	assert(node.isRightLeaf());
	assert(node.getLeftGeomIndex() == 3);
	assert(node.getLeftNumGeom() == 7);
	assert(node.getRightGeomIndex() == 567);
	assert(node.getRightNumGeom() == 4565);

	//printVar(z);

	num_maxdepth_leaves = 0;
	num_under_thresh_leaves = 0;
	num_leaves = 0;
	max_num_tris_per_leaf = 0;
	leaf_depth_sum = 0;
	max_leaf_depth = 0;
	num_cheaper_nosplit_leaves = 0;

	assert(sizeof(BVHNode) == 64);
}


BVH::~BVH()
{
	assert(tri_aabbs == NULL);
	SSE::alignedArrayFree(nodes);
	SSE::alignedSSEFree(root_aabb);
	SSE::alignedSSEFree(intersect_tris);
	intersect_tris = NULL;
}


const Vec3f& BVH::triVertPos(unsigned int tri_index, unsigned int vert_index_in_tri) const
{
	return raymesh->triVertPos(tri_index, vert_index_in_tri);
}


unsigned int BVH::numTris() const
{
	return raymesh->getNumTris();
}


/*
		root: left geom index, left num geom, right = not used

or

		root left index, right index
			/					\
	left child					right child
*/


class CenterPredicate
{
public:
	CenterPredicate(int axis_, const std::vector<Vec3f>& tri_centers_) : axis(axis_), tri_centers(tri_centers_) {}

	inline bool operator()(unsigned int t1, unsigned int t2)
	{
		return tri_centers[t1][axis] < tri_centers[t2][axis];
	}
private:
	int axis;
	const std::vector<Vec3f>& tri_centers;
};


void BVH::build()
{
	conPrint("\tBVH::build()");
	
	try
	{
		const int num_tris = (int)numTris();

		root_aabb = (js::AABBox*)SSE::alignedSSEMalloc(sizeof(AABBox));
		TreeUtils::buildRootAABB(*raymesh, *root_aabb);
		assert(root_aabb->invariant());

		
		conPrint("\tAllocating intersect triangles...");
		this->num_intersect_tris = numTris();
		if(::atDebugLevel(DEBUG_LEVEL_VERBOSE))
			conPrint("\t\tIntersect_tris mem usage: " + ::getNiceByteSize(num_intersect_tris * sizeof(INTERSECT_TRI_TYPE)));

		SSE::alignedSSEArrayMalloc(num_intersect_tris, intersect_tris);
		intersect_tri_i = 0;

		//original_tri_index.resize(num_tris);
		//new_tri_index.resize(num_tris);

		{

		// Build tri AABBs
		SSE::alignedSSEArrayMalloc(numTris(), tri_aabbs);

		for(unsigned int i=0; i<numTris(); ++i)
		{
			const SSE_ALIGN PaddedVec3f v1(triVertPos(i, 1));
			const SSE_ALIGN PaddedVec3f v2(triVertPos(i, 2));

			tri_aabbs[i].min_ = tri_aabbs[i].max_ = triVertPos(i, 0);
			tri_aabbs[i].enlargeToHoldAlignedPoint(v1);
			tri_aabbs[i].enlargeToHoldAlignedPoint(v2);
		}

		// Build tri centers
		std::vector<Vec3f> tri_centers(numTris());
	#pragma omp parallel for
		for(int i=0; i<num_tris; ++i)
			tri_centers[i] = tri_aabbs[i].centroid();

		std::vector<std::vector<TRI_INDEX> > tris(3);
		std::vector<std::vector<TRI_INDEX> > temp(2);
		temp[0].resize(numTris());
		temp[1].resize(numTris());

		// Sort indices based on center position along the axes
		conPrint("\tSorting...");
		Timer sort_timer;
	#pragma omp parallel for
		for(int axis=0; axis<3; ++axis)
		{
			tris[axis].resize(numTris());
			for(unsigned int i=0; i<numTris(); ++i)
				tris[axis][i] = i;

			// Sort based on center along axis 'axis'
			std::sort<std::vector<unsigned int>::iterator, CenterPredicate>(tris[axis].begin(), tris[axis].end(), CenterPredicate(axis, tri_centers));
		}
		conPrint("\t\tDone (" + toString(sort_timer.getSecondsElapsed()) + " s).");

		nodes_capacity = myMax(2u, numTris() / 8);
		SSE::alignedArrayMalloc(nodes_capacity, BVHNode::requiredAlignment(), nodes);
		num_nodes = 1;

		// Make root node
		nodes[0].setToInterior();
		nodes[0].setLeftAABB(*root_aabb);
		nodes[0].setRightAABB(AABBox(Vec3f(std::numeric_limits<float>::infinity()), Vec3f(std::numeric_limits<float>::infinity()))); // Pick an AABB that will never get hit
		nodes[0].setRightToLeaf();
		nodes[0].setRightNumGeom(0);

		doBuild(
			*root_aabb, // AABB
			tris,
			temp,
			tri_centers,
			0, // left
			numTris(), // right
			0, // depth
			0, // parent index
			0 // child index (=left)
			);
		}

		//------------------------------------------------------------------------
		//alloc intersect tri array
		//------------------------------------------------------------------------
		/*conPrint("\tAllocating intersect triangles...");
		this->num_intersect_tris = numTris();
		if(::atDebugLevel(DEBUG_LEVEL_VERBOSE))
			conPrint("\t\tIntersect_tris mem usage: " + ::getNiceByteSize(num_intersect_tris * sizeof(INTERSECT_TRI_TYPE)));

		SSE::alignedSSEArrayMalloc(num_intersect_tris, intersect_tris);
*/
		// Copy tri data.
		for(unsigned int i=0; i<num_intersect_tris; ++i)
			intersect_tris[i].set(triVertPos(i, 0), triVertPos(i, 1), triVertPos(i, 2));
		//conPrint("\tdone.");


		SSE::alignedSSEArrayFree(tri_aabbs);
		tri_aabbs = NULL;

		conPrint("\tBuild Stats:");
		conPrint("\t\tTotal nodes used: " + ::toString((unsigned int)num_nodes) + " (" + ::getNiceByteSize(num_nodes * sizeof(BVHNode)) + ")");
		conPrint("\t\tTotal tri indices used: " + ::toString((unsigned int)leafgeom.size()) + " (" + ::getNiceByteSize(leafgeom.size() * sizeof(TRI_INDEX)) + ")");
		conPrint("\t\tNum sub threshold leaves: " + toString(num_under_thresh_leaves));
		conPrint("\t\tNum max depth leaves: " + toString(num_maxdepth_leaves));
		conPrint("\t\tNum cheaper no-split leaves: " + toString(num_cheaper_nosplit_leaves));
		conPrint("\t\tMean tris per leaf: " + toString((float)numTris() / (float)num_leaves));
		conPrint("\t\tMax tris per leaf: " + toString(max_num_tris_per_leaf));
		conPrint("\t\tMean leaf depth: " + toString((float)leaf_depth_sum / (float)num_leaves));
		conPrint("\t\tMax leaf depth: " + toString(max_leaf_depth));

		conPrint("\tFinished building tree.");
	}
	catch(std::bad_alloc& e)
	{
		throw TreeExcep(e.what());
	}
}


void BVH::markLeafNode(BVHNode* nodes, unsigned int parent_index, unsigned int child_index, int left, int right, const std::vector<std::vector<TRI_INDEX> >& tris)
{
	if(myMax(right - left, 0) > (int)BVHNode::maxNumGeom())
		throw TreeExcep("Build failure, too many tris in leaf.");

	// 8 => 8
	// 7 => 8
	// 6 => 8
	// 5 => 8
	// 4 => 4
	const int num_tris = myMax(right - left, 0);
	const int num_4tris = ((num_tris % 4) == 0) ? (num_tris / 4) : (num_tris / 4) + 1;
	const int num_tris_rounded_up = num_4tris * 4;
	const int num_padding = num_tris_rounded_up - num_tris;
	//const int num_4tris = num_tris_rounded_up / 4;

	assert(num_tris + num_padding == num_tris_rounded_up);
	assert(num_tris_rounded_up % 4 == 0);

	if(child_index == 0)
	{
		nodes[parent_index].setLeftToLeaf();
		nodes[parent_index].setLeftGeomIndex((unsigned int)leafgeom.size()); //intersect_tri_i);
		nodes[parent_index].setLeftNumGeom(num_4tris);
	}
	else
	{
		nodes[parent_index].setRightToLeaf();
		nodes[parent_index].setRightGeomIndex((unsigned int)leafgeom.size()); // intersect_tri_i);
		nodes[parent_index].setRightNumGeom(num_4tris);
	}

	for(int i=left; i<right; ++i)
	{
		const TRI_INDEX source_tri = tris[0][i];
		/*assert(intersect_tri_i < num_intersect_tris);
		original_tri_index[intersect_tri_i] = source_tri;
		new_tri_index[source_tri] = intersect_tri_i;
		intersect_tris[intersect_tri_i++].set(triVertPos(source_tri, 0), triVertPos(source_tri, 1), triVertPos(source_tri, 2));*/
		leafgeom.push_back(source_tri);
	}

	for(int i=0; i<num_padding; ++i)
		leafgeom.push_back(leafgeom.back());

	// Update build stats
	max_num_tris_per_leaf = myMax(max_num_tris_per_leaf, right - left);
	num_leaves++;
}


/*
The triangles [left, right) are considered to belong to this node.
The AABB for this node (build_nodes[node_index]) should be set already.
*/
void BVH::doBuild(const AABBox& aabb, std::vector<std::vector<TRI_INDEX> >& tris, std::vector<std::vector<TRI_INDEX> >& temp, 
		const std::vector<Vec3f>& tri_centers, int left, int right, int depth, unsigned int parent_index, unsigned int child_index)
{
	//assert(left >= 0 && left < (int)numTris() && right >= 0 && right < (int)numTris());
	//assert(node_index < build_nodes.size());

	const int LEAF_NUM_TRI_THRESHOLD = 4;

	const int MAX_DEPTH = js::Tree::MAX_TREE_DEPTH;

	if(right - left <= LEAF_NUM_TRI_THRESHOLD || depth >= MAX_DEPTH)
	{
		markLeafNode(nodes, parent_index, child_index, left, right, tris);

		// Update build stats
		if(depth >= MAX_DEPTH)
			num_maxdepth_leaves++;
		else
			num_under_thresh_leaves++;
		leaf_depth_sum += depth;
		max_leaf_depth = myMax(max_leaf_depth, depth);
		return;
	}

	// Compute best split plane
	const float traversal_cost = 1.0f;
	const float intersection_cost = 4.0f;
	const float aabb_surface_area = aabb.getSurfaceArea();
	const float recip_aabb_surf_area = 1.0f / aabb_surface_area;

	const unsigned int nonsplit_axes[3][2] = {{1, 2}, {0, 2}, {0, 1}};

	// Compute non-split cost
	float smallest_cost = (float)(right - left) * intersection_cost;

	float axis_smallest_cost[3] = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
	float axis_best_div_val[3] = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};

	// for each axis 0..2
	for(unsigned int axis=0; axis<3; ++axis)
	{
		const std::vector<TRI_INDEX>& axis_tris = tris[axis];

		// SAH stuff
		const unsigned int axis1 = nonsplit_axes[axis][0];
		const unsigned int axis2 = nonsplit_axes[axis][1];
		const float two_cap_area = (aabb.axisLength(axis1) * aabb.axisLength(axis2)) * 2.0f;
		const float circum = (aabb.axisLength(axis1) + aabb.axisLength(axis2)) * 2.0f;

		// For Each triangle centroid
		for(int i=left; i<right; ++i)
		{
			const float splitval = tri_centers[axis_tris[i]][axis];//triCenter(tris[axis][i], axis);

			// Compute the SAH cost at the centroid position
			const int N_L = (i - left) + 1;
			const int N_R = (right - left) - N_L;

			//assert(N_L >= 0 && N_L <= (int)centers.size() && N_R >= 0 && N_R <= (int)centers.size());

			// Compute SAH cost
			const float negchild_surface_area = two_cap_area + (splitval - aabb.min_[axis]) * circum;
			const float poschild_surface_area = two_cap_area + (aabb.max_[axis] - splitval) * circum;

			const float cost = traversal_cost + ((float)N_L * negchild_surface_area + (float)N_R * poschild_surface_area) * 
						recip_aabb_surf_area * intersection_cost;

			if(cost < axis_smallest_cost[axis])
			{
				axis_smallest_cost[axis] = cost;
				//best_axis = axis;
				axis_best_div_val[axis] = splitval;
				//best_i = i;
			}
		}
	}

	int best_axis = -1;
	float best_div_val;
	for(int axis=0; axis<3; ++axis)
		if(axis_smallest_cost[axis] < smallest_cost)
		{
			smallest_cost = axis_smallest_cost[axis];
			best_axis = axis;
			best_div_val = axis_best_div_val[axis];
		}

	if(best_axis == -1)
	{
		// If the least cost is to not split the node, then make this node a leaf node
		markLeafNode(nodes, parent_index, child_index, left, right, tris);

		// Update build stats
		num_cheaper_nosplit_leaves++;
		leaf_depth_sum += depth;
		max_leaf_depth = myMax(max_leaf_depth, depth);
		return;
	}

	// Now we need to partition the triangle index lists, while maintaining ordering.
	int split_i;
	for(int axis=0; axis<3; ++axis)
	{
		std::vector<TRI_INDEX>& axis_tris = tris[axis];

		int num_left_tris = 0;
		int num_right_tris = 0;

		for(int i=left; i<right; ++i)
		{
			if(tri_centers[axis_tris[i]][best_axis]/*triCenter(tris[axis][i], best_axis)*/ <= best_div_val) // If on Left side
				temp[0][num_left_tris++] = axis_tris[i];
			else // else if on Right side
				temp[1][num_right_tris++] = axis_tris[i];
		}

		// Copy temp back to the tri list

		for(int z=0; z<num_left_tris; ++z)
			axis_tris[left + z] = temp[0][z];

		for(int z=0; z<num_right_tris; ++z)
			axis_tris[left + num_left_tris + z] = temp[1][z];

		split_i = left + num_left_tris;
		assert(split_i >= left && split_i <= right);
	}


	// Compute AABBs for children
	SSE_ALIGN AABBox left_aabb(
		Vec3f(std::numeric_limits<float>::infinity()),
		Vec3f(-std::numeric_limits<float>::infinity())
		);

	SSE_ALIGN AABBox right_aabb(
		Vec3f(std::numeric_limits<float>::infinity()),
		Vec3f(-std::numeric_limits<float>::infinity())
		);

	for(int i=left; i<split_i; ++i)
	{
		//SSE_ALIGN AABBox tri_aabb;
		//triAABB(tris[0][i], tri_aabb);

		left_aabb.enlargeToHoldAABBox(tri_aabbs[tris[0][i]]);
	}

	for(int i=split_i; i<right; ++i)
	{
		//SSE_ALIGN AABBox tri_aabb;
		//triAABB(tris[0][i], tri_aabb);

		right_aabb.enlargeToHoldAABBox(tri_aabbs[tris[0][i]]);
	}

	
	// Alloc space for this node
	const unsigned int node_index = num_nodes;

	const unsigned int new_num_nodes = num_nodes + 1;
	if(new_num_nodes > nodes_capacity)
	{
		nodes_capacity *= 2;
		BVHNode* new_nodes;
		SSE::alignedArrayMalloc(nodes_capacity, BVHNode::requiredAlignment(), new_nodes); // Alloc new array
		memcpy(new_nodes, nodes, sizeof(BVHNode) * num_nodes); // Copy over old array
		SSE::alignedArrayFree(nodes); // Free old array
		nodes = new_nodes; // Update nodes pointer to point at new array
	}
	num_nodes = new_num_nodes; // Update size

	assert(node_index < num_nodes);
	nodes[node_index].setLeftAABB(left_aabb);
	nodes[node_index].setRightAABB(right_aabb);
	nodes[node_index].setToInterior(); // may be overridden later

	// Update link from parent to this node
	if(child_index == 0)
		nodes[parent_index].setLeftChildIndex(node_index);
	else
		nodes[parent_index].setRightChildIndex(node_index);

	// Recurse to build left subtree
	doBuild(left_aabb, tris, temp, tri_centers, left, split_i, depth+1, node_index, 0);

	// Recurse to build right subtree
	doBuild(right_aabb, tris, temp, tri_centers, split_i, right, depth+1, node_index, 1);
}


class DoesFiniteRayHitFunctions
{
public:
	inline static bool testAgainstTriangles(const BVH& bvh, unsigned int leaf_geom_index, unsigned int num_leaf_tris, 
		const Ray& ray,
		HitInfo& hitinfo_out,
		float& best_t
		)
	{
		for(unsigned int i=0; i<num_leaf_tris; ++i)
		{
			const float* const t0 = bvh.intersect_tris[bvh.leafgeom[leaf_geom_index + 0]].data;
			const float* const t1 = bvh.intersect_tris[bvh.leafgeom[leaf_geom_index + 1]].data;
			const float* const t2 = bvh.intersect_tris[bvh.leafgeom[leaf_geom_index + 2]].data;
			const float* const t3 = bvh.intersect_tris[bvh.leafgeom[leaf_geom_index + 3]].data;

			Vec4 u, v, t, hit;
			MollerTrumboreTri::intersectTris(&ray, t0, t1, t2, t3, 
				&u, &v, &t, &hit
				);

			if((hit.i[0] != 0) && t.f[0] < best_t) // && object mask test
				return true;
			if((hit.i[1] != 0) && t.f[1] < best_t) // && object mask test
				return true;
			if((hit.i[2] != 0) && t.f[2] < best_t) // && object mask test
				return true;
			if((hit.i[3] != 0) && t.f[3] < best_t) // && object mask test
				return true;

			leaf_geom_index += 4;
		}

		return false; // Don't early out from BVHImpl::traceRay()
	}
};


bool BVH::doesFiniteRayHit(const ::Ray& ray, double raylength, ThreadContext& thread_context, js::TriTreePerThreadData& context, const Object* object) const
{
	//int hitinfo_dummy;
	//SSE_ALIGN Vec4 best_u;
	//SSE_ALIGN Vec4 best_v;
	//SSE_ALIGN Vec4 best_tri_index;
	/*SSE_ALIGN Vec4 best_data;*/

	HitInfo hitinfo;
	const double t = BVHImpl::traceRay<DoesFiniteRayHitFunctions>(*this, ray, raylength, thread_context, context, object, hitinfo); // &best_u, &best_v, &best_tri_index);
	return t >= 0.0;

	//HitInfo hitinfo;
	//return traceRay(ray, raylength, thread_context, context, object, hitinfo) > 0.0;
}


class TraceRayFunctions
{
public:
	inline static bool testAgainstTriangles(const BVH& bvh, unsigned int leaf_geom_index, unsigned int num_leaf_tris, 
		const Ray& ray,
		//Vec4* best_t,
		HitInfo& hitinfo_out,
		float& best_t
		//Vec4* best_tri_index,
		//Vec4* best_u,
		//Vec4* best_v
		//Vec4* best_data
		)
	{
		/*for(unsigned int i=0; i<num_leaf_tris; ++i)
		{
			float u, v, raydist;
			if(bvh.intersect_tris[leaf_geom_index].rayIntersect(ray, closest_dist, raydist, u, v))
			{
				closest_dist = raydist;
				hitinfo_out.sub_elem_index = bvh.original_tri_index[leaf_geom_index];
				hitinfo_out.sub_elem_coords.set(u, v);
			}
			++leaf_geom_index;
		}*/
		for(unsigned int i=0; i<num_leaf_tris; ++i)
		{
			const float* const t0 = bvh.intersect_tris[bvh.leafgeom[leaf_geom_index + 0]].data;
			const float* const t1 = bvh.intersect_tris[bvh.leafgeom[leaf_geom_index + 1]].data;
			const float* const t2 = bvh.intersect_tris[bvh.leafgeom[leaf_geom_index + 2]].data;
			const float* const t3 = bvh.intersect_tris[bvh.leafgeom[leaf_geom_index + 3]].data;
		
			/*SSE_ALIGN Vec4 tri_indices;
			tri_indices.i[0] = bvh.leafgeom[leaf_geom_index + 0];
			tri_indices.i[1] = bvh.leafgeom[leaf_geom_index + 1];
			tri_indices.i[2] = bvh.leafgeom[leaf_geom_index + 2];
			tri_indices.i[3] = bvh.leafgeom[leaf_geom_index + 3];

			assert(tri_indices.i[0] < bvh.num_intersect_tris);
			assert(tri_indices.i[1] < bvh.num_intersect_tris);
			assert(tri_indices.i[2] < bvh.num_intersect_tris);
			assert(tri_indices.i[3] < bvh.num_intersect_tris);*/

			Vec4 u, v, t, hit;
			MollerTrumboreTri::intersectTris(&ray, t0, t1, t2, t3, 
				//best_t,
				&u, &v, &t, &hit
				//&tri_indices, 
				//best_data
				);
				//best_tri_index, &tri_indices, best_u, best_v);

			if((hit.i[0] != 0) && t.f[0] < best_t) // && object mask test
			{
				best_t = t.f[0];
				hitinfo_out.sub_elem_index = bvh.leafgeom[leaf_geom_index + 0];
				hitinfo_out.sub_elem_coords.set(u.f[0], v.f[0]);
			}
			if((hit.i[1] != 0) && t.f[1] < best_t) // && object mask test
			{
				best_t = t.f[1];
				hitinfo_out.sub_elem_index = bvh.leafgeom[leaf_geom_index + 1];
				hitinfo_out.sub_elem_coords.set(u.f[1], v.f[1]);
			}
			if((hit.i[2] != 0) && t.f[2] < best_t) // && object mask test
			{
				best_t = t.f[2];
				hitinfo_out.sub_elem_index = bvh.leafgeom[leaf_geom_index + 2];
				hitinfo_out.sub_elem_coords.set(u.f[2], v.f[2]);
			}
			if((hit.i[3] != 0) && t.f[3] < best_t) // && object mask test
			{
				best_t = t.f[3];
				hitinfo_out.sub_elem_index = bvh.leafgeom[leaf_geom_index + 3];
				hitinfo_out.sub_elem_coords.set(u.f[3], v.f[3]);
			}

			leaf_geom_index += 4;
		}

		return false; // Don't early out from BVHImpl::traceRay()
	}
};


double BVH::traceRay(const Ray& ray, double ray_max_t, ThreadContext& thread_context, js::TriTreePerThreadData& context, const Object* object, HitInfo& hitinfo_out) const
{
	//SSE_ALIGN Vec4 best_u;
	//SSE_ALIGN Vec4 best_v;
	//SSE_ALIGN Vec4 best_tri_index;
	//SSE_ALIGN Vec4 best_data;

	const double d = BVHImpl::traceRay<TraceRayFunctions>(*this, ray, ray_max_t, thread_context, context, object, hitinfo_out);//&best_data); // &best_u, &best_v, &best_tri_index);
	//hitinfo_out.sub_elem_index = best_data.i[2]; //best_tri_index.i[0];
	//hitinfo_out.sub_elem_coords.x = best_data.f[0]; // best_u.f[0];
	//hitinfo_out.sub_elem_coords.y = best_data.f[1]; // best_v.f[0];

	return d;
}


class GetAllHitsFunctions
{
public:
	inline static bool testAgainstTriangles(const BVH& bvh, unsigned int leaf_geom_index, unsigned int num_leaf_tris, 
		const Ray& ray,
		Vec4* best_t,
		Vec4* best_tri_index,
		Vec4* best_u,
		Vec4* best_v
		)
	{
		/*for(unsigned int i=0; i<num_leaf_tris; ++i)
		{
			float u, v, raydist;
			if(bvh.intersect_tris[leaf_geom_index].rayIntersect(ray, closest_dist, raydist, u, v))
			{
				// Check to see if we have already recorded the hit against this triangle (while traversing another leaf volume)
				// NOTE that this is a slow linear time check, but N should be small, hopefully :)
				bool already_got_hit = false;
				for(unsigned int z=0; z<hitinfos_out.size(); ++z)
					if(hitinfos_out[z].sub_elem_index == bvh.original_tri_index[leaf_geom_index])//if tri index is the same
						already_got_hit = true;

				if(!already_got_hit)
				{
					//if(!object || object->isNonNullAtHit(thread_context, ray, (double)raydist, leafgeom[triindex], u, v)) // Do visiblity check for null materials etc..
					//{
						hitinfos_out.push_back(DistanceHitInfo(
							bvh.original_tri_index[leaf_geom_index],
							Vec2d(u, v),
							raydist
							));
					//}
				}
			}
			++leaf_geom_index;
		}*/
		return false; // Don't early out from BVHImpl::traceRay()
	}
};


void BVH::getAllHits(const Ray& ray, ThreadContext& thread_context, js::TriTreePerThreadData& context, const Object* object, std::vector<DistanceHitInfo>& hitinfos_out) const
{
	hitinfos_out.resize(0);

	//BVHImpl::traceRay<GetAllHitsFunctions>(*this, ray, std::numeric_limits<float>::max(), thread_context, context, object, hitinfos_out);
}


const js::AABBox& BVH::getAABBoxWS() const
{
	return *root_aabb;
}


const Vec3f BVH::triGeometricNormal(unsigned int tri_index) const //slow
{
	//return intersect_tris[new_tri_index[tri_index]].getNormal();
	return intersect_tris[tri_index].getNormal();
}


} //end namespace js
