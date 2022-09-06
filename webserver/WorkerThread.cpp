/*=====================================================================
WorkerThread.cpp
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "WorkerThread.h"


#include <ResponseUtils.h>
#include "RequestInfo.h"
#include "WebsiteExcep.h"
#include "Escaping.h"
#include "Log.h"
#include "RequestHandler.h"
#include <ConPrint.h>
#include <Clock.h>
#include <AESEncryption.h>
#include <SHA256.h>
#include <Base64.h>
#include <Exception.h>
#include <MySocket.h>
#include <Lock.h>
#include <StringUtils.h>
#include <PlatformUtils.h>
#include <KillThreadMessage.h>
#include <Parser.h>
#include <MemMappedFile.h>
#include <openssl/err.h>


namespace web
{


static const std::string CRLF = "\r\n";
static const bool VERBOSE = false;


WorkerThread::WorkerThread(int thread_id_, const Reference<SocketInterface>& socket_, 
	const Reference<RequestHandler>& request_handler_, bool tls_connection_)
:	thread_id(thread_id_),
	socket(socket_),
	request_handler(request_handler_),
	tls_connection(tls_connection_)
{
	if(VERBOSE) print("event_fd.efd: " + toString(event_fd.efd));
}


WorkerThread::~WorkerThread()
{
}


// NOTE: not handling "<unit>=-<suffix-length>" form for now
void WorkerThread::parseRanges(const string_view field_value, std::vector<web::Range>& ranges_out)
{
	ranges_out.clear();

	Parser parser(field_value.data(), field_value.size());

	// Parse unit.  Just accept 'bytes' for now
	if(!parser.parseCString("bytes="))
		return;

	while(!parser.eof())
	{
		web::Range range;
		if(!parser.parseInt64(range.start))
			return;
		if(range.start < 0)
			return;
		if(!parser.parseChar('-'))
			return;
		if(parser.eof())
		{
			// "<unit>=<range-start>-" form
			range.end_incl = -1;
			ranges_out.push_back(range);
		}
		else
		{
			if(!parser.parseInt64(range.end_incl))
				return;
			if(range.end_incl < 0)
				return;
			ranges_out.push_back(range);

			parser.parseWhiteSpace(); // NOTE: this needed?
			if(parser.currentIsChar(','))
			{
				parser.consume(',');
				parser.parseWhiteSpace();
			}
			else
				break;
		}
	}
}


// Handle a single HTTP request.
// The request header is in [socket_buffer[request_start_index], socket_buffer[request_start_index + request_header_size])
// Returns if should keep connection alive.
// Advances this->request_start_index. to index after the current request (e.g. will be at the beginning of the next request)
WorkerThread::HandleRequestResult WorkerThread::handleSingleRequest(size_t request_header_size)
{
	// conPrint(std::string(&socket_buffer[request_start_index], &socket_buffer[request_start_index] + request_header_size));

	assert(request_header_size > 0);
	bool keep_alive = true;

	// Parse request
	assert(request_start_index + request_header_size <= socket_buffer.size());
	Parser parser(&socket_buffer[request_start_index], (unsigned int)request_header_size);

	// Parse HTTP verb (GET, POST etc..)
	RequestInfo request_info;
	request_info.tls_connection = tls_connection;
	request_info.client_ip_address = socket->getOtherEndIPAddress();

	string_view verb;
	if(!parser.parseAlphaToken(verb))
		throw WebsiteExcep("Failed to parse HTTP verb");
	request_info.verb = verb.to_string();
	if(!parser.parseChar(' '))
		throw WebsiteExcep("Parse error");

	// Parse URI
	string_view URI;
	if(!parser.parseNonWSToken(URI))
		throw WebsiteExcep("Failed to parse request URI");
	if(!parser.parseChar(' '))
		throw WebsiteExcep("Parse error");

	//------------- Parse HTTP version ---------------
	if(!parser.parseCString("HTTP/"))
		throw WebsiteExcep("Failed to parse HTTP version");
		
	uint32 major_version;
	if(!parser.parseUnsignedInt(major_version))
		throw WebsiteExcep("Failed to parse HTTP major version");

	// Parse '.'
	if(!parser.parseChar('.'))
		throw WebsiteExcep("Parser error");

	uint32 minor_version;
	if(!parser.parseUnsignedInt(minor_version))
		throw WebsiteExcep("Failed to parse HTTP minor version");

	if(major_version == 1 && minor_version == 0)
		keep_alive = false;

	//------------------------------------------------

	// Parse CRLF at end of request header
	if(!parser.parseString(CRLF))
		throw WebsiteExcep("Failed to parse CRLF at end of request header");

	// Print out request args:
	/*conPrint("HTTP Verb: '" + verb + "'");
	conPrint("URI: '" + URI + "'");
	conPrint("HTTP version: HTTP/" + toString(major_version) + "." + toString(minor_version));
*/
	
	// Read header fields

	std::string websocket_key;
	std::string websocket_protocol;
	std::string encoded_websocket_reply_key;

	int content_length = -1;
	while(1)
	{
		if(parser.eof())
			throw WebsiteExcep("Parser error while parsing header fields");
		if(parser.current() == '\r')
			break;

		// Parse header field name
		string_view field_name;
		if(!parser.parseToChar(':', field_name))
			throw WebsiteExcep("Parser error while parsing header fields");
		parser.advance(); // Advance past ':'

		// If there is a space, consume it
		if(parser.currentIsChar(' '))
			parser.advance();
		
		// Parse the field value
		string_view field_value;
		if(!parser.parseToChar('\r', field_value))
			throw WebsiteExcep("Parser error while parsing header fields");

		parser.advance(); // Advance past \r
		if(!parser.parseChar('\n')) // Advance past \n
			throw WebsiteExcep("Parse error");

		Header header;
		header.key = field_name;
		header.value = field_value;
		request_info.headers.push_back(header);

		//TEMP:
		//conPrint(field_name + ": " + field_value);

		if(StringUtils::equalCaseInsensitive(field_name, "content-length"))
		{
			try
			{
				content_length = stringToInt(field_value.to_string());
			}
			catch(StringUtilsExcep& e)
			{
				throw WebsiteExcep("Failed to parse content length: " + e.what());
			}
		}
		else if(StringUtils::equalCaseInsensitive(field_name, "cookie"))
		{
			// Parse cookies
			Parser cookie_parser(field_value.data(), field_value.size());
			// e.g. Cookie: CookieTestKey=CookieTestValue; email-address=nick@indigorenderer.com

			while(1)
			{
				request_info.cookies.resize(request_info.cookies.size() + 1);
				
				// Parse cookie key
				string_view key;
				if(!cookie_parser.parseToChar('=', key))
					throw WebsiteExcep("Parser error while parsing cookies");
				request_info.cookies.back().key = key.to_string();
				cookie_parser.advance(); // Advance past '='
				// Parse cookie value
				string_view value;
				cookie_parser.parseToCharOrEOF(';', value);
				request_info.cookies.back().value = value.to_string();
						
				if(cookie_parser.currentIsChar(';'))
				{
					cookie_parser.advance(); // Advance past ';'
					if(cookie_parser.currentIsChar(' ')) // Advance past ' ' if present.
						cookie_parser.advance();
				}
				else
				{
					break; // Finish parsing cookies.
				}
			}
		}
		else if(StringUtils::equalCaseInsensitive(field_name, "sec-websocket-key"))
		{
			websocket_key = field_value.to_string();

			const std::string magic_key = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"; // From https://tools.ietf.org/html/rfc6455, section 1.3 (page 6)
			const std::string combined_key = websocket_key + magic_key;

			std::vector<unsigned char> digest;
			SHA256::SHA1Hash((const unsigned char*)combined_key.c_str(), (const unsigned char*)combined_key.c_str() + combined_key.size(), digest);

			Base64::encode(&digest[0], digest.size(), encoded_websocket_reply_key);
		}
		else if(StringUtils::equalCaseInsensitive(field_name, "sec-websocket-protocol"))
		{
			websocket_protocol = field_value.to_string();
		}
		else if(StringUtils::equalCaseInsensitive(field_name, "upgrade"))
		{
			// For websockets:
			if(field_value == "websocket")
			{
				
			}
		}
		else if(StringUtils::equalCaseInsensitive(field_name, "range"))
		{
			// Parse ranges, e.g. Range : bytes=167116800-167197941  
			parseRanges(field_value, request_info.ranges);
		}
		else if(StringUtils::equalCaseInsensitive(field_name, "connection"))
		{
			if(field_value == "close")
				keep_alive = false; // Sender has indicated this is the last request if will make before closing the connection: https://datatracker.ietf.org/doc/html/rfc2616#section-14.10
		}
	}

	parser.advance(); // Advance past \r
	if(!parser.parseChar('\n')) // Advance past \n
		throw WebsiteExcep("Parse error");

	// Do websockets handshake
	if(!encoded_websocket_reply_key.empty())
	{
		const std::string response = ""
			"HTTP/1.1 101 Switching Protocols\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Accept: " + encoded_websocket_reply_key + "\r\n"
			"Sec-WebSocket-Protocol: " + websocket_protocol + "\r\n"
			"Cache-Control: no-cache\r\n"
			"Pragma:no-cache\r\n"
			"\r\n";

		socket->writeData(response.c_str(), response.size());
		is_websocket_connection = true;

		// Advance request_start_index to point to after end of this post body.
		request_start_index += request_header_size; // TODO: skip over content as well (if content length > 0)?

		const bool connection_handled_by_handler = handleWebsocketConnection(request_info); // May throw exception

		return connection_handled_by_handler ? HandleRequestResult_ConnectionHandledElsewhere : HandleRequestResult_Finished;
	}

		
	// Get part of URI before CGI string (before ?)
	Parser uri_parser(URI.data(), (unsigned int)URI.size());
	
	string_view path;
	uri_parser.parseToCharOrEOF('?', path);
	request_info.path = path.to_string();

	if(uri_parser.currentIsChar('?')) // Advance past '?' if present.
		uri_parser.advance();

	// Parse URL parameters (stuff after '?')
	while(!uri_parser.eof())
	{
		request_info.URL_params.resize(request_info.URL_params.size() + 1);
				
		// Parse key
		string_view escaped_key;
		if(!uri_parser.parseToChar('=', escaped_key))
			throw WebsiteExcep("Parser error while parsing URL params");
		request_info.URL_params.back().key = Escaping::URLUnescape(escaped_key.to_string());
		uri_parser.advance(); // Advance past '='
			
		// Parse value
		string_view escaped_value;
		uri_parser.parseToCharOrEOF('&', escaped_value);
		request_info.URL_params.back().value = Escaping::URLUnescape(escaped_value.to_string());
						
		if(uri_parser.currentIsChar('&'))
			uri_parser.advance(); // Advance past '&'
		else
			break; // Finish parsing URL params.
	}

	ReplyInfo reply_info;
	reply_info.socket = socket.getPointer();

	if(request_info.verb == "GET")
	{
		//conPrint("thread_id " + toString(thread_id) + ": got GET request, path: " + path); //TEMP

		request_handler->handleRequest(request_info, reply_info);

		// Advance request_start_index to point to after end of this post body.
		request_start_index += request_header_size;
	}
	else if(request_info.verb == "POST")
	{
		// From HTTP 1.1 RFC: "Any Content-Length greater than or equal to zero is a valid value."  (http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html#sec14.13)
		if(content_length < 0)
			throw WebsiteExcep("Invalid content length");

		const size_t MAX_CONTENT_LENGTH = 10000000; // 10 MB or so.
		if(content_length > (int)MAX_CONTENT_LENGTH)
			throw WebsiteExcep("Invalid content length: " + toString(content_length));

		const size_t total_msg_size = request_header_size + (size_t)content_length;

		if(content_length > 0)
		{
			const size_t required_buffer_size = request_start_index + total_msg_size;
			if(socket_buffer.size() < required_buffer_size) // If we haven't read the entire post body yet
			{
				// Read remaining data
				const size_t current_amount_read = socket_buffer.size(); // NOTE: this correct?
				socket_buffer.resize(required_buffer_size);

				socket->readData(&socket_buffer[current_amount_read], required_buffer_size - current_amount_read);
			}

			// Read post content from socket
			const std::string post_content(&socket_buffer[request_start_index + request_header_size], &socket_buffer[request_start_index] + total_msg_size);

			//conPrint("Read content:");
			//conPrint(post_content);

			// Parse form data
			Parser form_parser(post_content.c_str(), (unsigned int)post_content.size());

			while(!form_parser.eof())
			{
				request_info.post_fields.resize(request_info.post_fields.size() + 1);

				// Parse key
				string_view escaped_key;
				if(!form_parser.parseToChar('=', escaped_key))
					throw WebsiteExcep("Parser error while parsing URL params");
				request_info.post_fields.back().key = Escaping::URLUnescape(escaped_key.to_string());

				assert(form_parser.currentIsChar('='));
				form_parser.advance(); // Advance past '='

				// Parse value
				string_view escaped_value;
				form_parser.parseToCharOrEOF('&', escaped_value);
				request_info.post_fields.back().value = Escaping::URLUnescape(escaped_value.to_string());
						
				if(form_parser.currentIsChar('&'))
					form_parser.advance(); // Advance past '&'
				else
					break; // Finish parsing URL params.
			}

			request_info.post_content = post_content;
		}

		request_handler->handleRequest(request_info, reply_info);


		// Advance request_start_index to point to after end of this post body.
		request_start_index += total_msg_size;
	}
	else
	{
		throw WebsiteExcep("Unhandled verb " + request_info.verb);
	}

	return keep_alive ? HandleRequestResult_KeepAlive : HandleRequestResult_Finished;
}


// Move bytes [request_start, socket_buffer.size()) to [0, socket_buffer.size() - request_start),
// then resize buffer to socket_buffer.size() - request_start.
static void moveToFrontOfBuffer(std::vector<char>& socket_buffer, size_t request_start)
{
	assert(request_start < socket_buffer.size());
	if(request_start < socket_buffer.size())
	{
		const size_t len = socket_buffer.size() - request_start; // num bytes to copy
		for(size_t i=0; i<len; ++i)
			socket_buffer[i] = socket_buffer[request_start + i];

		socket_buffer.resize(len);
	}
}


// return true if connection handled by handler
bool WorkerThread::handleWebsocketConnection(RequestInfo& request_info)
{
	if(VERBOSE) print("WorkerThread: Connection upgraded to websocket connection.");

	socket->setNoDelayEnabled(true); // For websocket connections, we will want to send out lots of little packets with low latency.  So disable Nagle's algorithm, e.g. send coalescing.
	socket->enableTCPKeepAlive(30.0f); // Keep alive the connection.

	// If the request handler wants to handle this websocket connection completely, let it.
	if(this->request_handler->handleWebSocketConnection(request_info, socket))
		return true;

	ReplyInfo reply_info;
	reply_info.socket = socket.getPointer();

	while(1) // write to / read from socket loop
	{
		// See if we have any pending data to send in the data_to_send queue, and if so, send all pending data.
		if(VERBOSE) print("WorkerThread: checking for pending data to send...");
		{
			Lock lock(data_to_send.getMutex());

			while(!data_to_send.unlockedEmpty())
			{
				std::string data;
				data_to_send.unlockedDequeue(data);

				// Write the data to the socket
				if(!data.empty())
				{
					if(VERBOSE) print("WorkerThread: calling writeWebsocketTextMessage() with data '" + data + "'...");
					ResponseUtils::writeWebsocketTextMessage(reply_info, data);
				}
			}
		}

		// Process any complete frames we have in socket_buffer.
		// Initial frame header is 2 bytes
		bool keep_processing_frames = true;
		while(keep_processing_frames)
		{
			if(VERBOSE) print("WorkerThread: Processing frame...");

			keep_processing_frames = false; // Break the loop, unless we process a full frame below

			const size_t socket_buf_size = socket_buffer.size();
			if(request_start_index + 2 <= socket_buf_size) // If there are 2 bytes in the buffer:
			{
				const unsigned char* msg = (const unsigned char*)&socket_buffer[request_start_index];
				
				bool got_header = false; // Have we read the full frame header?
							
				const uint32 fin = msg[0] & 0x80; // Fin bit. Indicates that this is the final fragment in a message.
				const uint32 opcode = msg[0] & 0xF; // Opcode.  4 bits
				const uint32 mask = msg[1] & 0x80; // Mask bit.  Defines whether the "Payload data" is masked.
				uint32 payload_len = msg[1] & 0x7F; // Payload length.  7 bits.
				uint32 header_size = mask != 0 ? 6 : 2; // If mask is present, it adds 4 bytes to the header size.
				uint32 cur_offset = 2;

				// "If 126, the following 2 bytes interpreted as a 16-bit unsigned integer are the payload length" - https://tools.ietf.org/html/rfc6455
				if(payload_len == 126)
				{
					if(request_start_index + 4 <= socket_buf_size) // If there are 4 bytes in the buffer:
					{
						payload_len = (msg[2] << 8) | msg[3];
						header_size += 2;
						cur_offset += 2;
						got_header = true;
					}
				}
				else
					got_header = true;

				if(got_header)
				{
					// See if we have already read the full frame body:
					if(request_start_index + header_size + payload_len <= socket_buf_size)
					{
						// Read masking key
						unsigned char masking_key[4];
						if(mask != 0)
						{
							masking_key[0] = msg[cur_offset++];
							masking_key[1] = msg[cur_offset++];
							masking_key[2] = msg[cur_offset++];
							masking_key[3] = msg[cur_offset++];
						}
						else
							masking_key[0] = masking_key[1] = masking_key[2] = masking_key[3] = 0;

						// We should have read the entire header (including masking key) now.
						assert(cur_offset == header_size);

						if(payload_len > 0)
						{
							std::vector<unsigned char> unmasked_payload(payload_len);
							for(uint32 i=0; i<payload_len; ++i)
							{
								assert(request_start_index + header_size + i < socket_buffer.size());
								unmasked_payload[i] = msg[header_size + i] ^ masking_key[i % 4];
							}

							const std::string unmasked_payload_str((const char*)&unmasked_payload[0], (const char*)&unmasked_payload[0] + unmasked_payload.size());

							if(opcode == 0x1 && fin != 0) // Text frame:
							{ 
								if(VERBOSE) print("WorkerThread: Received websocket text frame: '" + unmasked_payload_str + "'");
								request_handler->handleWebsocketTextMessage(unmasked_payload_str, reply_info, this);
							}
							if(opcode == 0x2 && fin != 0) // Binary frame:
							{ 
								if(VERBOSE) print("WorkerThread: Received websocket binary frame: '" + unmasked_payload_str + "'");
								request_handler->handleWebsocketBinaryMessage((const uint8*)unmasked_payload_str.data(), unmasked_payload_str.size(), reply_info, this);
							}
							else if(opcode == 0x8) // Close frame:
							{
								this->request_handler->websocketConnectionClosed(socket, this);
								return false;
							}
							else if(opcode == 0x9) // Ping
							{
								//conPrint("PING");
								// TODO: Send back pong frame
								//ResponseUtils::writeWebsocketTextMessage(reply_info, unmasked_payload_str);
							}
							else
							{
								//conPrint("Got unknown websocket opcode: " + toString(opcode));
							}
						}

						// We have finished processing the websocket frame.  Advance request_start_index
						request_start_index = request_start_index + header_size + payload_len;
						keep_processing_frames = true;

						// Move the remaining data in the buffer to the start of the buffer, and resize the buffer down.
						// We do this so as to not exhaust memory when a connection is open for a long time and receiving lots of frames.
						if(request_start_index < socket_buf_size)
						{
							moveToFrontOfBuffer(socket_buffer, request_start_index);
							request_start_index = 0;
						}
					}
				}
			}
		}

		if(VERBOSE) print("WorkerThread: about to call socket->readable().");

		// At this point we have processed all full frames in socket_buffer.
		// So we need to read more data until we have a full frame, or until the connection is closed.
#if defined(_WIN32) || defined(OSX)
		if(socket->readable(0.05)) // If socket has some data to read from it:
#else
		if(socket->readable(event_fd)) // Block until either the socket is readable or the event fd is signalled, which means we have data to write.
#endif
		{
			if(VERBOSE) print("WorkerThread: socket readable.");

			// Read up to 'read_chunk_size' bytes of data from the socket.  Note that we may read multiple frames at once.
			const size_t old_socket_buffer_size = socket_buffer.size();
			const size_t read_chunk_size = 2048;
			socket_buffer.resize(old_socket_buffer_size + read_chunk_size);
			const size_t num_bytes_read = socket->readSomeBytes(&socket_buffer[old_socket_buffer_size], read_chunk_size);
			if(num_bytes_read == 0) // if connection was closed gracefully
			{
				this->request_handler->websocketConnectionClosed(socket, this);
				return false;
			}
			socket_buffer.resize(old_socket_buffer_size + num_bytes_read); // Trim the buffer down so it only extends to what we actually read.
		}
		else
		{
#if defined(_WIN32) || defined(OSX)
#else
			if(VERBOSE) print("WorkerThread: event FD was signalled.");

			// The event FD was signalled, which means there is some data to send on the socket.
			// Reset the event fd by reading from it.
			event_fd.read();

			if(VERBOSE) print("WorkerThread: event FD has been reset.");
#endif
		}
	}

	this->request_handler->websocketConnectionClosed(socket, this);
	return false;
}


// This main loop code is in a separate function so we can easily return from it, while still excecuting the thread cleanup code in doRun().
void WorkerThread::doRunMainLoop()
{
	is_websocket_connection = false;
	request_start_index = 0; // Index in socket_buffer of the start of the current request.
	size_t double_crlf_scan_position = 0; // Index in socket_buffer of the last place we looked for a double CRLF.
	// Loop to handle multiple requests (HTTP persistent connection)
	while(true)
	{
		// Read up to 'read_chunk_size' bytes of data from the socket.  Note that we may read multiple requests at once.
		const size_t old_socket_buffer_size = socket_buffer.size();
		const size_t read_chunk_size = 2048;
		socket_buffer.resize(old_socket_buffer_size + read_chunk_size);
		const size_t num_bytes_read = socket->readSomeBytes(&socket_buffer[old_socket_buffer_size], read_chunk_size); // Read up to 'read_chunk_size' bytes.
		//print("thread_id " + toString(thread_id) + ": read " + toString(num_bytes_read) + " bytes.");
		if(num_bytes_read == 0) // if connection was closed gracefully
			return;
		socket_buffer.resize(old_socket_buffer_size + num_bytes_read); // Trim the buffer down so it only extends to what we actually read.

		// Process any complete requests
		// Look for the double CRLF at the end of the request header.
		const size_t socket_buf_size = socket_buffer.size();
		if(socket_buf_size >= 4) // Make sure socket_buf_size is >= 4, to avoid underflow and wraparound when computing 'socket_buf_size - 4' below.
			for(; double_crlf_scan_position <= socket_buf_size - 4; ++double_crlf_scan_position)
				if(socket_buffer[double_crlf_scan_position] == '\r' && socket_buffer[double_crlf_scan_position+1] == '\n' && socket_buffer[double_crlf_scan_position+2] == '\r' && socket_buffer[double_crlf_scan_position+3] == '\n')
				{
					// We have found the CRLFCRLF at index 'double_crlf_scan_position'.
					const size_t request_header_end = double_crlf_scan_position + 4;
							
					// Process the request:
					const size_t request_header_size = request_header_end - request_start_index;
					const HandleRequestResult result = handleSingleRequest(request_header_size); // Advances this->request_start_index. to index after the current request (e.g. will be at the beginning of the next request)
					// May also set is_websocket_connection to true.

					double_crlf_scan_position = request_start_index;

					if(result != HandleRequestResult_KeepAlive)
					{
						// If result is HandleRequestResult_ConnectionHandledElsewhere, then another thread might be still using the socket.  So don't call startGracefulShutdown() on it.
						if(result == HandleRequestResult_Finished)
						{
							socket->startGracefulShutdown(); // Tell sockets lib to send a FIN packet to the client.
							socket->waitForGracefulDisconnect(); // Wait for a FIN packet from the client. (indicated by recv() returning 0).  We can then close the socket without going into a wait state.
						}
						return;
					}
				}

		assert(double_crlf_scan_position >= request_start_index);
			
		// If the latest request does not start at byte zero in the buffer,
		// And there is some data stored for the request to actually move,
		// then move the remaining data in the buffer to the start of the buffer.
		if(request_start_index > 0 && request_start_index < socket_buf_size)
		{
			const size_t old_request_start_index = request_start_index;
			moveToFrontOfBuffer(socket_buffer, request_start_index);
			request_start_index = 0;
			double_crlf_scan_position -= old_request_start_index;
		}
	}
}


void WorkerThread::doRun()
{
	try
	{
		doRunMainLoop();
	}
	catch(MySocketExcep& )
	{
		//print("thread_id " + toString(thread_id) + ": Socket error: " + e.what());
	}
	catch(WebsiteExcep& )
	{
		//print("WebsiteExcep: " + e.what());
	}
	catch(glare::Exception& )
	{
		//print("glare::Exception: " + e.what());
	}
	catch(std::exception& e) // catch std::bad_alloc etc..
	{
		print(std::string("Caught std::exception: ") + e.what());
	}

	// An exception may have been thrown from handleWebsocketConnection(), so we need to call websocketConnectionClosed in that case.
	if(is_websocket_connection)
		this->request_handler->websocketConnectionClosed(socket, this);


	// Remove thread-local OpenSSL error state, to avoid leaking it.
	// NOTE: have to destroy socket first, before calling ERR_remove_thread_state(), otherwise memory will just be reallocated.
	socket = NULL;
	ERR_remove_thread_state(/*thread id=*/NULL); // Set thread ID to null to use current thread.
}


/*
moveToFrontOfBuffer():

|         req 0                    |         req 1            |                req 2
|----------------------------------|--------------------------|--------------------------------
                                                              ^               ^               ^
                                           request start index    double_crlf_scan_position   socket_buffer.size()

=>


                   |                req 2
                   |--------------------------------
                   ^               ^               ^
request start index    double_crlf_scan_position   socket_buffer.size()

*/


void WorkerThread::enqueueDataToSend(const std::string& data)
{
	if(VERBOSE) print("WorkerThread::enqueueDataToSend(), data: '" + data + "'");
	data_to_send.enqueue(data);
	event_fd.notify();
}


} // end namespace web
