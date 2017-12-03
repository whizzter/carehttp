# CareHTTP
Care HTTP, a caring bare async C http service library

# Purpose
A minimal but useful library for writing C services with C idioms and compact code.


See test.c for a simple but mostly complete usage sample
and see care.h for API documentation.

# Security
Usually C idioms such as scanf and their ilk can be error prone so some
effort has been done to shield programmers from errors in the design.

- %s format specifiers for the matcher MUST include a size or the library will terminate!
  (leaving the size unspecified would be an security issue)
- the library does careful checking of memory allocations and overflows and
   will flag a connection as failed and ready for recycling if any user issued command
   has failed.
- The user should not need to worry about error state and the connection handle will be left
  valid until the user calls finish. The user however can and should inspect error codes to
  terminate processing early in cases of error. 
