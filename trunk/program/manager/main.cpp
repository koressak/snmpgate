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


/*
Definitions
*/
#define MANAGER_LINE_MAX 512

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

static char errorBuffer[CURL_ERROR_SIZE];

static size_t write_data( void *ptr, size_t size, size_t nmemb, void *data)
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = ( struct MemoryStruct *) data;
	
	mem->memory = new char[ mem->size + 1 + realsize ];

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


/***********************************
	Ostatni potrebne fce
***********************************/
struct manager_data
{
	list<string> messages;
	list<int> subscriptions;
	bool has_subscription;

	string agent_url;
	string password;

	manager_data()
	{
		has_subscription = false;
		password = "";
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
	int pos1, pos2; //pozice pro splitovani stringu
	ifstream fin;
	ifstream *tmp;
	char msg_line[ MANAGER_LINE_MAX ];
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
				tmp_str += " </xpath>";

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
				tmp_str += " </xpath>";

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

			tmp_str += " >\n";

			//xpaths
			list<string>::iterator it = msg_parts.begin();
			while ( it != msg_parts.end() )
			{

				if ( (msg_parts.front()).size() > 0 )
				{
					tmp_str += "<xpath>";
					tmp_str += msg_parts.front();
					tmp_str += " </xpath>\n";

				}
				it++;
				msg_parts.pop_front();
			}

			tmp_str += "</subscribe>";
			data->messages.push_back( tmp_str );
			data->has_subscription = true;
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
Hlavni fce - parsuje options a 
pak generuje zpravy
*/
int main(int argc, char *argv[])
{
	/*
	Deklarace promennych
	*/
	CURL 					*curl;
	CURLcode 				res;
	struct curl_httppost 	*formpost=NULL;
	struct curl_httppost 	*lastptr=NULL;
	struct curl_slist 		*headerlist=NULL;
	static const char 		buf[] = "Expect:";
	struct MemoryStruct response;

	/*
	Inicializace potrebnych struktur
	*/
	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();

	/*
	Parsovani options
	*/
    struct arg_int  *version  = arg_int0("v",NULL,"<protocol>",              "Protocol version [default 1]");
	struct arg_int  *listen_port  = arg_int1(NULL,"lport","<listen port>",              		 "Manager listening port (for notifications, etc. )");
	struct arg_int  *port  = arg_int1("p",NULL,"<port>",              		 "Agent port to send messages");
	struct arg_lit *notification  = arg_lit0("n","notification",              		 "Just listen to the notification");
    struct arg_str  *url = arg_str1("a","agent","<agent ip>", "Agents url");
    struct arg_str  *password = arg_str0(NULL,"password","<xml password>", "");
    struct arg_file  *msg_file = arg_file0("m","message","<msg data>",  "file containing message data");
    struct arg_end  *end     = arg_end(20);
    void* argtable[] = {version, listen_port, url, port,  password, msg_file, notification, end};
	int nerrors;
	int ret;

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
	TODO:
	jestli jenom notification - skip message creation
	*/
	if ( notification->count > 0 )
	{
		//TODO: start server function
	}

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
	TODO: create message, send message
	if subscriptions - start micro httpd and listen
		on end -> send message killing all subscriptions
	*/
	if ( password->count )
		msg_data->password = string( password->sval[0] );

	string *msg = create_message( msg_data );

	cout << *msg << endl;


	/*curl_formadd( &formpost, &lastptr,
					CURLFORM_COPYNAME, "selection",
					CURLFORM_COPYCONTENTS, msg,
					CURLFORM_CONTENTTYPE,"text/xml",
					CURLFORM_END);

  headerlist = curl_slist_append(headerlist, buf);

	if(curl) {*/
		/* what URL that receives this POST */
	/*	curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8888");
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
      curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

		curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, write_data );
		curl_easy_setopt( curl, CURLOPT_WRITEDATA, (void *) &response );

		curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
		curl_easy_perform(curl);*/

		/* always cleanup */
		/*curl_easy_cleanup(curl);*/


		/*
		now we output data in respose
		*/
		/*cout << response.memory << endl;
		cout << "End of response------"<<endl;
    curl_slist_free_all (headerlist);*/

		/* then cleanup the formpost chain */
		//curl_formfree(formpost);
	//}

	/*
	Clear up the argtable
	*/
	arg_freetable( argtable, sizeof(argtable)/sizeof(argtable[0]) );

	/*
	Ostatni deletes
	*/
	//delete( msg );


	return 0;
}
