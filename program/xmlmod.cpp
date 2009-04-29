#include "header/xmlmod.h"

/*
Globalni xml module
*/
XmlModule *xm;

/*
Konstruktor
*/
XmlModule::XmlModule()
{
	main_root = NULL;
	log_file = NULL;
	xsd_dir = NULL;

	//ziskame implementaci pro vytvareni dokumentu
	impl = DOMImplementationRegistry::getDOMImplementation( XMLString::transcode("core") );

	//Parser je urcen pro zpracovani message
	parser = new XercesDOMParser;
	parser->setValidationScheme( XercesDOMParser::Val_Never);
	parser->setDoNamespaces( false );
	parser->setDoSchema( false );
	parser->setLoadExternalDTD( false );

	response_queue.clear();

	/*
	Init libCUrl
	*/
	curl_global_init(CURL_GLOBAL_ALL);

}

/*
Destruktor
*/
XmlModule::~XmlModule()
{
	delete( main_xa_doc );

	/*
	Smazeme vsechny mutexy
	*/
	for( int a = 0; a < devices_no; a++ )
	{
		pthread_mutex_destroy( queue_mutex[a]);
	}

	delete( queue_mutex );

	pthread_mutex_destroy( &response_lock );
	pthread_mutex_destroy( &condition_lock );
	pthread_mutex_destroy( &subscr_cond_lock );
	pthread_mutex_destroy( &xa_doc_lock );

	pthread_cond_destroy( &resp_cond );
	pthread_cond_destroy( &subscr_wait );
}

/*
Nastaveni parametru
*/
void XmlModule::set_parameters( DOMElement *r, list<DOMElement *> *roots, char *log, char *xsd, SnmpModule *sn )
{
	main_root = r;
	log_file = log;
	xsd_dir = xsd;
	snmpmod = sn;


	/*
	Build Xalan Documents
	*/
	//list<DOMElement *>::iterator it; 

	DOMDocument *doc;
	XercesDOMSupport		theDOMSupport;
	XercesParserLiaison	*theLiaison;
	XalanDocument *theDocument;
	

	/*
	Jeste pro hlavni dokument
	*/
	doc = main_root->getOwnerDocument();
	main_xa_doc = new xalan_docs_list;

	theLiaison = new XercesParserLiaison( theDOMSupport );
	theDocument = theLiaison->createDocument( doc, true, true, true );

	main_xa_doc->doc = theDocument;
	main_xa_doc->liaison = theLiaison;

	/*
	Vybudujeme si mutexy na zamykani front
	*/
	devices_no = snmpmod->get_device_count();
	queue_mutex = new pthread_mutex_t*[ devices_no ];

	for( int a = 0; a < devices_no; a++ )
	{
		queue_mutex[a] = new pthread_mutex_t;
		pthread_mutex_init( queue_mutex[a], NULL );
	}

	pthread_mutex_init( &response_lock, NULL );
	pthread_mutex_init( &condition_lock, NULL );
	pthread_mutex_init( &getset_lock, NULL );
	pthread_mutex_init( &subscr_cond_lock, NULL );
	pthread_mutex_init( &xa_doc_lock, NULL );


	/*
	Inicializace condition promenne
	*/
	pthread_cond_init( &resp_cond, NULL );
	pthread_cond_init( &subscr_wait, NULL );

	/*
	Nemame zadne subscriptions
	*/
	subscriptions_changed = false;

}

/*
Process message
*/

int XmlModule::process_request( void *cls, struct MHD_Connection *connection, const char *url, const char *method, 
			const char *version, const char *upload_data, size_t *upload_data_size, void **con_cls )
{
	if ( NULL == method )
		return MHD_NO;

	if ( NULL == *con_cls )
	{
		struct connection_info *con_info = new connection_info;

		
		if ( strcmp( method,"POST") == 0 )
		{
			con_info->post_processor = MHD_create_post_processor( connection, POSTBUFFERSIZE, iterate_post, (void *) con_info);

			if ( NULL == con_info->post_processor )
			{
				delete( con_info );
				return MHD_NO;
			}

			con_info->conn_type = HTTP_POST;
		}
		else
		{
			log_message( log_file, "GET request cancelled" );
			send_error_response( connection, XML_ERR_NO_HTTP_POST, NULL );
		}

		/*
		Ziskavame adresu klienta
		*/
		const union MHD_ConnectionInfo *connInfo = MHD_get_connection_info( connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS );
		char ip_addr[ INET_ADDRSTRLEN ];
		const char *buf;
		struct sockaddr_in *addr = connInfo->client_addr;

		buf = inet_ntop( addr->sin_family, &(addr->sin_addr), ip_addr, INET_ADDRSTRLEN );

		if ( buf == NULL )
		{
			//Error - cannot get the ip address
			log_message( log_file, "Cannot get the client IP address" );
			send_error_response( connection, XML_ERR_UNKNOWN, NULL );
		}
		else
		{
			con_info->ip_addr = string( buf );
		}



		*con_cls = (void *)con_info;
		return MHD_YES;

	}


	if ( strcmp( method, "POST") == 0 )
	{
		struct connection_info *con_info = (struct connection_info *) *con_cls;

		if ( *upload_data_size != 0 )
		{
			if ( upload_data == NULL)
				return MHD_NO;

			MHD_post_process( con_info->post_processor, upload_data, *upload_data_size );
			*upload_data_size = 0;

			return MHD_YES;
		}
		else
		{
			//parsujeme zpravu - ziskani XML struktury

		//pthread_mutex_lock( &getset_lock );
			MemBufInputSource *mem_buf;

			try
			{
			 mem_buf = new  MemBufInputSource( (const XMLByte *) con_info->data.c_str(), strlen(con_info->data.c_str()),
					"message", NULL);
			}
			catch ( ... )
			{
				log_message( log_file, "Exception parsing message!!!" );
				return MHD_NO;
			}

			//Zakladni promenne
			DOMDocument *message = NULL;
			const XMLCh* value;
			char* msg_context;
			string *response_string;
			char* password;


			//pocty zprav
			bool finished = false;
			bool internal_error = false;
			int msg_count = 0;
			int msg_tmp = 0;
			list<struct request_data *> responses;
			list<struct request_data *>::iterator res_it;

			DOMElement *msg_root = NULL;

			/*
			Get/Set message a pak i subscribe navrati
			strukturu dat, kterou musime pak najednou rozhodit
			mezi vsechny zarizeni.

			Paklize navrati NULL, tak je to asi error a uz je to v response
			*/
			struct request_data* xr;
			list<struct request_data*> req_to_dev[ devices_no ]; 

			try {
				parser->parse( *mem_buf );
				message = parser->getDocument();
				//Zjistime, jestli je message root elementem
				if ( message )
					msg_root = message->getDocumentElement();
			}
			catch ( const XMLException &e )
			{
				log_message( log_file, "Error parsing buffer" );
				log_message( log_file, XMLString::transcode( e.getMessage() ) );
				delete( mem_buf );
				return send_error_response( connection, XML_ERR_UNKNOWN, NULL );
			}


			if ( !msg_root )
			{
				log_message( log_file, "No root element in message" );
				//odesilame error
				message->release();
				delete( mem_buf );
				return send_error_response( connection, XML_ERR_UNKNOWN, NULL );
			}

			//paklize neni root == message, tak spatnej format zpravy
			if ( !XMLString::equals( msg_root->getTagName(), X("message") ) )
			{
				log_message( log_file, "No message element in message" );
				//spatny format zpravy
				message->release();
				delete( mem_buf );
				return send_error_response( connection, XML_ERR_WRONG_MSG, NULL );
			}

			//Ulozime password = community string
			value = msg_root->getAttribute( X( "password" ) );
			password = XMLString::transcode( value );

			//Ulozime si Message Context atribut
			value = msg_root->getAttribute( X( "context" ) );
			msg_context = XMLString::transcode( value );

			response_string = new string;

			//Nasleduje cyklus pro kazdy podelement rootu -> jeden prikaz na zpracovani
			DOMNodeList *children;
			try
			{
				children = msg_root->getChildNodes();
			}
			catch (...)
			{
				log_message( log_file, "Cannot get child nodes of the message" );
				message->release();
				delete( mem_buf);
				return MHD_NO;
			}

			for( XMLSize_t i = 0; i < children->getLength(); i++ )
			{
				DOMNode *node = children->item(i);
				
				if ( node->getNodeType() && node->getNodeType() == DOMNode::ELEMENT_NODE)
				{
					DOMElement *elem = dynamic_cast<DOMElement *>(node);

					//struct request_data *xr;

					//zpracovani nejake zpravy
					if ( XMLString::equals( elem->getTagName(), X( "discovery" ) ) )
					{
						//xr = process_discovery_message( connection, elem );
						process_discovery_message( elem );
						msg_count++;
					}
					else if ( XMLString::equals( elem->getTagName(), X( "get" ) ) )
					{
						//xr = process_get_set_message( elem, password, XML_MSG_TYPE_GET );
						xr = process_get_set_message( elem, password, XML_MSG_TYPE_GET, 0 );
						msg_count++;

						//Zaradime to do fronty pro dany device
						if ( xr != NULL )
						{
							req_to_dev[ xr->object_id ].push_back( xr );
							//enqueue_response( xr );
						}
					}
					else if ( XMLString::equals( elem->getTagName(), X( "set" ) ) )
					{
						//xr = process_get_set_message( elem, password, XML_MSG_TYPE_SET );
						xr = process_get_set_message( elem, password, XML_MSG_TYPE_SET, 0 );
						msg_count++;

						//Zaradime to do fronty pro dany device
						if ( xr != NULL )
							req_to_dev[ xr->object_id ].push_back( xr );
					}
					else if ( XMLString::equals( elem->getTagName(), X( "subscribe" ) ) )
					{
						//xr = process_subscribe_message( elem, password );
						xr = process_subscribe_message( elem, password, con_info->ip_addr );
						msg_count++;

						if ( xr != NULL )
							req_to_dev[ xr->object_id ].push_back( xr );
					}
					else
					{
						//Neznama zprava -> error
						log_message( log_file, "element error");
						internal_error = true;
						break;
					}

				}

			} //end of FOR

			

		 if ( message )
			 message->release();
		 //pthread_mutex_unlock( &getset_lock );

		 /*
		 Rozhozeni jednotlivych messages jako celky do jednotlivych zarizeni
		 */
		 for( int i = 0; i < devices_no; i++ )
		 {
			 if (  (!req_to_dev[i].empty()) )
			 {
				if (  snmpmod->dispatch_request( &req_to_dev[i] ) != 0 )
					internal_error = true;

				req_to_dev[i].clear();
			 }
		 }

		 /*
		 Zde se thread koukne do fronty odpovedi a kdyz tam neco je, vezme si to.
		 Paklize tam nic neni, tak se uspime na nejake condition a cekame na probuzeni.
		 Az mame vsechny responses, volame opakovane build_response_string() a pak send response
		 */

		 struct timespec ts;

		 ts.tv_sec = time(NULL) + 2;
		 ts.tv_nsec = 0;

		 //income = NULL;
		 while ( !finished )
		 {
			 ts.tv_sec = time(NULL) + 2;
			 ts.tv_nsec = 0;
			 //Nejprve se podivame, jestli nemame nejakou odpoved (treba discovery message)
			 pthread_mutex_lock( &response_lock );

			 res_it = response_queue.begin();

			 //sprintf( ooo, "%d", response_queue.size() );
			 //log_message( log_file, ooo );

			 while ( res_it != response_queue.end() )
			 {
				 //log_message( log_file, thid );
				 if ( (*res_it)->thread_id == pthread_self() ) //check na moje thread ID
				 {
					 //vybereme zpravu a ulozime si ji k dalsimu zpracovani
					 responses.push_back( (*res_it) );
					 res_it = response_queue.erase( res_it );
					 msg_tmp++;
				 }
				 else
				 {
					 res_it++;
				 }

			 }

			 pthread_mutex_unlock( &response_lock );

			 //sprintf( ooo, "%d - %d", msg_count, msg_tmp );
			 //log_message( log_file, ooo );

			 //Paklize nejsou vsechny odpovedi, tak se uspime na conditione
			 if ( msg_tmp != msg_count )
			 {
				 //log_message( log_file, "Client thread: going to sleep" );
				 pthread_mutex_lock( &condition_lock );
				 //pthread_cond_wait( &resp_cond, &condition_lock );
				pthread_cond_timedwait( &resp_cond, &condition_lock, &ts);
				 pthread_mutex_unlock( &condition_lock );
				 //log_message( log_file, "Client thread: waking up" );
			 }
			 else
			 {
				 //log_message( log_file, "Client thread: finishing, has got all responses" );
				 finished = true;

				 msg_count = 0;
				 msg_tmp = 0;

				 // Process building string for all messages
				 res_it = responses.begin();
				 list<struct request_data*>::iterator tmp_it;
				 while( res_it != responses.end() )
				 {
					 /*
					 Toto je kvuli SUBSCRIBE, abychom mohli vyuzit
					 fci process_get message, tak ulozime odpovedni typ
					 do request_data a pak jej vytahneme zpet.
					 */
					 if ( (*res_it)->msg_type_resp != 0 )
						 (*res_it)->msg_type = (*res_it)->msg_type_resp;

					string *out = build_response_string( (*res_it) );
					*response_string += *out;

					tmp_it = res_it;
					res_it++;

					responses.pop_front();

					delete( out );
					log_message( log_file, "CLIENT: deleting request_data" );
					//delete( (*tmp_it) );

				 }
			 }

		 }

		 //odeslani odpovedi
		 int ret;
		 if ( !internal_error )
			 ret =  send_response( connection, msg_context,  response_string );
		 else
			 ret = send_error_response( connection, 0, "Internal server error" );

		 delete( response_string );
		 XMLString::release( &password );
		 XMLString::release( &msg_context );
		 delete( mem_buf );

		 return ret;
		}


		return MHD_NO;
	}


	return MHD_NO;
}

/*
Skoncena prace s requestem
*/
void XmlModule::request_completed( void *cls, struct MHD_Connection *connection, void **con_cls, 
			enum MHD_RequestTerminationCode toe )
{
	struct connection_info *con_info = (struct connection_info *) *con_cls;

	if ( con_info == NULL )
		return;
	
	if ( con_info->conn_type == HTTP_POST )
	{
		MHD_destroy_post_processor( con_info->post_processor );
	}


	delete(con_info);

	*con_cls = NULL;

}

/*
Ziskani dat z daneho requestu
*/
int XmlModule::post_iterate( void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
              const char *filename, const char *content_type,
	          const char *transfer_encoding, const char *data, size_t off,
	          size_t size)
{

	struct connection_info *con_info = (struct connection_info*) coninfo_cls;

	if ( strcmp( content_type, "text/xml" ) != 0 )
	{
		//nezpracovavame nic jineho nez xml data.
		con_info->data = "";
		return MHD_NO;
	}

	string tmp = string(data);

	int message_start = tmp.find( XML_MSG_ROOT, 0 );
	int message_end = tmp.find( XML_MSG_ROOT_END, 0 );
	message_end += strlen( XML_MSG_ROOT_END );

	tmp = tmp.substr( message_start, message_end );

	log_message( log_file, tmp.c_str() );

	con_info->data = tmp;

	return MHD_NO;
}


/*
Odeslani zpravy zpet
*/
int XmlModule::send_response( struct MHD_Connection *connection, const char* msg_context,  string *out )
{
	int ret;
	struct MHD_Response *response = NULL;

	/*
	Vybudujeme finalni obalku okolo vsech vystupnich odpovedi
	*/
	string message = "<message";

	if ( strcmp( msg_context, "" ) == 0 )
		message += ">";
	else
	{
		message += "context=\"";
		message += msg_context;
		message += "\">";
	}

	message += *out;
	message += "</message>";

	response = MHD_create_response_from_data(  message.size(), (void *) message.c_str(), MHD_NO, MHD_YES );

	if ( response == NULL )
	{
		log_message( log_file, "Cannot create response from data. Cannot send back anything." );
		return MHD_NO;
	}

	ret = MHD_queue_response( connection, MHD_HTTP_OK, response );
	MHD_destroy_response( response);

	return ret;
}

/*
Odeslani chybne zpravy zpet
*/
int XmlModule::send_error_response( struct MHD_Connection *connection, int error, const char *err_msg )
{
	int ret;
	struct MHD_Response *response = NULL;

	const char *wrong_message_type = "Must be POST method";
	const char *message_missing = "Element <message></message> missing.";
	const char *unknown_error = "Server experienced uknown error while parsing request.";

	switch (error)
	{
		case XML_ERR_NO_HTTP_POST:
			//send back page
			response = MHD_create_response_from_data( strlen (wrong_message_type), (void *)wrong_message_type, MHD_NO, MHD_NO);
			break;
		case XML_ERR_UNKNOWN:
			//send back page
			response = MHD_create_response_from_data( strlen (unknown_error), (void *)unknown_error, MHD_NO, MHD_NO);
			break;

		case XML_ERR_WRONG_MSG:
			//send back page
			response = MHD_create_response_from_data( strlen (message_missing), (void *)message_missing, MHD_NO, MHD_NO);
			break;
		default:
			if ( err_msg != NULL )
				response = MHD_create_response_from_data( strlen( err_msg ), (void *)err_msg, MHD_NO, MHD_YES );
		break;
	}

	if ( response != NULL )
	{
		ret = MHD_queue_response( connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response );

		MHD_destroy_response( response );

		return ret;
	
	}
	else
		return MHD_NO;
}



/*
Vybudovani odpovedniho stringu
*/
string * XmlModule::build_response_string( struct request_data *data )
{
	string *out = new string;
	char *buf;
	char tmpid[10];

	/*
	DISCOVERY message response
	*/
	if ( data->msg_type == XML_MSG_TYPE_DISCOVERY )
	{
		DOMImplementation *impl;
		DOMWriter *writer = NULL;
		DOMDocument *xmlDoc;
		DOMElement *rootElem;
		XMLCh *xsd = NULL;
		char file[100];

		WriterErrHandler *err = new WriterErrHandler( log_file );


		if ( data->error != XML_MSG_ERR_XML )
		{
			impl = DOMImplementationRegistry::getDOMImplementation( XMLString::transcode("LS") );

			writer = ((DOMImplementationLS*)impl)->createDOMWriter();

			writer->setErrorHandler( err );


			writer->setFeature( XMLUni::fgDOMWRTFormatPrettyPrint, true );

			XercesDOMParser *conf_parser = new XercesDOMParser;

			conf_parser->setValidationScheme( XercesDOMParser::Val_Never );
			conf_parser->setDoNamespaces( false );
			conf_parser->setDoSchema( false );
			conf_parser->setLoadExternalDTD( false );
			

			//Jestli budeme posilat main xsd, nebo xsd zarizeni
			if ( data->object_id == -1 || data->object_id == 0 )
			{
				//main xsd
				try {
					sprintf( file, "%s%s", xsd_dir, MAIN_XSD );
					conf_parser->parse( file );
					
					xmlDoc = conf_parser->getDocument();
					rootElem = xmlDoc->getDocumentElement();

					//zapis do stringu
					xsd = writer->writeToString( *rootElem );

					xmlDoc->release();
				}
				catch ( ... )
				{
					log_message( log_file, "Exception writing to string" );
				}
			}
			else
			{
				//xsd daneho zarizeni
				try {

					/*
					Osetreni, kdyz ma nekdo spolecny dokument jako jiny device
					(setreni pameti)
					*/
					SNMP_device *dev = snmpmod->get_device_ptr( data->object_id );

					if ( dev->similar_as != -1 )
					{
						sprintf( file, "%s%d.xsd",xsd_dir, dev->similar_as );
					}
					else
						sprintf( file, "%s%d.xsd", xsd_dir, data->object_id );

					conf_parser->parse( file );
					
					xmlDoc = conf_parser->getDocument();
					rootElem = xmlDoc->getDocumentElement();

					xsd = writer->writeToString( *rootElem );

					xmlDoc->release();
				}
				catch (...)
				{
					log_message( log_file, "Exception while answering discovery");
				}
			}
		}

		/*
		Odpoved - PUBLICATION
		*/
		*out = "<publication msgid=\"";
		sprintf( tmpid, "%d", data->msgid );
		*out += tmpid;
		*out += "\">\n";

		if ( xsd != NULL )
		{

			*out += "<info><xpath>1.0</xpath></info>\n";
			*out += "<dataModel>";


			buf = XMLString::transcode( xsd );
			*out += buf;


			*out += "</datamodel>\n";

			//log_message( log_file, out->c_str() );

			//delete ( buf );
			XMLString::release( &buf );
		
			XMLString::release( &xsd );
		}
		else if ( data->error == XML_MSG_ERR_XML )
		{
			*out += "<error>";
			*out += data->error_str;
			*out += "</error>\n";
		}
		else
		{
			*out += "<error>Cannot generate data model</error>\n";
			log_message( log_file, "Cannot generate data model in response to DISCOVERY message" );
		}

		//uzavreni vystupni zpravy
		*out += "</publication>\n";


		//writer->release();
		if ( writer != NULL )
			delete( writer );
	}
	/*
	GET message response
	*/
	else if ( data->msg_type == XML_MSG_TYPE_GET )
	{
		/*
		Odpoved - RESPONSE
		*/
		*out = "<response msgid=\"";
		sprintf( tmpid, "%d", data->msgid );
		*out += tmpid;
		*out += "\">\n";

		if ( data->error == XML_MSG_ERR_XML || data->error == XML_MSG_ERR_SNMP )
		{
			*out += "<error>";
			*out += data->error_str;
			*out += "</error>\n";

		}
		else if ( data->snmp_err )
		{
			*out += "<error>";
			*out += data->snmp_err_str;
			*out += "</error>\n";
		}
		else
		{
			list<struct value_pair *>::iterator rit;
			string oid;

			if ( data->snmp_getnext == 0 )
			{
				for ( rit = data->request_list.begin(); rit != data->request_list.end(); rit++ )
				{
					*out += "<value>\n";
					*out += (*rit)->value;
					*out += "</value>\n";
				}
			}
			else
			{
				//Generujeme cely podstrom, co jsme dostali za hodnoty
				//nejspise rekurzivne, pac uroven podstromu je "nekonecna"

				//prvni value_pair znaci korenovy node
				*out += "<value>\n";
				rit = data->request_list.begin();
				oid = (*rit)->oid;
				data->request_list.pop_front();

				*out += "<";
				*out += oid;
				*out += ">\n";

				//rekurzivni sestup
				*out += build_out_xml( data->found_el, &(data->request_list), rit);

				*out += "</";
				*out += oid;
				*out += ">\n";

				*out += "</value>\n";


			}
		}

		*out += "</response>\n";
	}
	/*
	SET message response
	*/
	else if ( data->msg_type == XML_MSG_TYPE_SET )
	{
		/*
		RESPONSE
		*/
		*out = "<response msgid=\"";
		sprintf( tmpid, "%d", data->msgid );
		*out += tmpid;

		//Paklize vznikl error, zasleme jej managerovi
		if ( data->error == XML_MSG_ERR_XML )
		{
			*out += "\">\n";

			*out += "<error>";
			*out += data->error_str;
			*out += "</error>\n</response>\n";

		}
		else if ( data->snmp_err )
		{
			*out += "\">\n";

			*out += "<error>";
			*out += data->snmp_err_str;
			*out += "</error>\n</response>\n";
		}
		else
			*out += "\" />\n";
	}
	/*
	DISTRIBUTION response
	*/
	else if ( data->msg_type == XML_MSG_TYPE_SUBSCRIBE )
	{
		*out = "<distribution msgid=\"";
		sprintf( tmpid, "%d", data->msgid );
		*out += tmpid;

		*out += "\" distrid =\"";
		sprintf( tmpid, "%d", data->distr_id );
		*out += tmpid;
		*out += "\">\n";

		if ( data->error == XML_MSG_ERR_XML || data->error == XML_MSG_ERR_SNMP )
		{
			*out += "<error>";
			*out += data->error_str;
			*out += "</error>\n";

		}
		else
		{
			list<struct value_pair *>::iterator rit;
			string oid;

			if ( data->snmp_getnext == 0 )
			{
				for ( rit = data->request_list.begin(); rit != data->request_list.end(); rit++ )
				{
					*out += "<value>\n";
					*out += (*rit)->value;
					*out += "</value>\n";
				}
			}
			else
			{
				//Generujeme cely podstrom, co jsme dostali za hodnoty
				//nejspise rekurzivne, pac uroven podstromu je "nekonecna"

				//prvni value_pair znaci korenovy node
				*out += "<value>\n";
				rit = data->request_list.begin();
				oid = (*rit)->oid;
				data->request_list.pop_front();

				*out += "<";
				*out += oid;
				*out += ">\n";

				//rekurzivni sestup
				*out += build_out_xml( data->found_el, &(data->request_list), rit);

				*out += "</";
				*out += oid;
				*out += ">\n";

				*out += "</value>\n";


			}
		}

		*out += "</distribution>\n";

	}

	return out;
}


/*
Build out xml
Rekurzivni fce pro vybudovani odpovedi, kdy hodnotou
je cely xml dokument (podstrom nejakeho elementu)
*/
string XmlModule::build_out_xml( const DOMElement *el, list<struct value_pair*> *list, list<struct value_pair*>::iterator it )
{
	string 			ret = "";
	DOMNodeList 	*children = el->getChildNodes();
	DOMNode 		*node;
	DOMElement 		*elem;

	char 			*buf;
	bool 			has_value;

	//list<struct value_pair *>::iterator it;

	for( unsigned int i = 0; i < children->getLength(); i++ )
	{
		node = children->item( i );
		has_value = false;

		if ( node->getNodeType() && node->getNodeType() == DOMNode::ELEMENT_NODE )
		{
			elem = dynamic_cast<DOMElement *>(node);

			buf = XMLString::transcode( elem->getTagName() );


			it = list->begin();

			while ( it != list->end() )
			{
				//Paklize mame v seznamu hodnotu, dosadime a vracime se
				if ( strcmp( buf, (*it)->oid.c_str() ) == 0 )
				{
					//log_message( log_file, "We have the value!!!!" );
					//log_message( log_file, buf );
					has_value = true;

					ret += "<";
					ret += string( buf );
					ret += ">\n";

					ret += (*it)->value;
					ret += "\n";

					ret += "</";
					ret += string( buf );
					ret += ">\n";

					//delete( (*it) );
					it = list->erase( it );
				}
				else
					it++;
			}

			if ( !has_value )
			{
				ret += "<";
				ret += string( buf );
				ret += ">\n";

				ret += build_out_xml( elem, list, it );

				ret += "</";
				ret += string( buf );
				ret += ">\n";
			}






			XMLString::release( &buf );

		}

	}

	return ret;
}


/*
Nalezne element dle xpath vyrazu a vrati na nej odkaz
*/
const DOMElement* XmlModule::find_element( const XMLCh* name, XalanDocument* theDocument, struct request_data* req_data, bool deep = true )
{
	//Thread safe - ochrana proti recreate dokumentu
	pthread_mutex_lock( &xa_doc_lock );

	const DOMElement *ret_elm = NULL;

	//Ziskame pointer na dokument daneho zarizeni

	//Pripravime struktury k vyhledani
	XercesDOMSupport		theDOMSupport;
	
	if ( theDocument == NULL )
	{
		log_message( log_file, "cannot create document" );
		pthread_mutex_unlock( &xa_doc_lock );
		return ret_elm;
	}	


	XalanDocumentPrefixResolver		thePrefixResolver(theDocument);
	XPathEvaluator	theEvaluator;
	XalanNode* theContextNode;

	try {
		theContextNode = theDocument->getDocumentElement();


		//Vyhledani daneho elementu
		 const XObjectPtr result( theEvaluator.evaluate(
					   theDOMSupport,        // DOMSupport
					  theContextNode,       // context node
					  XalanDOMString(name).c_str(),  // XPath expr
					   thePrefixResolver));     // Namespace resolver

		const NodeRefListBase&        nodeset = result->nodeset( );

		if ( nodeset.getLength() > 1 )
		{
			log_message( log_file, "Found more than one element by xpath expression" );
			pthread_mutex_unlock( &xa_doc_lock );
			return ret_elm;
		}
		else if ( nodeset.getLength() == 1 )
		{
			/*
			Nasli jsme pomoci XPath vyrazu dany element
			*/
			XalanNode *n = nodeset.item(0);

			if ( n->getNodeType() && n->getNodeType() != XalanNode::ELEMENT_NODE)
			{
				log_message( log_file, "Cannot get the element node" );
				pthread_mutex_unlock( &xa_doc_lock );
				return ret_elm;
			}

			//Z XalanElementu musime udelat DOMElement a ten vratit
			XalanElement *elm = dynamic_cast<XalanElement *>(n);
			XercesElementWrapper *wrap = dynamic_cast<XercesElementWrapper *>(elm);
			ret_elm = wrap->getXercesNode();

			pthread_mutex_unlock( &xa_doc_lock );

			return ret_elm;
		}
		else
		{
			/*
			Nenasli jsme element. JE mozne, ze je to indexovana polozka.
			Proto odsekneme pripadne cisla a podivame se znova.
			Jinak vracime NULL, pac takovy element neexistuje
			*/
			if ( deep )
			{
				char *buf = XMLString::transcode( name );
				string xp = string( buf );
				string to_search;
				string leftover;
				unsigned int pos1, pos2, pos3;
				string name;
				string tmp_xp;
				
				const DOMElement *tmp_find = NULL;

				XMLString::release( &buf );
				/*
				Nejprve se pokusime odstranit z nazvu cisilka (u posledniho elementu)
				a pak teprve pujdeme rezat dany xpath
				*/
				pos1 = xp.rfind( "/" );
				if ( pos1 != string::npos )
				{
					pos2 = xp.size();

					name = xp.substr( pos1+1, pos2 - (pos1+1) );

					pos3 = name.find( "." );
					if ( pos3 != string::npos )
					{
						req_data->snmp_indexed_name = name;
						log_message( log_file, name.c_str() );

						name = name.substr( 0, pos3 );

						tmp_xp = xp.replace( pos1+1, pos2 - (pos1+1), name, 0, name.size() );

						tmp_find = find_element( X( tmp_xp.c_str() ), theDocument, req_data, false );

						if ( tmp_find != NULL )
						{
							req_data->xpath_end = "";
							pthread_mutex_unlock( &xa_doc_lock );
							return tmp_find;
						}
					}



				}

				pthread_mutex_unlock( &xa_doc_lock );
				return ret_elm;

			}
			else 
			{
				pthread_mutex_unlock( &xa_doc_lock );
				return ret_elm;
			}

		}
	}
	catch ( ... )
	{
		log_message( log_file, "Exception while evaluating XPath expression" );
		pthread_mutex_unlock( &xa_doc_lock );
		return ret_elm;
	}

}


/****************************************************
************** Cast zpracovani zprav*****************
*****************************************************/


/*
DISCOVERY MESSAGE
*/
int XmlModule::process_discovery_message( DOMElement *elem )
{
	struct request_data *xr = new request_data;
	const XMLCh* value;
	SNMP_device *gate = snmpmod->get_gate_device();
	char *buf;

	xr->thread_id = pthread_self();

	log_message( log_file, "Dostali jsme DISCOVERY message" );

	xr->msg_type = XML_MSG_TYPE_DISCOVERY;

	value = elem->getAttribute( X( "msgid" ) );
	xr->msgid = atoi( XMLString::transcode( value ) );

	/*
	Protocol version check
	*/
	value = elem->getAttribute( X( "protocolVersion" ) );
	buf = XMLString::transcode( value );

	if ( strcmp( buf, gate->xml_protocol_version) != 0 )
	{
		xr->error = XML_MSG_ERR_XML;
		xr->error_str = "Unsupported protocol version.";
	}

	XMLString::release( &buf );

	value = elem->getAttribute( X( "objectId" ) );
	if ( !XMLString::equals( value, X( "" ) ) )
	{
		xr->object_id = atoi( XMLString::transcode( value ) );
	}
	else
	{
		xr->object_id = 0;
	}

	if ( ( xr->object_id > 0 ) && !snmpmod->is_device( xr->object_id ) )
	{
		//Error, takove zarizeni neexistuje
		xr->error = XML_MSG_ERR_XML;
		xr->error_str = "Such device does not exist!";
	}

	//zaradime odpoved do fronty
	enqueue_response( xr );

	return 0;
}


/*
GET / SET MESSAGE
*/
struct request_data * XmlModule::process_get_set_message( DOMElement *elem, const char *password, int msg_type, int obj_id = 0 )
{
	struct request_data *xr = new request_data;
	const XMLCh* value;
	struct value_pair *vp;
	DOMNodeList *children;
	DOMElement *el;
	DOMNode *node;

	const DOMElement *found_el;

	string tmp_str_xpath = "";
	char *tmp_buf;
	bool create_vp = true;

	char tmpid[50];
	
	//prava k pristupu k device
	int permission;

	/*
	serach_id vyjadruje, ve kterem stromu se bude vyhledavat
	Je to z duvodu setreni pameti, kdy jestlize ukazuje na stejny strom
	jako nekdo, tak musime hledat tam.
	*/
	int search_id;
	SNMP_device *dev;

	xr->thread_id = pthread_self();


	log_message( log_file, "Dostali jsme GET/SET message" );

	xr->msg_type = msg_type;
	value = elem->getAttribute( X( "msgid" ) );
	tmp_buf = XMLString::transcode( value );
	xr->msgid = atoi( tmp_buf );
	XMLString::release( &tmp_buf );

	value = elem->getAttribute( X( "objectId" ) );

	if ( !XMLString::equals( value, X( "" ) ) )
	{
		tmp_buf = XMLString::transcode( value );
		xr->object_id = atoi( tmp_buf );
		XMLString::release( &tmp_buf );
	}
	else if ( obj_id != 0 )
	{
		xr->object_id = obj_id;
	}
	else xr->object_id = 0;

	/*
	Nastaveni search_id
	*/

	log_message( log_file, "XML: before pointer to device" );
	dev = snmpmod->get_device_ptr( xr->object_id );
	if ( dev == NULL )
	{
		//Internal ERROR
		xr->error = XML_MSG_ERR_XML;
		xr->error_str = string("Cannot fulfil the request. Such device does not exist.");
		enqueue_response( xr );
		return NULL;
	}
	else
	{
		if ( dev->similar_as != -1 )
		{
			search_id = dev->similar_as;
		}
		else
			search_id = xr->object_id;
	}
	log_message( log_file, "XML: after pointer to device" );

	/*
	ACCESS CONTROL
	*/
	if ( (permission=operation_permitted( password, snmpmod->get_gate_device(), msg_type )) == 0 )
	{
		xr->error = XML_MSG_ERR_XML;
		xr->error_str = string( "You have no permission for this operation" );
		enqueue_response( xr );
		return NULL;
	}
	else
	{

	log_message( log_file, "XML: before get community" );
		xr->community = get_snmp_community( permission, dev );
	}

	log_message( log_file, "XML: before parsing requests" );


	//Parsovani <xpath> elementu
	children = elem->getChildNodes();

	for( XMLSize_t i = 0; i < children->getLength(); i++ )
	{
		node = children->item(i);
		
		if ( node->getNodeType() && node->getNodeType() == DOMNode::ELEMENT_NODE)
		{
			el = dynamic_cast<DOMElement *>(node);

			if ( create_vp )
			{
				vp = new value_pair;

				create_vp = false;
			}

			//paklize je to <xpath>
			if ( XMLString::equals( el->getTagName(), X( "xpath" ) ) )
			{
				value = el->getTextContent();


				//Search id je kvuli pametovemu zlepseni.
				sprintf( tmpid, "%d", search_id );
				tmp_str_xpath = "//device[@id='";
				tmp_str_xpath += string( tmpid );
				tmp_str_xpath +="']/data";
				tmp_buf = XMLString::transcode( value );
				tmp_str_xpath += string( tmp_buf );
				XMLString::release( &tmp_buf );

				log_message( log_file, tmp_str_xpath.c_str() );

				found_el = find_element( X( tmp_str_xpath.c_str() ), main_xa_doc->doc, xr, true );
				log_message( log_file, "XML: after found element" );

				if ( found_el == NULL )
				{
					log_message( log_file, "NULL found element" );
					xr->error = XML_MSG_ERR_XML;
					xr->error_str = "Such element cannot be found in managed data tree.";
					
					delete( vp );
					enqueue_response( xr );
					return NULL;
				}


				/*
				Paklize je element korenovym elementem, budeme generovat GETNEXT dotazy na cely podstrom.

				Jestli je to koncovym elementem, nastavime jmeno a hodnotu.
				*/

				if ( found_el->hasChildNodes() )
				{

					xr->snmp_getnext = 1;
					value = found_el->getTagName();
					tmp_buf = XMLString::transcode( value );

					vp->oid = string( tmp_buf );
					XMLString::release( &tmp_buf );

					xr->found_el = found_el;

				}
				else
				{
					if ( xr->snmp_indexed_name.compare( "" ) == 0 )
					{
						// ziskame nazev typu elementu
						value = found_el->getTagName();
						tmp_buf = XMLString::transcode( value );

						vp->oid = string( tmp_buf );

						//Chceme pouze jedinou instanci daneho objektu
						vp->oid += ".0";


						XMLString::release( &tmp_buf );
					}
					else
					{
						vp->oid = xr->snmp_indexed_name;
					}

					//uklizeni v nasi erarni strukture
					xr->snmp_indexed_name = "";
					xr->xpath_end = "";

				}

				if ( msg_type == XML_MSG_TYPE_GET )
				{
					xr->request_list.push_back( vp );
					create_vp = true;
				}
			}
			else if ( msg_type == XML_MSG_TYPE_SET && XMLString::equals( el->getTagName(), X( "value" ) ) )
			{
				value =  el->getTextContent();
				tmp_buf = XMLString::transcode( value );
				vp->value = string( tmp_buf );

				XMLString::release( &tmp_buf );

				xr->request_list.push_back( vp );
				create_vp = true;
			}
			else
			{
				xr->error = XML_MSG_ERR_XML;
				xr->error_str = "Unknown element in GET/SET message";
				delete ( vp );
				enqueue_response( xr );
				return NULL;

			}
		}
	}

	/*
	Navratime element, ktery je pote poslan do fronty
	*/
	return xr;
}


/*
SUBSCRIBE message
*/
struct request_data* XmlModule::process_subscribe_message( DOMElement *elem, const char* password, string manager_ip )
{
	/*
	Podivame se, jestli je pritomen atribut distrId a delete.
	Paklize ano, tak budeme modifikovat existujici subscription.
	Nejprve ale musime zjistit, jestli takova existuje. Paklize ne, tak
	je to chyba a hodime error. Jinak pokracujeme ziskanim dat.
	*/

	//odpovedni data
	struct request_data *xr; // = new request_data;

	//pro upravu ci smazani subscriptions
	int distr_id = 0;
	bool del = false;

	//sprava hodnot
	const XMLCh* value;
	string tmp_str_xpath = "";
	char *tmp_buf;
	char tmpid[50];

	//elementove zalezitosti
	DOMDocument *doc = main_root->getOwnerDocument();
	DOMElement *xpath;
	DOMElement *subscription;
	DOMAttr *attr;
	DOMText* txt;
	string tmp_str;

	const DOMElement *sub;
	DOMElement *sub_append = NULL;
	DOMElement *subscriptions;

	DOMAttr *sub_attr;

	DOMNode *parent;
	DOMElement *device;

	log_message( log_file, "Dostali jsme SUBSCRIBE message" );

	value = elem->getAttribute( X( "distrid" ) );
	tmp_buf = XMLString::transcode( value );
	distr_id = atoi( tmp_buf );

	tmp_str_xpath = "//subscriptions/subscription[@distrid='";
	tmp_str_xpath += tmp_buf;
	tmp_str_xpath += "']";

	XMLString::release( &tmp_buf );

	value = elem->getAttribute( X( "delete" ) );
	tmp_buf = XMLString::transcode( value );
	del = (bool) atoi( tmp_buf );
	XMLString::release( &tmp_buf );

	if ( distr_id > 0 )
	{
		//zjistime, jestli existuje dana subscriptiona
		sub = find_element( X( tmp_str_xpath.c_str() ), main_xa_doc->doc, false );

		if ( sub == NULL )
		{
			//Error
			xr = new request_data;

			value = elem->getAttribute( X( "msgid" ) );
			tmp_buf = XMLString::transcode( value );
			xr->msgid =  atoi( tmp_buf );
			XMLString::release( &tmp_buf );


			xr->thread_id = pthread_self();
			xr->msg_type = XML_MSG_TYPE_SUBSCRIBE;
			xr->distr_id = distr_id;
			xr->error = XML_MSG_ERR_XML;
			xr->error_str = "Such subscription does not exist.";

			enqueue_response( xr );
			return NULL;

		}

		/*
		Delete dane subscription
		*/
		if ( del )
		{
			parent = sub->getParentNode();
			DOMNodeList *lst = parent->getChildNodes();

			for ( unsigned int i = 0; i < lst->getLength(); i++ )
			{
				subscription = dynamic_cast<DOMElement *>( lst->item( i ) );
				value = subscription->getAttribute( X( "distrid" ) );
				tmp_buf = XMLString::transcode( value );

				if ( distr_id == atoi( tmp_buf ) )
				{
					XMLString::release( &tmp_buf );
					break;
				}

				XMLString::release( &tmp_buf );

			}

			//Nyni je v subscription dany element, ktery chceme smazat.

			attr = doc->createAttribute( X( "delete" ) );
			attr->setNodeValue( X("1") );

			xr = new request_data;

			value = elem->getAttribute( X( "msgid" ) );
			tmp_buf = XMLString::transcode( value );
			xr->msgid =  atoi( tmp_buf );
			XMLString::release( &tmp_buf );

			xr->thread_id = pthread_self();
			xr->distr_id = distr_id;
			xr->msg_type = XML_MSG_TYPE_SUBSCRIBE;

			pthread_mutex_lock( &subscr_cond_lock );

			subscription->setAttributeNode( attr );
			subscriptions_changed = true;
			
			pthread_cond_broadcast( &subscr_wait );
			pthread_mutex_unlock( &subscr_cond_lock );


			//ihned vratime prazdna data
			enqueue_response( xr );
			return NULL;
		}



	}


	/*
	Subscribe ma stejnou strukturu jako get message.
	Nechame tedy ziskat data pomoci GET message, cimz se overi, ze je vse v poradku
	a teprve pote budeme s daty vice manipulovat.
	*/
	xr = process_get_set_message( elem, password, XML_MSG_TYPE_GET, 0 );


	if ( xr == NULL )
	{
		return NULL;
	}


	//Nyni mame data ziskana.
	if ( xr->error == XML_MSG_ERR_XML || xr->error == XML_MSG_ERR_SNMP )
	{
		/*
		Error. Nektera data nemusi existovat. 
		Posleme zpet error
		*/
		
		xr->msg_type = XML_MSG_TYPE_SUBSCRIBE;
		enqueue_response( xr );
		return NULL;
		//return xr;
	}
	else
	{
		xr->msg_type_resp = XML_MSG_TYPE_SUBSCRIBE;
	}

	//Data jsou v poradku, nez je vratime zpet, zapiseme tento element do
	//hlavniho stromu

	char di[20];
	if ( distr_id > 0 )
	{

		/*
		Altering subscription
		*/
		DOMNode *parent = sub->getParentNode();
		DOMNodeList *ls = parent->getChildNodes();
		DOMNode *lch;

		sprintf( di, "%d", distr_id );



		for ( unsigned int i = 0; i < ls->getLength(); i++ )
		{
			sub_append = dynamic_cast<DOMElement *> ( ls->item( i ) );
			if ( XMLString::equals( X( di ), sub_append->getAttribute( X( "distrid" ) ) ) )
				break;
		}

		if ( sub_append == NULL )
		{
			log_message( log_file, "SUBSCR: cannot find element to change" );
		}



		pthread_mutex_lock( &subscr_cond_lock );


		//subscr uz je handle na ten element. Muzeme pracovat s nim
		sub_attr = sub_append->getAttributeNode( X( "frequency" ) );
		value = elem->getAttribute( X("frequency") );
		if ( XMLString::equals( value, X("") ) )
		{
			sprintf( di, "%d", XML_SUBSCRIBE_DEF_FREQUENCY );
			value = XMLString::transcode( di );
		}
		sub_attr->setNodeValue( value );
		sub_attr = sub_append->getAttributeNode( X( "manager" ) );
		sub_attr->setNodeValue( X( manager_ip.c_str() ) );

		//smazeme vsechny deti
		ls = sub_append->getChildNodes();
		while ( ls->getLength() > 0 )
		{
			DOMNode *n = ls->item(0);
			sub_append->removeChild( n );
		}

		ls = elem->getChildNodes();

		for ( unsigned int i=0; i < ls->getLength(); i++ )
		{
			lch = ls->item(i);
			value = lch->getTextContent();
			
			if ( lch->getNodeType() == DOMNode::ELEMENT_NODE && !XMLString::equals( value, X("") ) )
			{
				xpath = doc->createElement( X( "xpath" ) );
				txt = doc->createTextNode( value );
				xpath->appendChild( txt );

				sub_append->appendChild( xpath );
			}
			
		}

		//dame mu prizvisko, changed
		attr = doc->createAttribute( X( "changed" ) );
		attr->setNodeValue( X( "1" ) );
		sub_append->setAttributeNode( attr );

		attr = doc->createAttribute( X( "obj_id" ) );
		sprintf( tmpid, "%d", xr->object_id );
		attr->setNodeValue( X( tmpid ) );
		sub_append->setAttributeNode( attr );

		pthread_mutex_unlock( &subscr_cond_lock );

		xr->distr_id = distr_id;


	}
	else
	{
		//Pridavame

		//Nejrpve najdeme posledni distr_id
		DOMNodeList *sl = main_root->getElementsByTagName( X( "subscriptions" ) );
		if ( sl->getLength() <= 0 )
		{
			log_message( log_file, "No element subscriptions found" );
			enqueue_response( xr );
			return NULL;

		}

		//nejprve nalezeni toho spravneho device
		for( unsigned int i = 0; i < sl->getLength(); i++ )
		{
			subscriptions = dynamic_cast<DOMElement *> ( sl->item( i ) );

			parent = subscriptions->getParentNode();
			device = dynamic_cast<DOMElement *>(parent);

			value = device->getAttribute( X("id") );
			tmp_buf = XMLString::transcode( value );

			if ( atoi( tmp_buf ) == xr->object_id )
			{
				XMLString::release( &tmp_buf );
				break;
			}

			XMLString::release( &tmp_buf );

		}

		DOMNode *lch = subscriptions->getLastChild();
		if ( lch == NULL )
		{
			distr_id = 1;
		}
		else
		{
			subscription = dynamic_cast< DOMElement *> (lch );
			value = subscription->getAttribute( X( "distrid" ) );
			tmp_buf = XMLString::transcode( value );

			distr_id = atoi( tmp_buf );
			//zvolima nasledujici id
			distr_id++;
			XMLString::release( &tmp_buf );

		}

		xr->distr_id = distr_id;

		/*
		Vytvoreni noveho elementu do hlavniho stromu!!
		*/
		subscription = doc->createElement( X( "subscription" ) );
		attr = doc->createAttribute( X( "distrid" ) );
		sprintf( tmpid, "%d", distr_id );
		attr->setNodeValue( X( tmpid ) );


		subscription->setAttributeNode( attr );

		attr = doc->createAttribute( X( "frequency" ) );
		value = elem->getAttribute( X( "frequency" ) );

		//paklize tam neni frekvence - dame to na definovany interval
		if ( XMLString::equals( value, X("") ) )
		{
			sprintf( di, "%d", XML_SUBSCRIBE_DEF_FREQUENCY );
			value = XMLString::transcode( di );
		}

		attr->setNodeValue( value );
		subscription->setAttributeNode( attr );


		//Vsechny xpathy
		sl = elem->getChildNodes();

		for ( unsigned int i=0; i < sl->getLength(); i++ )
		{
			lch = sl->item(i);
			value = lch->getTextContent();

			if ( lch->getNodeType() == DOMNode::ELEMENT_NODE && !XMLString::equals( value, X("") ) )
			{
				xpath = doc->createElement( X( "xpath" ) );
				txt = doc->createTextNode( value );
				xpath->appendChild( txt );

				subscription->appendChild( xpath );
			}
			
		}

		//identifikace managera je hodnota textoveho nodu
		attr = doc->createAttribute( X("manager") );
		attr->setNodeValue( X( manager_ip.c_str() ) );

		subscription->setAttributeNode( attr );

		//dame mu prizvisko, changed
		attr = doc->createAttribute( X( "changed" ) );
		attr->setNodeValue( X( "1" ) );
		subscription->setAttributeNode( attr );

		attr = doc->createAttribute( X( "obj_id" ) );
		sprintf( tmpid, "%d", xr->object_id );
		attr->setNodeValue( X( tmpid ) );
		subscription->setAttributeNode( attr );



		//Zapsani do celkoveho seznamu subscriptions
		pthread_mutex_lock( &subscr_cond_lock );
		subscriptions->appendChild( subscription );

		//Musime domapovat novou vec do xml
		pthread_mutex_unlock( &subscr_cond_lock );

		
	}

	//probudit thread, aby si to zpracoval

	pthread_mutex_lock( &subscr_cond_lock );
	//subscriptions se zmenily - nutne oznamit distr threadu
	subscriptions_changed = true;
	
	pthread_cond_signal( &subscr_wait );
	pthread_mutex_unlock( &subscr_cond_lock );


	return xr;
}

/****************************************************
********** Enqueue a probouzeni threadu
******************************************************/

/*
Zaradime strukturu do odpovedni fronty a probudime vsechny spici thready
*/
void XmlModule::enqueue_response( struct request_data * xr )
{
	//Zamkneme pristup do fronty
	pthread_mutex_lock( &response_lock );
	//pridame odpoved
	response_queue.push_back( xr );
	pthread_mutex_unlock( &response_lock );


	pthread_mutex_lock( &condition_lock );
	//Probudime vsechny, kteri cekaji na nejakou odpoved
	pthread_cond_broadcast( &resp_cond );
	pthread_mutex_unlock( &condition_lock );

}

/************************************************
********* Access Control
************************************************/
int XmlModule::operation_permitted( const char *passwd, SNMP_device *dev, int msg_type)
{
	bool write_perm = false;
	bool read_perm = false;

	//Nejprve porovname xml passwords
	log_message( log_file, "XML: a" );
	if ( strcmp( passwd, dev->xml_read ) == 0 )
	{
		read_perm = true;
	}
	else if( strcmp( passwd, dev->xml_write ) == 0 )
	{
		write_perm = true;
	}
	else
	{
		//nema pravo jakehokoliv pristupu. Heslo je spatne
		return 0;
	}

	log_message( log_file, "XML: b" );

	//o jakou operaci jde
	switch ( msg_type )
	{
		case XML_MSG_TYPE_GET:
			if ( read_perm )
				return XML_PERM_READ;
			else if ( write_perm )
				return XML_PERM_WRITE;
			break;

		case XML_MSG_TYPE_SET:
			if ( write_perm )
				return XML_PERM_WRITE;
			else
				return 0;
			break;
	}

	return 0;

}


/*
Vrati snmp community dle typu message, ktery provozujeme
*/
string XmlModule::get_snmp_community( int perm_type, SNMP_device *dev)
{
	string ret;

	switch ( perm_type )
	{
		case XML_PERM_READ:
			ret = string( dev->snmp_read );
			break;

		case XML_PERM_WRITE:
			ret = string( dev->snmp_write );
			break;

		default:
			ret = "";
	}

	return ret;
}


/*
Spusti thread na distribution
*/
int XmlModule::initialize_threads() 
{
	int th_ret;

	th_ret = pthread_create( &distribution_th, NULL, distrib_handle, (void *) this); 

	if ( th_ret != 0 )
	{
		log_message( log_file, "Cannot start distribution thread" );
		return th_ret;
	}

	return 0;
}

pthread_t *XmlModule::get_distr_thread_id( )
{
	return &distribution_th;
}


/************************************
****Distribution handler************
************************************/
int XmlModule::distribution_handler()
{

	/*
	Reseni timeoutu
	- kazda struktura obsahuje time_remaining. Vezmeme nejmensi time remaining a na ten se uspime.
	Odecteme to ode vsech, pripadne nastavime cely timeout tam, kde jsme prekrocili 0.
	Opakujeme cely proces do nekonecna
	*/
	log_message( log_file, "Distribution thread started" );

	//definovane struktury
	DOMElement *subscriptions;
	DOMNodeList *sl;
	DOMElement *subscription;
	DOMNodeList *all_subs;
	DOMDocument *doc;

	//seznam vsech subscriptions
	list<struct subscription_element *> sub_list;
	list<struct subscription_element *>::iterator it;
	list<struct request_data *> responses;
	list<struct request_data *>::iterator res_it;
	list<DOMElement *> delete_nodes;
	list<DOMElement *>::iterator del_it;

	//casova struktura
	struct timespec ts;
	int timeout;
	int time_slept;
	int sleep_start;

	//pomocne buffery
	char *tmp_buf;
	const XMLCh *value;
	int tmp_int;
	int changed;
	int to_del;
	int list_length;

	/*
	libCUrl section
	*/
	CURL 					*curl;
	CURLcode 				res;
	struct curl_httppost 	*formpost=NULL;
	struct curl_httppost 	*lastptr=NULL;
	struct curl_slist 		*headerlist=NULL;
	static const char 		buf[] = "Expect:";
	string url;
	char trans_port[50];
	string final_message;


	//data
	struct request_data *xr;
	list<struct request_data*> req_to_dev[ devices_no ]; 

	//hlida pocet odeslanych zadosti
	int msg_sent = 0;

	/*
	Ziskame handle na subscriptions
	*/
	all_subs = main_root->getElementsByTagName( X("subscriptions") );

	time_slept = 0;
	sleep_start = 0;

	/*
	Ulozime si port, na ktery mame posilat data
	*/
	SNMP_device *dev = snmpmod->get_gate_device();
	sprintf( trans_port, ":%d", dev->xml_trans_port );

	headerlist = curl_slist_append(headerlist, buf);

	doc = main_root->getOwnerDocument();

	while( 1 )
	{
		//check na zmenu
		pthread_mutex_lock( &subscr_cond_lock );

		if ( msg_sent == 0 && sub_list.size() <= 0 )
		{
			//jdeme spat, pac nic nemame obsluhovat
			pthread_cond_wait( &subscr_wait, &subscr_cond_lock );
		}
		else
		{
			//jdeme spat pouze na dany timeout
			if ( msg_sent != 0 )
				timeout = 1;

			 ts.tv_sec = time(NULL) + timeout;
			 ts.tv_nsec = 0;
				
			 sleep_start = time(NULL);
			 pthread_cond_timedwait( &subscr_wait, &subscr_cond_lock, &ts );

			 //Kolik jsme opravdu spali
			 //dulezite pak pro odecteni casu v ramci jednotlivych subscriptions
			 time_slept = time(NULL) - sleep_start;
		}

		if ( subscriptions_changed )
		{
			//Predelani subscriptions
			subscriptions_changed = false;


			for ( unsigned int m = 0; m < all_subs->getLength(); m++ )
			{
				subscriptions = dynamic_cast<DOMElement *> ( all_subs->item( m ) );

				//toto uz jsou jednotlive subscriptiony
				sl = subscriptions->getChildNodes();
				list_length = sl->getLength();

				for ( int i = 0; i < list_length; i++ )
				{
					subscription = dynamic_cast<DOMElement*> ( sl->item( i ) );
					
					changed = 0;
					value = subscription->getAttribute( X("changed") );
					tmp_buf = XMLString::transcode( value );
					changed = atoi( tmp_buf );
					XMLString::release( &tmp_buf );

					to_del = 0;
					value = subscription->getAttribute( X("delete") );
					tmp_buf = XMLString::transcode( value );
					to_del = atoi( tmp_buf );
					XMLString::release( &tmp_buf );

					value = subscription->getAttribute( X( "distrid" ) );
					tmp_buf = XMLString::transcode( value );
					tmp_int = atoi( tmp_buf );
					XMLString::release( &tmp_buf );

					//Smazeme pripadny vyskyt v seznamu a nahradime jej novym
					for ( it = sub_list.begin(); it != sub_list.end(); it++ )
					{
						if ( changed && (*it)->distr_id == tmp_int )
						{
							sub_list.erase( it );
							break;
						}
						else if ( ( (*it)->distr_id == tmp_int ) && ( to_del == 0 ) )
						{
							(*it)->in_list = true;
							break;
						}
						else if ( ( (*it)->distr_id == tmp_int ) && to_del )
						{
							(*it)->in_list = false;
							break;
						}

					}

					//paklize se node zmenil
					if ( changed )
					{
						log_message( log_file, "subscription changed" );


						//vytvorime nove informace
						struct subscription_element *el = new subscription_element;

						el->distr_id = tmp_int;
						el->subscription = subscription;

						value = subscription->getAttribute( X( "frequency" ) );
						tmp_buf = XMLString::transcode( value );
						tmp_int = atoi( tmp_buf );
						XMLString::release( &tmp_buf );
						el->frequency = tmp_int;
						el->time_remaining = tmp_int;

						value = subscription->getAttribute( X( "manager" ) );
						tmp_buf = XMLString::transcode( value );
						el->manager_ip = string( tmp_buf );
						XMLString::release( &tmp_buf );

						value = subscription->getAttribute( X( "obj_id" ) );
						tmp_buf = XMLString::transcode( value );
						tmp_int = atoi( tmp_buf );
						el->object_id = tmp_int;
						XMLString::release( &tmp_buf );

						subscription->removeAttribute( X( "changed" ) );
						subscription->removeAttribute( X( "obj_id" ) );

						//ziskani hesla pro get
						struct SNMP_device *dev = snmpmod->get_device_ptr( el->object_id );
						el->password = dev->xml_read;

						el->in_list = true;

						//zaradime do seznamu nasich subscriptionu
						sub_list.push_back( el );
						
					}
					else if ( to_del )
					{
						/*
						Mazany dame do listu a pak jej smazeme
						*/
						delete_nodes.push_back( subscription );
					}

				} //end of jednotlive subscriptiony jednotliveho device
			} //end of cely for cyklus projizdeni vsech subscriptions

			/*
			Fyzicke smazani danych subscriptions
			*/
			del_it = delete_nodes.begin();
			while ( del_it != delete_nodes.end() )
			{
				DOMNode *tmp_node = (*del_it)->getParentNode();
				tmp_node->removeChild( (*del_it) );
				del_it = delete_nodes.erase( del_it );
			}

			/*
			Smazeme prispevky, ktere nejsou uz nikde
			*/
			it = sub_list.begin();
			while ( it != sub_list.end() )
			{
				if ( (*it)->in_list == true )
				{
					(*it)->in_list = false;
					it++;
				}
				else
				{
					it = sub_list.erase( it );
				}
			}

			/*
			Prepocitame timeout
			*/
			timeout = recalculate_sleep( &sub_list, it );


			/*
			Znovu-vybudujeme mapovani DOMDocument-XalanDocument
			*/
			recreate_xalan_doc();


		}


		pthread_mutex_unlock( &subscr_cond_lock );



		/*
		Processneme vsechny subscriptions, jestli je mame zpracovat
		*/
		for ( it = sub_list.begin(); it != sub_list.end(); it++ )
		{
			//odecteme kolik jsme opravdu spali.
			if ( (*it)->is_new )
				(*it)->is_new = false;
			else
				(*it)->time_remaining -= time_slept;

			if ( (*it)->time_remaining <= 0 )
			{
				//Nutno ziskat informace a hodit je managerovi
				(*it)->time_remaining = (*it)->frequency;


				xr = process_get_set_message( (*it)->subscription, (*it)->password.c_str(), XML_MSG_TYPE_GET, (*it)->object_id );
				msg_sent++;

				//if error
				if ( xr != NULL )
				{
					//dispatch
					xr->msg_type_resp = XML_MSG_TYPE_SUBSCRIBE;
					xr->distr_id = (*it)->distr_id;
					req_to_dev[ xr->object_id ].push_back( xr );
				}

			}

		}

		 /*
		 Rozhozeni jednotlivych messages jako celky do jednotlivych zarizeni
		 */
		 for( int i = 0; i < devices_no; i++ )
		 {
			 if (  (!req_to_dev[i].empty()) )
			 {
				if (  snmpmod->dispatch_request( &req_to_dev[i] ) != 0 )
				{
					msg_sent--;
					log_message( log_file, "Nepodarilo se odeslat request v ramci SUBSCRIBE" );
				}

				req_to_dev[i].clear();
			 }
		 }


		/*
		Znovu prepocitame dane timeouty
		*/
		timeout = recalculate_sleep( &sub_list , it );
		/*
		Check na odpovedi a odeslani clobrdum
		*/
		if ( msg_sent > 0 )
		{
			log_message( log_file, "DISTR: processing respnoses" );
			 pthread_mutex_lock( &response_lock );

			 res_it = response_queue.begin();


			 while ( res_it != response_queue.end() )
			 {
				 if ( (*res_it)->thread_id == pthread_self() ) //check na moje thread ID
				 {
					 //vybereme zpravu a ulozime si ji k dalsimu zpracovani
					 responses.push_back( (*res_it) );
					 res_it = response_queue.erase( res_it );
					 msg_sent--;
				 }
				 else
				 {
					 res_it++;
				 }

			 }

			 pthread_mutex_unlock( &response_lock );
			 res_it = responses.begin();
			 list<struct request_data*>::iterator tmp_it;
			 while( res_it != responses.end() )
			 {
				 /*
				 Toto je kvuli SUBSCRIBE, abychom mohli vyuzit
				 fci process_get message, tak ulozime odpovedni typ
				 do request_data a pak jej vytahneme zpet.
				 */
				 if ( (*res_it)->msg_type_resp != 0 )
					 (*res_it)->msg_type = (*res_it)->msg_type_resp;


				for ( it = sub_list.begin(); it != sub_list.end(); it++ )
				{
					if ( (*it)->distr_id == (*res_it)->distr_id )
					{
						break;
					}
				}

				/*
				Paklize je tam snmp error, tak rusime dorucovani
				*/
				if ( (*res_it)->error == XML_MSG_ERR_SNMP )
				{
					log_message( log_file, "DELETING subscription. Snmp error" );
					//dat k nemu change a delete
					DOMAttr *attr;
					attr = doc->createAttribute( X( "delete" ) );
					attr->setNodeValue( X("1") );

					pthread_mutex_lock( &subscr_cond_lock );

					(*it)->subscription->setAttributeNode( attr );
					subscriptions_changed = true;
					
					pthread_mutex_unlock( &subscr_cond_lock );
				}

				(*it)->last_msg_id++;
				(*res_it)->msgid = (*it)->last_msg_id;

				string *out = build_response_string( (*res_it) );

				final_message = "<message context=\"\">";
				final_message += *out;
				final_message += "</message>";




				curl = curl_easy_init();

				if ( curl )
				{
					curl_formadd( &formpost, &lastptr,
									CURLFORM_COPYNAME, "selection",
									CURLFORM_COPYCONTENTS, final_message.c_str(),
									CURLFORM_CONTENTTYPE,"text/xml",
									CURLFORM_END);

					

					url = "http://";

					/*
					Get the manager ip
					*/
					url += (*it)->manager_ip;

					url += trans_port;

					//nastavime parametry a odesleme
					curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
					curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);

					curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
					res = curl_easy_perform(curl);

					if ( res != CURLE_OK )
					{
						log_message( log_file, "DISTR: could not send the distribution data" );
						log_message( log_file, curl_easy_strerror( res ) );

						//it obsahuje odkaz na distribution element
						(*it)->lost_msg_count++;

						if ( (*it)->lost_msg_count >= XML_SUBSCRIBE_MAX_LOST )
						{
							log_message( log_file, "DELETING subscription. 5 times not delivered" );
							//dat k nemu change a delete
							DOMAttr *attr;
							attr = doc->createAttribute( X( "delete" ) );
							attr->setNodeValue( X("1") );

							pthread_mutex_lock( &subscr_cond_lock );

							(*it)->subscription->setAttributeNode( attr );
							subscriptions_changed = true;
							
							pthread_mutex_unlock( &subscr_cond_lock );
						}
					}


					/*
					clean up
					*/
					curl_easy_cleanup(curl);
				}
				else
				{
					log_message( log_file, "DISTR: ERROR, could not initialize curl session" );
				}

				/*
				ostatni delete
				*/
				//curl_slist_free_all (headerlist);
				curl_formfree(formpost);
				lastptr = NULL;
				formpost = NULL;


				tmp_it = res_it;
				res_it++;

				responses.pop_front();

				delete( out );
				//delete( (*tmp_it) );
			 }

			
		}



	}


	return 0;
}


/*
Projede list a vrati nejmensi hodnotu remaining_time
*/
int XmlModule::recalculate_sleep( list<struct subscription_element*> *list, list<struct subscription_element*>::iterator it)
{

	int min = (list->front())->time_remaining;

	for ( it = list->begin(); it != list->end(); it++ )
	{
		if ( (*it)->time_remaining < min )
			min = (*it)->time_remaining;
	}

	return min;
}

/*
Znovu namapuje DOMDocument na XalanDocument
*/
void XmlModule::recreate_xalan_doc()
{
	pthread_mutex_lock( &xa_doc_lock );

	delete( main_xa_doc->liaison );

	DOMDocument *doc;
	XercesDOMSupport		theDOMSupport;

	doc = main_root->getOwnerDocument();
	main_xa_doc->liaison = new XercesParserLiaison( theDOMSupport );
	main_xa_doc->doc = main_xa_doc->liaison->createDocument( doc, true, true, true );

	/*
	TODO: TEMP zaznam. pouze pro kontrolu vystupu. V produkci smazat!
	*/
	Mib2Xsd::output_xml2file( "xalan.xsd", main_root->getOwnerDocument());

	pthread_mutex_unlock( &xa_doc_lock );

}
