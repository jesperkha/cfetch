#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "fetch.h"

#define KB(n) (1024 * (n))
#define MB(n) (1024 * KB(n))

#define HTTP_PORT "80"
#define HTTP_HEADER_CAP KB(1)

#define errorf(fmt, ...) { fprintf(stderr, "fetch: "fmt"\n", __VA_ARGS__); }
#define error(msg) { fprintf(stderr, "fetch: %s\n", msg); }

// 'Maximum' size of http header. The standard doesnt
// specify a max size, but 8kb/4kb is a common max.
#define HEADER_BUF_SIZE KB(8)
char header_buf[HEADER_BUF_SIZE];

char *methods[] = {
	[HTTP_GET] = "GET",
	[HTTP_POST] = "POST",
	[HTTP_PUT] = "PUT",
};

char url_buffer[2048];
uint32_t url_buffer_len = 0;

// Returns the host name part of the url. Copies to global buffer, do not free.
static char *get_url_hostname(const char *url)
{
	if (!strncmp("https://", url, 8))
	{
		error("https not supported");
		exit(1);
	}
	if (!strncmp("http://", url, 7))
		url += 7;

	char *port_begin = strchr(url, ':');
	char *path_begin = strchr(url, '/');
	char *ptr = url_buffer + url_buffer_len;

	int size;
	if (port_begin != NULL)
	{
		size = port_begin - url;
		strncpy(ptr, url, size);
	}
	else if (path_begin != NULL)
	{
		size = path_begin - url;
		strncpy(ptr, url, size);
	}
	else
	{
		size = strlen(url);
		strcpy(ptr, url);
	}

	url_buffer_len += size+1;
	ptr[size] = 0;
	return ptr;
}

// Returns the path part of the url. Copies to global buffer, do not free.
static char *get_url_path(const char *url)
{
	char *start = strchr(url, '/');
	char *ptr = url_buffer + url_buffer_len;

	int size;

	if (start != NULL)
	{
		size = strlen(url);
		strcpy(ptr, start);
	}
	else
	{
		size = 1;
		ptr[0] = '/';
	}

	url_buffer_len += size+1;
	return ptr;
}

static char *get_url_port_or_default(const char *url, char *_default)
{
	char *port_begin = strchr(url, ':');
	if (port_begin == NULL)
		return _default;
	
	char *path_begin = strchr(port_begin, '/');
	int length;
	if (path_begin == NULL)
		length = strlen(port_begin+1);
	else
		length = path_begin - port_begin;

	char *ptr = url_buffer + url_buffer_len;
	strncpy(ptr, port_begin+1, length);

	url_buffer_len += length;
	url_buffer[url_buffer_len] = 0;
	return ptr;
}

static void free_header(HttpHeader h)
{
	free(h.raw);
}

// Writes raw data to header. Does not format or add newlines or null terminator
static void header_write(HttpHeader *h, char *data, size_t size)
{
	if (h->size + size > h->cap)
	{
		h->raw = realloc(h->raw, h->cap*2);
		h->cap *= 2;
		assert(h->raw != NULL);
	}

	memcpy(h->raw + h->size, data, size);
	h->size += size;
}

// Appends newline and null character suffix
static void header_close(HttpHeader *h)
{
	header_write(h, "\n\0", 2);
}

// Sets a field/value pair in the header. Does not check if the field is already
// set, may cause a bad request.
void header_add(HttpHeader *h, char *field, char *value)
{
	header_write(h, field, strlen(field));
	header_write(h, ": ", 2);
	header_write(h, value, strlen(value));
	header_write(h, "\n", 1);
}

// Returns a new header with the method, request type, and http version already
// set. Also sets the host field based in the given url.
HttpHeader new_header(HttpMethod method, const char *url)
{
	char *hostname = get_url_hostname(url);
	char *path = get_url_path(url);

	HttpHeader h = {0};
	h.cap = HTTP_HEADER_CAP;
	h.raw = calloc(h.cap, sizeof(char));
	assert(h.raw != NULL);

	char request[128];
	sprintf(request, "%s %s HTTP/1.1\n", methods[method], path);
	header_write(&h, request, strlen(request));

	header_add(&h, "Host", hostname); // 'Required' field

	return h;
}

// Returns socket fd of established tcp connection at port 80. Returns -1 on error.
static int tcp_connect(const char *ip, char *port)
{
	struct addrinfo hints = {0};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	// hints.ai_flags = AI_PASSIVE; // Debug

	struct addrinfo *res = malloc(sizeof(hints));
	int error_code = getaddrinfo(ip, port, &hints, &res);
	if (error_code != 0)
	{
		errorf("%s", gai_strerror(error_code));
		return -1;
	}

	int sockfd;
	for (struct addrinfo *p = res; p != NULL; p = p->ai_next)
	{
		sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sockfd == -1)
			continue;

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
		{
			sockfd = -1;
			continue;
		}

		if (p == NULL)
		{
			error("failed to connect socket");
			return -1;
		}

		// Connection made
		break;
	}

	freeaddrinfo(res);
	return sockfd;
}

// Parses http header fields from given data block.
static HttpHeader parse_http_header(char *data)
{
	HttpHeader h = {0};
	char *p = strtok(data, " ");

	// Get http version, status code, and message
	strcpy(h.http_version, p);
	h.status = atoi(strtok(NULL, " "));

	strtok(NULL, "\n");

	while ((p = strtok(NULL, "\n")) != NULL)
	{
		char *field = p;
		char *value = strchr(p, ' ')+1;
		int field_len = value - field - 2;

		int linelen = strlen(p);
		if (p[linelen-1] == '\r')
			p[linelen-1] = 0;

#define match(s, v)	\
		if (!strncmp((s), field, field_len)) strcpy((v), value);

#define match_int(s, v) \
		if (!strncmp((s), field, field_len)) (v) = atoi(value);

		match("Date", h.date);
		match("Content-Type", h.content_type);
		match("Last-Modified", h.last_modified);
		match("Accept-Ranges", h.accept_ranges);
		match_int("Content-Length", h.content_length);

		h.size = p - data;

		if (*p == 0) // End of header section
			break;
	}

	h.size += 2; // Last \n\r
	return h;
}

// Peeks http header from socket. Blocks when waiting for recv. Returns true on success.
static bool peek_header_from_socket(int sockfd, HttpHeader *header)
{
	ssize_t size = recv(sockfd, header_buf, HEADER_BUF_SIZE, MSG_PEEK);
	if (size <= 0)
		return false;

	header_buf[size] = 0; // parse_http_header expects NULL terminated string
	*header = parse_http_header(header_buf);
	return true;
}

HttpResponse fetch_ex(const char *url, HttpMethod method, HttpHeader header)
{
	HttpResponse res_error = {.ok = false};

	// Close given header
	header_close(&header);

	// Attempt TCP connection with server
	char *hostname = get_url_hostname(url);
	char *port = get_url_port_or_default(url, HTTP_PORT);
	int sockfd = tcp_connect(hostname, port);
	if (sockfd <= 0)
		return res_error;

	// Send request
	int n = send(sockfd, header.raw, header.size, 0);
	if (n <= 0)
	{
		error("failed to send");
		return res_error;
	}

	// Await response and parse the header first
	HttpHeader res_h;
	if (!peek_header_from_socket(sockfd, &res_h))
	{
		error("failed to read response");
		return res_error;
	}

	// Read in the entire response now that we know the total size
	size_t resbuf_size = res_h.size + res_h.content_length + 1;
	char *res_buffer = malloc(resbuf_size);
	if (recv(sockfd, res_buffer, resbuf_size, 0) < 0)
		return res_error;

	res_buffer[resbuf_size-1] = 0;
	char *res_body = res_buffer + res_h.size;

	HttpResponse res = {
		.ok = true,
		.header = res_h,
		.raw_data = res_buffer,
		.body = res_body,
	};

	close(sockfd);
	free_header(header);

	return res;
}

HttpResponse fetch(const char *url, HttpMethod method)
{
	HttpHeader h = new_header(method, url);
	header_add(&h, "Content-Type", "text/html");
	return fetch_ex(url, method, h);
}

void print_header(HttpHeader h)
{
	printf("Status: %d\n", h.status);
	printf("HTTP Version: %s\n", h.http_version);
	printf("Content-Type: %s\n", h.content_type);
	printf("Content-Length: %d\n", h.content_length);
	printf("Accept-Ranges: %s\n", h.accept_ranges);
	printf("Date: %s\n", h.date);
	printf("Last-Modified: %s\n", h.last_modified);
}

void free_response(HttpResponse res)
{
	free(res.raw_data);
}

