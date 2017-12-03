// see care.h for usage

// uncomment this line to get verbose output to debug the servercode
//#define VERBOSE
#define VERBOSELEVEL 2

// Some defines to detect win32 compilation if it isn't specified already!
#ifndef WIN32
 #ifdef _MSC_VER
  #define WIN32
 #endif

 #ifdef _WIN32
  #define WIN32
 #endif
 #ifdef __MINGW32__
  #define WIN32
 #endif
#endif

// now for some includes and globals
#ifdef WIN32
#include <windows.h>
#include <winsock.h>

#ifdef _MSC_VER
 // this pragma is visual c++ specific
 #pragma comment(lib,"wsock32.lib")
#endif

static int wsIsInit=0;
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>

// win32 has these separated so we have an ifdef to emulate it
#define closesocket(x) close(x)
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "carehttp.h"

// there's slightly different ways of setting the nonblocking
// status of a socket between *nix platforms and win32
static void carehttp_socket_set_nonblocking(int sock) {
#ifdef WIN32
	unsigned long nbl=1;
	ioctlsocket(sock,FIONBIO,&nbl);
#else
	int flags=fcntl(sock,F_GETFL);
	flags|=O_NONBLOCK;
	fcntl(sock,F_SETFL,flags);
#endif
}

// async read errors aren't directly distinguishable from plain
// errors so we nede to poll the system to query the error state.
static int carehttp_socket_wasblock(int sock) {
#ifdef WIN32
	return WSAGetLastError()==WSAEWOULDBLOCK;
#else
	return errno==EAGAIN;
#endif
}

// a byte buffer is used to buffer up data.
struct carehttp_buf {
	int length;
	int cap;
	char *data;
};

// this struct contains both listening sockets (unparented) and data sockets(parented)
struct carehttp_connection {
	struct carehttp_connection *next;
	int handle;

	struct carehttp_connection *parent;

	// this member indicates if a connection is visible to a user, if it is then
	// don't deallocate it until it has been made invisible.
	int visible;

	// input state can have a bunch of different values.
	// * for listener sockets it doubles as a port number 
	// 
	// regular states:
	// * negative values indicates an error and tells the system to clean up and not perform more operations.
	// * 0 means that we're still reading the header.
	// * 1 means that we've finished the initial headers and are producing data
	// * (todo) 2+ means that we are expecting post data either directly or via chunked encodings.

	int instate;
	int headsize;
	struct carehttp_buf inbuf;

	// output state and buffers (we have a circular array of buffers)
	// usually the woutidx and woutidx buffers are used for writing headers and data respectivly.
	// routidx while not equal to woutidx will mean that that buffer is being transmitted.
#define OUTBUFS 4
	int routidx;  // the sending buffer index
	int roffset;  // the offset inside that buffer
	int woutidx;  // the user facing output buffer index
	struct carehttp_buf outbufs[OUTBUFS];
};

// a 64k tmp buffer for the recv command (data is then copied to indivdual connection buffers for parsing)
static char tmpbuf[1<<16];
// our single linked list of connections
static struct carehttp_connection *connections=0;

// a utility function to reserve space in our buffers
// returns -1 if we're out of memory
static int carehttp_buf_reserve(struct carehttp_buf *line,int sz) {
	if (line->cap<sz) {
		void *old=line->data;
		int newsize=sz+200; // 200 is a random addition in case we get a small trailing read afterwards
		line->data=realloc(line->data,newsize+1);
		if (!line->data) {
			// some kind of out-of-memory condition, dispose of data and return an error.
			line->cap=0;
			line->length=0;
			free(old);
			return -1;
		}
		line->cap=newsize;
	}
	return 0;
}

// poll listening on the specified port and for connections on port-associated sockets
void* carehttp_poll(int port) {
	struct carehttp_connection **pcon=&connections; // keep a pointer to the previous link to a connection so we can update

	int found_at_port=0;    // is the requested port opened?
	int first_connection=1; // is this the first connection we will open?

	void *outval=0; // will be a header complete connection opened from the same port as specified in the argument
	int work=0;     // work flag indicates if this function should sleep when finished

	// process all active connections and listeners
	while(*pcon) {
		struct carehttp_connection *cur=*pcon;

		// parentless connections are listeners
		if (!cur->parent) {
			int sock=-1;
			struct carehttp_connection *newconn;
			struct sockaddr_in sa;
#ifdef WIN32
			int sasize=sizeof(sa);
#else
			socklen_t sasize=sizeof(sa);
#endif

#ifdef VERBOSE
#if VERBOSELEVEL >= 5
			fprintf(stderr,"Listening conn %p with handle %d\n",cur,cur->handle);
#endif
#endif

			first_connection=0; // a newly created one cannot be the first connection since we've already have one open!
			if (cur->instate==port)
				found_at_port=1; // we have the argument port open!

			// accept call is done if we have a valid handle, this accepts new sockets from a listening port.
			if (cur->handle!=-1)
				sock=accept(cur->handle,(struct sockaddr*)&sa,&sasize);

			if (sock!=-1) {
				work=1;
				// a new socket was opened, allocate an associated connection
				newconn=(struct carehttp_connection*)calloc(1,sizeof(struct carehttp_connection));
#ifdef VERBOSE
				fprintf(stderr,"Got a new connection %p:%d\n",newconn,sock);
#endif
				if (!newconn) {
					// not enough memory, close it and continue processing.
					closesocket(sock);
				} else {
					newconn->parent=cur;  // set the parent port
					newconn->handle=sock; // set the socket
					carehttp_socket_set_nonblocking(sock); // and make it non-blocking
					newconn->next=*pcon;
					*pcon=newconn;
				}
			}
		} else if (cur->parent->instate==port && !outval) {
			// data socket not a listening socket, so let's handle processing here!
			// also only handle processing for those connections matching the polled port while there hasn't been a completed request yet.

#ifdef VERBOSE
#if VERBOSELEVEL > 3
			fprintf(stderr,"Conn %p:%d state %d %d\n",cur,cur->handle,cur->instate,cur->headsize);
#endif
#endif

			if (cur->instate<0) {
				int i;
				conerr:
#ifdef VERBOSE
					fprintf(stderr,"Closing conn %p with socket %d\n",cur,cur->handle);
#endif
					// close our socket
					if (cur->handle!=-1)
						closesocket(cur->handle);
					cur->handle=-1;
					// free our buffers
					if (cur->inbuf.data)
						free(cur->inbuf.data);
					cur->inbuf.data=0;
					for (i=0;i<OUTBUFS;i++) {
						if (cur->outbufs[i].data)
							free(cur->outbufs[i].data);
						cur->outbufs[i].data=0;
					}
					// unlink this ptr if it isn't visible
					if (!cur->visible) {
						*pcon=cur->next;
						free(cur);
					}
					continue;
			} else {
				int rc;
				int i;
				// while we have buffers pending to be sent to the network queue them up for sending.
				while(cur->routidx!=cur->woutidx) {
					int wr;
					// take the first buffer
					struct carehttp_buf *buf=cur->outbufs+cur->routidx;
					
					// is this buffer empty or did we finish sending?
					if (cur->roffset>=buf->length) {
						// if so, advance to the next buffer
						cur->routidx=(cur->routidx+1)%OUTBUFS;
						// begin from the start of that one.
						cur->roffset=0;
						// and clear this output buffer for the next round of data.
						buf->length=0;
						// go to the next output buffer
						continue;
					}
					
					// try to send the remainder in one go if possible.
					wr=send(cur->handle,buf->data+cur->roffset,buf->length-cur->roffset,0);
					if (wr<0) {
						// blocking or some kind of error
						if (!carehttp_socket_wasblock(cur->handle))
							goto conerr; // not blocking so an real error
					} else {
						work|=wr>0;
						// consume the sent amount of bytes
						cur->roffset+=wr;
						// finished sending?
						if (cur->roffset==buf->length) {
							continue;
						} else {
							// could not send all pending data in the buffer so let's try again later.
							break;
						}
					}
				}
				// read in some data
				rc=recv(cur->handle,tmpbuf,sizeof(tmpbuf),0);
				if (rc<0) {
					if (!carehttp_socket_wasblock(cur->handle))
						goto conerr; // A real error so we need to close and clean up
				} else if (rc>0) {
					if (carehttp_buf_reserve(&cur->inbuf,cur->inbuf.length+rc+1)) {
						// error allocating memory, clean up the connection
						goto conerr;
					}
					memcpy(cur->inbuf.data+cur->inbuf.length,tmpbuf,rc); // add new data
					cur->inbuf.length+=rc; // update length
					cur->inbuf.data[cur->inbuf.length]=0; // null terminate the buffer
					work=1;
				}
				// do header parsing assuming we are in that input state and have space to produce new output!
				if (cur->instate==0 && ((cur->woutidx+2)%OUTBUFS)!=cur->routidx && ((cur->woutidx+3)%OUTBUFS)!=cur->routidx) {
					for (;cur->headsize<cur->inbuf.length-3;cur->headsize++) {
						if (memcmp(cur->inbuf.data+cur->headsize,"\r\n\r\n",4))
							continue;
						cur->instate=1;
						cur->headsize+=4;
						break;
					}
					// did we find the end of headers and enter state 1 ?
					if (cur->instate==1) {
						// replace cr/lf chars with 0's so we can separate the request line and headers
						for (i=0;i<cur->headsize;i++) {
							if (cur->inbuf.data[i]=='\r' || cur->inbuf.data[i]=='\n') {
								cur->inbuf.data[i]=0;
							}
						}
						// flag the output
						cur->visible=1;
						outval=cur;
						work=1;
					}
				} else {
					if (cur->instate==0) {
#ifdef VERBOSE
						fprintf(stderr,"Cannot process request yet... waiting for data to be flushed!\n");
#endif
					}
				}
				// en of non-error processing.
			}
		}
		// update our next ref
		pcon=&cur->next;
	}

	// no connection listening at the port was found so proceed to create a connection for this purpose
	if (!found_at_port) {
		// this below is assumed to succeed since it is to be run upon startup.
		struct carehttp_connection *nc=*pcon=(struct carehttp_connection*)malloc(sizeof(struct carehttp_connection));
		if (!nc) {
			fprintf(stderr,"Error, could not allocate memory for an listening socked\n");
			exit(-1);
		}
		nc->instate=port;
		nc->parent=0;
		nc->next=0;

#ifdef WIN32
		if (first_connection) { 
			WSADATA wsadata;
			if (WSAStartup( MAKEWORD(1,0),&wsadata) ) {
				fprintf(stderr,"Winsock startup problem\n");
				exit(-1);
			}
			wsIsInit=1;
		}
#endif

		do {
			struct sockaddr_in sa;
			if (-1==(nc->handle=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP))) {
				fprintf(stderr,"Could not open create socket for port %d\n",port);
				break;
			}
			memset(&sa,0,sizeof(sa));
			sa.sin_family=AF_INET;
			sa.sin_addr.s_addr=0;
			sa.sin_port=htons(port);
			if (bind(nc->handle,(struct sockaddr*)&sa,sizeof(sa))) {
				fprintf(stderr,"Could not bind port %d\n",port);
				closesocket(nc->handle);
				nc->handle=-1;
				break;
			}
			if (listen(nc->handle,50)) {
				fprintf(stderr,"Could not listen on port %d\n",port);
				closesocket(nc->handle);
				nc->handle=-1;
				break;
			}
			carehttp_socket_set_nonblocking(nc->handle);
			work=1;
		} while(0);
	}

	if (!work) {
#ifdef WIN32
		Sleep(1);
#else
		usleep(10000); // a value smaller than this seems to hog more than 1% cpu on BSD and Linux machines
#endif
	}

	return outval;
}

int carehttp_responsecode(void *conn,int code) {
	struct carehttp_connection *cur=conn;
	struct carehttp_buf *buf=cur->outbufs+(cur->woutidx);
	const char *err="OK";

	if (cur->instate<0)
		return -1;

	if (buf->length)
		return 1; // don't update an already set response code

	switch(code) {
	case 200 : err="OK"; break;
	case 404 : err="Not found"; break;
	default:   err="Err"; break;
	}

	if (carehttp_buf_reserve(buf,buf->length+20+strlen(err))) { // approximate reserve
		cur->instate=-1; // out of memory, shut down this connection.
		return -1;
	}
	buf->length+=sprintf(buf->data+buf->length,"HTTP/1.1 %3d %s\r\n",code,err);
	return 0;
}
int carehttp_header(void *conn,const char *head,const char *data) {
	struct carehttp_connection *cur=conn;
	struct carehttp_buf *buf=cur->outbufs+(cur->woutidx);

	if (cur->instate<0)
		return -1;

	// make a default 200 response incase we haven't already
	if (!buf->length) {
		if (carehttp_responsecode(conn,200)<0)
			return -1;
	}
	// reserve memory for response code
	if (carehttp_buf_reserve(buf,buf->length+strlen(head)+strlen(data)+5)) {
		cur->instate=-1;
		return -1;
	}
	buf->length+=sprintf(buf->data+buf->length,"%s: %s\r\n",head,data);

	return 0;
}
int carehttp_match(void *conn,const char *fmt,...) {
	struct carehttp_connection *cur=conn;
	int eor=0;  // end of request
	int i=0; // headerline index
	int sz=cur->headsize;
	int mt=-1; // matchtype: -1 indicates the default
	int msz=0; // matchsize

	char *sdp=0; // match string dest ptr
	int  *ddp=0; // match decimal dest ptr
	int   num=0; // number parsing number
	int  sign=0;

	char *rd=cur->inbuf.data;

	va_list args;

	if (cur->instate!=1)
		return 0;

	va_start(args,fmt);

	// skip past the method
	while(rd[i] && !isspace(rd[i]))
		i++;
	// skip past the spaces afterwards
	while(rd[i] && isspace(rd[i]))
		i++;

	// now scan the request string
	while(rd[i]) {
		char c=rd[i];
		eor=!c || c=='?' || c==' ';
#ifdef VERBOSE
#if VERBOSELEVEL > 2
		fprintf(stderr,"Parsing: %c at %d with state mtint:%d mtchar:%c rest:%d num:%d\n",c,i,mt,mt,msz,num);
#endif
#endif
		if (eor)
			break;
		if (mt=='s') {
			if (!msz || *fmt==c) {
				*sdp=0;
				mt=-1;
				continue; // no more space, try continue scanning afterwards
			}
			*sdp++=c;
			*sdp=0;  // add null term for each char so we can jump away when we're out of bounds or encounter our endchar
			i++;
			msz--;
			continue;
		} else if (mt=='d') {
			if (!sign) {
				if (c=='-') {
					sign=-1;
					i++;
					continue;
				} else if (c=='+') {
					sign=1;
					i++;
					continue;
				}
			}
			if (!sign)
				sign=1;
			if ('0'<=c && c<='9') {
				num=num*10 + (c-'0');
				*ddp=num*sign;
				i++;
				continue;
			} else {
				mt=-1;
				continue;
			}
		} else if (mt=='*') {
			if (*fmt==c) {
				mt=-1;
			} else {
				i++; // just gobble up the rest!
			}
			continue;
		} else {
			if (*fmt=='%') {
				fmt++;
				// parse the request size from the format string
				msz=0;
				while(isdigit(*fmt)) {
					msz=msz*10 + (*fmt-'0');
					fmt++;
				}
				mt=(*fmt++)&0x7f; // negative codes might be reserved for some other purpose
				if (mt=='s') {
					if (msz<1) {
						fprintf(stderr,"SECURITY ERROR, %%s given without a size to carehttp_match\n");
						exit(-1);
					}
					msz--;
					sdp=va_arg(args,char*); // get the dest ptr
					continue;
				} else if (mt=='d') {
					num=sign=0; // reset parsing data
					ddp=va_arg(args,int*); // get the dest ptr
					continue;
				} else if (mt=='*') {
					continue;
				}
			} else {
				mt=*fmt++;
				if (!mt)
					break; // reached end of fmt string without ending the request
			}
			if (mt!=c)
				break;
			i++;
			mt=-1;
		}
	}
	// if we had a any-match termination afterwards then allow the match
	if (!strcmp(fmt,"%*")) {
		fmt+=2;
	}
	va_end(args);
	return eor && !*fmt;
}

int carehttp_printf(void *conn,const char *fmt,...) {
	struct carehttp_connection *cur=conn;
	struct carehttp_buf *buf=cur->outbufs+(cur->woutidx+1);
	int len;
	va_list args;

	if (cur->instate<0)
		return -1;

	// calculate space (TODO: add support for compilers that doesn't support vsnprintf?)
	va_start(args,fmt);
	len=1+vsnprintf(NULL,0,fmt,args);
	va_end(args);

	if (carehttp_buf_reserve(buf,buf->length+len+1)) {
		cur->instate=-1;
		return -1; // could not print
	}

	// now actually print something out and increase the size
	va_start(args,fmt);
	buf->length+=len=vsnprintf(buf->data+buf->length,len,fmt,args);
	va_end(args);

	return len;
}

int carehttp_write(void * conn,const char *inbuf,int count) {
	struct carehttp_connection *cur=conn;
	struct carehttp_buf *buf=cur->outbufs+(cur->woutidx+1);

	if (cur->instate<0)
		return -1;

	// reserve space for the write
	if (carehttp_buf_reserve(buf,buf->length+count+1)<0) {
		cur->instate=-1;
		return -1;
	}
	// copy in the data and increase the buf size
	memcpy(buf->data+buf->length,inbuf,count);
	buf->length+=count;
	buf->data[buf->length]=0; // null terminate
	return count;
}

void carehttp_finish(void *conn) {
	struct carehttp_connection *cur=conn;
	char tmp[40];

	// this call will force the connection to be non-visible to a user so that it can be deallocated.
	cur->visible=0;

	// wrong state when calling this, ignore any effects.
	if (cur->instate!=1)
		return;

	// setup the content length automatically
	{
		sprintf(tmp,"%d",cur->outbufs[cur->woutidx+1].length);
		if (carehttp_header(conn,"Content-Length",tmp)<0) {
			cur->instate=-1;
			return;
		}
	}

	// terminate headers with a newline
	{
		struct carehttp_buf *buf=cur->outbufs+cur->woutidx;
		if (carehttp_buf_reserve(buf,buf->length+3)<0) {
			cur->instate=-1; // flag error!
			return;
		}
		strcpy(buf->data+buf->length,"\r\n");
		buf->length+=2;
	}

	// swap the buffers
	cur->woutidx=(cur->woutidx+2)%OUTBUFS;

	// on the request side dump the request header data to process the next request on this socket.
	memmove(cur->inbuf.data,cur->inbuf.data+cur->headsize,cur->inbuf.length-cur->headsize);
	cur->inbuf.length-=cur->headsize;
	cur->instate=0; // reset the parsing state once we've finished
	cur->headsize=0;
}

