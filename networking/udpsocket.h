/*=====================================================================
udpsocket.h
-----
File created by ClassTemplate on Sat Apr 13 04:40:34 2002
Code By Nicholas Chapman.
=====================================================================*/
#ifndef __UDPSOCKET_H_666_
#define __UDPSOCKET_H_666_

#pragma warning(disable : 4786)//disable long debug name warning

//class UDPPacketHandler;
#include <winsock.h>
//#include "winsock2.h"
class IPAddress;
class Packet;
#include "../utils/mutex.h"
#include <string>
#include "port.h"

class UDPSocketExcep
{
public:
	UDPSocketExcep(){}
	UDPSocketExcep(const std::string& message_) : message(message_) {}
	~UDPSocketExcep(){}

	const std::string& what() const { return message; }
private:
	std::string message;
};


/*=====================================================================
UDPSocket
---------
Provides an interface to the net's connectionless service


NOTE: testme
=====================================================================*/
class UDPSocket
{
public:
	/*=====================================================================
	UDPSocket
	---
	
	=====================================================================*/
	UDPSocket();//create outgoing only socket

	//UDP(int port);//create incoming/outgoing socket: listen on a particluar port

	~UDPSocket();

	void bindToPort(const Port& port);//listen/send from a particluar port

	const Port& getThisEndPort() const { return thisend_port; }


	//not threadsafe
	void sendPacket(const Packet& packet, const IPAddress& dest_ip, const Port& destport);
	void sendPacket(const char* data, int datalen, const IPAddress& dest_ip, const Port& destport);


	//returns num bytes read.  blocking.  notthreadsafe
	void readPacket(Packet& packet_out, IPAddress& sender_ip_out, Port& senderport_out, IPAddress& thisend_ip_out);
	int readPacket(char* buf, int buflen, IPAddress& sender_ip_out, Port& senderport_out, 
							IPAddress& thisend_ip_out, bool peek = false);

	//returns true if packet waiting. NOTE: should set setBlocking = false
	bool pollForPacket(Packet& packet_out, IPAddress& sender_ip_out, Port& senderport_out, IPAddress& thisend_ip_out);
		
	void setBlocking(bool blocking);

	void enableBroadcast();

		//void setPacketHandler(UDPPacketHandler* handler);

	//int getCSPort() const;



	//-----------------------------------------------------------------
	//funcs for measuring data rate
	//-----------------------------------------------------------------
	static int getNumBytesSent();
	static int getNumBytesRcvd();

	static void resetNumBytesSent();
	static void resetNumBytesRcvd();


private:


		//UDPPacketHandler* handler;
	//SOCKET insocket_handle;
	//SOCKET outsocket_handle;
	SOCKET socket_handle;



	Port thisend_port;


	static int num_bytes_sent;
	static int num_bytes_rcvd;
};



#endif //__UDP_H_666_




