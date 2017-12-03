#include <stdio.h>

#include "care.h"

int main(int argc,char **argv) {

	// keep working forever
	while(1) {
		void *req;
		
		// check if we have a request pending
		if (req=care_poll(8080)) {
			char name[400];
			int a,b;

			if (care_match(req,"/hello/%400s/%d/%d",name,&a,&b)) {
				// we will match here if we have a response with an match to the above pattern
				int i;
				care_printf(req,"Hello %s %d %d\n",name,a,b);
				for (i=0;i<a;i++) {
					care_printf(req,"Hello again %s\n",name);
				}

			} else if (care_match(req,"/blog%3s%*",name)) {
				// this pattern will match 1-2 characters and ignore any following but accept the match
				care_printf(req,"Blog matched %s\n",name);

			} else if (care_match(req,"/blag%3s",name)) {
				// this patern will match 1-2 characters and fail on longer strings
				care_printf(req,"Blag matched %s\n",name);

			} else if (care_match(req,"/")) {
				// this will match exactly one url
				care_printf(req,"This is the base URL\n");

			} else {
				/ othewise report an 404 error
				care_responsecode(req,404);
				care_printf(req,"Resource not found");
			}
			
			// once we've finished with processing we finish the connection and send out the data.
			care_finish(req);
		}
	}

	return 0;
}
