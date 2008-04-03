/*=====================================================================
tritree.cpp
-----------
File created by ClassTemplate on Fri Nov 05 01:09:27 2004
Code By Nicholas Chapman.
=====================================================================*/
#include "jscol_tritree.h"

#include "jscol_treenode.h"
#include "jscol_TriHash.h"
#include "jscol_TriTreePerThreadData.h"
#include "../utils/stringutils.h"
#include "../indigo/globals.h"
#include "../raytracing/hitinfo.h"
#include "../indigo/FullHitInfo.h"
#include "../maths/SSE.h"
#include "../utils/platformutils.h"
#include "../simpleraytracer/raymesh.h"
#include "TreeUtils.h"
#include <zlib.h>
#include <limits>
#include <algorithm>
#include "../graphics/TriBoxIntersection.h"
#include "../indigo/TestUtils.h"
#include "../utils/timer.h" // TEMP

#ifdef USE_SSE
#define DO_PREFETCHING 1
#endif


namespace js
{

static void triTreeDebugPrint(const std::string& s)
{
	conPrint("\t" + s);
}

//#define RECORD_TRACE_STATS 1
#define CLIP_TRIANGLES 1
#define USE_LETTERBOX 1


TriTree::TriTree(RayMesh* raymesh_)
:	root_aabb(NULL)
{
//	nodes.setAlignment(8);

	checksum_ = 0;
	calced_checksum = false;
	intersect_tris = NULL;
	num_intersect_tris = 0;
	//max_depth = 0;
	num_cheaper_no_split_leafs = 0;
	num_inseparable_tri_leafs = 0;
	num_maxdepth_leafs = 0;
	num_under_thresh_leafs = 0;
//	build_checksum = 0;
	raymesh = raymesh_;//NULL;

	assert(sizeof(AABBox) == 32);
	root_aabb = (js::AABBox*)alignedMalloc(sizeof(AABBox), sizeof(AABBox));
	new(root_aabb) AABBox(Vec3f(0,0,0), Vec3f(0,0,0));

	assert(isAlignedTo(root_aabb, sizeof(AABBox)));

	nodes.push_back(TreeNode());//root node
	assert((uint64)&nodes[0] % 8 == 0);//assert aligned

	numnodesbuilt = 0;

	num_traces = 0;
	num_root_aabb_hits = 0;
	total_num_nodes_touched = 0;
	total_num_leafs_touched = 0;
	total_num_tris_intersected = 0;
	total_num_tris_considered = 0;
	num_empty_space_cutoffs = 0;
}


TriTree::~TriTree()
{
	alignedSSEFree(root_aabb);
	alignedSSEFree(intersect_tris);
	intersect_tris = NULL;
}

void TriTree::printTraceStats() const
{
	printVar(num_traces);
	conPrint("AABB hit fraction: " + toString((double)num_root_aabb_hits / (double)num_traces));
	conPrint("av num nodes touched: " + toString((double)total_num_nodes_touched / (double)num_traces));
	conPrint("av num leaves touched: " + toString((double)total_num_leafs_touched / (double)num_traces));
	conPrint("av num tris considered: " + toString((double)total_num_tris_considered / (double)num_traces));
	conPrint("av num tris tested: " + toString((double)total_num_tris_intersected / (double)num_traces));
}

//returns dist till hit tri, neg number if missed.
double TriTree::traceRay(const Ray& ray, double ray_max_t, js::TriTreePerThreadData& context, HitInfo& hitinfo_out) const
{
	assertSSEAligned(&ray);
	assert(ray.unitDir().isUnitLength());
	assert(ray_max_t >= 0.0);

#ifdef RECORD_TRACE_STATS
	this->num_traces++;
#endif

	hitinfo_out.hittriindex = 0;
	hitinfo_out.hittricoords.set(0.0, 0.0);

	assert(!nodes.empty());
	assert(root_aabb);

#ifdef DO_PREFETCHING
	// Prefetch first node
	// _mm_prefetch((const char *)(&nodes[0]), _MM_HINT_T0);	
#endif

	float aabb_enterdist, aabb_exitdist;
	if(root_aabb->rayAABBTrace(ray.startPosF(), ray.getRecipRayDirF(), aabb_enterdist, aabb_exitdist) == 0)
		return -1.0; // Ray missed aabbox
	assert(aabb_enterdist <= aabb_exitdist);

#ifdef RECORD_TRACE_STATS
	this->num_root_aabb_hits++;
#endif

	const float root_t_min = myMax(0.0f, aabb_enterdist);
	const float root_t_max = myMin((float)ray_max_t, aabb_exitdist);// * (1.1f + (float)NICKMATHS_EPSILON));
	if(root_t_min > root_t_max)
		return -1.0; // Ray interval does not intersect AABB

#ifdef USE_LETTERBOX
	context.tri_hash->clear();
#endif

	SSE_ALIGN unsigned int ray_child_indices[8];
	TreeUtils::buildFlatRayChildIndices(ray, ray_child_indices);

	REAL closest_dist = std::numeric_limits<float>::max();

	context.nodestack[0] = StackFrame(0, root_t_min, root_t_max);

	int stacktop = 0;//index of node on top of stack
	
	while(stacktop >= 0)
	{
		//pop node off stack
		unsigned int current = context.nodestack[stacktop].node;
		assert(current < nodes.size());
		float tmin = context.nodestack[stacktop].tmin;
		float tmax = context.nodestack[stacktop].tmax;
		assert(tmax >= tmin);

		stacktop--;

		//tmax = myMin(tmax, closest_dist);

		while(nodes[current].getNodeType() != TreeNode::NODE_TYPE_LEAF)//nodes[current].isLeafNode())//while current node is not a leaf..
		{
#ifdef RECORD_TRACE_STATS
			this->total_num_nodes_touched++;
#endif
			//prefetch child node memory
#ifdef DO_PREFETCHING
			//_mm_prefetch((const char *)(&nodes[current+1]), _MM_HINT_T0);	
			//_mm_prefetch((const char *)(&nodes[current+2]), _MM_HINT_T0);	
			//_mm_prefetch((const char *)(&nodes[current+3]), _MM_HINT_T0);	
			_mm_prefetch((const char *)(&nodes[nodes[current].getPosChildIndex()]), _MM_HINT_T0);	
			//_mm_prefetch((const char *)(node_zero_addr + nodes[current].getPosChildIndex()), _MM_HINT_T0);	
#endif			
	
			const unsigned int splitting_axis = nodes[current].getSplittingAxis();
			//const REAL t_split_old = (nodes[current].data2.dividing_val - ray.startPosF()[splitting_axis]) * ray.getRecipRayDirF()[splitting_axis];	

			//TEMP:
			const SSE4Vec t = mult4Vec(
				sub4Vec(
					loadScalarCopy(&nodes[current].data2.dividing_val), 
					load4Vec(&ray.startPosF().x)
					), 
				load4Vec(&ray.getRecipRayDirF().x)
				);

			const float t_split = t.m128_f32[splitting_axis];
		
			//const float diff = t_split_old - t_split;

			//assert(diff == 0.0f);

			


			const unsigned int child_nodes[2] = {current + 1, nodes[current].getPosChildIndex()};

			if(t_split > tmax) // whole interval is on near cell	
			{
				current = child_nodes[ray_child_indices[splitting_axis]];
			}
			else if(tmin > t_split) // whole interval is on far cell.
			{
				current = child_nodes[ray_child_indices[splitting_axis + 4]];//farnode;
			}
			else // ray hits plane - double recursion, into both near and far cells.
			{
				const unsigned int nearnode = child_nodes[ray_child_indices[splitting_axis]];
				const unsigned int farnode = child_nodes[ray_child_indices[splitting_axis + 4]];
					
				//push far node onto stack to process later
				stacktop++;
				assert(stacktop < context.nodestack_size);
				context.nodestack[stacktop] = StackFrame(farnode, t_split, tmax);
					
				//process near child next
				current = nearnode;
				tmax = t_split;
			}
		}//end while current node is not a leaf..

		//'current' is a leaf node..
	
#ifdef RECORD_TRACE_STATS
		this->total_num_leafs_touched++;
#endif
		//prefetch all data
		//for(int i=0; i<nodes[current].num_leaf_tris; ++i)
		//	_mm_prefetch((const char *)(leafgeom[nodes[current].positive_child + i]), _MM_HINT_T0);		

		unsigned int leaf_geom_index = nodes[current].getLeafGeomIndex();
		const unsigned int num_leaf_tris = nodes[current].getNumLeafGeom();

		for(unsigned int i=0; i<num_leaf_tris; ++i)
		{
#ifdef RECORD_TRACE_STATS
			total_num_tris_considered++;
#endif
			assert(leaf_geom_index < leafgeom.size());
			const unsigned int triangle_index = leafgeom[leaf_geom_index];

#ifdef USE_LETTERBOX
			if(!context.tri_hash->containsTriIndex(triangle_index)) //If this tri has not already been intersected against
			{
#endif
#ifdef RECORD_TRACE_STATS
				total_num_tris_intersected++;
#endif
				// Try prefetching next tri.
				//_mm_prefetch((const char *)((const char *)leaftri + sizeof(js::Triangle)*(triindex + 1)), _MM_HINT_T0);		

				float u, v, raydist;
				if(intersect_tris[triangle_index].rayIntersect(
					ray, 
					closest_dist, //max t NOTE: because we're using the tri hash, this can't just be the current leaf volume interval.
					raydist, //distance out
					u, v)) //hit uvs out
				{
					assert(raydist < closest_dist);

					closest_dist = raydist;
					hitinfo_out.hittriindex = triangle_index;
					hitinfo_out.hittricoords.set(u, v);
				}

#ifdef USE_LETTERBOX
				// Add tri index to hash of tris already intersected against
				context.tri_hash->addTriIndex(triangle_index);
			}
#endif
			leaf_geom_index++;
		}

		if(closest_dist <= tmax)
		{
			// If intersection point lies before ray exit from this leaf volume, then finished.
			return closest_dist;
		}

	}//end while stacktop >= 0

	//assert(closest_dist == std::numeric_limits<float>::max());
	//return -1.0; // Missed all tris
	//assert(closest_dist == std::numeric_limits<float>::max() || Maths::inRange(closest_dist, aabb_exitdist, aabb_exitdist + (float)NICKMATHS_EPSILON));
	return closest_dist < std::numeric_limits<float>::max() ? closest_dist : -1.0;
		
}






void TriTree::getAllHits(const Ray& ray, js::TriTreePerThreadData& context, std::vector<FullHitInfo>& hitinfos_out) const
{
	assertSSEAligned(&ray);

	assert(!nodes.empty());
	assert(root_aabb);	
	
	hitinfos_out.resize(0);

	float aabb_enterdist, aabb_exitdist;
	if(root_aabb->rayAABBTrace(ray.startPosF(), ray.getRecipRayDirF(), aabb_enterdist, aabb_exitdist) == 0)
		return; //missed aabbox

#ifdef USE_LETTERBOX
	context.tri_hash->clear();
#endif



	//const float MIN_TMIN = 0.00000001f;//above zero to avoid denorms
	//TEMP:
	//aabb_enterdist = myMax(aabb_enterdist, MIN_TMIN);

	//assert(aabb_enterdist >= 0.0f);
	//assert(aabb_exitdist >= aabb_enterdist);

	const float root_t_min = myMax(0.0f, aabb_enterdist);
	const float root_t_max = aabb_exitdist; //myMin((float)ray_max_t, aabb_exitdist);// * (1.1f + (float)NICKMATHS_EPSILON));
	if(root_t_min > root_t_max)
		return; // Ray interval does not intersect AABB

	REAL closest_dist = std::numeric_limits<float>::max();

	//const REAL initial_closest_dist = aabb_exitdist + 0.01f;
	//REAL closest_dist = initial_closest_dist;//(REAL)2.0e9;//closest hit on a tri so far.

	context.nodestack[0] = StackFrame(0, root_t_min, root_t_max);

	int stacktop = 0;//index of node on top of stack
	
	while(stacktop >= 0)//!nodestack.empty())
	{
		//pop node off stack
		unsigned int current = context.nodestack[stacktop].node;
		float tmin = context.nodestack[stacktop].tmin;
		float tmax = context.nodestack[stacktop].tmax;

		stacktop--;

		while(nodes[current].getNodeType() != TreeNode::NODE_TYPE_LEAF)//!nodes[current].isLeafNode())
		{
			//------------------------------------------------------------------------
			//prefetch child node memory
			//------------------------------------------------------------------------
#ifdef DO_PREFETCHING
			_mm_prefetch((const char *)(&nodes[nodes[current].getPosChildIndex()]), _MM_HINT_T0);	
#endif			
			//while current node is not a leaf..
	
			const unsigned int splitting_axis = nodes[current].getSplittingAxis();

			//signed dist to split plane along ray direction from ray origin
			const REAL axis_sgnd_dist = nodes[current].data2.dividing_val - ray.startPosF()[splitting_axis];
			const REAL sgnd_dist_to_split = axis_sgnd_dist * ray.getRecipRayDirF()[splitting_axis];			

			//nearnode is the halfspace that the ray origin is in
			unsigned int nearnode;
			unsigned int farnode;
			if(axis_sgnd_dist > (REAL)0.0)
			{
				nearnode = current + 1;//= neg child index
				farnode = nodes[current].getPosChildIndex();
			}
			else
			{
				nearnode = nodes[current].getPosChildIndex();
				farnode = current + 1;//= neg child index
			}

//TEMP WAS FAILING TOO MUCH			assert(tmin >= 0.0f);

			if(sgnd_dist_to_split < (REAL)0.0 || //if heading away from split
				sgnd_dist_to_split > tmax)//or ray segment ends b4 split
			{
				//whole interval is on near cell	
				
				//process near child next
				current = nearnode;
			}
			else
			{
				if(tmin > sgnd_dist_to_split)//else if ray seg starts after split
				{
					//whole interval is on far cell.
					current = farnode;
				}
				else
				{
					//ray hits plane - double recursion, into both near and far cells.
					
					//push far node onto stack to process later
					stacktop++;
					assert(stacktop < context.nodestack_size);
					context.nodestack[stacktop] = StackFrame(farnode, sgnd_dist_to_split, tmax);
					
					//process near child next
					current = nearnode;			

					tmax = sgnd_dist_to_split;
				}
			}

		}//end while current node is not a leaf..

		//'current' is a leaf node..

		//------------------------------------------------------------------------
		//intersect with leaf tris
		//------------------------------------------------------------------------
		unsigned int triindex = nodes[current].getLeafGeomIndex();//positive_child;

		//lookup the number of leaf tris
		const unsigned int num_leaf_tris = nodes[current].getNumLeafGeom();

		for(unsigned int i=0; i<num_leaf_tris; ++i)
		{
			assert(triindex >= 0 && triindex < leafgeom.size());

			//If this tri has not already been intersected against
			if(!context.tri_hash->containsTriIndex(leafgeom[triindex]))
			{
				float u, v, raydist;
				if(intersect_tris[leafgeom[triindex]].rayIntersect(ray, closest_dist, raydist, u, v))
				{
					assert(raydist < closest_dist);

					// Check to see if we have already recorded the hit against this triangle (while traversing another leaf volume)
					// NOTE that this is a slow linear time check, but N should be small, hopefully :)
					bool already_got_hit = false;
					for(unsigned int z=0; z<hitinfos_out.size(); ++z)
						if(hitinfos_out[z].hittri_index == leafgeom[triindex])//if tri index is the same
							already_got_hit = true;

					if(!already_got_hit)
					{
						hitinfos_out.push_back(FullHitInfo());
						hitinfos_out.back().hittri_index = leafgeom[triindex];
						hitinfos_out.back().tri_coords.set(u, v);
						hitinfos_out.back().dist = raydist;
						hitinfos_out.back().hitpos = ray.startPos();
						hitinfos_out.back().hitpos.addMult(ray.unitDir(), raydist);
					}

					//NOTE: this looks fully bogus :(
					/*const int last_hit_index = hitinfos_out.size() - 1;
					if(last_hit_index >= 0 && hitinfos_out[last_hit_index].dist == raydist)
					{
						//then we already have this hit
					}
					else
					{
						hitinfos_out.push_back(FullHitInfo());
						hitinfos_out.back().hittri_index = leafgeom[triindex];
						hitinfos_out.back().tri_coords.set(u, v);
						hitinfos_out.back().dist = raydist;
						hitinfos_out.back().hitpos = ray.startpos;
						hitinfos_out.back().hitpos.addMult(ray.unitdir, raydist);
					}*/
				}

				//Add tri index to hash of tris already intersected against
				context.tri_hash->addTriIndex(leafgeom[triindex]);
			}
			triindex++;
		}
	}//end while !bundlenodestack.empty()
}







bool TriTree::doesFiniteRayHit(const ::Ray& ray, double raylength, js::TriTreePerThreadData& context) const
{
	assertSSEAligned(&ray);
	assert(ray.unitDir().isUnitLength());
	assert(raylength > 0.0);

	// Intersect ray with AABBox
	float aabb_enterdist, aabb_exitdist;
	if(root_aabb->rayAABBTrace(ray.startPosF(), ray.getRecipRayDirF(), aabb_enterdist, aabb_exitdist) == 0)
		return false; // missed bounding box

	const float root_t_min = myMax(0.0f, aabb_enterdist);
	const float root_t_max = myMin((float)raylength, aabb_exitdist);
	if(root_t_min > root_t_max)
		return false; // Ray interval does not intersect AABB

	SSE_ALIGN unsigned int ray_child_indices[8];
	TreeUtils::buildFlatRayChildIndices(ray, ray_child_indices);

#ifdef USE_LETTERBOX
	context.tri_hash->clear();
#endif

	context.nodestack[0] = StackFrame(0, root_t_min, root_t_max);
	int stacktop = 0; // index of node on top of stack

	while(stacktop >= 0)
	{
		unsigned int current = context.nodestack[stacktop].node;
		assert(current < nodes.size());
		float tmin = context.nodestack[stacktop].tmin;
		float tmax = context.nodestack[stacktop].tmax;

		stacktop--; //pop current node off stack

		while(nodes[current].getNodeType() != TreeNode::NODE_TYPE_LEAF) //while current node is not a leaf..
		{
#ifdef DO_PREFETCHING
			//_mm_prefetch((const char *)(&nodes[current+1]), _MM_HINT_T0);	
			_mm_prefetch((const char *)(&nodes[nodes[current].getPosChildIndex()]), _MM_HINT_T0);	
#endif			
			const unsigned int splitting_axis = nodes[current].getSplittingAxis();
			//const REAL t_split = (nodes[current].data2.dividing_val - ray.startPosF()[splitting_axis]) * ray.getRecipRayDirF()[splitting_axis];	
			const SSE4Vec t = mult4Vec(
				sub4Vec(
					loadScalarCopy(&nodes[current].data2.dividing_val), 
					load4Vec(&ray.startPosF().x)
					), 
				load4Vec(&ray.getRecipRayDirF().x)
				);

			const float t_split = t.m128_f32[splitting_axis];
			const unsigned int child_nodes[2] = {current + 1, nodes[current].getPosChildIndex()};

			if(t_split > tmax) // whole interval is on near cell	
			{
				current = child_nodes[ray_child_indices[splitting_axis]];
			}
			else if(tmin > t_split) // whole interval is on far cell.
			{
				current = child_nodes[ray_child_indices[splitting_axis + 4]];//farnode;
			}
			else // ray hits plane - double recursion, into both near and far cells.
			{
				const unsigned int nearnode = child_nodes[ray_child_indices[splitting_axis]];
				const unsigned int farnode = child_nodes[ray_child_indices[splitting_axis + 4]];
					
				//push far node onto stack to process later
				stacktop++;
				assert(stacktop < context.nodestack_size);
				context.nodestack[stacktop] = StackFrame(farnode, t_split, tmax);
					
				// process near child next
				current = nearnode;
				tmax = t_split;
			}
		} // end while current node is not a leaf..

		// 'current' is a leaf node..

		unsigned int leaf_geom_index = nodes[current].getLeafGeomIndex(); //get index into leaf geometry array
		const unsigned int num_leaf_tris = nodes[current].getNumLeafGeom();

		for(unsigned int i=0; i<num_leaf_tris; ++i)
		{
			assert(leaf_geom_index < leafgeom.size());
			const unsigned int triangle_index = leafgeom[leaf_geom_index]; //get the actual intersection triangle index

#ifdef USE_LETTERBOX
			//If this tri has not already been intersected against
			if(!context.tri_hash->containsTriIndex(triangle_index))
			{
#endif
				//assert(tmax <= raylength);
				float u, v, dummy_hitdist;
				if(intersect_tris[triangle_index].rayIntersect(ray, 
					(float)raylength, // raylength is better than tmax, because we don't mind if we hit a tri outside of this leaf volume, we can still return now.
					dummy_hitdist, u, v))
				{
					return true;
				}
				
#ifdef USE_LETTERBOX
				//Add tri index to hash of tris already intersected against
				context.tri_hash->addTriIndex(triangle_index);
			}
#endif
			leaf_geom_index++;
		}
	}
	return false;
}


const Vec3f& TriTree::triVertPos(unsigned int tri_index, unsigned int vert_index_in_tri) const
{
	return raymesh->triVertPos(tri_index, vert_index_in_tri);
}

unsigned int TriTree::numTris() const
{
	return raymesh->getNumTris();
}

/*
void TriTree::AABBoxForTri(unsigned int tri_index, AABBox& aabbox_out)
{
	aabbox_out.min_ = aabbox_out.max_ = triVertPos(tri_index, 0);//(*tris)[i].v0();
	aabbox_out.enlargeToHoldPoint(triVertPos(tri_index, 1));//(*tris)[i].v1());
	aabbox_out.enlargeToHoldPoint(triVertPos(tri_index, 2));//(*tris)[i].v2());

	assert(aabbox_out.invariant());
}*/


bool TriTree::diskCachable()
{
	return true;
}

const js::AABBox& TriTree::getAABBoxWS() const
{
	assert(this->root_aabb);
	return *root_aabb;
}

unsigned int TriTree::calcMaxDepth() const
{
	return MAX_KDTREE_DEPTH - 1;
}

void TriTree::build()
{
	triTreeDebugPrint("Building kd-tree...");

	if(numTris() == 0)
		throw TreeExcep("Error, tried to build tree with zero triangles.");

	try
	{
		//------------------------------------------------------------------------
		//calc root node's aabbox
		//------------------------------------------------------------------------
		triTreeDebugPrint("calcing root AABB.");
		{
		root_aabb->min_ = triVertPos(0, 0);
		root_aabb->max_ = triVertPos(0, 0);
		for(unsigned int i=0; i<numTris(); ++i)
			for(int v=0; v<3; ++v)
			{
				const SSE_ALIGN PaddedVec3 vert = triVertPos(i, v);
				root_aabb->enlargeToHoldAlignedPoint(vert);
			}
		}

		triTreeDebugPrint("AABB: (" + ::toString(root_aabb->min_.x) + ", " + ::toString(root_aabb->min_.y) + ", " + ::toString(root_aabb->min_.z) + "), " + 
							"(" + ::toString(root_aabb->max_.x) + ", " + ::toString(root_aabb->max_.y) + ", " + ::toString(root_aabb->max_.z) + ")"); 
								
		assert(root_aabb->invariant());

		//------------------------------------------------------------------------
		//compute max allowable depth
		//------------------------------------------------------------------------
		//const int numtris = tris->size();
		//max_depth = (int)(2.0f + logBase2((float)numtris) * 1.2f);
	/*TEMP	max_depth = myMin(
			(int)(2.0f + logBase2((float)numTris()) * 2.0f),
			(int)MAX_KDTREE_DEPTH-1
			);*/
		//max_depth = (int)MAX_KDTREE_DEPTH-1;


		const unsigned int max_depth = calcMaxDepth();
		triTreeDebugPrint("max tree depth: " + ::toString(max_depth));

		const int expected_numnodes = (int)((float)numTris() * 1.0f);
		const int nodemem = expected_numnodes * sizeof(js::TreeNode);

		//triTreeDebugPrint("reserving N nodes: " + ::toString(expected_numnodes) + "(" 
		//	+ ::getNiceByteSize(nodemem) + ")");
		//nodes.reserve(expected_numnodes);

		//------------------------------------------------------------------------
		//reserve space for leaf geom array
		//------------------------------------------------------------------------
		leafgeom.reserve(numTris() * 4);

		//const int leafgeommem = leafgeom.capacity() * sizeof(TRI_INDEX);
		//triTreeDebugPrint("leafgeom reserved: " + ::toString(leafgeom.capacity()) + "("
		//	+ ::getNiceByteSize(leafgeommem) + ")");

		{ // Scope for various arrays
		std::vector<std::vector<TriInfo> > node_tri_layers(MAX_KDTREE_DEPTH); // One list of tris for each depth

		//for(int t=0; t<MAX_KDTREE_DEPTH; ++t)
		//	node_tri_layers[t].reserve(1000);
		
		node_tri_layers[0].resize(numTris());
		for(unsigned int i=0; i<numTris(); ++i)
		{
			node_tri_layers[0][i].tri_index = i;

			// Get tri AABB
			SSE_ALIGN AABBox tri_aabb(triVertPos(i, 0), triVertPos(i, 0));
			const SSE_ALIGN PaddedVec3 v1 = triVertPos(i, 1);
			tri_aabb.enlargeToHoldAlignedPoint(v1);
			const SSE_ALIGN PaddedVec3 v2 = triVertPos(i, 2);
			tri_aabb.enlargeToHoldAlignedPoint(v2);

			node_tri_layers[0][i].lower = tri_aabb.min_;
			node_tri_layers[0][i].upper = tri_aabb.max_;
		}

		std::vector<SortedBoundInfo> lower(numTris());
		std::vector<SortedBoundInfo> upper(numTris());
		doBuild(0, node_tri_layers, 0, max_depth, *root_aabb, lower, upper);
		}

		if(!nodes.empty())
		{
			assert(isAlignedTo(&nodes[0], sizeof(js::TreeNode)));
		}

		const unsigned int numnodes = (unsigned int)nodes.size();
		const unsigned int leafgeomsize = (unsigned int)leafgeom.size();

		triTreeDebugPrint("total nodes used: " + ::toString(numnodes) + " (" + 
			::getNiceByteSize(numnodes * sizeof(js::TreeNode)) + ")");

		triTreeDebugPrint("total leafgeom size: " + ::toString(leafgeomsize) + " (" + 
			::getNiceByteSize(leafgeomsize * sizeof(TRI_INDEX)) + ")");

		//------------------------------------------------------------------------
		//alloc intersect tri array
		//------------------------------------------------------------------------
		num_intersect_tris = numTris();
		triTreeDebugPrint("Allocing intersect triangles...");
		if(::atDebugLevel(DEBUG_LEVEL_VERBOSE))
			triTreeDebugPrint("intersect_tris mem usage: " + ::getNiceByteSize(num_intersect_tris * sizeof(INTERSECT_TRI_TYPE)));

		::alignedSSEArrayMalloc(num_intersect_tris, intersect_tris);

		for(unsigned int i=0; i<num_intersect_tris; ++i)
			intersect_tris[i].set(triVertPos(i, 0), triVertPos(i, 1), triVertPos(i, 2));

		const unsigned int total_tree_mem_usage = numnodes * sizeof(js::TreeNode) + leafgeomsize * sizeof(TRI_INDEX) + num_intersect_tris * sizeof(INTERSECT_TRI_TYPE);
		triTreeDebugPrint("Total tree mem usage: " + ::getNiceByteSize(total_tree_mem_usage));

		postBuild();
	}
	catch(std::bad_alloc& )
	{
		throw TreeExcep("Memory allocation failed while building kd-tree");
	}
}



void TriTree::buildFromStream(std::istream& stream)
{
	if(numTris() == 0)
		throw TreeExcep("Error, tried to build tree with zero triangles.");

	try
	{
		triTreeDebugPrint("Loading kd-tree from cache...");

		//------------------------------------------------------------------------
		//alloc intersect tri array
		//------------------------------------------------------------------------
		num_intersect_tris = numTris();
		::alignedSSEArrayMalloc(num_intersect_tris, intersect_tris);

		// Build intersect tris
		for(unsigned int i=0; i<num_intersect_tris; ++i)
			intersect_tris[i].set(triVertPos(i, 0), triVertPos(i, 1), triVertPos(i, 2));

		if(::atDebugLevel(DEBUG_LEVEL_VERBOSE))
			triTreeDebugPrint("intersect_tris mem usage: " + ::getNiceByteSize(num_intersect_tris * sizeof(js::BadouelTri)));

		//------------------------------------------------------------------------
		//calc root node's aabbox
		//------------------------------------------------------------------------
		triTreeDebugPrint("calcing root AABB.");
		{
		root_aabb->min_ = triVertPos(0, 0);
		root_aabb->max_ = triVertPos(0, 0);
		for(unsigned int i=0; i<num_intersect_tris; ++i)
			for(unsigned int v=0; v<3; ++v)
			{
				const SSE_ALIGN PaddedVec3 vert = triVertPos(i, v);
				root_aabb->enlargeToHoldAlignedPoint(vert);
			}
				
		}

		triTreeDebugPrint("AABB: (" + ::toString(root_aabb->min_.x) + ", " + ::toString(root_aabb->min_.y) + ", " + ::toString(root_aabb->min_.z) + "), " + 
							"(" + ::toString(root_aabb->max_.x) + ", " + ::toString(root_aabb->max_.y) + ", " + ::toString(root_aabb->max_.z) + ")"); 
								
		assert(root_aabb->invariant());

		// Compute max allowable depth
		const unsigned int max_depth = calcMaxDepth();

		triTreeDebugPrint("max tree depth: " + ::toString(max_depth));

		//------------------------------------------------------------------------
		// Read nodes
		//------------------------------------------------------------------------
		// Read num nodes
		unsigned int num_nodes;
		stream.read((char*)&num_nodes, sizeof(unsigned int));

		// Read actual node data
		nodes.resize(num_nodes);
		stream.read((char*)&nodes[0], sizeof(js::TreeNode) * num_nodes);

		//------------------------------------------------------------------------
		// Read leafgeom
		//------------------------------------------------------------------------
		// Read num leaf geom
		unsigned int leaf_geom_count;
		stream.read((char*)&leaf_geom_count, sizeof(unsigned int));

		// Read actual leaf geom data
		leafgeom.resize(leaf_geom_count);
		stream.read((char*)&leafgeom[0], sizeof(TRI_INDEX) * leaf_geom_count);


		triTreeDebugPrint("total nodes used: " + ::toString(num_nodes) + " (" + 
			::getNiceByteSize(num_nodes * sizeof(js::TreeNode)) + ")");

		triTreeDebugPrint("total leafgeom size: " + ::toString(leaf_geom_count) + " (" + 
			::getNiceByteSize(leaf_geom_count * sizeof(TRI_INDEX)) + ")");

		const unsigned int total_tree_mem_usage = num_nodes * sizeof(js::TreeNode) + leaf_geom_count * sizeof(TRI_INDEX) + num_intersect_tris * sizeof(INTERSECT_TRI_TYPE);
		triTreeDebugPrint("Total tree mem usage: " + ::getNiceByteSize(total_tree_mem_usage));

		postBuild();
	}
	catch(std::bad_alloc& )
	{
		throw TreeExcep("Memory allocation failed while loading kd-tree from disk");
	}

}

void TriTree::postBuild() const
{
	//triTreeDebugPrint("finished building tree.");

	//------------------------------------------------------------------------
	//TEMP: check things are aligned
	//------------------------------------------------------------------------
	if((uint64)(&nodes[0]) % 8 != 0)
		conPrint("Warning: nodes are not aligned properly.");

	/*printVar((uint64)&nodes[0]);
	printVar((uint64)(&nodes[0]) % 8);
	printVar((uint64)&intersect_tris[0]);
	printVar((uint64)&intersect_tris[0] % 16);
	printVar((uint64)&leafgeom[0]);
	printVar((uint64)&leafgeom[0] % 4);
	printVar((uint64)&root_aabb[0]);
	printVar((uint64)&root_aabb[0] % 32);*/
}



static inline bool SortedBoundInfoLowerPred(const TriTree::SortedBoundInfo& a, const TriTree::SortedBoundInfo& b)
{
   return a.lower < b.lower;
}

static inline bool SortedBoundInfoUpperPred(const TriTree::SortedBoundInfo& a, const TriTree::SortedBoundInfo& b)
{
   return a.upper < b.upper;
}



//precondition: node_tri_layers[depth] is valid and contains all tris for this volume.

void TriTree::doBuild(unsigned int cur, //index of current node getting built
						std::vector<std::vector<TriInfo> >& node_tri_layers,
						unsigned int depth, //depth of current node
						unsigned int maxdepth, //max permissible depth
						const AABBox& cur_aabb, //AABB of current node
						std::vector<SortedBoundInfo>& upper, 
						std::vector<SortedBoundInfo>& lower)
{
	// Get the list of tris intersecting this volume
	assert(depth < (unsigned int)node_tri_layers.size());
	const std::vector<TriInfo>& nodetris = node_tri_layers[depth];

	assert(cur < (int)nodes.size());

	// Print progress message
	if(depth == 5)
	{
		triTreeDebugPrint(::toString(numnodesbuilt) + "/" + ::toString(1 << 5) + " nodes at depth 5 built.");
		numnodesbuilt++;
	}
	
	//------------------------------------------------------------------------
	//test for termination of splitting
	//------------------------------------------------------------------------
	const int SPLIT_THRESHOLD = 2;
	if(nodetris.size() <= SPLIT_THRESHOLD || depth >= maxdepth)
	{
		// Make this node a leaf node.
		nodes[cur] = TreeNode(
			(unsigned int)leafgeom.size(), // Leaf geom index
			(unsigned int)nodetris.size() // num leaf geom
			);

		for(unsigned int i=0; i<nodetris.size(); ++i)
			leafgeom.push_back(nodetris[i].tri_index);

		// Do stats
		if(depth >= maxdepth)
			num_maxdepth_leafs++;
		else
			num_under_thresh_leafs++;
		return;
	}

	const float traversal_cost = 1.0f;
	const float intersection_cost = 4.0f;
	const float aabb_surface_area = cur_aabb.getSurfaceArea();
	const float recip_aabb_surf_area = 1.0f / aabb_surface_area;

	int best_axis = -1;
	float best_div_val = -std::numeric_limits<float>::max();
	int best_num_in_neg = 0;
	int best_num_in_pos = 0;
	//int best_num_on_split_plane = 0;
	bool best_push_right = false;

	float best_cutoff_frac = 0.f;
	float best_cutoff_splitval = -666.0f;
	int best_cutoff_axis = -1;
	bool best_cutoff_push_right = false;

	//------------------------------------------------------------------------
	//compute non-split cost
	//------------------------------------------------------------------------
	float smallest_cost = (float)nodetris.size() * intersection_cost;

	const unsigned int numtris = (unsigned int)nodetris.size();

	const unsigned int nonsplit_axes[3][2] = {{1, 2}, {0, 2}, {0, 1}};

	//------------------------------------------------------------------------
	//Find Best axis and split plane value
	//------------------------------------------------------------------------
	for(int axis=0; axis<3; ++axis)
	{
		const unsigned int axis1 = nonsplit_axes[axis][0];
		const unsigned int axis2 = nonsplit_axes[axis][1];
		const float two_cap_area = (cur_aabb.axisLength(axis1) * cur_aabb.axisLength(axis2)) * 2.0f;
		const float circum = (cur_aabb.axisLength(axis1) + cur_aabb.axisLength(axis2)) * 2.0f;

		//if(two_cap_area == 0.0)
		//	continue;
		if(cur_aabb.axisLength(axis) == 0.0f)
			continue; // Don't try to split a zero-width bounding box
		
		//------------------------------------------------------------------------
		//Sort lower and upper tri AABB bounds into ascending order
		//------------------------------------------------------------------------
		for(unsigned int i=0; i<numtris; ++i)
		{
//			assert(nodetris[i].lower[axis] >= cur_aabb.min_[axis] && nodetris[i].upper[axis] <= cur_aabb.max_[axis]);
			assert(nodetris[i].lower[axis] <= nodetris[i].upper[axis]);

			lower[i].lower = nodetris[i].lower[axis];
			lower[i].upper = nodetris[i].upper[axis];
			upper[i].lower = nodetris[i].lower[axis];
			upper[i].upper = nodetris[i].upper[axis];
		}

		Timer sort_timer;

		//Note that we don't want to sort the whole buffers here, as we're only using the first numtris elements.
		std::sort(lower.begin(), lower.begin() + numtris, SortedBoundInfoLowerPred); //lower.end());
		std::sort(upper.begin(), upper.begin() + numtris, SortedBoundInfoUpperPred); //upper.end());

		conPrint("sort took " + toString(sort_timer.getSecondsElapsed()));

		// Try explicit 'Early empty-space cutoff', see section 4.4 of Havran's thesis
		// 'Construction of Kd-Trees with Utilization of Empty Spatial Regions'
		const float lower_cutoff_frac = myClamp((lower[0].lower - cur_aabb.min_[axis]) / cur_aabb.axisLength(axis), 0.f, 1.f);
		const float upper_cutoff_frac = myClamp((cur_aabb.max_[axis] - upper[numtris-1].upper) / cur_aabb.axisLength(axis), 0.f, 1.f);
		assert(Maths::inRange(lower_cutoff_frac, 0.0f, 1.0f));
		assert(Maths::inRange(upper_cutoff_frac, 0.0f, 1.0f));
		if(lower_cutoff_frac > best_cutoff_frac)
		{
			best_cutoff_frac = lower_cutoff_frac;
			best_cutoff_splitval = lower[0].lower;
			best_cutoff_axis = axis;
			best_cutoff_push_right = true;
		}
		if(upper_cutoff_frac > best_cutoff_frac)
		{
			best_cutoff_frac = upper_cutoff_frac;
			best_cutoff_splitval = upper[numtris-1].upper;
			best_cutoff_axis = axis;
			best_cutoff_push_right = false;
		}


		// Tris in contact with splitting plane will not imply tri is in either volume,
		// except for tris lying totally on splitting plane which are considered to be in positive volume.

		unsigned int upper_index = 0;
		//Index of first triangle in upper volume
		//Index of first tri with upper bound >= then current split val.
		//Also the number of tris with upper bound <= current split val.
		//which is the number of tris *not* in pos volume

		float last_splitval = -std::numeric_limits<float>::max();
		for(unsigned int i=0; i<numtris; ++i)
		{
			const float splitval = lower[i].lower;
			//assert(splitval >= cur_aabb.min_[axis] && splitval <= cur_aabb.max_[axis]);

			/*int num_on_splitting_plane = 0;
			while(i<numtris && lower[i].lower == splitval)
			{
				// If the tri has zero extent along the current axis
				if(lower[i].upper == splitval)
					num_on_splitting_plane++;

				i++;
			}*/

			if(splitval != last_splitval)//only consider first tri seen with a given lower bound.
			{
				if(splitval >= cur_aabb.min_[axis] && splitval <= cur_aabb.max_[axis]) // If split val is actually in AABB
				{
				// Get number of tris on splitting plane
				int num_on_splitting_plane = 0;
				for(unsigned int z=i; z<numtris && lower[z].lower == splitval; ++z) // for all other triangles that share the current splitting plane as a lower bound
					if(lower[z].upper == splitval) // If the tri has zero extent along the current axis
						num_on_splitting_plane++;

				//if(splitval > cur_aabb.min_[axis] && splitval < cur_aabb.max_[axis]) // If split val is actually in AABB
				//{
					//advance upper index to maintain invariant above
					//while(upper_index < numtris && upper[upper_index] < splitval) //<= splitval)
					//	upper_index++;

					//advance upper_index while it points to triangles with upper < split (definitely in negative volume)
					/*while(upper_index < numtris && upper[upper_index] < splitval)
						upper_index++;

					unsigned int num_coplanar_tris = 0;
					//advance upper_index while it points to triangles with upper == split, which may be coplanar and hence in positive volume, if lower == split as well.
					while(upper_index < numtris && upper[upper_index] == splitval)
					{
						if(lower[upper_index] == splitval)
							num_coplanar_tris++;
						upper_index++;
					}

					assert(upper_index == numtris || upper[upper_index] > splitval);

					const int num_in_neg = i;//upper_index;
					const int num_in_pos = num_coplanar_tris + numtris - upper_index;//numtris - i;
					assert(num_in_neg >= 0 && num_in_neg <= (int)numtris);
					assert(num_in_pos >= 0 && num_in_pos <= (int)numtris);
					assert(num_in_neg + num_in_pos >= (int)numtris);*/

				while(upper_index < numtris && upper[upper_index].upper <= splitval)
					upper_index++;

				assert(upper_index == numtris || upper[upper_index].upper > splitval);

				const int num_in_neg = i;
				const int num_in_pos = numtris - upper_index;
				assert(num_in_neg >= 0 && num_in_neg <= (int)numtris);
				assert(num_in_pos >= 0 && num_in_pos <= (int)numtris);
				assert(num_in_neg + num_in_pos + num_on_splitting_plane >= (int)numtris);

				//const int num_on_splitting_plane = numtris - (num_in_neg + num_in_pos);



				const float negchild_surface_area = two_cap_area + (splitval - cur_aabb.min_[axis]) * circum;
				const float poschild_surface_area = two_cap_area + (cur_aabb.max_[axis] - splitval) * circum;
				assert(negchild_surface_area >= 0.f && negchild_surface_area <= aabb_surface_area * (1.0f + NICKMATHS_EPSILON));
				assert(poschild_surface_area >= 0.f && negchild_surface_area <= aabb_surface_area * (1.0f + NICKMATHS_EPSILON));
				assert(::epsEqual(negchild_surface_area + poschild_surface_area - two_cap_area, aabb_surface_area, aabb_surface_area * 1.0e-6f));

				if(num_on_splitting_plane == 0)
				{
					const float cost = traversal_cost + ((float)num_in_neg * negchild_surface_area + (float)num_in_pos * poschild_surface_area) * 
						recip_aabb_surf_area * intersection_cost;
					assert(cost >= 0.0);

					if(cost < smallest_cost)
					{
						smallest_cost = cost;
						best_axis = axis;
						best_div_val = splitval;
						best_num_in_neg = num_in_neg;
						best_num_in_pos = num_in_pos;
						//best_num_on_split_plane = 0;
					}
				}
				else
				{
					// Try pushing tris on splitting plane to left
					const float push_left_cost = traversal_cost + ((float)(num_in_neg + num_on_splitting_plane) * negchild_surface_area + (float)num_in_pos * poschild_surface_area) * 
						recip_aabb_surf_area * intersection_cost;
					assert(push_left_cost >= 0.0);

					if(push_left_cost < smallest_cost)
					{
						smallest_cost = push_left_cost;
						best_axis = axis;
						best_div_val = splitval;
						best_num_in_neg = num_in_neg + num_on_splitting_plane;
						best_num_in_pos = num_in_pos;
						//best_num_on_split_plane = 0;
						best_push_right = false;
					}

					// Try pushing tris on splitting plane to right
					const float push_right_cost = traversal_cost + ((float)num_in_neg * negchild_surface_area + (float)(num_in_pos + num_on_splitting_plane) * poschild_surface_area) * 
						recip_aabb_surf_area * intersection_cost;
					assert(push_right_cost >= 0.0);

					if(push_right_cost < smallest_cost)
					{
						smallest_cost = push_right_cost;
						best_axis = axis;
						best_div_val = splitval;
						best_num_in_neg = num_in_neg;
						best_num_in_pos = num_in_pos + num_on_splitting_plane;
						//best_num_on_split_plane = 0;
						best_push_right = true;
					}
				}

				}

				last_splitval = splitval;
			}
		}
		unsigned int lower_index = 0;//index of first tri with lower bound >= current split val
		//Also the number of tris that are in the negative volume

		for(unsigned int i=0; i<numtris; ++i)
		{
			// Advance to greatest index with given upper bound
			while(i+1 < numtris && upper[i].upper == upper[i+1].upper) // While triangle i has some upper bound as triangle i+1
				++i;


			const float splitval = upper[i].upper;
			//assert(splitval >= cur_aabb.min_[axis] && splitval <= cur_aabb.max_[axis]);
			
			if(splitval >= cur_aabb.min_[axis] && splitval <= cur_aabb.max_[axis])
			{

			int num_on_splitting_plane = 0;
			for(int z=i; z>=0 && upper[z].upper == splitval; --z) // For each triangle sharing an upper bound with the current triangle
				if(upper[z].lower == splitval) // If tri has zero extent along this axis
					num_on_splitting_plane++;

			/*unsigned int num_coplanar_tris = lower[i] == upper[i] ? 1 : 0;
			//if tri i and tri i+1 share upper bounds, advance to largest index of tris sharing same upper bound
			while(i<numtris-1 && upper[i] == upper[i+1])
			{
				++i;
				if(lower[i] == upper[i])
					num_coplanar_tris++;
			}


			assert(i < upper.size());
			const float splitval = upper[i];
			
			//if(splitval != last_splitval)
			//{
				//if(splitval > cur_aabb.min_[axis] && splitval < cur_aabb.max_[axis])
				//{
					//advance lower index to maintain invariant above
					while(lower_index < numtris && lower[lower_index] < splitval)
						lower_index++;

					assert(lower_index == numtris || lower[lower_index] >= splitval);

					const int num_in_neg = lower_index;//i;
					const int num_in_pos = num_coplanar_tris + numtris - (i + 1);//numtris - lower_index;
					assert(num_in_neg >= 0 && num_in_neg <= (int)numtris);
					assert(num_in_pos >= 0 && num_in_pos <= (int)numtris);
					assert(num_in_neg + num_in_pos >= (int)numtris);*/

			while(lower_index < numtris && lower[lower_index].lower < splitval)
				lower_index++;

			// Postcondition:
			assert(lower_index == numtris || lower[lower_index].lower >= splitval);

			const int num_in_neg = lower_index;
			const int num_in_pos = numtris - (i + 1);
			assert(num_in_neg >= 0 && num_in_neg <= (int)numtris);
			assert(num_in_pos >= 0 && num_in_pos <= (int)numtris);
			assert(num_in_neg + num_in_pos + num_on_splitting_plane >= (int)numtris);

			//const int num_on_splitting_plane = numtris - (num_in_neg + num_in_pos);

			const float negchild_surface_area = two_cap_area + (splitval - cur_aabb.min_[axis]) * circum;
			const float poschild_surface_area = two_cap_area + (cur_aabb.max_[axis] - splitval) * circum;
			assert(negchild_surface_area >= 0.f && negchild_surface_area <= aabb_surface_area * (1.0f + NICKMATHS_EPSILON));
			assert(poschild_surface_area >= 0.f && negchild_surface_area <= aabb_surface_area * (1.0f + NICKMATHS_EPSILON));
			assert(::epsEqual(negchild_surface_area + poschild_surface_area - two_cap_area, aabb_surface_area, aabb_surface_area * 1.0e-6f));

			if(num_on_splitting_plane == 0)
			{
				const float cost = traversal_cost + ((float)num_in_neg * negchild_surface_area + (float)num_in_pos * poschild_surface_area) * 
					recip_aabb_surf_area * intersection_cost;
				assert(cost >= 0.0);

				if(cost < smallest_cost)
				{
					smallest_cost = cost;
					best_axis = axis;
					best_div_val = splitval;
					best_num_in_neg = num_in_neg;
					best_num_in_pos = num_in_pos;
					//best_num_on_split_plane = 0;
				}
			}
			else
			{
				// Try pushing tris on splitting plane to left
				const float push_left_cost = traversal_cost + ((float)(num_in_neg + num_on_splitting_plane) * negchild_surface_area + (float)num_in_pos * poschild_surface_area) * 
					recip_aabb_surf_area * intersection_cost;
				assert(push_left_cost >= 0.0);

				if(push_left_cost < smallest_cost)
				{
					smallest_cost = push_left_cost;
					best_axis = axis;
					best_div_val = splitval;
					best_num_in_neg = num_in_neg + num_on_splitting_plane;
					best_num_in_pos = num_in_pos;
					//best_num_on_split_plane = 0;
					best_push_right = false;
				}

				// Try pushing tris on splitting plane to right
				const float push_right_cost = traversal_cost + ((float)num_in_neg * negchild_surface_area + (float)(num_in_pos + num_on_splitting_plane) * poschild_surface_area) * 
					recip_aabb_surf_area * intersection_cost;
				assert(push_right_cost >= 0.0);

				if(push_right_cost < smallest_cost)
				{
					smallest_cost = push_right_cost;
					best_axis = axis;
					best_div_val = splitval;
					best_num_in_neg = num_in_neg;
					best_num_in_pos = num_in_pos + num_on_splitting_plane;
					//best_num_on_split_plane = 0;
					best_push_right = true;
				}
			}

			}

					/*const float negchild_surface_area = two_cap_area + (splitval - cur_aabb.min_[axis]) * circum;
					const float poschild_surface_area = two_cap_area + (cur_aabb.max_[axis] - splitval) * circum;
					assert(negchild_surface_area >= 0.f && negchild_surface_area <= aabb_surface_area + NICKMATHS_EPSILON);
					assert(poschild_surface_area >= 0.f && negchild_surface_area <= aabb_surface_area + NICKMATHS_EPSILON);
					assert(::epsEqual(negchild_surface_area + poschild_surface_area - two_cap_area, aabb_surface_area, aabb_surface_area * (float)1.0e-6));

					const float cost = traversal_cost + 
						((float)num_in_neg * negchild_surface_area + (float)num_in_pos * poschild_surface_area) * recip_aabb_surf_area * intersection_cost;

					assert(cost >= 0.0);

					if(cost < smallest_cost)
					{
						smallest_cost = cost;
						best_axis = axis;
						best_div_val = splitval;
						best_num_in_neg = num_in_neg;
						best_num_in_pos = num_in_pos;
					}*/
				//}
				//last_splitval = splitval;
			//}
		}
	}

	// TEMP NEW: Do empty space cutoff
	if(best_cutoff_frac > 0.4f)
	{
		best_axis = best_cutoff_axis;
		best_div_val = best_cutoff_splitval;
		best_push_right = best_cutoff_push_right;
		if(best_cutoff_push_right)
		{
			best_num_in_neg = 0;
			best_num_in_pos = numtris;
		}
		else
		{
			best_num_in_neg = numtris;
			best_num_in_pos = 0;
		}
		num_empty_space_cutoffs++;
	}

	if(best_axis == -1)
	{
		// If the least cost is to not split the node, then make this node a leaf node
		nodes[cur] = TreeNode(
			(unsigned int)leafgeom.size(),
			(unsigned int)nodetris.size()
			);

		for(unsigned int i=0; i<nodetris.size(); ++i)
			leafgeom.push_back(nodetris[i].tri_index);

		num_cheaper_no_split_leafs++;
		return;
	}

	assert(best_axis >= 0 && best_axis <= 2);
	assert(best_div_val >= cur_aabb.min_[best_axis]);
	assert(best_div_val <= cur_aabb.max_[best_axis]);
	
	//------------------------------------------------------------------------
	//compute AABBs of child nodes
	//------------------------------------------------------------------------
	SSE_ALIGN AABBox negbox(cur_aabb.min_, cur_aabb.max_);
	negbox.max_[best_axis] = best_div_val;

	SSE_ALIGN AABBox posbox(cur_aabb.min_, cur_aabb.max_);
	posbox.min_[best_axis] = best_div_val;

	if(best_num_in_neg == numtris && best_num_in_pos == numtris)
	{
		// If we were unable to get a reduction in the number of tris in either of the children,
		// then splitting is pointless.  So make this a leaf node.
		nodes[cur] = TreeNode(
			(unsigned int)leafgeom.size(),
			(unsigned int)nodetris.size()
			);

		for(unsigned int i=0; i<(unsigned int)nodetris.size(); ++i)
			leafgeom.push_back(nodetris[i].tri_index);

		num_inseparable_tri_leafs++;
		return;
	}

	assert(depth + 1 < (int)node_tri_layers.size());
	std::vector<TriInfo>& child_tris = node_tri_layers[depth + 1];

	//------------------------------------------------------------------------
	// Build list of neg tris
	//------------------------------------------------------------------------
	child_tris.resize(0);
	child_tris.reserve(myMax(best_num_in_neg, best_num_in_pos));

	assert(numtris == nodetris.size());
	const float split = best_div_val;
	for(unsigned int i=0; i<numtris; ++i)//for each tri
	{
		const float tri_lower = nodetris[i].lower[best_axis];
		const float tri_upper = nodetris[i].upper[best_axis];

		/*if(tri_upper <= split)//does tri lie entirely in min volume (including splitting plane)?
		{
			// Tri is either in neg box, or on splitting plane
			if(tri_lower < split)
				child_tris.push_back(nodetris[i]);
		}
		else // else tri_upper > split
		{
			if(tri_lower < split)
			{
				// Tri lies in both boxes
				child_tris.push_back(TriInfo());
				child_tris.back().tri_index = nodetris[i].tri_index;

				SSE_ALIGN AABBox clipped_tri_aabb;
				TriBoxIntersection::slowGetClippedTriAABB(
					triVertPos(nodetris[i].tri_index, 0), triVertPos(nodetris[i].tri_index, 1), triVertPos(nodetris[i].tri_index, 2),
					negbox,
					clipped_tri_aabb
					);
				assert(negbox.containsAABBox(clipped_tri_aabb));
				child_tris.back().lower = clipped_tri_aabb.min_;
				child_tris.back().upper = clipped_tri_aabb.max_;
			}
		}*/
		if(tri_lower <= split) // Tri touches left volume, including splitting plane
		{
			if(tri_lower < split) // Tri touches left volume, not including splitting plane
			{
				if(tri_upper > split)
				{
					// Tri straddles split plane
#ifdef CLIP_TRIANGLES
					child_tris.push_back(TriInfo());
					child_tris.back().tri_index = nodetris[i].tri_index;

					SSE_ALIGN AABBox clipped_tri_aabb;
					TriBoxIntersection::getClippedTriAABB(
						triVertPos(nodetris[i].tri_index, 0), triVertPos(nodetris[i].tri_index, 1), triVertPos(nodetris[i].tri_index, 2),
						negbox,
						clipped_tri_aabb
						);
					assert(negbox.containsAABBox(clipped_tri_aabb));
					assert(clipped_tri_aabb.invariant());
					child_tris.back().lower = clipped_tri_aabb.min_;
					child_tris.back().upper = clipped_tri_aabb.max_;
#else
					child_tris.push_back(nodetris[i]);
#endif
				}
				else // else tri_upper <= split
					child_tris.push_back(nodetris[i]); // Tri is only in left child
			}
			else
			{
				// else tri_lower == split
				if(tri_upper == split && !best_push_right)
					child_tris.push_back(nodetris[i]);
			}
		}
		// Else tri_lower > split, so doesn't intersect left box of splitting plane.

	}

	//conPrint("Finished binning neg tris, depth=" + toString(depth) + ", size=" + toString(child_tris.size()) + ", capacity=" + toString(child_tris.capacity()));
	assert(child_tris.size() == best_num_in_neg);

	//------------------------------------------------------------------------
	//create negative child node, next in the array.
	//------------------------------------------------------------------------
	nodes.push_back(TreeNode());

	doBuild(
		(unsigned int)nodes.size() - 1, // current index
		node_tri_layers, 
		depth + 1, 
		maxdepth, 
		negbox, 
		lower, 
		upper
		);

	//------------------------------------------------------------------------
	// Build list of pos tris
	//------------------------------------------------------------------------
	child_tris.resize(0);

	assert(numtris == nodetris.size());
	//assert(best_axis == nodes[cur].getSplittingAxis());
	for(unsigned int i=0; i<numtris; ++i) // For each tri
	{
		const float tri_lower = nodetris[i].lower[best_axis];
		const float tri_upper = nodetris[i].upper[best_axis];

		/*if(tri_upper <= split) // Does tri lie entirely in min volume (including splitting plane)?
		{
			// Tri is either in neg box, or on splitting plane
			if(tri_lower >= split)
				child_tris.push_back(nodetris[i]);//tri lies entirely on splitting plane.  so assign it to positive half.
		}
		else // else tri_upper > split
		{
			if(tri_lower < split)
			{
				// tri straddles splitting plane
				child_tris.push_back(TriInfo());
				child_tris.back().tri_index = nodetris[i].tri_index;

				SSE_ALIGN AABBox clipped_tri_aabb;
				TriBoxIntersection::slowGetClippedTriAABB(
					triVertPos(nodetris[i].tri_index, 0), triVertPos(nodetris[i].tri_index, 1), triVertPos(nodetris[i].tri_index, 2),
					posbox,
					clipped_tri_aabb
					);
				assert(posbox.containsAABBox(clipped_tri_aabb));
				child_tris.back().lower = clipped_tri_aabb.min_;
				child_tris.back().upper = clipped_tri_aabb.max_;
			}
			else
				child_tris.push_back(nodetris[i]); // Tri lies totally in positive half
		}*/

		if(tri_upper >= split) // Tri touches right volume, including splitting plane
		{
			if(tri_upper > split) // Tri touches right volume, not including splitting plane
			{
				if(tri_lower < split)
				{
					// Tri straddles splitting plane
#ifdef CLIP_TRIANGLES
					child_tris.push_back(TriInfo());
					child_tris.back().tri_index = nodetris[i].tri_index;

					SSE_ALIGN AABBox clipped_tri_aabb;
					TriBoxIntersection::getClippedTriAABB(
						triVertPos(nodetris[i].tri_index, 0), triVertPos(nodetris[i].tri_index, 1), triVertPos(nodetris[i].tri_index, 2),
						posbox,
						clipped_tri_aabb
						);
					assert(posbox.containsAABBox(clipped_tri_aabb));
					assert(clipped_tri_aabb.invariant());
					child_tris.back().lower = clipped_tri_aabb.min_;
					child_tris.back().upper = clipped_tri_aabb.max_;
#else

					child_tris.push_back(nodetris[i]);
#endif
				}
				else // else tri_lower >= split
					child_tris.push_back(nodetris[i]); // Tri is only in right child
			}
			else
			{
				// else tri_upper == split
				if(tri_lower == split && best_push_right)
					child_tris.push_back(nodetris[i]);
			}
		}
		// Else tri_upper < split, so doesn't intersect right box of splitting plane.
	}

	assert(child_tris.size() == best_num_in_pos);

	//conPrint("Finished binning pos tris, depth=" + toString(depth) + ", size=" + toString(child_tris.size()) + ", capacity=" + toString(child_tris.capacity()));

	//------------------------------------------------------------------------
	//create positive child
	//------------------------------------------------------------------------
	nodes.push_back(TreeNode());
	
	// Set details of current node
	nodes[cur] = TreeNode(
		best_axis, // split axis
		best_div_val, // split value
		(unsigned int)nodes.size() - 1 // right child index
		);

	// Build right subtree
	doBuild(
		nodes[cur].getPosChildIndex(), 
		node_tri_layers, 
		depth + 1, 
		maxdepth, 
		posbox, 
		lower, 
		upper
		);
}




















void TriTree::printTree(unsigned int cur, unsigned int depth, std::ostream& out)
{
	if(nodes[cur].getNodeType() == TreeNode::NODE_TYPE_LEAF)//nodes[cur].isLeafNode())
	{
		for(unsigned int i=0; i<depth; ++i)
			out << "  ";
//		out << "leaf node (num leaf tris: " << nodes[cur].num_leaf_tris << ")\n";
	}
	else
	{	
		//process neg child
		this->printTree(cur + 1, depth + 1, out);

		for(unsigned int i=0; i<depth; ++i)
			out << "  ";
//		out << "interior node (split axis: "  << nodes[cur].dividing_axis << ", split val: "
//				<< nodes[cur].dividing_val << ")\n";
		//process pos child
		this->printTree(nodes[cur].getPosChildIndex(), depth + 1, out);
	}
}

void TriTree::debugPrintTree(unsigned int cur, unsigned int depth)
{
	if(nodes[cur].getNodeType() == TreeNode::NODE_TYPE_LEAF)//nodes[cur].isLeafNode())
	{
		std::string lineprefix;
		for(unsigned int i=0; i<depth; ++i)
			lineprefix += " ";

		triTreeDebugPrint(lineprefix + "leaf node");// (num leaf tris: " + nodes[cur].num_leaf_tris + ")");
	}
	else
	{	
		//process neg child
		this->debugPrintTree(cur + 1, depth + 1);

		std::string lineprefix;
		for(unsigned int i=0; i<depth; ++i)
			lineprefix += " ";

		triTreeDebugPrint(lineprefix + "interior node (split axis: " + ::toString(nodes[cur].getSplittingAxis()) + ", split val: "
				+ ::toString(nodes[cur].data2.dividing_val) + ")");

		//process pos child
		this->debugPrintTree(nodes[cur].getPosChildIndex(), depth + 1);
	}
}



void TriTree::getTreeStats(TreeStats& stats_out, unsigned int cur, unsigned int depth) const
{
	if(nodes.empty())
		return;


	if(nodes[cur].getNodeType() == TreeNode::NODE_TYPE_LEAF)//nodes[cur].isLeafNode())
	{
		stats_out.num_leaf_nodes++;
		stats_out.num_leaf_geom_tris += (int)nodes[cur].getNumLeafGeom();
		stats_out.average_leafnode_depth += (double)depth;

		stats_out.leaf_geom_counts[nodes[cur].getNumLeafGeom()]++;
	}
	else
	{
		stats_out.num_interior_nodes++;

		//------------------------------------------------------------------------
		//recurse to children
		//------------------------------------------------------------------------
		this->getTreeStats(stats_out, cur + 1, depth + 1);
		this->getTreeStats(stats_out, nodes[cur].getPosChildIndex(), depth + 1);
	}

	if(cur == 0)
	{
		//if this is the root node
		assert(depth == 0);

		stats_out.total_num_nodes = (int)nodes.size();
		stats_out.num_tris = num_intersect_tris;

		stats_out.average_leafnode_depth /= (double)stats_out.num_leaf_nodes;
		stats_out.average_numgeom_per_leafnode = (double)stats_out.num_leaf_geom_tris / (double)stats_out.num_leaf_nodes;
	
		stats_out.max_depth = calcMaxDepth();//max_depth;

		stats_out.total_node_mem = (int)nodes.size() * sizeof(TreeNode);
		stats_out.leafgeom_indices_mem = stats_out.num_leaf_geom_tris * sizeof(TRI_INDEX);
		stats_out.tri_mem = num_intersect_tris * sizeof(INTERSECT_TRI_TYPE);

		stats_out.num_cheaper_no_split_leafs = this->num_cheaper_no_split_leafs;
		stats_out.num_inseparable_tri_leafs = this->num_inseparable_tri_leafs;
		stats_out.num_maxdepth_leafs = this->num_maxdepth_leafs;
		stats_out.num_under_thresh_leafs = this->num_under_thresh_leafs;
		stats_out.num_empty_space_cutoffs = this->num_empty_space_cutoffs;

		//stats_out.leaf_geom_counts = this->leaf_geom_counts;
	}
}



	//returns dist till hit tri, neg number if missed.
double TriTree::traceRayAgainstAllTris(const ::Ray& ray, double t_max, HitInfo& hitinfo_out) const
{
	hitinfo_out.hittriindex = 0;
	hitinfo_out.hittricoords.set(0.f, 0.f);

	double closest_dist = std::numeric_limits<double>::max(); //2e9f;//closest hit so far, also upper bound on hit dist

	for(unsigned int i=0; i<num_intersect_tris; ++i)
	{
		float u, v, raydist;
		//raydist is distance until hit if hit occurred
				
		if(intersect_tris[i].rayIntersect(ray, (float)closest_dist, raydist, u, v))
		{			
			assert(raydist < closest_dist);

			closest_dist = raydist;
			hitinfo_out.hittriindex = i;
			hitinfo_out.hittricoords.set(u, v);
		}
	}

	if(closest_dist > t_max)
		closest_dist = -1.0;

	return closest_dist;
}

void TriTree::getAllHitsAllTris(const Ray& ray, std::vector<FullHitInfo>& hitinfos_out) const
{
	for(unsigned int i=0; i<num_intersect_tris; ++i)
	{
		float u, v, raydist;
		//raydist is distance until hit if hit occurred
				
		if(intersect_tris[i].rayIntersect(ray, std::numeric_limits<float>::max(), raydist, u, v))
		{			
			hitinfos_out.push_back(FullHitInfo());
			hitinfos_out.back().dist = raydist;
			hitinfos_out.back().hittri_index = i;
			hitinfos_out.back().tri_coords.set(u, v);
		}
	}
}


js::TriTreePerThreadData* allocPerThreadData()
{
	return new js::TriTreePerThreadData();
}

void TriTree::saveTree(std::ostream& stream)
{
	// Write checksum
	const unsigned int the_checksum = checksum();
	stream.write((const char*)&the_checksum, sizeof(unsigned int));

	//------------------------------------------------------------------------
	//write nodes
	//------------------------------------------------------------------------
	// Write number of nodes
	unsigned int temp = (unsigned int)nodes.size();
	stream.write((const char*)&temp, sizeof(unsigned int));
	
	// Write actual node data
	stream.write((const char*)&nodes[0], sizeof(js::TreeNode)*(unsigned int)nodes.size());

	//------------------------------------------------------------------------
	//write leafgeom
	//------------------------------------------------------------------------
	// Write number of leafgeom
	temp = leafgeom.size();
	stream.write((const char*)&temp, sizeof(unsigned int));

	// Write actual leafgeom data
	stream.write((const char*)&leafgeom[0], sizeof(TRI_INDEX)*leafgeom.size());

}

// Checksum over triangle vertex positions
unsigned int TriTree::checksum()
{
	if(calced_checksum)
		return checksum_;

	std::vector<Vec3f> buf(numTris() * 3);
	for(unsigned int t=0; t<numTris(); ++t)
		for(unsigned int v=0; v<3; ++v)
		{
			buf[t * 3 + v] = triVertPos(t, v);
		}

	checksum_ = crc32(0, (const Bytef*)(&buf[0]), sizeof(Vec3f) * (unsigned int)buf.size());
	calced_checksum = true;
	return checksum_;
}




void TriTree::printStats() const
{
	TreeStats stats;
	getTreeStats(stats);
	stats.print();
}

void TriTree::test()
{

	{
	TreeNode n(
		2, //axis
		666.0, //split
		43 // right child index
		);

	testAssert(n.getNodeType() != TreeNode::NODE_TYPE_LEAF);
	testAssert(n.getSplittingAxis() == 2);
	testAssert(n.data2.dividing_val == 666.0);
	testAssert(n.getPosChildIndex() == 43);
	}
	{
	TreeNode n(
		67, // leaf geom index
		777 // num leaf geom
		);

	testAssert(n.getNodeType() == TreeNode::NODE_TYPE_LEAF);
	testAssert(n.getLeafGeomIndex() == 67);
	testAssert(n.getNumLeafGeom() == 777);
	}


}












TreeStats::TreeStats()
{
	total_num_nodes = 0;//total number of nodes, both interior and leaf
	num_interior_nodes = 0;
	num_leaf_nodes = 0;
	num_tris = 0;//number of triangles stored
	num_leaf_geom_tris = 0;//number of references to tris held in leaf nodes
	average_leafnode_depth = 0;//average depth in tree of leaf nodes, where depth of 0 is root level.
	average_numgeom_per_leafnode = 0;//average num tri refs per leaf node
	max_depth = 0;

	total_node_mem = 0;
	leafgeom_indices_mem = 0;
	tri_mem = 0;
}

TreeStats::~TreeStats()
{
}


void TreeStats::print()
{
	conPrint("Nodes");
	conPrint("\tTotal num nodes: " + toString(total_num_nodes));
	conPrint("\tTotal num interior nodes: " + toString(num_interior_nodes));
	conPrint("\tTotal num leaf nodes: " + toString(num_leaf_nodes));

	printVar(num_tris);
	printVar(num_leaf_geom_tris);

	printVar(total_node_mem);
	printVar(leafgeom_indices_mem);
	printVar(tri_mem);

	conPrint("num_empty_space_cutoffs: " + toString(num_empty_space_cutoffs));

	conPrint("Build termination reasons:");
	conPrint("\tCheaper to not split: " + toString(num_cheaper_no_split_leafs));
	conPrint("\tInseperable: " + toString(num_inseparable_tri_leafs));
	conPrint("\tReached max depth: " + toString(num_maxdepth_leafs));
	conPrint("\t<=tris per leaf threshold: " + toString(num_under_thresh_leafs));

	conPrint("Depth Statistics:");
	conPrint("\tmax depth limit: " + toString(max_depth));
	conPrint("\tAverage leaf depth: " + toString(average_leafnode_depth));


	conPrint("Number of leaves with N triangles:");
	/*int sum = 0;
	for(int i=0; i<(int)leaf_geom_counts.size(); ++i)
	{
		sum += leaf_geom_counts[i];
		conPrint("\tN=" + toString(i) + ": " + toString(leaf_geom_counts[i]));
	}
	conPrint("\tN>=" + toString((unsigned int)leaf_geom_counts.size()) + ": " + toString(num_leaf_nodes - sum));
	conPrint("\tAverage N: " + toString(average_numgeom_per_leafnode));*/
	for(std::map<int, int>::iterator i=leaf_geom_counts.begin(); i!=leaf_geom_counts.end(); ++i)
	{
		conPrint("\tN=" + toString((*i).first) + ": " + toString((*i).second));
	}

}




} //end namespace jscol















