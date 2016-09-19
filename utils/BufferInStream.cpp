/*=====================================================================
BufferInStream.cpp
-------------------
Copyright Glare Technologies Limited 2013 -
Generated at 2013-01-27 17:57:00 +0000
=====================================================================*/
#include "BufferInStream.h"


#include "Exception.h"
#include <cstring> // For std::memcpy


BufferInStream::BufferInStream()
:	read_index(0)
{}


BufferInStream::BufferInStream(const std::vector<unsigned char>& buf_)
:	read_index(0)
{
	buf.resizeUninitialised(buf_.size());
	for(size_t i=0; i<buf_.size(); ++i)
		buf[i] = buf_[i];
}


BufferInStream::~BufferInStream()
{
}


int32 BufferInStream::readInt32()
{
	if(read_index + sizeof(int32) > buf.size())
		throw Indigo::Exception("Read past end of buffer.");

	int32 x;
	std::memcpy(&x, &buf[read_index], sizeof(x));
	read_index += sizeof(x);
	return x;
}


uint32 BufferInStream::readUInt32()
{
	if(read_index + sizeof(uint32) > buf.size())
		throw Indigo::Exception("Read past end of buffer.");

	uint32 x;
	std::memcpy(&x, &buf[read_index], sizeof(x));
	read_index += sizeof(x);
	return x;
}


void BufferInStream::readData(void* target_buf, size_t num_bytes)
{
	if(num_bytes > 0)
	{
		if(read_index + num_bytes > buf.size())
			throw Indigo::Exception("Read past end of buffer.");

		std::memcpy(target_buf, &buf[read_index], num_bytes);
		read_index += num_bytes;
	}
}


bool BufferInStream::endOfStream()
{
	return read_index >= buf.size();
}
