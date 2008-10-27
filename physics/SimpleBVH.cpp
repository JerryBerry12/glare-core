/*=====================================================================
SimpleBVH.cpp
-------------
File created by ClassTemplate on Mon Oct 27 20:35:09 2008
Code By Nicholas Chapman.
=====================================================================*/
#include "SimpleBVH.h"


#include "jscol_aabbox.h"
#include "../indigo/globals.h"
#include "../utils/timer.h"
#include "../simpleraytracer/raymesh.h"
#include "TreeUtils.h"


namespace js
{


SimpleBVH::SimpleBVH(RayMesh* raymesh_)
:	raymesh(raymesh_),
	num_intersect_tris(0),
	intersect_tris(NULL),
	root_aabb(NULL)//,
	//tri_aabbs(NULL)
{
	assert(raymesh);

	num_intersect_tris = 0;
	intersect_tris = NULL;

	root_aabb = (js::AABBox*)alignedSSEMalloc(sizeof(AABBox));
	new(root_aabb) AABBox(Vec3f(0,0,0), Vec3f(0,0,0));

	num_maxdepth_leaves = 0;
	num_under_thresh_leaves = 0;
	num_leaves = 0;
	max_num_tris_per_leaf = 0;
	leaf_depth_sum = 0;
	max_leaf_depth = 0;
	num_cheaper_nosplit_leaves = 0;
}


SimpleBVH::~SimpleBVH()
{
	alignedSSEFree(root_aabb);
	alignedSSEFree(intersect_tris);
	intersect_tris = NULL;
}


const Vec3f& SimpleBVH::triVertPos(unsigned int tri_index, unsigned int vert_index_in_tri) const
{
	return raymesh->triVertPos(tri_index, vert_index_in_tri);
}


unsigned int SimpleBVH::numTris() const
{
	return raymesh->getNumTris();
}


void SimpleBVH::build()
{
	conPrint("\tSimpleBVH::build()");
	Timer buildtimer;

	//------------------------------------------------------------------------
	//calc root node's aabbox
	//NOTE: could do this faster by looping over vertices instead.
	//------------------------------------------------------------------------
	conPrint("\tCalcing root AABB.");
	root_aabb->min_ = triVertPos(0, 0);
	root_aabb->max_ = triVertPos(0, 0);
	for(unsigned int i=0; i<numTris(); ++i)
	{
		root_aabb->enlargeToHoldPoint(triVertPos(i, 0));
		root_aabb->enlargeToHoldPoint(triVertPos(i, 1));
		root_aabb->enlargeToHoldPoint(triVertPos(i, 2));
	}
	conPrint("\tdone.");
	conPrint("\tRoot AABB min: " + root_aabb->min_.toString());
	conPrint("\tRoot AABB max: " + root_aabb->max_.toString());

	//------------------------------------------------------------------------
	//alloc intersect tri array
	//------------------------------------------------------------------------
	conPrint("\tAllocing intersect triangles...");
	this->num_intersect_tris = numTris();
	if(::atDebugLevel(DEBUG_LEVEL_VERBOSE))
		conPrint("\tintersect_tris mem usage: " + ::getNiceByteSize(num_intersect_tris * sizeof(INTERSECT_TRI_TYPE)));

	::alignedSSEArrayMalloc(num_intersect_tris, intersect_tris);
	intersect_tri_i = 0;

	{
	std::vector<TRI_INDEX> tris(numTris());
	for(unsigned int i=0; i<numTris(); ++i)
		tris[i] = i;

	// Build tri AABBs
	/*::alignedSSEArrayMalloc(numTris(), tri_aabbs);

	for(unsigned int i=0; i<numTris(); ++i)
	{
		tri_aabbs[i].min_ = tri_aabbs[i].max_ = triVertPos(i, 0);
		tri_aabbs[i].enlargeToHoldPoint(triVertPos(i, 1));
		tri_aabbs[i].enlargeToHoldPoint(triVertPos(i, 2));
	}*/

	// Build tri centers
	//tri_centers.resize(numTris());
	//for(unsigned int i=0; i<numTris(); ++i)
	//	tri_centers[i] = tri_aabbs[i].centroid();


	nodes_capacity = myMax(2u, numTris() / 4);
	alignedArrayMalloc(nodes_capacity, SimpleBVHNode::requiredAlignment(), nodes);
	num_nodes = 1;
	doBuild(
		*root_aabb, // AABB
		tris, 
		0, // left
		numTris(), // right
		0, // node index to use
		0 // depth
		);

	assert(intersect_tri_i == num_intersect_tris);
	}


	conPrint("\tdone.");

//	::alignedSSEArrayFree(tri_aabbs);

	
	conPrint("\tBuild Stats:");
	conPrint("\t\tTotal nodes used: " + ::toString((unsigned int)num_nodes) + " (" + ::getNiceByteSize(num_nodes * sizeof(SimpleBVHNode)) + ")");
	conPrint("\t\tNum sub threshold leaves: " + toString(num_under_thresh_leaves));
	conPrint("\t\tNum max depth leaves: " + toString(num_maxdepth_leaves));
	conPrint("\t\tNum cheaper no-split leaves: " + toString(num_cheaper_nosplit_leaves));
	conPrint("\t\tMean tris per leaf: " + toString((float)numTris() / (float)num_leaves));
	conPrint("\t\tMax tris per leaf: " + toString(max_num_tris_per_leaf));
	conPrint("\t\tMean leaf depth: " + toString((float)leaf_depth_sum / (float)num_leaves));
	conPrint("\t\tMax leaf depth: " + toString(max_leaf_depth));


	conPrint("\tFinished building tree.");	
}


void SimpleBVH::markLeafNode(std::vector<TRI_INDEX>& tris, unsigned int node_index, int left, int right)
{
	nodes[node_index].leaf = 1;
	nodes[node_index].geometry_index = intersect_tri_i;

	for(int i=left; i<right; ++i)
	{
		const unsigned int source_tri = tris[i];
		assert(intersect_tri_i < num_intersect_tris);
		intersect_tris[intersect_tri_i++].set(triVertPos(source_tri, 0), triVertPos(source_tri, 1), triVertPos(source_tri, 2));
	}

	nodes[node_index].num_geom = myMax(right - left, 0);

	// Update build stats
	max_num_tris_per_leaf = myMax(max_num_tris_per_leaf, right - left);
	num_leaves++;
}


float SimpleBVH::triCenter(unsigned int tri_index, unsigned int axis)
{
	/*const float min = myMin(triVertPos(tri_index, 0)[axis], triVertPos(tri_index, 1)[axis], triVertPos(tri_index, 2)[axis]);
	const float max = myMax(triVertPos(tri_index, 0)[axis], triVertPos(tri_index, 1)[axis], triVertPos(tri_index, 2)[axis]);
	return 0.5f * (min + max);*/

	SSE_ALIGN AABBox aabb;
	triAABB(tri_index, aabb);
	return 0.5f * (aabb.min_[axis] + aabb.max_[axis]);
}


void SimpleBVH::triAABB(unsigned int tri_index, AABBox& aabb_out)
{
	const SSE_ALIGN PaddedVec3f v1(triVertPos(tri_index, 1));
	const SSE_ALIGN PaddedVec3f v2(triVertPos(tri_index, 2));

	aabb_out.min_ = aabb_out.max_ = triVertPos(tri_index, 0);
	aabb_out.enlargeToHoldAlignedPoint(v1);
	aabb_out.enlargeToHoldAlignedPoint(v2);
}


/*
The triangles [left, right) are considered to belong to this node.
The AABB for this node (build_nodes[node_index]) should be set already.
*/
void SimpleBVH::doBuild(const AABBox& aabb, std::vector<TRI_INDEX>& tris, int left, int right, unsigned int node_index, int depth)
{
	//assert(left >= 0 && left < (int)numTris() && right >= 0 && right < (int)numTris());
	assert(node_index < num_nodes);

	const int LEAF_NUM_TRI_THRESHOLD = 4;

	const int MAX_DEPTH = js::Tree::MAX_TREE_DEPTH;

	if(right - left <= LEAF_NUM_TRI_THRESHOLD || depth >= MAX_DEPTH)
	{
		markLeafNode(tris, node_index, left, right);

		// Update build stats
		if(depth >= MAX_DEPTH)
			num_maxdepth_leaves++;
		else
			num_under_thresh_leaves++;
		leaf_depth_sum += depth;
		max_leaf_depth = myMax(max_leaf_depth, depth);

		return;
	}

	SSE_ALIGN AABBox neg_aabb(
		Vec3f(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()),
		Vec3f(-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity())
		);

	SSE_ALIGN AABBox pos_aabb(
		Vec3f(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()),
		Vec3f(-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity())
		);
	
	int d = right-1;
	int i = left;

	const bool USE_SAH = true;
	if(USE_SAH)
	{
		// Compute best split plane
		const float traversal_cost = 1.0f;
		const float intersection_cost = 4.0f;
		const float aabb_surface_area = aabb.getSurfaceArea();
		const float recip_aabb_surf_area = 1.0f / aabb_surface_area;

		const unsigned int nonsplit_axes[3][2] = {{1, 2}, {0, 2}, {0, 1}};

		int best_axis = -1;
		float best_div_val = std::numeric_limits<float>::infinity();

		// Compute non-split cost
		float smallest_cost = (float)(right - left) * intersection_cost;

		centers.resize(right - left);

		// for each axis 0..2
		for(unsigned int axis=0; axis<3; ++axis)
		{
			// SAH stuff
			const unsigned int axis1 = nonsplit_axes[axis][0];
			const unsigned int axis2 = nonsplit_axes[axis][1];
			const float two_cap_area = (aabb.axisLength(axis1) * aabb.axisLength(axis2)) * 2.0f;
			const float circum = (aabb.axisLength(axis1) + aabb.axisLength(axis2)) * 2.0f;

			// Build list of triangle centers
			for(int i=left, z=0; i<right; ++i, ++z)
				centers[z] = triCenter(tris[i], axis); //(tri_centers[tris[i]])[axis];
			
			// Sort centers
			std::sort(centers.begin(), centers.end());
		
			// For Each triangle centroid
			for(unsigned int z=0; z<centers.size(); ++z)
			{
				const float splitval = centers[z];
				// Compute the SAH cost at the centroid position
				const int N_L = z + 1;
				const int N_R = centers.size() - (z + 1);

				assert(N_L >= 0 && N_L <= (int)centers.size() && N_R >= 0 && N_R <= (int)centers.size());

				const float negchild_surface_area = two_cap_area + (splitval - aabb.min_[axis]) * circum;
				const float poschild_surface_area = two_cap_area + (aabb.max_[axis] - splitval) * circum;

				const float cost = traversal_cost + ((float)N_L * negchild_surface_area + (float)N_R * poschild_surface_area) * 
							recip_aabb_surf_area * intersection_cost;

				if(cost < smallest_cost)
				{
					smallest_cost = cost;
					best_axis = axis;
					best_div_val = splitval;
				}
			}
		}

		if(best_axis == -1)
		{
			markLeafNode(tris, node_index, left, right);

			// Update build stats
			num_cheaper_nosplit_leaves++;
			leaf_depth_sum += depth;
			max_leaf_depth = myMax(max_leaf_depth, depth);
			return;
		}

		
		while(i <= d)
		{
			// Categorise triangle at i

			assert(i >= 0 && i < (int)numTris());
			assert(d >= 0 && d < (int)numTris());
			assert(tris[i] < numTris());

			// Get triangle AABB
			SSE_ALIGN AABBox tri_aabb;
			triAABB(tris[i], tri_aabb);

			if(/*(tri_centers[tris[i]])[best_axis]*/triCenter(tris[i], best_axis) > best_div_val)
			{
				// If mostly on positive side of split - categorise as R
				pos_aabb.enlargeToHoldAABBox(tri_aabb/*tri_aabbs[tris[i]]*/); // Update right AABB

				// Swap element at i with element at d
				mySwap(tris[i], tris[d]);

				assert(d >= 0);
				--d;
			}
			else
			{
				// Tri mostly on negative side of split - categorise as L
				neg_aabb.enlargeToHoldAABBox(tri_aabb/*tri_aabbs[tris[i]]*/); // Update left AABB
				++i;
			}
		}
		assert(i == d+1);
	}
	else // else if not using SAH
	{
		const unsigned int split_axis = aabb.longestAxis();
		const float split_val = 0.5f * (aabb.min_[split_axis] + aabb.max_[split_axis]);

		while(i <= d)
		{
			// Categorise triangle at i

			assert(i >= 0 && i < (int)numTris());
			assert(d >= 0 && d < (int)numTris());
			assert(tris[i] < numTris());

			// Get triangle AABB
			SSE_ALIGN AABBox tri_aabb;
			triAABB(tris[i], tri_aabb);

			const float tri_min = tri_aabb/*tri_aabbs[tris[i]]*/.min_[split_axis];
			const float tri_max = tri_aabb/*tri_aabbs[tris[i]]*/.max_[split_axis];
			if(tri_max - split_val >= split_val - tri_min)
			{
				// If mostly on positive side of split - categorise as R
				pos_aabb.enlargeToHoldAABBox(tri_aabb/*tri_aabbs[tris[i]]*/); // Update right AABB

				// Swap element at i with element at d
				mySwap(tris[i], tris[d]);

				assert(d >= 0);
				--d;
			}
			else
			{
				// Tri mostly on negative side of split - categorise as L
				neg_aabb.enlargeToHoldAABBox(tri_aabb/*tri_aabbs[tris[i]]*/); // Update left AABB
				++i;
			}
		}
		assert(i == d+1);
	}

	const int num_in_left = (d - left) + 1;
	const int num_in_right = right - i;//+1;
	assert(num_in_left >= 0);
	assert(num_in_right >= 0);
	assert(num_in_left + num_in_right == right - left);
	
	const unsigned int left_child_index = num_nodes;
	const unsigned int right_child_index = left_child_index + 1;

	nodes[node_index].leaf = 0;
	nodes[node_index].left_child_index = left_child_index;
	nodes[node_index].right_child_index = right_child_index;
	nodes[node_index].left_aabb = neg_aabb;
	nodes[node_index].right_aabb = pos_aabb;

	// Reserve space for children
	const unsigned int new_num_nodes = num_nodes + 2;
	if(new_num_nodes > nodes_capacity)
	{
		nodes_capacity *= 2;
		SimpleBVHNode* new_nodes;
		alignedArrayMalloc(nodes_capacity, SimpleBVHNode::requiredAlignment(), new_nodes); // Alloc new array
		memcpy(new_nodes, nodes, sizeof(SimpleBVHNode) * num_nodes); // Copy over old array
		alignedArrayFree(nodes); // Free old array
		nodes = new_nodes; // Update nodes pointer to point at new array
	}
	num_nodes = new_num_nodes; // Update size

	// Recurse to build left subtree
	doBuild(neg_aabb, tris, left, d + 1, left_child_index, depth+1);

	// Recurse to build right subtree
	doBuild(pos_aabb, tris, i, right, right_child_index, depth+1);
}


bool SimpleBVH::doesFiniteRayHit(const ::Ray& ray, double raylength, ThreadContext& thread_context, js::TriTreePerThreadData& context, const Object* object) const
{
	//NOTE: can speed this up
	HitInfo hitinfo;
	const double dist = traceRay(ray, raylength, thread_context, context, object, hitinfo);
	return dist >= 0.0 && dist < raylength;
}


double SimpleBVH::traceRay(const Ray& ray, double ray_max_t, ThreadContext& thread_context, js::TriTreePerThreadData& context, const Object* object, HitInfo& hitinfo_out) const
{
	assertSSEAligned(&ray);
	assert(ray.unitDir().isUnitLength());
	assert(ray_max_t >= 0.0);

	hitinfo_out.sub_elem_index = 0;
	hitinfo_out.sub_elem_coords.set(0.0, 0.0);

	float aabb_enterdist, aabb_exitdist;
	if(root_aabb->rayAABBTrace(ray.startPosF(), ray.getRecipRayDirF(), aabb_enterdist, aabb_exitdist) == 0)
		return -1.0; // Ray missed aabbox
	assert(aabb_enterdist <= aabb_exitdist);

	const float root_t_min = myMax(0.0f, aabb_enterdist);
	const float root_t_max = myMin((float)ray_max_t, aabb_exitdist);
	if(root_t_min > root_t_max)
		return -1.0; // Ray interval does not intersect AABB

	float closest_dist = std::numeric_limits<float>::infinity();

	context.nodestack[0] = StackFrame(0, root_t_min, root_t_max);

	int stacktop = 0; // Index of node on top of stack
	
	while(stacktop >= 0)
	{
		// Pop node off stack
		unsigned int current = context.nodestack[stacktop].node;
		__m128 tmin = _mm_load_ss(&context.nodestack[stacktop].tmin);
		__m128 tmax = _mm_load_ss(&context.nodestack[stacktop].tmax);

		tmax = _mm_min_ps(tmax, _mm_load_ss(&closest_dist));

		stacktop--;

		while(nodes[current].leaf == 0)
		{
			// Test ray against left child
			__m128 left_near_t, left_far_t;
			nodes[current].left_aabb.rayAABBTrace(ray.startPosF(), ray.getRecipRayDirF(), left_near_t, left_far_t);
			
			// Take the intersection of the current ray interval and the ray/BB interval
			left_near_t = _mm_max_ps(left_near_t, tmin);
			left_far_t = _mm_min_ps(left_far_t, tmax);

			// Test against right child
			__m128 right_near_t, right_far_t;
			nodes[current].right_aabb.rayAABBTrace(ray.startPosF(), ray.getRecipRayDirF(), right_near_t, right_far_t);
				
			// Take the intersection of the current ray interval and the ray/BB interval
			right_near_t = _mm_max_ps(right_near_t, tmin);
			right_far_t = _mm_min_ps(right_far_t, tmax);

			if(_mm_comile_ss(right_near_t, right_far_t) != 0) // If ray hits right AABB
			{
				if(_mm_comile_ss(left_near_t, left_far_t) != 0) // If ray hits left AABB
				{
					// Push right child onto stack
					stacktop++;
					assert(stacktop < context.nodestack_size);
					context.nodestack[stacktop].node = nodes[current].right_child_index;
					_mm_store_ss(&context.nodestack[stacktop].tmin, right_near_t);
					_mm_store_ss(&context.nodestack[stacktop].tmax, right_far_t);

					current = nodes[current].left_child_index; tmin = left_near_t; tmax = left_far_t; // next = L
				}
				else // Else ray missed left AABB, so process right child next
				{
					current = nodes[current].right_child_index; tmin = right_near_t; tmax = right_far_t; // next = R
				}
			}
			else // Else ray misssed right AABB
			{
				if(_mm_comile_ss(left_near_t, left_far_t) != 0) // If ray hits left AABB
				{
					current = nodes[current].left_child_index; tmin = left_near_t; tmax = left_far_t; // next = L
				}
				else
				{
					// ray missed both child AABBs.  So break to popping node off stack
					//break;
					goto after_tri_test;
				}
			}
		}

		// At this point, either the current node is a leaf, or we missed both children of an interior node.
		//if(nodes[current].leaf == 1)
		assert(nodes[current].leaf == 1);
		{
			// Test against leaf triangles

			unsigned int leaf_geom_index = nodes[current].geometry_index;
			const unsigned int num_leaf_tris = nodes[current].num_geom;

			for(unsigned int i=0; i<num_leaf_tris; ++i)
			{
				float u, v, raydist;
				if(intersect_tris[/*leafgeom[*/leaf_geom_index/*]*/].rayIntersect(ray, closest_dist, raydist, u, v))
				{
					closest_dist = raydist;
					hitinfo_out.sub_elem_index = leaf_geom_index; // leafgeom[leaf_geom_index];
					hitinfo_out.sub_elem_coords.set(u, v);
				}
				++leaf_geom_index;
			}
		}

after_tri_test:
		int dummy = 7; // dummy statement
	}

	if(closest_dist < std::numeric_limits<float>::infinity())
		return closest_dist;
	else
		return -1.0f; // Missed all tris
}


void SimpleBVH::getAllHits(const Ray& ray, ThreadContext& thread_context, js::TriTreePerThreadData& context, const Object* object, std::vector<DistanceHitInfo>& hitinfos_out) const
{
	hitinfos_out.resize(0);
}


const js::AABBox& SimpleBVH::getAABBoxWS() const
{
	return *root_aabb;
}


const Vec3f& SimpleBVH::triGeometricNormal(unsigned int tri_index) const //slow
{
	return intersect_tris[tri_index].getNormal();
}


} //end namespace js
