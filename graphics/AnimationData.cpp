/*=====================================================================
AnimationData.cpp
-----------------
Copyright Glare Technologies Limited 2021 -
=====================================================================*/
#include "AnimationData.h"


#include "../utils/InStream.h"
#include "../utils/OutStream.h"
#include "../utils/Exception.h"
#include "../utils/StringUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/ContainerUtils.h"
#include <unordered_map>


// Throws an exception if b is false.
static inline void checkProperty(bool b, const char* on_false_message)
{
	if(!b)
		throw glare::Exception(std::string(on_false_message));
}


AnimationNodeData::AnimationNodeData() : retarget_adjustment(Matrix4f::identity()) {}


void AnimationNodeData::writeToStream(OutStream& stream) const
{
	stream.writeData(inverse_bind_matrix.e, sizeof(float) * 16);
	stream.writeData(trans.x, sizeof(float) * 4);
	stream.writeData(rot.v.x, sizeof(float) * 4);
	stream.writeData(scale.x, sizeof(float) * 4);
	stream.writeStringLengthFirst(name);
	stream.writeInt32(parent_index);
}


void AnimationNodeData::readFromStream(InStream& stream)
{
	stream.readData(inverse_bind_matrix.e, sizeof(float) * 16);
	stream.readData(trans.x, sizeof(float) * 4);
	stream.readData(rot.v.x, sizeof(float) * 4);
	stream.readData(scale.x, sizeof(float) * 4);
	name = stream.readStringLengthFirst(10000);
	parent_index = stream.readInt32();
}


void PerAnimationNodeData::writeToStream(OutStream& stream) const
{
	stream.writeInt32(translation_input_accessor);
	stream.writeInt32(translation_output_accessor);
	stream.writeInt32(rotation_input_accessor);
	stream.writeInt32(rotation_output_accessor);
	stream.writeInt32(scale_input_accessor);
	stream.writeInt32(scale_output_accessor);
}


void PerAnimationNodeData::readFromStream(InStream& stream)
{
	translation_input_accessor	= stream.readInt32();
	translation_output_accessor	= stream.readInt32();
	rotation_input_accessor		= stream.readInt32();
	rotation_output_accessor	= stream.readInt32();
	scale_input_accessor		= stream.readInt32();
	scale_output_accessor		= stream.readInt32();
}


void AnimationDatum::writeToStream(OutStream& stream) const
{
	stream.writeStringLengthFirst(name);

	// Write per_anim_node_data
	stream.writeUInt32((uint32)per_anim_node_data.size());
	for(size_t i=0; i<per_anim_node_data.size(); ++i)
		per_anim_node_data[i].writeToStream(stream);
}


void AnimationDatum::readFromStream(uint32 file_version, InStream& stream, std::vector<std::vector<float> >& old_keyframe_times_out, std::vector<js::Vector<Vec4f, 16> >& old_output_data)
{
	name = stream.readStringLengthFirst(10000);

	// Read per_anim_node_data
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw glare::Exception("invalid num");
		per_anim_node_data.resize(num);
		for(size_t i=0; i<per_anim_node_data.size(); ++i)
			per_anim_node_data[i].readFromStream(stream);
	}

	// Read keyframe_times
	if(file_version == 1)
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw glare::Exception("invalid num");
		old_keyframe_times_out.resize(num);
		for(size_t i=0; i<old_keyframe_times_out.size(); ++i)
		{
			std::vector<float>& vec = old_keyframe_times_out[i];
			const uint32 vec_size = stream.readUInt32();
			if(vec_size > 100000)
				throw glare::Exception("invalid vec_size");
			vec.resize(vec_size);
			stream.readData(vec.data(), vec_size * sizeof(float));
		}
	}

	// Read output_data
	if(file_version == 1)
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw glare::Exception("invalid num");
		old_output_data.resize(num);
		for(size_t i=0; i<old_output_data.size(); ++i)
		{
			js::Vector<Vec4f, 16>& vec = old_output_data[i];
			const uint32 vec_size = stream.readUInt32();
			if(vec_size > 100000)
				throw glare::Exception("invalid vec_size");
			vec.resize(vec_size);
			stream.readData(vec.data(), vec_size * sizeof(Vec4f));
		}
	}
}


void AnimationDatum::checkData(const std::vector<std::vector<float> >& keyframe_times, const std::vector<js::Vector<Vec4f, 16> >& output_data) const
{
	// Bounds-check data
	for(size_t i=0; i<per_anim_node_data.size(); ++i)
	{
		// -1 is a valid value
		const PerAnimationNodeData& data = per_anim_node_data[i];
		checkProperty(data.translation_input_accessor  >= -1 && data.translation_input_accessor   < (int)keyframe_times.size(), "invalid input accessor index");
		checkProperty(data.rotation_input_accessor     >= -1 && data.rotation_input_accessor      < (int)keyframe_times.size(), "invalid input accessor index");
		checkProperty(data.scale_input_accessor        >= -1 && data.scale_input_accessor         < (int)keyframe_times.size(), "invalid input accessor index");

		checkProperty(data.translation_output_accessor >= -1 && data.translation_output_accessor  < (int)output_data.size(), "invalid output accessor index");
		checkProperty(data.rotation_output_accessor    >= -1 && data.rotation_output_accessor     < (int)output_data.size(), "invalid output accessor index");
		checkProperty(data.scale_output_accessor       >= -1 && data.scale_output_accessor        < (int)output_data.size(), "invalid output accessor index");

		if(data.translation_input_accessor >= 0) checkProperty(keyframe_times[data.translation_input_accessor].size() >= 1, "invalid num keyframes");
		if(data.rotation_input_accessor    >= 0) checkProperty(keyframe_times[data.rotation_input_accessor   ].size() >= 1, "invalid num keyframes");
		if(data.scale_input_accessor       >= 0) checkProperty(keyframe_times[data.scale_input_accessor      ].size() >= 1, "invalid num keyframes");

		// Output data vectors should be the same size as the input keyframe vectors, when they are used together.
		if(data.translation_input_accessor >= 0) checkProperty(keyframe_times[data.translation_input_accessor].size() == output_data[data.translation_output_accessor].size(), "num keyframes != output_data size");
		if(data.rotation_input_accessor    >= 0) checkProperty(keyframe_times[data.rotation_input_accessor   ].size() == output_data[data.rotation_output_accessor   ].size(), "num keyframes != output_data size");
		if(data.scale_input_accessor       >= 0) checkProperty(keyframe_times[data.scale_input_accessor      ].size() == output_data[data.scale_output_accessor      ].size(), "num keyframes != output_data size");
	}
}


static const uint32 ANIMATION_DATA_VERSION = 2;


void AnimationData::writeToStream(OutStream& stream) const
{
	stream.writeUInt32(ANIMATION_DATA_VERSION);

	stream.writeData(skeleton_root_transform.e, sizeof(float)*16);

	// Write nodes
	stream.writeUInt32((uint32)nodes.size());
	for(size_t i=0; i<nodes.size(); ++i)
		nodes[i].writeToStream(stream);

	// Write sorted_nodes
	stream.writeUInt32((uint32)sorted_nodes.size());
	stream.writeData(sorted_nodes.data(), sizeof(int) * sorted_nodes.size());

	// Write joint_nodes
	stream.writeUInt32((uint32)joint_nodes.size());
	stream.writeData(joint_nodes.data(), sizeof(int) * joint_nodes.size());

	// Write keyframe_times
	stream.writeUInt32((uint32)keyframe_times.size());
	for(size_t i=0; i<keyframe_times.size(); ++i)
	{
		const std::vector<float>& vec = keyframe_times[i];
		stream.writeUInt32((uint32)vec.size());
		stream.writeData(vec.data(), sizeof(float) * vec.size());
	}

	// Write output_data
	stream.writeUInt32((uint32)output_data.size());
	for(size_t i=0; i<output_data.size(); ++i)
	{
		const js::Vector<Vec4f, 16>& vec = output_data[i];
		stream.writeUInt32((uint32)vec.size());
		stream.writeData(vec.data(), sizeof(Vec4f) * vec.size());
	}

	// Write animations
	stream.writeUInt32((uint32)animations.size());
	for(size_t i=0; i<animations.size(); ++i)
		animations[i]->writeToStream(stream);
}


void AnimationData::readFromStream(InStream& stream)
{
	const uint32 version = stream.readUInt32();
	if(version > ANIMATION_DATA_VERSION)
		throw glare::Exception("Invalid animation data version: " + toString(version));

	stream.readData(skeleton_root_transform.e, sizeof(float)*16);

	// Read nodes
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw glare::Exception("invalid num");
		nodes.resize(num);
		for(size_t i=0; i<nodes.size(); ++i)
			nodes[i].readFromStream(stream);
	}

	// Read sorted_nodes
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw glare::Exception("invalid num");
		sorted_nodes.resize(num);
		stream.readData(sorted_nodes.data(), sizeof(int) * sorted_nodes.size());
	}

	// Read joint_nodes
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw glare::Exception("invalid num");
		joint_nodes.resize(num);
		stream.readData(joint_nodes.data(), sizeof(int) * joint_nodes.size());
	}

	// Read keyframe_times
	if(version >= 2)
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw glare::Exception("invalid num");
		keyframe_times.resize(num);
		for(size_t i=0; i<keyframe_times.size(); ++i)
		{
			std::vector<float>& vec = keyframe_times[i];
			const uint32 vec_size = stream.readUInt32();
			if(vec_size > 100000)
				throw glare::Exception("invalid vec_size");
			vec.resize(vec_size);
			stream.readData(vec.data(), vec_size * sizeof(float));
		}
	}

	// Read output_data
	if(version >= 2)
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw glare::Exception("invalid num");
		output_data.resize(num);
		for(size_t i=0; i<output_data.size(); ++i)
		{
			js::Vector<Vec4f, 16>& vec = output_data[i];
			const uint32 vec_size = stream.readUInt32();
			if(vec_size > 100000)
				throw glare::Exception("invalid vec_size");
			vec.resize(vec_size);
			stream.readData(vec.data(), vec_size * sizeof(Vec4f));
		}
	}

	// Read animations
	{
		const uint32 num = stream.readUInt32();
		if(num > 100000)
			throw glare::Exception("invalid num");
		animations.resize(num);
		for(size_t i=0; i<animations.size(); ++i)
		{
			animations[i] = new AnimationDatum();

			std::vector<std::vector<float> > old_keyframe_times;
			std::vector<js::Vector<Vec4f, 16> > old_output_data;

			animations[i]->readFromStream(version, stream, old_keyframe_times, old_output_data);

			// Do backwwards-compat handling
			if(version == 1)
			{
				const int keyframe_times_offset = (int)keyframe_times.size();
				const int output_data_offset = (int)output_data.size();
				ContainerUtils::append(keyframe_times, old_keyframe_times);
				ContainerUtils::append(output_data, old_output_data);

				// Offset accessors
				for(size_t z=0; z<animations[i]->per_anim_node_data.size(); ++z)
				{
					PerAnimationNodeData& data = animations[i]->per_anim_node_data[z];
					if(data.translation_input_accessor != -1)	data.translation_input_accessor		+= keyframe_times_offset;
					if(data.rotation_input_accessor != -1)		data.rotation_input_accessor		+= keyframe_times_offset;
					if(data.scale_input_accessor != -1)			data.scale_input_accessor			+= keyframe_times_offset;
					if(data.translation_output_accessor != -1)	data.translation_output_accessor	+= output_data_offset;
					if(data.rotation_output_accessor != -1)		data.rotation_output_accessor		+= output_data_offset;
					if(data.scale_output_accessor != -1)		data.scale_output_accessor			+= output_data_offset;
				}
			}

			animations[i]->checkData(keyframe_times, output_data);
		}
	}

	

	// Bounds-check data
	for(size_t i=0; i<sorted_nodes.size(); ++i)
		checkProperty(sorted_nodes[i] >= 0 && sorted_nodes[i] < (int)nodes.size(), "invalid sorted_nodes index");

	for(size_t i=0; i<joint_nodes.size(); ++i)
		checkProperty(joint_nodes[i] >= 0 && joint_nodes[i] < (int)nodes.size(), "invalid joint_nodes index");

	for(size_t i=0; i<nodes.size(); ++i)
		checkProperty(nodes[i].parent_index >= -1 && nodes[i].parent_index < (int)nodes.size(), "invalid parent_index index"); // parent_index of -1 is valid.
}


int AnimationData::findAnimation(const std::string& name)
{
	for(size_t i=0; i<animations.size(); ++i)
		if(animations[i]->name == name)
			return (int)i;
	return -1;
}


AnimationNodeData* AnimationData::findNode(const std::string& name) // Returns NULL if not found
{
	for(size_t i=0; i<nodes.size(); ++i)
		if(nodes[i].name == name)
			return &nodes[i];
	return NULL;
}


void AnimationData::loadAndRetargetAnim(InStream& stream)
{
	const std::vector<AnimationNodeData> old_nodes = nodes; // Copy old node data
	//const std::vector<int> old_joint_nodes = joint_nodes;

	//AnimationData new_data;
	//new_data.readFromStream(stream);

	this->readFromStream(stream);

	//joint_nodes = old_joint_nodes;

	std::unordered_map<std::string, int> old_node_names_to_index;
	for(size_t z=0; z<old_nodes.size(); ++z)
		old_node_names_to_index.insert(std::make_pair(old_nodes[z].name, (int)z));

	for(size_t i=0; i<nodes.size(); ++i)
	{
		AnimationNodeData& new_node = nodes[i];

		auto res = old_node_names_to_index.find(new_node.name);
		if(res != old_node_names_to_index.end())
		{
			const int old_node_index = res->second;
			const AnimationNodeData& old_node = old_nodes[old_node_index];

			const Vec4f old_translation = old_node.trans;
			const Vec4f new_translation = new_node.trans;

			Vec4f retarget_trans = old_translation - new_translation;

			Matrix4f retarget_adjustment = Matrix4f::translationMatrix(retarget_trans);

			new_node.inverse_bind_matrix = old_node.inverse_bind_matrix;

			new_node.retarget_adjustment = retarget_adjustment;

			//conPrint(new_node.name + ": new node: " + toString(i) + ", old node: " + toString(old_node_index) + ", retarget_trans: " + retarget_trans.toStringNSigFigs(4));
		}
		else
		{
			//conPrint("could not find old node for new node " + new_node.name);
		}
	}
}


Vec4f AnimationData::getNodePositionModelSpace(const std::string& name, bool use_retarget_adjustment)
{
	const size_t num_nodes = sorted_nodes.size();
	js::Vector<Matrix4f, 16> node_matrices(num_nodes);

	for(int n=0; n<sorted_nodes.size(); ++n)
	{
		const int node_i = sorted_nodes[n];
		const AnimationNodeData& node_data = nodes[node_i];
		
		const Matrix4f rot_mat = node_data.rot.toMatrix();

		const Matrix4f TRS(
			rot_mat.getColumn(0) * copyToAll<0>(node_data.scale),
			rot_mat.getColumn(1) * copyToAll<1>(node_data.scale),
			rot_mat.getColumn(2) * copyToAll<2>(node_data.scale),
			setWToOne(node_data.trans));

		const Matrix4f node_transform = (node_data.parent_index == -1) ? TRS : (node_matrices[node_data.parent_index] * (use_retarget_adjustment ? node_data.retarget_adjustment : Matrix4f::identity()) * TRS);

		node_matrices[node_i] = node_transform;

		if(node_data.name == name)
			return node_transform.getColumn(3);
	}

	return Vec4f(0,0,0,1);
}
