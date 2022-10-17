/*=====================================================================
WorkerThread.h
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include <MessageableThread.h>
#include <Platform.h>
#include <MyThread.h>
#include <EventFD.h>
#include <ThreadManager.h>
#include <SocketInterface.h>
#include <set>
#include <string>
#include <vector>
#include <string_view.h>
class WorkUnit;
class PrintOutput;
class ThreadMessageSink;
class DataStore;


namespace web
{


class RequestInfo;
class RequestHandler;
class Range;


/*=====================================================================
WorkerThread
------------
Webserver worker thread
=====================================================================*/
class WorkerThread : public MessageableThread
{
public:
	WorkerThread(int thread_id, const Reference<SocketInterface>& socket, 
		const Reference<RequestHandler>& request_handler, bool tls_connection);

	virtual ~WorkerThread();

	virtual void doRun();

	friend class WorkerThreadTests;


	void doRunMainLoop();
private:
	void handleWebsocketConnection(RequestInfo& request_info);
	
	// Returns if should keep connection alive
	enum HandleRequestResult
	{
		HandleRequestResult_KeepAlive,
		HandleRequestResult_Finished,
		HandleRequestResult_ConnectionHandledElsewhere
	};
	HandleRequestResult handleSingleRequest(size_t request_header_size);
public:
	static void parseRanges(const string_view field_value, std::vector<web::Range>& ranges_out); // Just public for testing
private:

	int thread_id;
	
	Reference<SocketInterface> socket;
	
	std::vector<char> socket_buffer;
	Reference<RequestHandler> request_handler;
	size_t request_start_index; // Start index of request that we current processing.

	bool tls_connection;
};


} // end namespace web
