/*=====================================================================
SocketInterface.h
-----------------
Copyright Glare Technologies Limited 2016 -
=====================================================================*/
#pragma once


#include "../utils/InStream.h"
#include "../utils/OutStream.h"
#include "../utils/ThreadSafeRefCounted.h"
#include "../utils/Reference.h"
class EventFD;


/*=====================================================================
SocketInterface
---------------
Abstract interface class for a socket.
MySocket and TLSSocket, and TestSocket will inherit from this class.
=====================================================================*/
class SocketInterface : public InStream, public OutStream, public ThreadSafeRefCounted
{
public:
	virtual ~SocketInterface();
	
	// Read 1 or more bytes from the socket, up to a maximum of max_num_bytes.  Returns number of bytes read.
	// Returns zero if connection was closed gracefully.
	virtual size_t readSomeBytes(void* buffer, size_t max_num_bytes) = 0;

	virtual void setNoDelayEnabled(bool enabled) = 0; // NoDelay option is off by default.

	virtual bool readable(double timeout_s) = 0;
	virtual bool readable(EventFD& event_fd) = 0; // Block until either the socket is readable or the event_fd is signalled (becomes readable).  
	// Returns true if the socket was readable, false if the event_fd was signalled.
};


typedef Reference<SocketInterface> SocketInterfaceRef;
