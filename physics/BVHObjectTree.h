/*=====================================================================
BVHObjectTree.h
-------------------
Copyright Glare Technologies Limited 2015 -
Generated at 2012-11-10 19:47:31 +0000
=====================================================================*/
#pragma once


#include "../maths/Vec4f.h"
#include "../utils/Platform.h"
#include "../utils/Vector.h"
#include <vector>
namespace Indigo { class TaskManager; }
class Object;
class PrintOutput;
class ThreadContext;
class HitInfo;
class Ray;


class BVHObjectTreeNode
{
public:
	Vec4f x; // (left_min_x, right_min_x, left_max_x, right_max_x)
	Vec4f y; // (left_min_y, right_min_y, left_max_y, right_max_y)
	Vec4f z; // (left_min_z, right_min_z, left_max_z, right_max_z)

	int32 child[2];
	int32 padding[2]; // Pad to 64 bytes.
};


/*=====================================================================
BVHObjectTree
-------------------

=====================================================================*/
class BVHObjectTree
{
public:
	BVHObjectTree();
	~BVHObjectTree();

	friend class BVHObjectTreeCallBack;

	typedef float Real;


	//void insertObject(const Object* object);
	
	// hitob_out will be set to a non-null value if the ray hit somethig in the interval, and null otherwise.
	Real traceRay(const Ray& ray, Real ray_length, ThreadContext& thread_context, double time,
		const Object*& hitob_out, HitInfo& hitinfo_out) const;

	void build(Indigo::TaskManager& task_manager, PrintOutput& print_output, bool verbose);

//private:
	int32 root_node_index;
	js::Vector<const Object*, 16> objects;
	js::Vector<BVHObjectTreeNode, 64> nodes;
	js::Vector<const Object*, 16> leaf_objects;
};
