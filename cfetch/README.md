# cfetch

This is a very simple implementation of a javascript `fetch` function in C. It only works over HTTP/1.1 and just parses a few fields in the response header.

Helpful resources if you are new to http/sockets:
- [C Network Programming](https://beej.us/guide/bgnet/html/split/index.html)
- [HTTP/1.1 Protocol](https://http.dev/1.1)

### Example

In the [test](./test) directory there is a simple go server that responds with "Hello from server!".

```c
#include <stdio.h>
#include "fetch.h"

int main()
{
	HttpResponse res = fetch("localhost:8080", HTTP_GET);
	if (!res.ok)
		return 1;

	printf("Response: %s\n", res.body);

	free_response(res);
	return 0;
}
```
