/*=====================================================================
mysocket.cpp
------------
File created by ClassTemplate on Wed Apr 17 14:43:14 2002
Code By Nicholas Chapman.

Code Copyright Nicholas Chapman 2005.

=====================================================================*/
#include "mysocket.h"


#include "networking.h"
#include "../maths/vec3.h"
#include <vector>
#include "fractionlistener.h"
#include "../utils/stringutils.h"
#include <string.h>
//#include "../indigo/globals.h" // TEMP
#if defined(WIN32) || defined(WIN64)
#else
#include <netinet/in.h>
#include <unistd.h> // for close()
#include <sys/time.h> // fdset
#include <sys/types.h> // fdset
#include <sys/select.h>
#endif


#if defined(WIN32) || defined(WIN64)
typedef int SOCKLEN_TYPE;
#else
typedef socklen_t SOCKLEN_TYPE;

const int SOCKET_ERROR = -1;
#endif


MySocket::MySocket(const std::string& hostname, int port)
{

	thisend_port = -1;
	otherend_port = -1;
	sockethandle = 0;

	assert(Networking::isInited());

	if(!Networking::isInited())
		throw MySocketExcep("Networking not inited or destroyed.");


	//-----------------------------------------------------------------
	//do DNS lookup to get server host IP
	//-----------------------------------------------------------------
	try
	{
		std::vector<IPAddress> serverips = Networking::getInstance().doDNSLookup(hostname);

		assert(!serverips.empty());
		if(serverips.empty())
			throw MySocketExcep("could not lookup host IP with DNS");

		//-----------------------------------------------------------------
		//do the connect using the looked up ip address
		//-----------------------------------------------------------------
		doConnect(serverips[0], port);

	}
	catch(NetworkingExcep& e)
	{
		throw MySocketExcep("DNS Lookup failed: " + std::string(e.what()));
	}
}


MySocket::MySocket(const IPAddress& ipaddress, int port)
{
	thisend_port = -1;
	otherend_port = -1;
	sockethandle = 0;

	assert(Networking::isInited());

	doConnect(ipaddress, port);
}


MySocket::MySocket()
{
	thisend_port = -1;
	sockethandle = 0;

}


void MySocket::doConnect(const IPAddress& ipaddress, int port)
{

	otherend_ipaddr = ipaddress;//remember ip of other end

	assert(port >= 0 && port <= 65536);

	//-----------------------------------------------------------------
	//fill out server address structure
	//-----------------------------------------------------------------
	struct sockaddr_in server_address;

	memset(&server_address, 0, sizeof(server_address));//clear struct
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons((unsigned short)port);
	server_address.sin_addr.s_addr = ipaddress.getAddr();

	//-----------------------------------------------------------------
	//create the socket
	//-----------------------------------------------------------------
	const int DEFAULT_PROTOCOL = 0; // Use default protocol
	sockethandle = socket(PF_INET, SOCK_STREAM, DEFAULT_PROTOCOL);

	if(!isSockHandleValid(sockethandle))
	{
		throw MySocketExcep("could not create a socket.  Error code == " + Networking::getError());
	}

	//-----------------------------------------------------------------
	//Connect to server
	//NOTE: get rid of this blocking connect.
	//-----------------------------------------------------------------
	if(connect(sockethandle, (struct sockaddr*)&server_address, sizeof(server_address)) == -1)
	{
		throw MySocketExcep("could not make a TCP connection with server. Error code == " + Networking::getError());
	}

	otherend_port = port;


	//-----------------------------------------------------------------
	//get the interface address of this host used for the connection
	//-----------------------------------------------------------------
	struct sockaddr_in interface_addr;
	SOCKLEN_TYPE length = sizeof(interface_addr);

	memset(&interface_addr, 0, length);
	const int result = getsockname(sockethandle, (struct sockaddr*)&interface_addr, &length);

	if(result != SOCKET_ERROR)
	{
		thisend_ipaddr = interface_addr.sin_addr.s_addr; //NEWCODE: removed ntohl

		thisend_port = ntohs(interface_addr.sin_port);

		//if(ipaddress != IPAddress("127.0.0.1"))//loopback connection, ignore
		//	Networking::getInstance().setUsedIPAddr(thisend_ipaddr);
	}
}


MySocket::~MySocket()
{
	close();
}


void MySocket::bindAndListen(int port) // throw (MySocketExcep)
{
	assert(Networking::isInited());

	//-----------------------------------------------------------------
	//if socket already exists, destroy it
	//-----------------------------------------------------------------
	if(sockethandle)
	{
		assert(0);
		throw MySocketExcep("Socket already created.");
	}

	//-----------------------------------------------------------------
	//set up address struct for this host, the server.
	//-----------------------------------------------------------------
	struct sockaddr_in server_addr;

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Accept on any interface
	server_addr.sin_port = htons(port);


	//-----------------------------------------------------------------
	//create the socket
	//-----------------------------------------------------------------
	const int DEFAULT_PROTOCOL = 0;			// use default protocol
	sockethandle = socket(PF_INET, SOCK_STREAM, DEFAULT_PROTOCOL);

	if(!isSockHandleValid(sockethandle))
	{
		throw MySocketExcep("could not create a socket.  Error code == " + Networking::getError());
	}


	if(::bind(sockethandle, (struct sockaddr*)&server_addr, sizeof(server_addr)) ==
															SOCKET_ERROR)
		throw MySocketExcep("bind failed");


	const int backlog = 100; // NOTE: wtf should this be?

	if(::listen(sockethandle, backlog) == SOCKET_ERROR)
		throw MySocketExcep("listen failed");

	thisend_port = port;
}


void MySocket::acceptConnection(MySocket& new_socket, SocketShouldAbortCallback* should_abort_callback) // throw (MySocketExcep)
{
	assert(Networking::isInited());

	// Wait until the accept() will succeed.
	while(1)
	{
		const double BLOCK_DURATION = 0.5; // in seconds
		timeval wait_period;
		wait_period.tv_sec = 0; // seconds
		wait_period.tv_usec = (long)(BLOCK_DURATION * 1000000.0); // and microseconds

		// Create the file descriptor set
		fd_set sockset;
		initFDSetWithSocket(sockset, sockethandle);

		if(should_abort_callback && should_abort_callback->shouldAbort())
			throw AbortedMySocketExcep();

		const int num_ready = select(
			sockethandle + SOCKETHANDLE_TYPE(1), // nfds: range of file descriptors to test
			&sockset, // read fds
			NULL, // write fds
			NULL, // error fds
			&wait_period // timeout
			);

		if(should_abort_callback && should_abort_callback->shouldAbort())
			throw AbortedMySocketExcep();

		if(num_ready != 0)
			break;
	}

	// Now this should accept immediately .... :)

	sockaddr_in client_addr; // Data struct to get the client IP
	SOCKLEN_TYPE length = sizeof(client_addr);

	memset(&client_addr, 0, length);

	SOCKETHANDLE_TYPE newsockethandle = ::accept(sockethandle, (sockaddr*)&client_addr, &length);

	if(!isSockHandleValid(newsockethandle))
		throw MySocketExcep("accept failed");

	//-----------------------------------------------------------------
	//copy data over to new socket that will do actual communicating
	//-----------------------------------------------------------------
	new_socket.sockethandle = newsockethandle;

	//-----------------------------------------------------------------
	//get other end ip and port
	//-----------------------------------------------------------------
	new_socket.otherend_ipaddr = IPAddress(client_addr.sin_addr.s_addr);
	//NEWCODE removed ntohl

	new_socket.otherend_port = ntohs(client_addr.sin_port);

	//-----------------------------------------------------------------
	//get the interface address of this host used for the connection
	//-----------------------------------------------------------------
	sockaddr_in interface_addr;
	length = sizeof(interface_addr);

	memset(&interface_addr, 0, length);
	const int result = getsockname(sockethandle, (struct sockaddr*)&interface_addr, &length);

	if(result != SOCKET_ERROR)
	{
		thisend_ipaddr = interface_addr.sin_addr.s_addr;
		//NEWCODE removed ntohl

		thisend_port = ntohs(interface_addr.sin_port);

		//if(thisend_ipaddr != IPAddress("127.0.0.1"))//loopback connection, ignore
		//	Networking::getInstance().setUsedIPAddr(thisend_ipaddr);
	}
}


void MySocket::close()
{
	if(sockethandle)
	{
		//------------------------------------------------------------------------
		//try shutting down the socket
		//------------------------------------------------------------------------
		int result = shutdown(sockethandle,  2);//2 == SD_BOTH, was 1

		//this seems to give an error on non-connected sockets (which may be the case)
		//so just ignore the error
		//assert(result == 0);
		//if(result)
		//{
			//::printWarning("Error while shutting down TCP socket: " + Networking::getError());
		//}

		//-----------------------------------------------------------------
		//close socket
		//-----------------------------------------------------------------
#if defined(WIN32) || defined(WIN64)
		result = closesocket(sockethandle);
#else
		result = ::close(sockethandle);
#endif
		assert(result == 0);
		if(result)
		{
			//::printWarning("Error while destroying TCP socket: " + Networking::getError());
		}
	}

	sockethandle = 0;
}


static const int USE_BUFFERSIZE = 1024;


void MySocket::write(const void* data, int datalen, SocketShouldAbortCallback* should_abort_callback)
{
	write(data, datalen, NULL, should_abort_callback);
}


void MySocket::write(const void* data, int datalen, FractionListener* frac, SocketShouldAbortCallback* should_abort_callback)
{
	const int totalnumbytestowrite = datalen;

	while(datalen > 0)//while still bytes to write
	{
		const int numbytestowrite = myMin(USE_BUFFERSIZE, datalen);

		//------------------------------------------------------------------------
		//NEWCODE: loop until either the prog is exiting or can write to socket
		//------------------------------------------------------------------------

		while(1)
		{
			const float BLOCK_DURATION = 0.5f;
			timeval wait_period;
			wait_period.tv_sec = 0;
			wait_period.tv_usec = (long)(BLOCK_DURATION * 1000000.0f);

			fd_set sockset;
			initFDSetWithSocket(sockset, sockethandle);

			if(should_abort_callback && should_abort_callback->shouldAbort())
				throw AbortedMySocketExcep();

			// Get number of handles that are ready to write to
			const int num_ready = select(sockethandle + SOCKETHANDLE_TYPE(1), NULL, &sockset, NULL, &wait_period);

			if(should_abort_callback && should_abort_callback->shouldAbort())
				throw AbortedMySocketExcep();

			if(num_ready != 0)
				break;
		}

		const int numbyteswritten = send(sockethandle, (const char*)data, numbytestowrite, 0);

		if(numbyteswritten == SOCKET_ERROR)
			throw MySocketExcep("write failed.  Error code == " + Networking::getError());

		datalen -= numbyteswritten;
		data = (void*)((char*)data + numbyteswritten); // Advance data pointer

		//MySocket::num_bytes_sent += numbyteswritten;

		if(frac)
			frac->setFraction((float)(totalnumbytestowrite - datalen) / (float)totalnumbytestowrite);
	}
}


void MySocket::readTo(void* buffer, int readlen, SocketShouldAbortCallback* should_abort_callback)
{
	readTo(buffer, readlen, NULL, should_abort_callback);
}


void MySocket::readTo(void* buffer, int readlen, FractionListener* frac, SocketShouldAbortCallback* should_abort_callback)
{
	const int totalnumbytestoread = readlen;

	while(readlen > 0) // While still bytes to read
	{
		const int numbytestoread = myMin(USE_BUFFERSIZE, readlen);

		//------------------------------------------------------------------------
		//Loop until either the prog is exiting or have incoming data
		//------------------------------------------------------------------------
		while(1)
		{
			const float BLOCK_DURATION = 0.5f;
			timeval wait_period;
			wait_period.tv_sec = 0;
			wait_period.tv_usec = (long)(BLOCK_DURATION * 1000000.0f);

			fd_set sockset;
			initFDSetWithSocket(sockset, sockethandle);

			if(should_abort_callback && should_abort_callback->shouldAbort())
				throw AbortedMySocketExcep();

			// Get number of handles that are ready to read from
			const int num_ready = select(sockethandle + SOCKETHANDLE_TYPE(1), &sockset, NULL, NULL, &wait_period);

			if(should_abort_callback && should_abort_callback->shouldAbort())
				throw AbortedMySocketExcep();

			if(num_ready != 0)
				break;
		}

		//------------------------------------------------------------------------
		//do the read
		//------------------------------------------------------------------------
		const int numbytesread = recv(sockethandle, (char*)buffer, numbytestoread, 0);

		if(numbytesread == SOCKET_ERROR) // Connection was reset/broken
			throw MySocketExcep("Read failed, error: " + Networking::getError());
		else if(numbytesread == 0) // Connection was closed gracefully
			throw MySocketExcep("Connection Closed.");

		readlen -= numbytesread;
		buffer = (void*)((char*)buffer + numbytesread);

		//MySocket::num_bytes_rcvd += numbytesread;

		if(frac)
			frac->setFraction((float)(totalnumbytestoread - readlen) / (float)totalnumbytestoread);

		if(should_abort_callback && should_abort_callback->shouldAbort())
			throw AbortedMySocketExcep();
	}
}


const std::string MySocket::readString(size_t max_string_length, SocketShouldAbortCallback* should_abort_callback) // Read null-terminated string.
{
	std::string s;
	while(1)
	{
		char c;
		this->readTo(&c, sizeof(c), should_abort_callback);
		if(c == 0) // If we just read the null terminator.
			return s;
		else
			s += std::string(1, c); // Append the character to the string. NOTE: maybe faster way to do this?

		if(s.size() > max_string_length)
			throw MySocketExcep("String too long");
	}
	return s;
}


/*void MySocket::readMessage(std::string& data_out, FractionListener* frac)
{


	std::vector<char> buf(USE_BUFFERSIZE);
	data_out = "";

	bool done = false;

	while(!done)
	{

		//-----------------------------------------------------------------
		//read data to buf
		//-----------------------------------------------------------------
		const int num_chars_received =
				recv(sockethandle, &(*buf.begin()), USE_BUFFERSIZE, 0);


   		if(num_chars_received == SOCKET_ERROR)
   		{
			throw MySocketExcep("socket error while reading data.  Error code == " + Networking::getError());
		}

		MySocket::num_bytes_rcvd += num_chars_received;

		//-----------------------------------------------------------------
		//append buf to end of string 'data_out'
		//-----------------------------------------------------------------
		const int writeindex = data_out.size();
		data_out.resize(data_out.size() + num_chars_received);
		for(int i=0; i<num_chars_received; ++i)
			data_out[writeindex + i] = buf[i];

   		if(num_chars_received < USE_BUFFERSIZE)
   			done = true;

		if(::threadsShutDown())
			throw MySocketExcep("::threadsShutDown() == true");

   }
}*/




void MySocket::write(float x, SocketShouldAbortCallback* should_abort_callback)
{
	//conPrint("MySocket::write(float"); // TEMP

	//*((unsigned int*)&x) = htonl(*((unsigned int*)&x));//convert to network byte ordering

	union data
	{
		float x;
		unsigned int i;
	};


	union data d;
	d.x = x;

	const unsigned int i = htonl(d.i);

	write(&i, sizeof(float), should_abort_callback);
}

void MySocket::write(int x, SocketShouldAbortCallback* should_abort_callback)
{
	//conPrint("MySocket::write(int"); // TEMP

	union data
	{
		int si;
		unsigned int i;
	};

	//*((unsigned int*)&x) = htonl(*((unsigned int*)&x));//convert to network byte ordering

	union data d;
	d.si = x;

	const unsigned int i = htonl(d.i);

	write(&i, sizeof(int), should_abort_callback);
}


void MySocket::writeUInt32(uint32 x, SocketShouldAbortCallback* should_abort_callback)
{
	const uint32 i = htonl(x); // Convert to network byte ordering.
	write(&i, sizeof(uint32), should_abort_callback);
}


void MySocket::writeUInt64(uint64 x, SocketShouldAbortCallback* should_abort_callback)
{
	//NOTE: not sure if this byte ordering is correct.
	union data
	{
		uint32 i32[2];
		uint64 i64;
	};

	data d;
	d.i64 = x;
	writeUInt32(d.i32[0], should_abort_callback);
	writeUInt32(d.i32[1], should_abort_callback);
}


void MySocket::write(char x, SocketShouldAbortCallback* should_abort_callback)
{
	write(&x, sizeof(char), should_abort_callback);
}


void MySocket::write(unsigned char x, SocketShouldAbortCallback* should_abort_callback)
{
	write(&x, sizeof(unsigned char), should_abort_callback);
}


void MySocket::write(const std::string& s, SocketShouldAbortCallback* should_abort_callback) // writes string
{
	//write(s.c_str(), s.size() + 1);

	// Write length of string
	write((int)s.length(), should_abort_callback);

	// Write string data
	write(&(*s.begin()), s.length(), should_abort_callback);
}


void MySocket::writeString(const std::string& s, SocketShouldAbortCallback* should_abort_callback) // Write null-terminated string.
{
	this->write(
		s.c_str(), 
		s.size() + 1, // num bytes: + 1 for null terminator.
		should_abort_callback
	);
}


/*
void MySocket::write(const Vec3& vec)
{
	write(vec.x);
	write(vec.y);
	write(vec.z);
}*/


void MySocket::write(unsigned short x, SocketShouldAbortCallback* should_abort_callback)
{
	x = htons(x);//convert to network byte ordering

	write(&x, sizeof(unsigned short), should_abort_callback);
}


/*float MySocket::readFloat()
{
	float x;
	read(&x, sizeof(float));

	x = ntohl(x);//convert from network to host byte ordering

	return x;
}

int MySocket::readInt()
{
	int x;
	read(&x, sizeof(int));

	x = ntohl(x);//convert from network to host byte ordering

	return x;
}

char MySocket::readChar()
{
	char x;
	read(&x, sizeof(char));

	return x;
}*/


void MySocket::readTo(float& x, SocketShouldAbortCallback* should_abort_callback)
{
	union data
	{
		float x;
		unsigned int i;
	};

	data d;
	readTo(&d.i, sizeof(float), should_abort_callback);
	d.i = ntohl(d.i);
	x = d.x;
}


float MySocket::readFloat(SocketShouldAbortCallback* should_abort_callback)
{
	union data
	{
		float x;
		uint32 i;
	};
	data d;
	readTo(&d.i, sizeof(float), should_abort_callback);
	d.i = ntohl(d.i);
	return d.x;
}


void MySocket::readTo(int& x, SocketShouldAbortCallback* should_abort_callback)
{
	union data
	{
		int si;
		uint32 i;
	};
	data d;
	readTo(&d.i, sizeof(uint32), should_abort_callback);
	d.i = ntohl(d.i);
	x = d.si;
}


int MySocket::readInt32(SocketShouldAbortCallback* should_abort_callback)
{
	union data
	{
		int si;
		uint32 i;
	};
	data d;
	readTo(&d.i, sizeof(uint32), should_abort_callback);
	d.i = ntohl(d.i);
	return d.si;
}


uint32 MySocket::readUInt32(SocketShouldAbortCallback* should_abort_callback)
{
	uint32 x;
	readTo(&x, sizeof(uint32), should_abort_callback);
	return ntohl(x);
}


uint64 MySocket::readUInt64(SocketShouldAbortCallback* should_abort_callback)
{
	//NOTE: not sure if this byte ordering is correct.
	union data
	{
		uint32 i32[2];
		uint64 i64;
	};

	data d;
	d.i32[0] = readUInt32(should_abort_callback);
	d.i32[1] = readUInt32(should_abort_callback);
	return d.i64;
}


void MySocket::readTo(char& x, SocketShouldAbortCallback* should_abort_callback)
{
	readTo(&x, sizeof(char), should_abort_callback);
}

void MySocket::readTo(unsigned char& x, SocketShouldAbortCallback* should_abort_callback)
{
	readTo(&x, sizeof(unsigned char), should_abort_callback);
}

void MySocket::readTo(unsigned short& x, SocketShouldAbortCallback* should_abort_callback)
{
	readTo(&x, sizeof(unsigned short), should_abort_callback);
	x = ntohs(x);//convert from network to host byte ordering
}

/*
void MySocket::readTo(Vec3& vec)
{
	readTo(vec.x);
	readTo(vec.y);
	readTo(vec.z);
}
*/
void MySocket::readTo(std::string& s, int maxlength, SocketShouldAbortCallback* should_abort_callback)
{
	/*std::vector<char> buffer(1000);

	int i = 0;
	while(1)
	{
		buffer.push_back('\0');
		readTo(buffer[i]);

		if(buffer[i] == '\0')
			break;

		//TODO: break after looping to long
		++i;
	}

	s = &(*buffer.begin());*/

	// Read length
	int length;
	readTo(length, should_abort_callback);
	if(length < 0 || length > maxlength)
		throw MySocketExcep("String was too long.");
	//std::vector<char> buffer(length);
	//readTo(buffer

	s.resize(length);
	readTo(&(*s.begin()), length, NULL, should_abort_callback);
}

void MySocket::readTo(std::string& x, int numchars, FractionListener* frac, SocketShouldAbortCallback* should_abort_callback)
{
//	x.clear();
	assert(numchars >= 0);

	x.resize(numchars);
	readTo(&(*x.begin()), numchars, frac, should_abort_callback);
}

//-----------------------------------------------------------------
//funcs for measuring data rate
//-----------------------------------------------------------------
/*int MySocket::getNumBytesSent()
{
	return num_bytes_sent;
}
int MySocket::getNumBytesRcvd()
{
	return num_bytes_rcvd;
}

void MySocket::resetNumBytesSent()
{
	num_bytes_sent = 0;
}
void MySocket::resetNumBytesRcvd()
{
	num_bytes_rcvd = 0;
}*/

/*void MySocket::pollRead(std::string& data_out)
{
	data_out.resize(0);

	const float BLOCK_DURATION = 0.0f;
	timeval wait_period;
	wait_period.tv_sec = 0;
	wait_period.tv_usec = (long)(BLOCK_DURATION * 1000000.0f);

	//FD_SET fd_set;
	fd_set sockset;
	initFDSetWithSocket(sockset, sockethandle); //FD_SET(sockethandle, &sockset);

	//if(Networking::getInstance().shouldSocketsShutDown())
	//	throw MySocketExcep("::Networking::shouldSocketsShutDown() == true");

	//get number of handles that are ready to read from
	const int num_ready = select(sockethandle+1, &sockset, NULL, NULL, &wait_period);

	//if(Networking::getInstance().shouldSocketsShutDown())
	//	throw MySocketExcep("::Networking::shouldSocketsShutDown() == true");

	if(num_ready == 0)
		return;


	data_out.resize(USE_BUFFERSIZE);
	const int numbytesread = recv(sockethandle, &(*data_out.begin()), data_out.size(), 0);


	if(numbytesread == SOCKET_ERROR || numbytesread == 0)
		throw MySocketExcep("read failed, error code == " + Networking::getError());


	data_out.resize(numbytesread);

	//data_out.resize(1024);//max bytes to read at once

	//const int num_pending_bytes = recv(sockethandle, &(*data_out.begin()), data_out.size(), MSG_PEEK);

	////now remove from the input queue
	//data_out.resize(num_pending_bytes);

	//if(num_pending_bytes != 0)
	//{

	//	const int numread = recv(sockethandle, &(*data_out.begin()), data_out.size(), 0);

	//	assert(numread == data_out.size());
	//}
}*/


bool MySocket::readable(double timeout_s)
{
	assert(timeout_s < 1.0);

	timeval wait_period;
	wait_period.tv_sec = 0;
	wait_period.tv_usec = (long)(timeout_s * 1000000.0);

	fd_set sockset;
	initFDSetWithSocket(sockset, sockethandle);

	// Get number of handles that are ready to read from
	const int num = select(sockethandle+1, 
		&sockset, // Read fds
		NULL, // Read fds
		NULL, // Read fds
		&wait_period
		);

	return num > 0;
}


bool MySocket::isSockHandleValid(SOCKETHANDLE_TYPE handle)
{
#if defined(WIN32) || defined(WIN64)
	return handle != INVALID_SOCKET;
#else
	return handle >= 0;
#endif
}


void MySocket::setNagleAlgEnabled(bool enabled_)//on by default.
{
#if defined(WIN32) || defined(WIN64)
	BOOL enabled = enabled_;

	::setsockopt(sockethandle, //socket handle
		IPPROTO_TCP, //level
		TCP_NODELAY, //option name
		(const char*)&enabled,//value
		sizeof(BOOL));//size of value buffer
#else
	//int enabled = enabled_;
	//TODO
	assert(0);
#endif
}


void MySocket::initFDSetWithSocket(fd_set& sockset, SOCKETHANDLE_TYPE& sockhandle)
{
	FD_ZERO(&sockset);

	//FD_SET doesn´t seem to work when targeting x64 in gcc.
#ifdef COMPILER_GCC
	//sockset.fds_bits[0] = sockhandle;
	FD_SET(sockhandle, &sockset);
#else
	FD_SET(sockhandle, &sockset);
#endif
}


//int MySocket::num_bytes_sent = 0;
//int MySocket::num_bytes_rcvd = 0;
