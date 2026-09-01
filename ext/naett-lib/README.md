# naett /nɛt:/

Tiny HTTP client library in C.

Wraps native HTTP client functionality on macOS, Windows, Linux, iOS and Android in a single, simple non-blocking API.

## Using `naett`

Get the `naett.c` and `naett.h` files and throw them into your project. Check out the [example](./example) for a basic `Makefile` - based setup.

The library needs to be initialized by a call to `naettInit()`. On Android, you need to provide a `JavaVM*` handle in the call to `naettInit()`.
On the other platforms, call with `NULL`.

See `naett.h` for reference docs.

## Platform implementations

`naett` uses the following HTTP client libraries on each platform:

| Platform | Library / component | Build with |
| --- | --- | --- |
| macOS, iOS | NSURLRequest | -framework Foundation |
| Windows | WinHTTP Sessions | -lwinhttp |
| Android | java.net.URL | NDK |
| Linux | libcurl | -lcurl -lpthread |

### Example

```C
#include "naett.h"
#include <unistd.h>
#include <stdio.h>

int main(int argc, char** argv) {
    naettInit(NULL);

    naettReq* req =
        naettRequest("https://foo.site.net", naettMethod("GET"), naettHeader("accept", "application/json"));

    naettRes* res = naettMake(req);

    while (!naettComplete(res)) {
        usleep(100 * 1000);
    }

    if (naettGetStatus(res) < 0) {
        printf("Request failed\n");
        return 1;
    }

    int bodyLength = 0;
    const char* body = naettGetBody(res, &bodyLength);

    printf("Got %d bytes of type '%s':\n", bodyLength, naettGetHeader(res, "Content-Type"));
    printf("%.100s\n...\n", body);
}
```
