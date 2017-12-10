# CareHTTP
Care HTTP, a minimal async C http service library suitable for
embedding into other applications or microservices.

# Design
A user should not need to worry about HTTP or socket details while keeping
with regular C idioms and allowing for compact code with special cases.
 
# Usage
See carehttp.h for API documentation, usage description follows below.

The library does not expect to own the application loop but is rather polled by
the application to listen to a certain port with the poll function.

```
	void *req=carehttp_poll(8080);
	if (*req) {
		// do request processing here
	}
```
In the above code req is void pointer and the poll function waits for connections on port 8080.

If no request was found poll will return a void pointer, otherwise it will return a handle to the
connection that can be used by the other API functions, the most important one being match:

```
	if (carehttp_match(req,"/"))
```
This will match requests to the "/" url (for example http://localhost:8080/ ),
while fixed URL's are useful you usually want to be able to match patterns of URL's.

To keep with C idioms match accepts strict C style format specifiers.
```
	if (carehttp_match(req,"/blog/%200s",post))
```

This will match "/blog/" urls and fill in the char array at post with up to 199 characters
(for defensive purposes the 200 above is counted to be a buffer size and space is thus reserved
for null terminators).

*carehttp_match* also supports %d matching to read in integers (including negative ones) and
a %* match to read in an arbitrary amount of characters that are ignored.

Both %s and %\* will use the character following the parameter as a terminator characther.

Applications using html form parameters can use the *carehttp_get_param* function to get
those parameters.

Once the application has determined the input the output can be produced, the easiest way is to
use the *carehttp_printf* function

```
	carehttp_printf(req,"Hello world %d",20);
```

Here regular printf format specifiers can be used, also available is *carehttp_set_header* and
*carehttp_write* functions that are useful for binary data transmission.

When data output is done the request is finished a call is made to push out the data to
the client and invalidate the request handle (using the request handle after this is undefined
as the finish call marks it available for the library to free up).
```
	carehttp_finish(req);
```

See test.c for a simple but mostly complete usage sample

# Compiling
Compiling under linux,bsd and osX with the built in compilers should not require anything extra.

Compiling with visual studio on windows should also work without anything extra.

Compiling with mingw for win32 however will require you to add the winsock library.
```
 gcc.exe -o care.exe test.c carehttp.c -lwsock32
```
If compiling with an IDE such as code::blocks then add wsock32 into the linker options (this has the same effect as the line above)

# Security
Usually C idioms such as scanf and their ilk can be error prone so some
effort has been done to shield programmers from errors in the design.

- %s format specifiers for the matcher MUST include a size or the library will terminate the program!
  (leaving the size unspecified would be an security issue so this is considered a hard error)
- the library does careful checking of memory allocations and overflows and
   will flag a connection as failed if anything goes wrong internally.
- The user should not need to worry about error state and the connection handle will be left
  valid until the user calls finish. The user however can and should inspect error codes to
  terminate processing early in cases of error. 
