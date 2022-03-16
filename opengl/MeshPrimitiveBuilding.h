/*=====================================================================
MeshPrimitiveBuilding.h
-----------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "../maths/vec3.h"
#include "../utils/Reference.h"
class Vec4f;
class Matrix4f;
class Colour4f;
struct GLObject;
class OpenGLMeshRenderData;
class VertexBufferAllocator;


/*=====================================================================
MeshPrimitiveBuilding
---------------------
=====================================================================*/
class MeshPrimitiveBuilding
{
public:
	static Reference<OpenGLMeshRenderData> makeLineMesh(VertexBufferAllocator& allocator);
	static Reference<OpenGLMeshRenderData> makeSphereMesh(VertexBufferAllocator& allocator);
	static Reference<OpenGLMeshRenderData> makeCubeMesh(VertexBufferAllocator& allocator);
	static Reference<OpenGLMeshRenderData> makeQuadMesh(VertexBufferAllocator& allocator, const Vec4f& i, const Vec4f& j);
	static Reference<OpenGLMeshRenderData> makeUnitQuadMesh(VertexBufferAllocator& allocator); // Makes a quad from (0, 0, 0) to (1, 1, 0)
	static Reference<OpenGLMeshRenderData> makeCapsuleMesh(VertexBufferAllocator& allocator, const Vec3f& bottom_spans, const Vec3f& top_spans);
	static Reference<OpenGLMeshRenderData> makeCylinderMesh(VertexBufferAllocator& allocator); // Make a cylinder from (0,0,0), to (0,0,1) with radius 1;
	static Reference<OpenGLMeshRenderData> make3DArrowMesh(VertexBufferAllocator& allocator);
	static Reference<OpenGLMeshRenderData> make3DBasisArrowMesh(VertexBufferAllocator& allocator);
	static Reference<GLObject> makeDebugHexahedron(VertexBufferAllocator& allocator, const Vec4f* verts_ws, const Colour4f& col);
};