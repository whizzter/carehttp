#include <stdio.h>

#include "carehttp.h"

int main(int argc,char **argv) {

	// keep working forever
	while(1) {
		void *req;
		
		// check if we have a request pending
		if (req=carehttp_poll(8080)) {
			char name[400];
			int a,b;

			if (carehttp_match(req,"/hello/%400s/%d/%d",name,&a,&b)) {
				// we will match here if we have a response with an match to the above pattern
				int i;
				carehttp_printf(req,"Hello %s %d %d\n",name,a,b);
				for (i=0;i<a;i++) {
					carehttp_printf(req,"Hello again %s\n",name);
				}

			} else if (carehttp_match(req,"/blog%3s%*",name)) {
				// this pattern will match 1-2 characters and ignore any following but accept the match
				carehttp_printf(req,"Blog matched %s\n",name);

			} else if (carehttp_match(req,"/blag%3s",name)) {
				// this patern will match 1-2 characters and fail on longer strings
				carehttp_printf(req,"Blag matched %s\n",name);

			} else if (carehttp_match(req,"/")) {
				char extravalue[50];
				// this will match exactly one url
				carehttp_printf(req,"This is the base URL\n");

				// we also check for an extra parameter
				if (0<=carehttp_get_param(req,extravalue,50,"extra")) {
					carehttp_printf(req,"User supplied an extra parameter '%s'\n",extravalue);
				}
			} else {
				// othewise report an 404 error
				carehttp_responsecode(req,404);
				carehttp_printf(req,"Resource not found");
			}
			
			// once we've finished with processing we finish the connection and send out the data.
			carehttp_finish(req);
		}
	}

	return 0;
}
