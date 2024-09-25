#pragma once

#include <stdint.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct HttpHeader
{
	char *raw;
	uint32_t cap;
	uint32_t size; // Byte size of header

	// The following fields are only present in the response header
	
	uint32_t status;
	uint32_t content_length;
	char date[32];
	char http_version[16];
	char content_type[32];
	char last_modified[32];
	char accept_ranges[32];
} HttpHeader;

typedef struct HttpResponse
{
	bool ok;		// If a response was received, not the status code
	char *raw_data; // Pointer to raw http response data. Null terminated.
	char *body;		// Pointer to start of body within raw_data
	HttpHeader header;
} HttpResponse;

typedef enum HttpMethod
{
	HTTP_GET,
	HTTP_POST,
	HTTP_PUT,
} HttpMethod;

// Create new empty header for a request.
HttpHeader new_header(HttpMethod method, const char *url);
// Add a field to the header. Does not check if it has been set already.
void header_add(HttpHeader *h, char *field, char *value);
// Print header values to terminal.
void print_header(HttpHeader h);

// Free response and related data.
void free_response(HttpResponse res);

// Sends an HTTP request over a TCP socket to the specified url. Method
// can either be HTTP_GET, HTTP_POST, or HTTP_PUT. Fetch uses HTTP/1.1
//
//	The URL must be one of the following formats:
//		host/path
//		host:port/path
//		http://host/path
//		x.x.x.x/path
//		x.x.x.x:port/path
//
// The response type includes the raw http response data (raw_data). The
// value of 'ok' is just a check if the request was successful for not, NOT
// if the response was successful (see response.header).
//
// By default, fetch sets the Content-Type field to 'text/html'. To specify
// other fields use fetch_ex.
HttpResponse fetch(const char *url, HttpMethod method);

// Same as fetch but takes all header data as input. There are no default
// fields, only http version and method are set prior to sending the request.
HttpResponse fetch_ex(const char *url, HttpMethod method, HttpHeader header);

