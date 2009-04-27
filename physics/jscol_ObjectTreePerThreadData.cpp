/*=====================================================================
ObjectTreePerThreadData.cpp
---------------------------
File created by ClassTemplate on Sun Jul 02 00:24:06 2006
Code By Nicholas Chapman.
=====================================================================*/
#include "jscol_ObjectTreePerThreadData.h"


#include "jscol_TriHash.h"


namespace js
{




ObjectTreePerThreadData::ObjectTreePerThreadData(/*bool root*//*int numobjects, int stacksize*/)
{
//	object_context = NULL;

	nodestack_size = 256;//stacksize;
	SSE::alignedSSEArrayMalloc(nodestack_size, nodestack);

	/*last_test_time.resize(numobjects);
	for(int i=0; i<numobjects; ++i)
		last_test_time[i] = -1;*/

	hits.reserve(20);

	time = 0;

//	if(root)
//		object_context = new ObjectTreePerThreadData(false);

}


ObjectTreePerThreadData::~ObjectTreePerThreadData()
{
	//delete object_context;

	SSE::alignedSSEFree(nodestack);
}






} //end namespace js






