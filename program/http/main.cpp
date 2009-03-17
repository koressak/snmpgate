#include <microhttpd.h>

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT 8888

#define POSTBUFFERSIZE 512

#define GET 0
#define POST 1

using namespace std;
	const char* page = "<html><body>ahoj</body></html>";

struct connection_info {
	int connectiontype;
	string data;
	struct MHD_PostProcessor *process;
};


int print_out_key (void *cls, enum MHD_ValueKind kind, const char *key, const char *value)
{
	  printf ("%s = %s\n", key, value);
	    return MHD_YES;
}

int send_page( struct MHD_Connection *connection, const char* page, int error )
{
	int ret;
	struct MHD_Response *response;

	response = MHD_create_response_from_data( strlen( page ), (void *) page, MHD_NO, MHD_NO );

	if ( !response )
		return MHD_NO;
	
	if ( error == 0)
		ret = MHD_queue_response( connection, MHD_HTTP_OK, response );
	else
		ret = MHD_queue_response( connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
	MHD_destroy_response( response );

	return ret;
}


int
iterate_post (void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
              const char *filename, const char *content_type,
	          const char *transfer_encoding, const char *data, size_t off,
	          size_t size)
{

	struct connection_info *con_info = (struct connection_info *) coninfo_cls;

	cout << data <<endl;

	return MHD_NO;

}

void request_completed( void *cls, struct MHD_Connection *connection, 
						void **con_cls, enum MHD_RequestTerminationCode toe )
{
	struct connection_info *con_info = (struct connection_info *) *con_cls;

	if ( con_info == NULL )
		return;
	
	if ( con_info->connectiontype == POST )
	{
		MHD_destroy_post_processor( con_info->process );
	}


	delete(con_info);
	
	*con_cls = NULL;
}



int answer( void *cls, struct MHD_Connection *connection, const char *url,
			const char *method, const char* version, const char* upload_data,
			size_t *upload_data_size, void **con_cls )
{


	struct MHD_Response *response;
	int ret;

	/*
	Parsing request and data
	*/


	/*response = MHD_create_response_from_data ( strlen(page),  (void*) page, MHD_NO, MHD_NO );

	ret = MHD_queue_response( connection, MHD_HTTP_OK, response );
	MHD_destroy_response( response );*/

	if ( NULL == *con_cls)
	{
	printf ("New request %s for %s using version %s\n", method, url, version);
		struct connection_info *con_info = new connection_info;

		
		if ( strcmp( method,"POST") == 0 )
		{
			con_info->process = MHD_create_post_processor( connection, POSTBUFFERSIZE, iterate_post, (void *) con_info);

			if ( NULL == con_info->process )
			{
				delete( con_info );
				return MHD_NO;
			}

			con_info->connectiontype = POST;
		}
		else
			con_info->connectiontype = GET;

		*con_cls = (void *)con_info;
		return MHD_YES;

	}

	if ( strcmp( method, "POST") == 0 )
	{
		struct connection_info *con_info = (struct connection_info *) *con_cls;

		if ( *upload_data_size != 0 )
		{
			MHD_post_process( con_info->process, upload_data, *upload_data_size );
			*upload_data_size = 0;

			return MHD_YES;
		}
		else
			return send_page( connection, "page" ,0 );

		return MHD_NO;
	}

	return send_page( connection, "page", 1 );
}

int main( int argc, char** argv )
{

	struct MHD_Daemon *daemon;

	daemon = MHD_start_daemon( MHD_USE_THREAD_PER_CONNECTION, PORT, NULL, NULL, 
								&answer, NULL, MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL, MHD_OPTION_END);

	
	char x;

	cin >> x;

	MHD_stop_daemon( daemon );


	return 0;
}
