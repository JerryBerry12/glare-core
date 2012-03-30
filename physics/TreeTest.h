/*=====================================================================
TreeTest.h
----------
File created by ClassTemplate on Tue Jun 26 20:19:05 2007
Code By Nicholas Chapman.
=====================================================================*/
#ifndef __TREETEST_H_666_
#define __TREETEST_H_666_


#include <string>


namespace js
{




/*=====================================================================
TreeTest
--------

=====================================================================*/
class TreeTest
{
public:
	/*=====================================================================
	TreeTest
	--------
	
	=====================================================================*/
	TreeTest();

	~TreeTest();


	static void doTests(const std::string& appdata_path);
	static void doSpeedTest(int treetype);
	static void doVaryingNumtrisBuildTests();
	static void buildSpeedTest();
	static void testBuildCorrect();
	static void doRayTests();
};



} //end namespace js


#endif //__TREETEST_H_666_




