/*=====================================================================
BatchedMesh.cpp
---------------
Copyright Glare Technologies Limited 2020
=====================================================================*/
#include "BatchedMesh.h"


#include "../dll/include/IndigoMesh.h"
#include "../maths/vec3.h"
#include "../maths/vec2.h"
#include "../utils/StringUtils.h"
#include "../utils/Exception.h"
#include "../utils/Sort.h"
#include "../utils/FileUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/Timer.h"
#include "../utils/FileInStream.h"
#include "../utils/FileOutStream.h"
#include "../utils/IncludeHalf.h"
#include "../utils/HashMapInsertOnly2.h"
#include "../utils/BufferViewInStream.h"
#include <limits>
#include <zstd.h>


BatchedMesh::BatchedMesh()
{
}


BatchedMesh::~BatchedMesh()
{
}


struct BMeshTakeFirstElement
{
	inline uint32 operator() (const std::pair<uint32, uint32>& pair) const { return pair.first; }
};


struct BMeshVertKey
{
	Indigo::Vec3f pos;
	Indigo::Vec3f normal;
	Indigo::Vec2f uv0;
	Indigo::Vec2f uv1;

	inline bool operator == (const BMeshVertKey& b) const
	{
		return pos == b.pos && normal == b.normal && uv0 == b.uv0 && uv1 == b.uv1;
	}
	inline bool operator != (const BMeshVertKey& b) const
	{
		return pos != b.pos || normal != b.normal || uv0 != b.uv0 || uv1 != b.uv1;
	}
};


struct BMeshVertKeyHash
{
	size_t operator() (const BMeshVertKey& v) const
	{
		return hashBytes((const uint8*)&v.pos, sizeof(Vec3f));
	}
};


inline static uint32 BMeshPackNormal(const Indigo::Vec3f& normal)
{
	return batchedMeshPackNormal(Vec4f(normal.x, normal.y, normal.z, 0));
}


inline static const Indigo::Vec3f BMeshUnpackNormal(const uint32 packed_normal)
{
	const uint32 x_bits = (packed_normal >> 0 ) & 1023;
	const uint32 y_bits = (packed_normal >> 10) & 1023;
	const uint32 z_bits = (packed_normal >> 20) & 1023;

	const int x = convertToSigned(x_bits);
	const int y = convertToSigned(y_bits);
	const int z = convertToSigned(z_bits);

	return Indigo::Vec3f(x * (1.f / 511.f), y * (1.f / 511.f), z * (1.f / 511.f));
}


// Requires dest and src to be 4-byte aligned.
// size is in bytes.
inline static void copyUInt32s(void* const dest, const void* const src, size_t size_B)
{
	assert(((uint64)dest % 4 == 0) && ((uint64)src % 4 == 0));

	const size_t num_uints = size_B / 4;

	for(size_t z=0; z<num_uints; ++z)
		*((uint32*)dest + z) = *((const uint32*)src + z);
}


// Adapted from OpenGLEngine::buildIndigoMesh().
void BatchedMesh::buildFromIndigoMesh(const Indigo::Mesh& mesh_)
{
	const Indigo::Mesh* const mesh				= &mesh_;
	const Indigo::Triangle* const tris			= mesh->triangles.data();
	const size_t num_tris						= mesh->triangles.size();
	const Indigo::Quad* const quads				= mesh->quads.data();
	const size_t num_quads						= mesh->quads.size();
	const Indigo::Vec3f* const vert_positions	= mesh->vert_positions.data();
	const size_t vert_positions_size			= mesh->vert_positions.size();
	const Indigo::Vec3f* const vert_normals		= mesh->vert_normals.data();
	const Indigo::Vec3f* const vert_colours		= mesh->vert_colours.data();
	const Indigo::Vec2f* const uv_pairs			= mesh->uv_pairs.data();
	const size_t uvs_size						= mesh->uv_pairs.size();

	const bool mesh_has_shading_normals			= !mesh->vert_normals.empty();
	const bool mesh_has_uvs						= mesh->num_uv_mappings > 0;
	const bool mesh_has_uv1						= mesh->num_uv_mappings > 1;
	const uint32 num_uv_sets					= mesh->num_uv_mappings;
	const bool mesh_has_vert_cols				= !mesh->vert_colours.empty();


	// If UVs are somewhat small in magnitude, use GL_HALF_FLOAT instead of GL_FLOAT.
	// If the magnitude is too high we can get artifacts if we just use half precision.
	const bool use_half_uvs = false; // TEMP canUseHalfUVs(mesh);

	const size_t pos_size = sizeof(float)*3;
	const size_t packed_normal_size = 4; // 4 bytes since we are using GL_INT_2_10_10_10_REV format.
	const size_t packed_uv_size = use_half_uvs ? sizeof(half)*2 : sizeof(float)*2;

	/*
	Vertex data layout is
	position [always present]
	normal   [optional]
	uv_0     [optional]
	uv_1     [optional]
	colour   [optional]
	*/
	const size_t normal_offset      = pos_size;
	const size_t uv0_offset         = normal_offset   + (mesh_has_shading_normals ? packed_normal_size : 0);
	const size_t uv1_offset         = uv0_offset      + (mesh_has_uvs ? packed_uv_size : 0);
	const size_t vert_col_offset    = uv1_offset      + (mesh_has_uv1 ? packed_uv_size : 0);
	const size_t num_bytes_per_vert = vert_col_offset + (mesh_has_vert_cols ? sizeof(float)*3 : 0);
	js::Vector<uint8, 16>& vert_data = this->vertex_data;
	vert_data.reserve(mesh->vert_positions.size() * num_bytes_per_vert);

	js::Vector<uint32, 16> uint32_indices(mesh->triangles.size() * 3 + mesh->quads.size() * 6);

	size_t vert_index_buffer_i = 0; // Current write index into vert_index_buffer
	size_t next_merged_vert_i = 0;
	size_t last_pass_start_index = 0;
	uint32 current_mat_index = std::numeric_limits<uint32>::max();

	BMeshVertKey empty_key;
	empty_key.pos =    Indigo::Vec3f(std::numeric_limits<float>::infinity());
	empty_key.normal = Indigo::Vec3f(std::numeric_limits<float>::infinity());
	empty_key.uv0 = Indigo::Vec2f(0.f);
	empty_key.uv1 = Indigo::Vec2f(0.f);
	HashMapInsertOnly2<BMeshVertKey, uint32, BMeshVertKeyHash> vert_map(empty_key, // Map from vert data to merged index
		/*expected_num_items=*/mesh->vert_positions.size()); 
	

	if(mesh->triangles.size() > 0)
	{
		// Create list of triangle references sorted by material index
		js::Vector<std::pair<uint32, uint32>, 16> unsorted_tri_indices(num_tris);
		js::Vector<std::pair<uint32, uint32>, 16> tri_indices(num_tris); // Sorted by material

		for(uint32 t = 0; t < num_tris; ++t)
			unsorted_tri_indices[t] = std::make_pair(tris[t].tri_mat_index, t);

		Sort::serialCountingSort(/*in=*/unsorted_tri_indices.data(), /*out=*/tri_indices.data(), num_tris, BMeshTakeFirstElement());

		for(uint32 t = 0; t < num_tris; ++t)
		{
			// If we've switched to a new material then start a new triangle range
			if(tri_indices[t].first != current_mat_index)
			{
				if(t > 0) // Don't add zero-length passes.
				{
					IndicesBatch batch;
					batch.material_index = current_mat_index;
					batch.indices_start = (uint32)(last_pass_start_index);
					batch.num_indices = (uint32)(vert_index_buffer_i - last_pass_start_index);
					this->batches.push_back(batch);
				}
				last_pass_start_index = vert_index_buffer_i;
				current_mat_index = tri_indices[t].first;
			}

			const Indigo::Triangle& tri = tris[tri_indices[t].second];
			for(uint32 i = 0; i < 3; ++i) // For each vert in tri:
			{
				const uint32 pos_i		= tri.vertex_indices[i];
				const uint32 base_uv_i	= tri.uv_indices[i];
				const uint32 uv_i = base_uv_i * num_uv_sets; // Index of UV for UV set 0.
				if(pos_i >= vert_positions_size)
					throw glare::Exception("vert index out of bounds");
				if(mesh_has_uvs && uv_i >= uvs_size)
					throw glare::Exception("UV index out of bounds");

				// Look up merged vertex
				const Indigo::Vec2f uv0 = mesh_has_uvs ? uv_pairs[uv_i    ] : Indigo::Vec2f(0.f);
				const Indigo::Vec2f uv1 = mesh_has_uv1 ? uv_pairs[uv_i + 1] : Indigo::Vec2f(0.f);

				BMeshVertKey key;
				key.pos = vert_positions[pos_i];
				key.normal = mesh_has_shading_normals ? vert_normals[pos_i] : Indigo::Vec3f(0);
				key.uv0 = uv0;
				key.uv1 = uv1;

				const auto res = vert_map.find(key);
				uint32 merged_v_index;
				if(res == vert_map.end()) // If a vert for this vert data hasn't been created yet:
				{
					merged_v_index = (uint32)next_merged_vert_i++;

					vert_map.insert(std::make_pair(key, merged_v_index));

					const size_t cur_size = vert_data.size();
					vert_data.resize(cur_size + num_bytes_per_vert);
					copyUInt32s(&vert_data[cur_size], &vert_positions[pos_i].x, sizeof(Indigo::Vec3f)); // Copy vert position

					if(mesh_has_shading_normals)
					{
						const uint32 n = BMeshPackNormal(vert_normals[pos_i]); // Pack normal into GL_INT_2_10_10_10_REV format.
						copyUInt32s(&vert_data[cur_size + normal_offset], &n, 4);
					}

					if(mesh_has_uvs)
					{
						// Copy uv_0
						if(use_half_uvs)
						{
							half half_uv[2];
							half_uv[0] = half(uv0.x);
							half_uv[1] = half(uv0.y);
							copyUInt32s(&vert_data[cur_size + uv0_offset], half_uv, 4);
						}
						else
							copyUInt32s(&vert_data[cur_size + uv0_offset], &uv0.x, sizeof(Indigo::Vec2f));

						// Copy uv_1
						if(mesh_has_uv1)
						{
							if(use_half_uvs)
							{
								half half_uv[2];
								half_uv[0] = half(uv1.x);
								half_uv[1] = half(uv1.y);
								copyUInt32s(&vert_data[cur_size + uv1_offset], half_uv, 4);
							}
							else
								copyUInt32s(&vert_data[cur_size + uv1_offset], &uv1.x, sizeof(Indigo::Vec2f));
						}
					}

					if(mesh_has_vert_cols)
						copyUInt32s(&vert_data[cur_size + vert_col_offset], &vert_colours[pos_i].x, sizeof(Indigo::Vec3f));
				}
				else
				{
					merged_v_index = res->second; // Else a vertex with this position and UVs has already been created, use it.
				}

				uint32_indices[vert_index_buffer_i++] = (uint32)merged_v_index;
			}
		}
	}

	if(mesh->quads.size() > 0)
	{
		// Create list of quad references sorted by material index
		js::Vector<std::pair<uint32, uint32>, 16> unsorted_quad_indices(num_quads);
		js::Vector<std::pair<uint32, uint32>, 16> quad_indices(num_quads); // Sorted by material

		for(uint32 q = 0; q < num_quads; ++q)
			unsorted_quad_indices[q] = std::make_pair(quads[q].mat_index, q);

		Sort::serialCountingSort(/*in=*/unsorted_quad_indices.data(), /*out=*/quad_indices.data(), num_quads, BMeshTakeFirstElement());

		for(uint32 q = 0; q < num_quads; ++q)
		{
			// If we've switched to a new material then start a new quad range
			if(quad_indices[q].first != current_mat_index)
			{
				if(vert_index_buffer_i > last_pass_start_index) // Don't add zero-length passes.
				{
					IndicesBatch batch;
					batch.material_index = current_mat_index;
					batch.indices_start = (uint32)(last_pass_start_index);
					batch.num_indices = (uint32)(vert_index_buffer_i - last_pass_start_index);
					this->batches.push_back(batch);
				}
				last_pass_start_index = vert_index_buffer_i;
				current_mat_index = quad_indices[q].first;
			}

			const Indigo::Quad& quad = quads[quad_indices[q].second];
			uint32 vert_merged_index[4];
			for(uint32 i = 0; i < 4; ++i) // For each vert in quad:
			{
				const uint32 pos_i  = quad.vertex_indices[i];
				const uint32 base_uv_i = quad.uv_indices[i];
				const uint32 uv_i = base_uv_i * num_uv_sets; // Index of UV for UV set 0.
				if(pos_i >= vert_positions_size)
					throw glare::Exception("vert index out of bounds");
				if(mesh_has_uvs && uv_i >= uvs_size)
					throw glare::Exception("UV index out of bounds");

				// Look up merged vertex
				const Indigo::Vec2f uv0 = mesh_has_uvs ? uv_pairs[uv_i    ] : Indigo::Vec2f(0.f);
				const Indigo::Vec2f uv1 = mesh_has_uv1 ? uv_pairs[uv_i + 1] : Indigo::Vec2f(0.f);

				BMeshVertKey key;
				key.pos = vert_positions[pos_i];
				key.normal = mesh_has_shading_normals ? vert_normals[pos_i] : Indigo::Vec3f(0);
				key.uv0 = uv0;
				key.uv1 = uv1;

				const auto res = vert_map.find(key);

				uint32 merged_v_index;
				if(res == vert_map.end()) // If a vert for this vert data hasn't been created yet:
				{
					merged_v_index = (uint32)next_merged_vert_i++;

					vert_map.insert(std::make_pair(key, merged_v_index));

					const size_t cur_size = vert_data.size();
					vert_data.resize(cur_size + num_bytes_per_vert);
					copyUInt32s(&vert_data[cur_size], &vert_positions[pos_i].x, sizeof(Indigo::Vec3f));
					if(mesh_has_shading_normals)
					{
						const uint32 n = BMeshPackNormal(vert_normals[pos_i]); // Pack normal into GL_INT_2_10_10_10_REV format.
						copyUInt32s(&vert_data[cur_size + normal_offset], &n, 4);
					}

					if(mesh_has_uvs)
					{
						if(use_half_uvs)
						{
							half half_uv[2];
							half_uv[0] = half(uv0.x);
							half_uv[1] = half(uv0.y);
							copyUInt32s(&vert_data[cur_size + uv0_offset], half_uv, 4);
						}
						else
							copyUInt32s(&vert_data[cur_size + uv0_offset], &uv0.x, sizeof(Indigo::Vec2f));

						// Copy uv_1
						if(mesh_has_uv1)
						{
							if(use_half_uvs)
							{
								half half_uv[2];
								half_uv[0] = half(uv1.x);
								half_uv[1] = half(uv1.y);
								copyUInt32s(&vert_data[cur_size + uv1_offset], half_uv, 4);
							}
							else
								copyUInt32s(&vert_data[cur_size + uv1_offset], &uv1.x, sizeof(Indigo::Vec2f));
						}
					}

					if(mesh_has_vert_cols)
						copyUInt32s(&vert_data[cur_size + vert_col_offset], &vert_colours[pos_i].x, sizeof(Indigo::Vec3f));
				}
				else
				{
					merged_v_index = res->second; // Else a vertex with this position index and UVs has already been created, use it.
				}

				vert_merged_index[i] = merged_v_index;
			} // End for each vert in quad:

			// Tri 1 of quad
			uint32_indices[vert_index_buffer_i + 0] = vert_merged_index[0];
			uint32_indices[vert_index_buffer_i + 1] = vert_merged_index[1];
			uint32_indices[vert_index_buffer_i + 2] = vert_merged_index[2];
			// Tri 2 of quad
			uint32_indices[vert_index_buffer_i + 3] = vert_merged_index[0];
			uint32_indices[vert_index_buffer_i + 4] = vert_merged_index[2];
			uint32_indices[vert_index_buffer_i + 5] = vert_merged_index[3];

			vert_index_buffer_i += 6;
		}
	}

	// Build last pass data that won't have been built yet.
	IndicesBatch batch;
	batch.material_index = current_mat_index;
	batch.indices_start = (uint32)(last_pass_start_index);
	batch.num_indices = (uint32)(vert_index_buffer_i - last_pass_start_index);
	this->batches.push_back(batch);

	const size_t num_merged_verts = next_merged_vert_i;

	
	// Build index data
	setIndexDataFromIndices(uint32_indices, num_merged_verts);


	VertAttribute pos_attrib;
	pos_attrib.type = VertAttribute_Position;
	pos_attrib.component_type = ComponentType_Float;
	pos_attrib.offset_B = 0;
	vert_attributes.push_back(pos_attrib);

	if(mesh_has_shading_normals)
	{
		VertAttribute normal_attrib;
		normal_attrib.type = VertAttribute_Normal;
		normal_attrib.component_type = ComponentType_PackedNormal;
		normal_attrib.offset_B = normal_offset;
		vert_attributes.push_back(normal_attrib);
	}

	if(num_uv_sets >= 1)
	{
		VertAttribute uv_attrib;
		uv_attrib.type = VertAttribute_UV_0;
		uv_attrib.component_type = ComponentType_Float;
		uv_attrib.offset_B = uv0_offset;
		vert_attributes.push_back(uv_attrib);
	}
	if(num_uv_sets >= 2)
	{
		VertAttribute uv_attrib;
		uv_attrib.type = VertAttribute_UV_1;
		uv_attrib.component_type = ComponentType_Float;
		uv_attrib.offset_B = uv1_offset;
		vert_attributes.push_back(uv_attrib);
	}

	if(mesh_has_vert_cols)
	{
		VertAttribute colour_attrib;
		colour_attrib.type = VertAttribute_Colour;
		colour_attrib.component_type = ComponentType_Float;
		colour_attrib.offset_B = vert_col_offset;
		vert_attributes.push_back(colour_attrib);
	}

	aabb_os.min_ = Vec4f(mesh->aabb_os.bound[0].x, mesh->aabb_os.bound[0].y, mesh->aabb_os.bound[0].z, 1.f);
	aabb_os.max_ = Vec4f(mesh->aabb_os.bound[1].x, mesh->aabb_os.bound[1].y, mesh->aabb_os.bound[1].z, 1.f);
}


void BatchedMesh::setIndexDataFromIndices(const js::Vector<uint32, 16>& uint32_indices, size_t num_verts)
{
	// Build index data
	const size_t num_indices = uint32_indices.size();

	if(num_verts < 128)
	{
		this->index_type = ComponentType_UInt8;

		index_data.resize(num_indices * sizeof(uint8));

		uint8* const dest_indices = index_data.data();
		for(size_t i=0; i<num_indices; ++i)
			dest_indices[i] = (uint8)uint32_indices[i];
	}
	else if(num_verts < 32768)
	{
		this->index_type = ComponentType_UInt16;

		index_data.resize(num_indices * sizeof(uint16));

		uint16* const dest_indices = (uint16*)index_data.data();
		for(size_t i=0; i<num_indices; ++i)
			dest_indices[i] = (uint16)uint32_indices[i];
	}
	else
	{
		this->index_type = ComponentType_UInt32;

		index_data.resize(num_indices * sizeof(uint32));

		uint32* const dest_indices = (uint32*)index_data.data();
		for(size_t i=0; i<num_indices; ++i)
			dest_indices[i] = uint32_indices[i];
	}
}


void BatchedMesh::buildIndigoMesh(Indigo::Mesh& mesh_out) const
{
	const size_t num_verts = numVerts();
	const size_t vert_size_B = vertexSize();

	mesh_out.vert_positions.resize(num_verts);

	const VertAttribute* pos_attr = this->findAttribute(VertAttribute_Position);
	if(!pos_attr)
		throw glare::Exception("Pos attr missing");
	const size_t pos_offset_B = pos_attr->offset_B;

	const VertAttribute* normal_attr = this->findAttribute(VertAttribute_Normal);
	if(normal_attr)
		mesh_out.vert_normals.resize(num_verts);
	else
		mesh_out.vert_normals.resize(0);
	
	// Copy vert positions
	{
		if(pos_attr->component_type != ComponentType_Float)
			throw glare::Exception("Expected pos attr to have float component type.");
		const float* const pos_vertex_data_float = (const float*)(vertex_data.data() + pos_offset_B);

		for(size_t v=0; v<num_verts; ++v)
		{
			mesh_out.vert_positions[v].x = pos_vertex_data_float[v * (vert_size_B/4) + 0];
			mesh_out.vert_positions[v].y = pos_vertex_data_float[v * (vert_size_B/4) + 1];
			mesh_out.vert_positions[v].z = pos_vertex_data_float[v * (vert_size_B/4) + 2];
		}
	}

	// Copy vert normals
	if(normal_attr)
	{
		const size_t normal_offset_B = normal_attr->offset_B;

		if(normal_attr->component_type == ComponentType_Float)
		{
			const float* const normal_vertex_data_float = (const float*)(vertex_data.data() + normal_offset_B);
			for(size_t v=0; v<num_verts; ++v)
			{
				mesh_out.vert_normals[v].x = normal_vertex_data_float[v * (vert_size_B/4) + 0];
				mesh_out.vert_normals[v].y = normal_vertex_data_float[v * (vert_size_B/4) + 1];
				mesh_out.vert_normals[v].z = normal_vertex_data_float[v * (vert_size_B/4) + 2];
			}
		}
		else if(normal_attr->component_type == ComponentType_PackedNormal)
		{
			const uint32* const normal_vertex_data_uint32 = (const uint32*)(vertex_data.data() + normal_offset_B);
			for(size_t v=0; v<num_verts; ++v)
			{
				mesh_out.vert_normals[v] = BMeshUnpackNormal(normal_vertex_data_uint32[v * (vert_size_B/4)]);
			}
		}
		else
			throw glare::Exception("Unhandled component type for normals: " + toString(normal_attr->component_type));
	}

	
	const VertAttribute* uv0_attr = this->findAttribute(VertAttribute_UV_0);
	const VertAttribute* uv1_attr = this->findAttribute(VertAttribute_UV_1);
	
	size_t num_uv_sets = 0;
	if(uv0_attr)
		num_uv_sets++;
	if(uv0_attr && uv1_attr)
		num_uv_sets++;

	mesh_out.num_uv_mappings = (uint32)num_uv_sets;
	mesh_out.uv_pairs.resize(num_uv_sets * num_verts);

	// Copy UV 0
	if(uv0_attr)
	{
		const size_t uv0_offset_B = uv0_attr->offset_B;
		if(uv0_attr->component_type == ComponentType_Float)
		{
			const float* const uv0_vertex_data_float = (const float*)(vertex_data.data() + uv0_offset_B);
			for(size_t v=0; v<num_verts; ++v)
			{
				mesh_out.uv_pairs[v * num_uv_sets + 0].x = uv0_vertex_data_float[v * (vert_size_B/4) + 0];
				mesh_out.uv_pairs[v * num_uv_sets + 0].y = uv0_vertex_data_float[v * (vert_size_B/4) + 1];
			}
		}
		else if(uv0_attr->component_type == ComponentType_Half)
		{
			const half* const uv0_vertex_data_half = (const half*)(vertex_data.data() + uv0_offset_B);
			for(size_t v=0; v<num_verts; ++v)
			{
				mesh_out.uv_pairs[v * num_uv_sets + 0].x = uv0_vertex_data_half[v * (vert_size_B/2) + 0];
				mesh_out.uv_pairs[v * num_uv_sets + 0].y = uv0_vertex_data_half[v * (vert_size_B/2) + 1];
			}
		}
		else
			throw glare::Exception("Unhandled component type for uv0: " + toString(uv0_attr->component_type));
	}

	// Copy UV 1
	if(uv0_attr && uv1_attr)
	{
		const size_t uv1_offset_B = uv1_attr->offset_B;
		if(uv1_attr->component_type == ComponentType_Float)
		{
			const float* const uv1_vertex_data_float = (const float*)(vertex_data.data() + uv1_offset_B);
			for(size_t v=0; v<num_verts; ++v)
			{
				mesh_out.uv_pairs[v * num_uv_sets + 1].x = uv1_vertex_data_float[v * (vert_size_B/4) + 0];
				mesh_out.uv_pairs[v * num_uv_sets + 1].y = uv1_vertex_data_float[v * (vert_size_B/4) + 1];
			}
		}
		else if(uv1_attr->component_type == ComponentType_Half)
		{
			const half* const uv1_vertex_data_half = (const half*)(vertex_data.data() + uv1_offset_B);
			for(size_t v=0; v<num_verts; ++v)
			{
				mesh_out.uv_pairs[v * num_uv_sets + 1].x = uv1_vertex_data_half[v * (vert_size_B/2) + 0];
				mesh_out.uv_pairs[v * num_uv_sets + 1].y = uv1_vertex_data_half[v * (vert_size_B/2) + 1];
			}
		}
		else
			throw glare::Exception("Unhandled component type for uv1: " + toString(uv1_attr->component_type));
	}

	// Copy triangle data
	mesh_out.triangles.resize(numIndices() / 3);
	for(size_t b=0; b<batches.size(); ++b)
	{
		const IndicesBatch& batch = batches[b];

		if(index_type == ComponentType_UInt8)
		{
			const uint8* const index_data_uint8 = (const uint8*)index_data.data();

			const size_t begin_tri_i = batch.indices_start / 3;
			const size_t end_tri_i   = ((size_t)batch.indices_start + (size_t)batch.num_indices) / 3;
			for(size_t tri_i=begin_tri_i; tri_i < end_tri_i; ++tri_i)
			{
				for(size_t c=0; c<3; ++c)
				{
					mesh_out.triangles[tri_i].vertex_indices[c] = index_data_uint8[tri_i * 3 + c];
					mesh_out.triangles[tri_i].uv_indices[c]     = index_data_uint8[tri_i * 3 + c];
				}
				mesh_out.triangles[tri_i].tri_mat_index = batch.material_index;
			}
		}
		else if(index_type == ComponentType_UInt16)
		{
			const uint16* const index_data_uint16   = (const uint16*)index_data.data();

			const size_t begin_tri_i = batch.indices_start / 3;
			const size_t end_tri_i   = ((size_t)batch.indices_start + (size_t)batch.num_indices) / 3;

			for(size_t tri_i=begin_tri_i; tri_i < end_tri_i; ++tri_i)
			{
				for(size_t c=0; c<3; ++c)
				{
					mesh_out.triangles[tri_i].vertex_indices[c] = index_data_uint16[tri_i * 3 + c];
					mesh_out.triangles[tri_i].uv_indices[c]     = index_data_uint16[tri_i * 3 + c];
				}
				mesh_out.triangles[tri_i].tri_mat_index = batch.material_index;
			}
		}
		else if(index_type == ComponentType_UInt32)
		{
			const uint32* const index_data_uint32   = (const uint32*)index_data.data();

			const size_t begin_tri_i = batch.indices_start / 3;
			const size_t end_tri_i   = ((size_t)batch.indices_start + (size_t)batch.num_indices) / 3;

			for(size_t tri_i=begin_tri_i; tri_i < end_tri_i; ++tri_i)
			{
				for(size_t c=0; c<3; ++c)
				{
					mesh_out.triangles[tri_i].vertex_indices[c] = index_data_uint32[tri_i * 3 + c];
					mesh_out.triangles[tri_i].uv_indices[c]     = index_data_uint32[tri_i * 3 + c];
				}
				mesh_out.triangles[tri_i].tri_mat_index = batch.material_index;
			}
		}
		else
			throw glare::Exception("Unhandled index type: " + toString(index_type));
	}

	mesh_out.endOfModel();
}


bool BatchedMesh::operator == (const BatchedMesh& other) const
{
	return
		vert_attributes == other.vert_attributes &&
		batches == other.batches &&
		index_type == other.index_type &&
		index_data == other.index_data &&
		vertex_data == other.vertex_data &&
		aabb_os == other.aabb_os;
}


void BatchedMesh::operator = (const BatchedMesh& other)
{
	vert_attributes = other.vert_attributes;
	batches = other.batches;
	index_type = other.index_type;
	index_data = other.index_data;
	vertex_data = other.vertex_data;
	aabb_os = other.aabb_os;
}


static const uint32 MAGIC_NUMBER = 12456751;
static const uint32 FORMAT_VERSION = 1;

static const uint32 ANIMATION_DATA_CHUNK = 10000;

static const uint32 FLAG_USE_COMPRESSION = 1;


struct BatchedMeshHeader
{
	uint32 magic_number;
	uint32 format_version;
	uint32 header_size;
	uint32 flags;

	uint32 num_vert_attributes;
	uint32 num_batches;
	uint32 index_type;
	uint32 index_data_size_B;
	uint32 vertex_data_size_B;

	Vec3f aabb_min;
	Vec3f aabb_max;
};
static_assert(sizeof(BatchedMeshHeader) == sizeof(uint32) * 15, "sizeof(BatchedMeshHeader) == sizeof(uint32) * 15");


void BatchedMesh::writeToFile(const std::string& dest_path, const WriteOptions& write_options) const // throws glare::Exception on failure
{
	//Timer write_timer;

	FileOutStream file(dest_path);

	BatchedMeshHeader header;
	header.magic_number = MAGIC_NUMBER;
	header.format_version = FORMAT_VERSION;
	header.header_size = sizeof(BatchedMeshHeader);
	header.flags = write_options.use_compression ? FLAG_USE_COMPRESSION : 0;
	header.num_vert_attributes = (uint32)vert_attributes.size();
	header.num_batches = (uint32)batches.size();
	header.index_type = (uint32)index_type;
	header.index_data_size_B = (uint32)index_data.size();
	header.vertex_data_size_B = (uint32)vertex_data.size();
	header.aabb_min = Vec3f(aabb_os.min_);
	header.aabb_max = Vec3f(aabb_os.max_);

	file.writeData(&header, sizeof(BatchedMeshHeader));

	// Write vert attributes
	for(size_t i=0; i<vert_attributes.size(); ++i)
	{
		file.writeUInt32((uint32)vert_attributes[i].type);
		file.writeUInt32((uint32)vert_attributes[i].component_type);
	}

	// Write batches
	if(!batches.empty())
		file.writeData(batches.data(), batches.size() * sizeof(IndicesBatch));

	
	// Write the rest of the data compressed
	if(write_options.use_compression)
	{
		// We will write the index data and vertex data into separate compressed buffers, for a few reasons:
		// * Since we're processing in two parts, can reuse smaller buffers.
		// * Buffers for each part can be 4-byte aligned, without using padding
		// * Resulting compressed size is smaller (as measured with perf tests)

		js::Vector<uint8, 16> filtered_data_vec;
		filtered_data_vec.reserve(myMax(index_data.size(), vertex_data.size()));

		const size_t compressed_bound = myMax(ZSTD_compressBound(index_data.size()), ZSTD_compressBound(vertex_data.size())); // Make the buffer large enough that we don't need to resize it later.
		js::Vector<uint8, 16> compressed_data(compressed_bound);

		//size_t total_compressed_size = 0;
		Timer timer;
		timer.pause();

		{
			// Build filtered index data.
			filtered_data_vec.resizeNoCopy(index_data.size());
			
			uint32 last_index = 0;
			const size_t num_indices = index_data.size() / componentTypeSize(index_type);
			if(index_type == ComponentType_UInt8)
			{
				const uint8* const index_data_uint8    = (const uint8*)index_data.data();
					  uint8* const filtered_index_data = (uint8*)filtered_data_vec.data();
				for(size_t i=0; i<num_indices; ++i)
				{
					const uint8 index = index_data_uint8[i];
					assert(index <= (uint8)std::numeric_limits<int8>::max());
					const int8 rel_index = (int8)((int32)index - (int32)last_index);
					filtered_index_data[i] = rel_index;
					last_index = index;
				}
			}
			else if(index_type == ComponentType_UInt16)
			{
				const uint16* const index_data_uint16   = (const uint16*)index_data.data();
					  uint16* const filtered_index_data = (uint16*)filtered_data_vec.data();
				for(size_t i=0; i<num_indices; ++i)
				{
					const uint16 index = index_data_uint16[i];
					assert(index <= (uint16)std::numeric_limits<int16>::max());
					const int16 rel_index = (int16)((int32)index - (int32)last_index);
					filtered_index_data[i] = rel_index;
					last_index = index;
				}
			}
			else if(index_type == ComponentType_UInt32)
			{
				const uint32* const index_data_uint32   = (const uint32*)index_data.data();
					  uint32* const filtered_index_data = (uint32*)filtered_data_vec.data();
				for(size_t i=0; i<num_indices; ++i)
				{
					const uint32 index = index_data_uint32[i];
					assert(index <= (uint32)std::numeric_limits<int32>::max());
					const int32 rel_index = (int32)((int32)index - (int32)last_index);
					filtered_index_data[i] = rel_index;
					last_index = index;
				}
			}
			else
			{
				assert(0);
			}

			// Compress the index buffer with zstandard
			timer.unpause();
			const size_t compressed_size = ZSTD_compress(compressed_data.data(), compressed_data.size(), filtered_data_vec.data(), filtered_data_vec.size(),
				write_options.compression_level // compression level
			);
			if(ZSTD_isError(compressed_size))
				throw glare::Exception(std::string("Compression failed: ") + ZSTD_getErrorName(compressed_size));
			timer.pause();

			// Write compressed size as uint64
			file.writeUInt64(compressed_size);

			// Now write compressed data to disk
			file.writeData(compressed_data.data(), compressed_size);
		
			//total_compressed_size += compressed_size;
		}

		// Build de-interleaved vertex data, compress it and then write it to disk.
		{
			filtered_data_vec.resizeNoCopy(vertex_data.size());

			/*
			Separate interleaved vertex data into separate arrays

			p0 n0 c0 p1 n1 c1 p2 n2 c2 p3 n3 c3 ... pN nN cN
			=>
			p0 p1 p2 p3 ... pN n0 n1 n2 n3 ... nN c0 c1 c2 c3 ... cN
			*/
			const size_t vert_size = vertexSize(); // in bytes
			assert(vert_size % 4 == 0);
			const size_t num_verts = vertex_data.size() / vert_size;

			size_t attr_offset = 0;
			uint8* dst_ptr = filtered_data_vec.data();
			for(size_t b=0; b<vert_attributes.size(); ++b)
			{
				const size_t attr_size = vertAttributeSize(vert_attributes[b]);
				assert(attr_size % 4 == 0);
				const uint8* src_ptr = vertex_data.data() + attr_offset;

				for(size_t i=0; i<num_verts; ++i) // For each vertex
				{
					// Copy data for this attribute, for this vertex, to filtered_data
					assert(src_ptr + attr_size <= vertex_data.data() + vertex_data.size());
					assert(dst_ptr + attr_size <= filtered_data_vec.data() + filtered_data_vec.size());

					copyUInt32s(dst_ptr, src_ptr, attr_size);

					src_ptr += vert_size;
					dst_ptr += attr_size;
				}

				attr_offset += attr_size;
			}

			// Compress the vertex data with zstandard
			timer.unpause();
			const size_t compressed_size = ZSTD_compress(compressed_data.data(), compressed_data.size(), filtered_data_vec.data(), filtered_data_vec.size(),
				write_options.compression_level // compression level
			);
			if(ZSTD_isError(compressed_size))
				throw glare::Exception(std::string("Compression failed: ") + ZSTD_getErrorName(compressed_size));
			timer.pause();

			// Write compressed size as uint64
			file.writeUInt64(compressed_size);

			// Now write compressed data to disk
			file.writeData(compressed_data.data(), compressed_size);

			//total_compressed_size += compressed_size;
		}

		
		//const size_t uncompressed_size = index_data.size() + vertex_data.size();
		//const double compression_speed = uncompressed_size / timer.elapsed();
		// conPrint("");
		// conPrint("Uncompressed size:   " + toString(uncompressed_size) + " B");
		// conPrint("Compressed size:     " + toString(total_compressed_size) + " B");
		// conPrint("Compression ratio:   " + doubleToStringNSigFigs((double)uncompressed_size / total_compressed_size, 4));
		// conPrint("Compression took     " + timer.elapsedStringNSigFigs(4) + " (" + doubleToStringNSigFigs(compression_speed / (1024ull*1024ull), 4) + " MB/s)");
	}
	else
	{
		file.writeData(index_data.data(),  index_data.dataSizeBytes());
		file.writeData(vertex_data.data(), vertex_data.dataSizeBytes());
	}

	if(!animation_data.animations.empty() || !animation_data.joint_nodes.empty())
	{
		// Write animation data at the end
		file.writeUInt32(ANIMATION_DATA_CHUNK);
		animation_data.writeToStream(file);
	}

	//conPrint("Total time to write to disk: " + write_timer.elapsedString());
}


static const uint32 MAX_NUM_VERT_ATTRIBUTES = 100;
static const uint32 MAX_NUM_BATCHES = 1000000;


void BatchedMesh::readFromFile(const std::string& src_path, BatchedMesh& mesh_out)
{
	FileInStream file(src_path);

	readFromData(file.fileData(), file.fileSize(), mesh_out);
}


void BatchedMesh::readFromData(const void* data, size_t data_len, BatchedMesh& mesh_out)
{
	try
	{
		BufferViewInStream file(ArrayRef<uint8>((const uint8*)data, data_len));

		// Read header
		BatchedMeshHeader header;
		file.readData(&header, sizeof(header));

		if(header.magic_number != MAGIC_NUMBER)
			throw glare::Exception("Invalid magic number.");

		if(header.format_version < FORMAT_VERSION)
			throw glare::Exception("Unsupported format version " + toString(header.format_version) + ".");

		
		// Skip past rest of header
		if(header.header_size > 10000 || header.header_size > file.size())
			throw glare::Exception("Header size too large.");
		file.setReadIndex(header.header_size);


		// Read vert attributes
		if(header.num_vert_attributes == 0)
			throw glare::Exception("Zero vert attributes.");
		if(header.num_vert_attributes > MAX_NUM_VERT_ATTRIBUTES)
			throw glare::Exception("Too many vert attributes.");
		
		mesh_out.vert_attributes.resize(header.num_vert_attributes);
		size_t cur_offset = 0;
		for(size_t i=0; i<mesh_out.vert_attributes.size(); ++i)
		{
			const uint32 type = file.readUInt32();
			if(type > MAX_VERT_ATTRIBUTE_TYPE_VALUE)
				throw glare::Exception("Invalid vert attribute type value.");
			mesh_out.vert_attributes[i].type = (VertAttributeType)type;

			const uint32 component_type = file.readUInt32();
			if(component_type > MAX_COMPONENT_TYPE_VALUE)
				throw glare::Exception("Invalid vert attribute component type value.");
			mesh_out.vert_attributes[i].component_type = (ComponentType)component_type;

			mesh_out.vert_attributes[i].offset_B = cur_offset;
			const size_t vert_attr_size_B = vertAttributeSize(mesh_out.vert_attributes[i]);
			if((vert_attr_size_B % 4) != 0)
				throw glare::Exception("Invalid vert attribute: size must be a multiple of 4 bytes.");
			cur_offset += vert_attr_size_B;
		}

		// Read batches
		if(header.num_batches > MAX_NUM_BATCHES)
			throw glare::Exception("Too many batches.");

		mesh_out.batches.resize(header.num_batches);
		file.readData(mesh_out.batches.data(), mesh_out.batches.size() * sizeof(IndicesBatch));


		// Check header index type
		if(header.index_type > MAX_COMPONENT_TYPE_VALUE)
			throw glare::Exception("Invalid index type value.");

		mesh_out.index_type = (ComponentType)header.index_type;

		// Check total index data size is a multiple of each index size.
		if(header.index_data_size_B % componentTypeSize(mesh_out.index_type) != 0)
			throw glare::Exception("Invalid index_data_size_B.");

		const uint32 MAX_INDEX_DATA_SIZE = 1 << 29; // 512 MB
		if(header.index_data_size_B > MAX_INDEX_DATA_SIZE)
			throw glare::Exception("Invalid index_data_size_B (too large).");

		mesh_out.index_data.resize(header.index_data_size_B);

		// Check total vert data size is a multiple of each vertex size.  Note that vertexSize() should be > 0 since we have set mesh_out.vert_attributes and checked there is at least one attribute.
		if(header.vertex_data_size_B % mesh_out.vertexSize() != 0)
			throw glare::Exception("Invalid vertex_data_size_B.");

		const uint32 MAX_VERTEX_DATA_SIZE = 1 << 29; // 512 MB
		if(header.vertex_data_size_B > MAX_VERTEX_DATA_SIZE)
			throw glare::Exception("Invalid vertex_data_size_B (too large).");

		mesh_out.vertex_data.resize(header.vertex_data_size_B);

		mesh_out.aabb_os.min_ = Vec4f(header.aabb_min.x, header.aabb_min.y, header.aabb_min.z, 1.f);
		mesh_out.aabb_os.max_ = Vec4f(header.aabb_max.x, header.aabb_max.y, header.aabb_max.z, 1.f);
		// TODO: require AABB values finite?

		const bool compression = (header.flags & FLAG_USE_COMPRESSION) != 0;
		if(compression)
		{
			js::Vector<uint8, 16> plaintext(myMax(header.index_data_size_B, header.vertex_data_size_B)); // Make sure large enough so we don't need to resize later.

			Timer timer;
			timer.pause();
			{
				const uint64 index_data_compressed_size = file.readUInt64();
				if(!file.canReadNBytes(index_data_compressed_size)) // Check index_data_compressed_size is valid, while taking care with wraparound
					throw glare::Exception("index_data_compressed_size was invalid.");

				// Decompress index data into plaintext buffer.
				timer.unpause();
				const size_t res = ZSTD_decompress(/*dest=*/plaintext.begin(), /*dest capacity=*/header.index_data_size_B, /*src=*/file.currentReadPtr(), /*compressed size=*/index_data_compressed_size);
				if(ZSTD_isError(res))
					throw glare::Exception(std::string("Decompression of index buffer failed: ") + ZSTD_getErrorName(res));
				if(res < (size_t)header.index_data_size_B)
					throw glare::Exception("Decompression of index buffer failed: not enough bytes in result");
				timer.pause();

				// Unfilter indices, place in mesh_out.index_data.
				const size_t num_indices = header.index_data_size_B / componentTypeSize((ComponentType)header.index_type);
				if(header.index_type == ComponentType_UInt8)
				{
					int32 last_index = 0;
					const int8* filtered_index_data_int8 = (const int8*)plaintext.data();
					uint8* index_data = (uint8*)mesh_out.index_data.data();
					for(size_t i=0; i<num_indices; ++i)
					{
						int8 index = (int8)last_index + filtered_index_data_int8[i];
						index_data[i] = index;
						last_index = index;
					}
				}
				else if(header.index_type == ComponentType_UInt16)
				{
					int32 last_index = 0;
					const int16* filtered_index_data_int16 = (const int16*)plaintext.data(); // Note that we know plaintext.data() is 16-byte aligned.
					uint16* index_data = (uint16*)mesh_out.index_data.data();
					for(size_t i=0; i<num_indices; ++i)
					{
						int16 index = (int16)last_index + filtered_index_data_int16[i];
						index_data[i] = index;
						last_index = index;
					}
				}
				else if(header.index_type == ComponentType_UInt32)
				{
					int32 last_index = 0;
					const int32* filtered_index_data_int32 = (const int32*)plaintext.data(); // Note that we know plaintext.data() is 16-byte aligned.
					uint32* index_data = (uint32*)mesh_out.index_data.data();
					for(size_t i=0; i<num_indices; ++i)
					{
						int32 index = (int32)last_index + filtered_index_data_int32[i];
						index_data[i] = index;
						last_index = index;
					}
				}
				else
					throw glare::Exception("Invalid index component type " + toString((int)header.index_type));

				file.advanceReadIndex(index_data_compressed_size);
			}

		
			// Decompress and de-filter vertex data.
			{
				const uint64 vertex_data_compressed_size = file.readUInt64();
				if(!file.canReadNBytes(vertex_data_compressed_size)) // Check vertex_data_compressed_size is valid, while taking care with wraparound
					throw glare::Exception("vertex_data_compressed_size was invalid.");

				// Decompress data into plaintext buffer.
				timer.unpause();
				const size_t res = ZSTD_decompress(plaintext.begin(), header.vertex_data_size_B, file.currentReadPtr(), vertex_data_compressed_size);
				if(ZSTD_isError(res))
					throw glare::Exception(std::string("Decompression of index buffer failed: ") + ZSTD_getErrorName(res));
				if(res < (size_t)header.vertex_data_size_B)
					throw glare::Exception("Decompression of index buffer failed: not enough bytes in result");
				timer.pause();
				// const double elapsed = timer.elapsed();
				// conPrint("Decompression took   " + doubleToStringNSigFigs(elapsed, 4) + " (" + doubleToStringNSigFigs(((double)((size_t)header.index_data_size_B + header.vertex_data_size_B) / (1024ull*1024ull)) / elapsed, 4) + "MB/s)");

				/*
				Read de-interleaved vertex data, and interleave it.
			
				p0 p1 p2 p3 ... pN n0 n1 n2 n3 ... nN c0 c1 c2 c3 ... cN
				=>
				p0 n0 c0 p1 n1 c1 p2 n2 c2 p3 n3 c3 ... pN nN cN
				*/
				const size_t vert_size = mesh_out.vertexSize(); // in bytes
				assert(vert_size % 4 == 0);
				const size_t num_verts = mesh_out.vertex_data.size() / vert_size;

				const uint8* src_ptr = plaintext.data();
				size_t attr_offset = 0;
				for(size_t b=0; b<mesh_out.vert_attributes.size(); ++b)
				{
					const size_t attr_size = vertAttributeSize(mesh_out.vert_attributes[b]);
					assert(attr_size % 4 == 0);
					uint8* dst_ptr = mesh_out.vertex_data.data() + attr_offset;

					for(size_t i=0; i<num_verts; ++i) // For each vertex
					{
						// Copy data for this attribute, for this vertex, to filtered_data
						assert(src_ptr + attr_size <= plaintext.data() + plaintext.size());
						assert(dst_ptr + attr_size <= mesh_out.vertex_data.data() + mesh_out.vertex_data.size());

						copyUInt32s(dst_ptr, src_ptr, attr_size);

						src_ptr += attr_size;
						dst_ptr += vert_size;
					}

					attr_offset += attr_size;
				}

				file.advanceReadIndex(vertex_data_compressed_size);
			}
		}
		else // else if !compression:
		{
			file.readData(mesh_out.index_data.data(),  mesh_out.index_data.dataSizeBytes());
			file.readData(mesh_out.vertex_data.data(), mesh_out.vertex_data.dataSizeBytes());
		}

		// Read animation data chunk if present
		if(file.getReadIndex() != file.size())
		{
			const uint32 chunk = file.readUInt32();
			if(chunk == ANIMATION_DATA_CHUNK)
			{
				// Timer timer;
				mesh_out.animation_data.readFromStream(file);
				// conPrint("Reading animation data took " + timer.elapsedStringNSigFigs(4));
			}
			else
				throw glare::Exception("invalid chunk value: " + toString(chunk));
		}

		//assert(file.getReadIndex() == file.size());
	}
	catch(std::bad_alloc&)
	{
		throw glare::Exception("Bad allocation while reading from file.");
	}
}


const BatchedMesh::VertAttribute* BatchedMesh::findAttribute(VertAttributeType type) const // returns NULL if not present.
{
	for(size_t b=0; b<vert_attributes.size(); ++b)
		if(vert_attributes[b].type == type)
			return &vert_attributes[b];
	return NULL;
}


const BatchedMesh::VertAttribute& BatchedMesh::getAttribute(VertAttributeType type) const
{
	const VertAttribute* attr = findAttribute(type);
	if(!attr)
		throw glare::Exception("Could not find attribute.");
	return *attr;
}

size_t BatchedMesh::numMaterialsReferenced() const
{
	size_t max_i = 0;
	for(size_t b=0; b<batches.size(); ++b)
		max_i = myMax(max_i, (size_t)batches[b].material_index);
	return max_i + 1;
}


size_t BatchedMesh::getTotalMemUsage() const
{
	return index_data.capacitySizeBytes() +
		vertex_data.capacitySizeBytes();
}


js::AABBox BatchedMesh::computeAABB() const
{
	const BatchedMesh::VertAttribute& pos_attr = getAttribute(VertAttribute_Position);
	const size_t stride = vertexSize();
	const size_t num_verts = numVerts();

	js::AABBox aabb = js::AABBox::emptyAABBox();
	for(size_t i=0; i<num_verts; ++i)
	{
		Vec4f vertpos;
		std::memcpy(vertpos.x, &vertex_data[pos_attr.offset_B + stride * i], sizeof(float) * 3);
		vertpos.x[3] = 1.f;
		aabb.enlargeToHoldPoint(vertpos);
	}
	return aabb;
}


void BatchedMesh::checkValidAndSanitiseMesh()
{
	BatchedMesh& mesh = *this;

	const uint32 num_verts = (uint32)mesh.numVerts();
	const BatchedMesh::ComponentType the_index_type = mesh.index_type;
	const size_t num_indices = mesh.numIndices();
	const size_t num_tris = num_indices / 3;


	// Check batch index ranges are in range.
	for(size_t b=0; b<batches.size(); ++b)
	{
		const IndicesBatch& batch = batches[b];

		if((size_t)batch.indices_start + (size_t)batch.num_indices > num_indices)
			throw glare::Exception("Invalid batch index range");

		if(batch.material_index >= 10000)
			throw glare::Exception("Too many materials referenced.");
	}


	const uint8* const index_data_uint8  = (const uint8*)mesh.index_data.data();
	const uint16* const index_data_uint16 = (const uint16*)mesh.index_data.data();
	const uint32* const index_data_uint32 = (const uint32*)mesh.index_data.data();

	for(size_t t = 0; t < num_tris; ++t)
	{
		uint32 vertex_indices[3];
		if(the_index_type == BatchedMesh::ComponentType_UInt8)
		{
			vertex_indices[0] = index_data_uint8[t*3 + 0];
			vertex_indices[1] = index_data_uint8[t*3 + 1];
			vertex_indices[2] = index_data_uint8[t*3 + 2];
		}
		else if(the_index_type == BatchedMesh::ComponentType_UInt16)
		{
			vertex_indices[0] = index_data_uint16[t*3 + 0];
			vertex_indices[1] = index_data_uint16[t*3 + 1];
			vertex_indices[2] = index_data_uint16[t*3 + 2];
		}
		else if(the_index_type == BatchedMesh::ComponentType_UInt32)
		{
			vertex_indices[0] = index_data_uint32[t*3 + 0];
			vertex_indices[1] = index_data_uint32[t*3 + 1];
			vertex_indices[2] = index_data_uint32[t*3 + 2];
		}
		else
			throw glare::Exception("Invalid index_type.");

		for(unsigned int v = 0; v < 3; ++v)
			if(vertex_indices[v] >= num_verts)
				throw glare::Exception("Triangle vertex index is out of bounds.  (vertex index=" + toString(vertex_indices[v]) + ", num verts: " + toString(num_verts) + ")");
	}

	js::Vector<uint8, 16>& vert_data = mesh.vertex_data;
	const size_t vert_size_B = mesh.vertexSize();
	const size_t num_joints = mesh.animation_data.joint_nodes.size();

	// Check joint indices are valid for all vertices
	const BatchedMesh::VertAttribute* joints_attr = mesh.findAttribute(BatchedMesh::VertAttribute_Joints);
	if(joints_attr)
	{
		const size_t joint_offset_B = joints_attr->offset_B;
		if(joints_attr->component_type == BatchedMesh::ComponentType_UInt8)
		{
			for(uint32 i=0; i<num_verts; ++i)
			{
				uint8 joints[4];
				std::memcpy(joints, &vert_data[i * vert_size_B + joint_offset_B], sizeof(uint8) * 4);

				for(int c=0; c<4; ++c)
					if((size_t)joints[c] >= num_joints)
						throw glare::Exception("Joint index is out of bounds");
			}
		}
		else if(joints_attr->component_type == BatchedMesh::ComponentType_UInt16)
		{
			for(uint32 i=0; i<num_verts; ++i)
			{
				uint16 joints[4];
				std::memcpy(joints, &vert_data[i * vert_size_B + joint_offset_B], sizeof(uint16) * 4);

				for(int c=0; c<4; ++c)
					if((size_t)joints[c] >= num_joints)
						throw glare::Exception("Joint index is out of bounds");
			}
		}
		else
			throw glare::Exception("Invalid joint index component type: " + toString((uint32)joints_attr->component_type));
	}

	// Check weight data is valid for all vertices.
	// For now we will just catch cases where all weights are zero, and set one of them to 1 (or the uint equiv).
	// We could also normalise the weight sum but that would be slower.
	// This prevents the rendering error with dancedevil_glb_16934124793649044515_lod2.bmesh.
	const BatchedMesh::VertAttribute* weight_attr = mesh.findAttribute(BatchedMesh::VertAttribute_Weights);
	if(weight_attr)
	{
		//conPrint("Checking weight data");
		//Timer timer;

		const size_t weights_offset_B = weight_attr->offset_B;
		if(weight_attr->component_type == BatchedMesh::ComponentType_UInt8)
		{
			for(uint32 i=0; i<num_verts; ++i)
			{
				uint8 weights[4];
				std::memcpy(weights, &vert_data[i * vert_size_B + weights_offset_B], sizeof(uint8) * 4);
				if(weights[0] == 0 && weights[1] == 0 && weights[2] == 0 && weights[3] == 0)
				{
					weights[0] = 255;
					std::memcpy(&vert_data[i * vert_size_B + weights_offset_B], weights, sizeof(uint8) * 4); // Copy back to vertex data
				}
			}
		}
		else if(weight_attr->component_type == BatchedMesh::ComponentType_UInt16)
		{
			for(uint32 i=0; i<num_verts; ++i)
			{
				uint16 weights[4];
				std::memcpy(weights, &vert_data[i * vert_size_B + weights_offset_B], sizeof(uint16) * 4);
				if(weights[0] == 0 && weights[1] == 0 && weights[2] == 0 && weights[3] == 0)
				{
					weights[0] = 65535;
					std::memcpy(&vert_data[i * vert_size_B + weights_offset_B], weights, sizeof(uint16) * 4); // Copy back to vertex data
				}
			}
		}
		else if(weight_attr->component_type == BatchedMesh::ComponentType_Float)
		{
			for(uint32 i=0; i<num_verts; ++i)
			{
				float weights[4];
				std::memcpy(weights, &vert_data[i * vert_size_B + weights_offset_B], sizeof(float) * 4);
				/*const float sum = (weights[0] + weights[1]) + (weights[2] + weights[3]);
				if(sum < 0.9999f || sum >= 1.0001f)
				{
				if(std::fabs(sum) < 1.0e-3f)
				{
				weights[0] = 1.f;
				std::memcpy(&vert_data[i * vert_size_B + weights_offset_B], weights, sizeof(float) * 4); // Copy back to vertex data
				}
				else
				{
				const float scale = 1 / sum;
				for(int c=0; c<4; ++c) weights[c] *= scale;
				std::memcpy(&vert_data[i * vert_size_B + weights_offset_B], weights, sizeof(float) * 4); // Copy back to vertex data
				}
				}*/

				if(weights[0] == 0 && weights[1] == 0 && weights[2] == 0 && weights[3] == 0)
				{
					weights[0] = 1.f;
					std::memcpy(&vert_data[i * vert_size_B + weights_offset_B], weights, sizeof(float) * 4); // Copy back to vertex data
				}
			}
		}

		//conPrint("Checking weight data took " + timer.elapsedStringNSigFigs(4));
	}
}


// Tests are in BatchedMeshTests
