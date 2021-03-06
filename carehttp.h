#ifndef __INCLUDED_CAREHTTP_H__
#define __INCLUDED_CAREHTTP_H__

// Call carehttp_poll with a listening port number to listen for connections on that port.
// a non-null value will be returned if a connction has been made and headers received.
// that connection will then be available for matching and responding to
// the connection will NOT be free'd until a carehttp_finish call has been made.
void* carehttp_poll(int port);

// carehttp_match is used to match request adresses to determine what to respond to.
// it functions similarly to scanf but returns true only when a full match is made
//
// fmt allows for the following fmt codes right now:
//  %XXXs scans x chars until the following char is encountered or the limit XXX-1 is encountered
//  %s is NOT allowed for security reasons and will cause an instant termination
//  %d is used to indicate an integer match that will read back
//  %* is used to match anything until the next char matches (if this occurs at the end of the match then the rest of the string is matched)
int carehttp_match(void *conn,const char *fmt,...);

// request parameters can be fetched with this function
// if an error occured or not string was found then -1 is returned, 
// otherwise the string size is returned
int carehttp_get_param(void *conn,char *out,int outsize,const char *param_name);

// sets the response code to send back to the client.
// can only be called once per request and MUST be sent before any headers.
// a negative response indicates that an error has occured
int carehttp_responsecode(void *conn,int code);

// sets a response header, this will implicitly add an 200/OK response code
// so trying to report an error after a header has been set with this is not possible.
// a negative return value indicates that an error has occured
int carehttp_set_header(void *conn,const char *head,const char *data);

// prints characters to an output
// a negative return indicates that an error has occured
// otherwise return the number of characters printed
int carehttp_printf(void *conn,const char *fmt,...);

// writes a number of binary bytes
// either return the number of characters written or an negtive number on error
int carehttp_write(void *conn,const char *inbuf,int count);

// finalizes 
void carehttp_finish(void *conn);

#endif // __INCLUDED_CAREHTTP_H__
