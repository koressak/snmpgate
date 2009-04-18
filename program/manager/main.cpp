/************************************

	Manager pro XML agenty

	@autor= Tomas Hroch
	@mail= hrocht2@fel.cvut.cz


*************************************/

/*
arg library
*/
#include <argtable2.h>
/*
Normal includes
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <iostream>
#include <string.h>
#include <fstream>

/*
STL lib
*/
#include <list>

/*
LibCUrl include
*/
#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>

/*
MicroHTTPD inc
*/
#include <microhttpd.h>


/*
Definitions
*/
#define MANAGER_LINE_MAX 	512
#define POST 				1
#define GET 				0
#define POSTBUFFERSIZE		2048

using namespace std;



/**********************************
	LibCURL - struktury a fce
************************************/
struct MemoryStruct {
	char *memory;
	size_t size;

	MemoryStruct()
	{
		memory = NULL;
		size = 0;
	}
};

//static char errorBuffer[CURL_ERROR_SIZE];

static size_t write_data( void *ptr, size_t size, size_t nmemb, void *data)
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = ( struct MemoryStruct *) data;
	
	mem->memory = (char *) realloc( mem->memory,  mem->size + 1 + realsize );

	if ( mem->memory )
	{
		memcpy( &(mem->memory[mem->size]), ptr, realsize );
		mem->size += realsize;
		mem->memory[mem->size] = 0;
    }

	
	return realsize;

}

/***********************************
	MicroHTTPD fce a struktury
************************************/
struct connection_info {
	int connectiontype;
	string data;
	struct MHD_PostProcessor *process;
};

int send_blank_response( MHD_Connection *conn )
{
	int ret;
	struct MHD_Response *response;

	const char *page = "";

	response = MHD_create_response_from_data( strlen( page ), (void *) page, MHD_NO, MHD_NO );

	if ( !response )
		return MHD_NO;
	
	ret = MHD_queue_response( conn, MHD_HTTP_OK, response );
	MHD_destroy_response( response );

	return ret;
}

int
iterate_post (void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
              const char *filename, const char *content_type,
	          const char *transfer_encoding, const char *data, uint64_t off,
	          size_t size)
{

	//TODO: dodelat reakci jestli je to notification (event) si subscription/distribution
	struct connection_info *con_info = (struct connection_info *) coninfo_cls;

	if ( strcmp( content_type, "text/xml" ) != 0 )
	{
		con_info->data = "";
		return MHD_NO;
	}
	
	con_info->data = string( data );
	cout << "Subscription data received: " << endl;
	cout << data <<endl;
	cout <<endl;

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


	/*
	Parsing request and data
	*/

	if ( NULL == *con_cls)
	{
		cout << "Got an subscription response\n";
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
			return send_blank_response( connection );

		/*
		Nebudeme posilat zadnou odpoved. Proste to ukoncime a finito
		*/
		//return MHD_NO;
	}

	//return send_page( connection, "page", 1 );
	return MHD_NO;
}

/***********************************
	Ostatni potrebne fce
***********************************/
struct manager_data
{
	list<string> messages;
	int subscr_count;
	bool has_subscription;

	string agent_url;
	string password;

	manager_data()
	{
		has_subscription = false;
		password = "";
		subscr_count = 0;
	}

};


/*
Vytvori finalni obalku pro vsechny message, ktere chceme poslat
*/
string * create_message( struct manager_data *data )
{
	string *msg = new string;
	list<string>::iterator it;

	*msg = "<message password=\"";
	*msg += data->password;
	*msg += "\">\n";

	//vsechny zpravy z msg data pujdou sem
	for ( it = data->messages.begin(); it != data->messages.end(); it++ )
	{
		*msg += (*it);
		*msg += "\n";
	}


	*msg += "</message>\n";

	return msg;

}


/*
Otevre msg soubor a vyparsuje jednotlive zpravy
*/
int parse_msg_file( struct manager_data *data, const char *filename, int protocol )
{
	unsigned int pos1, pos2; //pozice pro splitovani stringu
	ifstream fin;
	string line;
	list<string> msg_parts;
	string tmp_str;
	char id_buf[100];
	int msg_id;

	fin.open( filename, ios::in );

	if ( !fin.is_open() )
	{
		cerr << "Cannot open file: " << filename << " for reading.\n";
		return -1;
	}

	msg_id = 1;

	/*
	Kazda radka obsahuje jednu zpravu
	*/
	while ( getline( fin, line ) )
	{

		if ( line.size() == 0 )
			break;

		pos2 = line.find( ";", 0 );
		pos1 = 0;

		//parsovani radky - rozdeleni na casti dle ';'
		while ( pos2 != string::npos )
		{
			tmp_str = line.substr( pos1, pos2 - pos1 );
			if ( tmp_str.size() > 0 )
				msg_parts.push_back( tmp_str );
			else
				msg_parts.push_back( "" );
			
			pos1 = pos2+1;

			pos2 = line.find( ";", pos1 );

		}


		//nyni vytvorime zpravu dle
		if ( strcmp( (msg_parts.front()).c_str(), "discovery" ) == 0 )
		{
			//discovery
			tmp_str = "<discovery protocolVersion=\"";
			sprintf( id_buf, "%d", protocol );
			tmp_str += id_buf;
			tmp_str += "\" ";

			if ( msg_parts.size() > 1 )
			{
				msg_parts.pop_front();

				tmp_str += " objectId=\"";
				tmp_str += msg_parts.front();
				tmp_str += "\" ";
			}

			tmp_str += " msgid=\"";
			sprintf( id_buf, "%d", msg_id );
			tmp_str += id_buf;
			tmp_str += "\" ";

			tmp_str += " />";

			data->messages.push_back( tmp_str );
		}
		else if ( strcmp( (msg_parts.front()).c_str(), "get" ) == 0 )
		{
			//GET
			tmp_str = "<get ";
			tmp_str += " msgid=\"";
			sprintf( id_buf, "%d", msg_id );
			tmp_str += id_buf;
			tmp_str += "\" ";

			msg_parts.pop_front();

			if ( (msg_parts.front()).size() > 0 )
			{
				tmp_str += " objectId=\"";
				tmp_str += msg_parts.front();
				tmp_str += "\" ";

			}

			msg_parts.pop_front();

			if ( (msg_parts.front()).size() > 0 )
			{
				tmp_str += " > <xpath>";
				tmp_str += msg_parts.front();
				tmp_str += "</xpath>";

			}
			else
			{
				cerr << "GET message: expected xpath expression, but missing" << endl;
				return -1;
			}



			tmp_str += "</get>";
			data->messages.push_back( tmp_str );
		}
		else if ( strcmp( (msg_parts.front()).c_str(), "set" ) == 0 )
		{
			//SET
			tmp_str = "<set ";
			tmp_str += " msgid=\"";
			sprintf( id_buf, "%d", msg_id );
			tmp_str += id_buf;
			tmp_str += "\" ";

			msg_parts.pop_front();

			if ( (msg_parts.front()).size() > 0 )
			{
				tmp_str += " objectId=\"";
				tmp_str += msg_parts.front();
				tmp_str += "\" ";

			}

			msg_parts.pop_front();

			tmp_str += " > ";

			//xpath
			if ( (msg_parts.front()).size() > 0 )
			{
				tmp_str += "<xpath>";
				tmp_str += msg_parts.front();
				tmp_str += "</xpath>";

			}
			else
			{
				cerr << "SET message: expected xpath expression, but missing" << endl;
				return -1;
			}
			msg_parts.pop_front();

			//value
			if ( (msg_parts.front()).size() > 0 )
			{
				tmp_str += "<value>";
				tmp_str += msg_parts.front();
				tmp_str += " </value>";

			}
			else
			{
				cerr << "SET message: expected value expression, but missing" << endl;
				return -1;
			}



			tmp_str += "</set>";
			data->messages.push_back( tmp_str );
		}
		else if ( strcmp( (msg_parts.front()).c_str(), "subscribe" ) == 0 )
		{
			//subscribe
			tmp_str = "<subscribe ";
			tmp_str += " msgid=\"";
			sprintf( id_buf, "%d", msg_id );
			tmp_str += id_buf;
			tmp_str += "\" ";

			msg_parts.pop_front();

			if ( (msg_parts.front()).size() > 0 )
			{
				tmp_str += " objectId=\"";
				tmp_str += msg_parts.front();
				tmp_str += "\" ";

			}

			msg_parts.pop_front();
			//frequency
			if ( (msg_parts.front()).size() > 0 )
			{
				tmp_str += " frequency=\"";
				tmp_str += msg_parts.front();
				tmp_str += "\" ";

			}

			msg_parts.pop_front();
			//distriid
			if ( (msg_parts.front()).size() > 0 )
			{
				tmp_str += " distrid=\"";
				tmp_str += msg_parts.front();
				tmp_str += "\" ";

			}

			msg_parts.pop_front();
			//delete
			if ( (msg_parts.front()).size() > 0 )
			{
				tmp_str += " delete=\"";
				tmp_str += msg_parts.front();
				tmp_str += "\" ";

			}

			msg_parts.pop_front();

			tmp_str += " >";

			//xpaths
			list<string>::iterator it = msg_parts.begin();
			while ( it != msg_parts.end() )
			{

				if ( (msg_parts.front()).size() > 0 )
				{
					tmp_str += "<xpath>";
					tmp_str += msg_parts.front();
					tmp_str += "</xpath>";

				}
				it++;
				msg_parts.pop_front();
			}

			tmp_str += "</subscribe>";
			data->messages.push_back( tmp_str );
			data->has_subscription = true;
			data->subscr_count++;
		}
		else
		{
			cerr << "Unknown element in message file"<<endl;
			return -1;
		}

		//inkrementujeme msgid
		msg_id++;

		//zrusit vsechny stavajici prvky msg_parts
		msg_parts.clear();

	}


	fin.close();
	return 0;

}


/*
Posle pomoci libcURL dotaz na agenta/branu
*/
struct MemoryStruct* send_request( string *msg, struct manager_data *msg_data )
{
	CURL 					*curl;
	CURLcode 				res;
	struct curl_httppost 	*formpost=NULL;
	struct curl_httppost 	*lastptr=NULL;
	struct curl_slist 		*headerlist=NULL;
	static const char 		buf[] = "Expect:";
	struct MemoryStruct		*response;
	string url;

	/*
	Vytvorime odpoved
	*/
	response = new MemoryStruct;

	curl = curl_easy_init();

	if ( curl )
	{
		curl_formadd( &formpost, &lastptr,
						CURLFORM_COPYNAME, "selection",
						CURLFORM_COPYCONTENTS, msg->c_str(),
						CURLFORM_CONTENTTYPE,"text/xml",
						CURLFORM_END);

		headerlist = curl_slist_append(headerlist, buf);

		url = "http://";
		url += msg_data->agent_url;

		cout << "Agent url: " << url << endl;


		//nastavime parametry a odesleme
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
	//	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

		curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, write_data );
		curl_easy_setopt( curl, CURLOPT_WRITEDATA, (void *) response );

		curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
		res = curl_easy_perform(curl);

		if ( res != CURLE_OK )
		{
			cerr << "ERROR: ";
			cerr << curl_easy_strerror( res );
			cerr << endl;
		}


		/*
		clean up
		*/
		curl_easy_cleanup(curl);
	}
	else
	{
		delete( response );
		response = NULL;
		cerr << "Cannot initialize curl library. Terminating.\n";
	}

	/*
	ostatni delete
	*/
	curl_slist_free_all (headerlist);
	curl_formfree(formpost);

	return response;
}

/*
Posle request na zabiti dane subscription
*/
void kill_subscription( string ds, struct manager_data *msg_data )
{
	int distrid = atoi( ds.c_str() );

	if ( distrid == 0 )
	{
		cerr << "This is not a valid distrid value!!" <<endl;
		return;
	}


	string message = "<message password=\"";
	message += msg_data->password;
	message += "\"> <subscribe msgid=\"1\" delete=\"1\" distrid=\"";
	message += ds;
	message += "\" />";
	message += "</message>";

	cout << endl << message << endl;

	struct MemoryStruct *response = send_request( &message, msg_data );
	if ( response != NULL )
	{
		cout << response->memory << endl;
	}

	msg_data->subscr_count--;
}

/*************************************
**************************************/

/*
Hlavni fce - parsuje options a 
pak generuje zpravy
*/
int main(int argc, char *argv[])
{
	/*
	Deklarace promennych
	*/
	struct MemoryStruct *response;
	char tmp_int[50];
	struct MHD_Daemon *daemon;

	/*
	Inicializace potrebnych struktur
	*/
	curl_global_init(CURL_GLOBAL_ALL);

	/*
	Parsovani options
	*/
    struct arg_int  *version  = arg_int0("v",NULL,"<protocol>",              "Protocol version [default 1]");
	struct arg_int  *listen_port  = arg_int0(NULL,"lport","<listen port>",              		 "Manager listening port (for notifications, etc. )");
	struct arg_int  *port  = arg_int0("p",NULL,"<port>",              		 "Agent port to send messages");
	struct arg_lit *notification  = arg_lit0("n","notification",              		 "Just listen to the notification");
	struct arg_lit *no_listen  = arg_lit0(NULL,"no-listen",              		 "Don't start subscription listening server. (for altering subscriptions)");
    struct arg_str  *url = arg_str0("a","agent","<agent ip>", "Agents url");
    struct arg_str  *password = arg_str0(NULL,"password","<xml password>", "");
    struct arg_file  *msg_file = arg_file0("m","message","<msg data>",  "file containing message data");
    struct arg_end  *end     = arg_end(20);
    void* argtable[] = {version, url, port,  password, msg_file, notification, listen_port, no_listen, end};
	int nerrors;

	if ( arg_nullcheck( argtable ) != 0 )
	{
		//Error while initializing argtable
		cerr << "Error: Cannot initialize the argtable structures. Terminating.\n";
		exit(1);
	}

	//default values for options
	version->ival[0] = 1;
	msg_file->filename[0] = "./msg";

	//parsovani options
	nerrors = arg_parse( argc, argv, argtable );

    /* special case: uname with no command line options induces brief help */
    if (argc==1)
        {
			//Vypiseme help
			cout << "Usage: " << argv[0];
			arg_print_syntax( stdout, argtable, "\n" );
			arg_print_glossary( stdout, argtable, "%-25s %s\n" );
			exit(0);
        }

    /* If the parser returned any errors then display them and exit */
    if (nerrors > 0)
        {
        /* Display the error details contained in the arg_end struct.*/
        arg_print_errors(stdout,end, argv[0]);
		exit(1);
        }
	
	/*
	Notifications
	*/
	if ( notification->count > 0 )
	{
		if ( !listen_port->count )
		{
			cerr << "Listen port must be specified! Exitting.\n";
			exit(1);
		}

		cout << "------------------" << endl;
		cout << "Starting server for notifications" << endl;
		cout << " -----------------"<<endl;

		daemon = MHD_start_daemon( MHD_USE_SELECT_INTERNALLY, listen_port->ival[0],
								NULL, NULL, 
								&answer, NULL, 
								MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
								MHD_OPTION_END);
		/*
		Nyni budeme cekat na uzivateluv vstup s cislem subscr id, abychom 
		mohli poslat zabijeci zpravy
		*/
		char x;
		cout << "For stopping the server, input a character.\n";

		cin >> x;

		MHD_stop_daemon( daemon );
		cout << "---------------"<<endl;
		cout << "Stopped server. Terminating manager" << endl;
		cout <<"------------"<<endl;
	}
	else
	{
		/*
		Vytvareni zpravy
		*/
		struct manager_data *msg_data = new manager_data;
		if ( parse_msg_file( msg_data, msg_file->filename[0], version->ival[0] ) != 0 )
		{
			cerr << "Error: Parsing message data failed." << endl;
			exit(1);
		}

		/*
		TODO: 
		if subscriptions - start micro httpd and listen
			pote budeme po uzivateli chtit jednotliva id subscriptionu, abychom
			zabili vsechny, co jsme poslali!!
		*/
		if ( password->count )
			msg_data->password = string( password->sval[0] );

		string *msg = create_message( msg_data );

		cout << *msg << endl;

		/*
		Nastaveni url
		*/
		if ( url->count > 0 )
		{
			msg_data->agent_url = url->sval[0];
			sprintf( tmp_int, ":%d", port->ival[0] );
			msg_data->agent_url += tmp_int;
		}




		response = send_request( msg, msg_data );

		delete( msg );

		if ( response == NULL )
		{
			exit ( 1 );
		}	

		cout << response->memory << endl;
		

		/*
		if subscriptions - start microhttpd
		*/
		if ( msg_data->has_subscription )
		{
			if ( !listen_port->count )
			{
				cerr << "Listen port must be specified for distribution to be received! Exitting.\n";
				exit(1);
			}

			cout << "------------------" << endl;
			cout << "Starting server for descriptions" << endl;
			cout << " -----------------"<<endl;

			daemon = MHD_start_daemon( MHD_USE_SELECT_INTERNALLY, listen_port->ival[0],
									NULL, NULL, 
									&answer, NULL, 
									MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
									MHD_OPTION_END);

			if ( !daemon )
			{
				cerr << "Cannot start http server.";
				exit(1);
			}

			/*
			Nyni budeme cekat na uzivateluv vstup s cislem subscr id, abychom 
			mohli poslat zabijeci zpravy
			*/
			string x;
			cout << "To stop the server - input all subscription ids, for killing them on the server" << endl;

			while ( msg_data->subscr_count > 0 )
			{
				cin >> x;
				kill_subscription( x, msg_data );
			}

			MHD_stop_daemon( daemon );
		}

		
	}




	/*
	Clear up the argtable
	*/
	arg_freetable( argtable, sizeof(argtable)/sizeof(argtable[0]) );

	/*
	Ostatni deletes
	*/


	return 0;
}
