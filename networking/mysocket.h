/*=====================================================================
mysocket.h
----------
Copyright Glare Technologies Limited 2013 -
File created by ClassTemplate on Wed Apr 17 14:43:14 2002
=====================================================================*/
#pragma once


#if defined(_WIN32)
// Stop windows.h from defining the min() and max() macros
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

#include "ipaddress.h"
#include "../utils/Platform.h"
#include "../utils/InStream.h"
#include "../utils/OutStream.h"
#include "../utils/ThreadSafeRefCounted.h"
#include "../utils/Reference.h"
#include <string>
class FractionListener;
class EventFD;


class MySocketExcep
{
public:
	MySocketExcep(const std::string& message_) : message(message_) {}
	const std::string& what() const { return message; }
private:
	std::string message;
};


/*=====================================================================
MySocket
--------
TCP socket class.
Blocking.
Does both client and server sockets.
=====================================================================*/
class MySocket : public InStream, public OutStream, public ThreadSafeRefCounted
{
public:
	MySocket(const std::string& hostname, int port); // Client connect via DNS lookup
	MySocket(const IPAddress& ipaddress, int port); // Client connect
	MySocket();

	~MySocket();

	// Connect given a hostname
	void connect(
		const std::string& hostname,
		int port
	);

	// Connect given an IP address
	void connect(
		const IPAddress& ipaddress, 
		const std::string& hostname, // Just for printing out in exceptions.  Can be empty string.
		int port
	);


	void bindAndListen(int port); // throws MySocketExcep


	Reference<MySocket> acceptConnection(); // throws MySocketExcep

	// Calls shutdown on the socket, then closes the socket handle.
	// This will cause the socket to return from any blocking calls.
	void ungracefulShutdown();

	// Wait for the other end to 'gracefully disconnect'
	// This allows the use of the 'Client Closes First' method from http://hea-www.harvard.edu/~fine/Tech/addrinuse.html
	// This is good because it allows the server to rebind to the same port without a long 30s wait on e.g. OS X.
	// Before this is called, the protocol should have told the other end to disconnect in some way. (e.g. a disconnect message)
	void waitForGracefulDisconnect();

	const IPAddress& getOtherEndIPAddress() const{ return otherend_ipaddr; }
	int getOtherEndPort() const { return otherend_port; }


	//void writeInt32(int32 x);
	//void writeUInt32(uint32 x);
	void writeUInt64(uint64 x);
	void writeString(const std::string& s); // Write null-terminated string.

	//-----------------------------------------------------------------
	//if you use this directly you must do host->network and vice versa byte reordering yourself
	//-----------------------------------------------------------------
	void write(const void* data, size_t numbytes);
	void write(const void* data, size_t numbytes, FractionListener* frac);


	//int32 readInt32();
	//uint32 readUInt32();
	uint64 readUInt64();
	const std::string readString(size_t max_string_length); // Read null-terminated string.



	// Read 1 or more bytes from the socket, up to a maximum of max_num_bytes.  Returns number of bytes read.
	// Returns zero if connection was closed gracefully
	size_t readSomeBytes(void* buffer, size_t max_num_bytes);


	void readTo(void* buffer, size_t numbytes);
	void readTo(void* buffer, size_t numbytes, FractionListener* frac);

	void setNoDelayEnabled(bool enabled); // NoDelay option is off by default.

	// Enable TCP Keep-alive, and set the period between keep-alive messages to 'period' seconds.
	void enableTCPKeepAlive(float period);

	bool readable(double timeout_s);
	bool readable(EventFD& event_fd); // Block until either the socket is readable or the event_fd is signalled (becomes readable).  
	// Returns true if the socket was readable, false if the event_fd was signalled.
	


	//------------------------ InStream ---------------------------------
	virtual int32 readInt32();
	virtual uint32 readUInt32();
	virtual void readData(void* buf, size_t num_bytes);
	virtual bool endOfStream();
	//------------------------------------------------------------------

	//------------------------ OutStream --------------------------------
	virtual void writeInt32(int32 x);
	virtual void writeUInt32(uint32 x);
	virtual void writeData(const void* data, size_t num_bytes);
	//------------------------------------------------------------------

private:
	MySocket(const MySocket& other);
	MySocket& operator = (const MySocket& other);

	void shutdown();


	void init();

public:
#if defined(_WIN32)
	typedef SOCKET SOCKETHANDLE_TYPE;
	SOCKETHANDLE_TYPE sockethandle;
#else
	typedef int SOCKETHANDLE_TYPE;
	SOCKETHANDLE_TYPE sockethandle;
#endif
private:
	SOCKETHANDLE_TYPE nullSocketHandle() const;
	bool isSockHandleValid(SOCKETHANDLE_TYPE handle);
	static void initFDSetWithSocket(fd_set& sockset, SOCKETHANDLE_TYPE& sockhandle);


	IPAddress otherend_ipaddr;
	int otherend_port;

	size_t max_buffersize;

	bool connected;

	// We will do a graceful disconnection when closing the socket, unless we have interrupted a read or write operation with an abort callback.
	// In that case we don't want to receive all the pending data on the socket, or the aborting won't have any affect.
	bool do_graceful_disconnect;
};


typedef Reference<MySocket> MySocketRef;
