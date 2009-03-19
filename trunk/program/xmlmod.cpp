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
	devices_root = NULL;
	log_file = NULL;
	xsd_dir = NULL;

	//ziskame implementaci pro vytvareni dokumentu
	impl = DOMImplementationRegistry::getDOMImplementation( XMLString::transcode("core") );

	parser = new XercesDOMParser;
	parser->setValidationScheme( XercesDOMParser::Val_Never);
	parser->setDoNamespaces( false );
	parser->setDoSchema( false );
	parser->setLoadExternalDTD( false );


	//proc_req_ptr = &XmlModule::process_request;

}

/*
Destruktor
*/
XmlModule::~XmlModule()
{
}

/*
Nastaveni parametru
*/
void XmlModule::set_parameters( DOMElement *r, list<DOMElement *> *roots, char *log, char *xsd, SnmpModule *sn )
{
	main_root = r;
	devices_root = roots;
	log_file = log;
	xsd_dir = xsd;
	snmpmod = sn;
}

/*
Process message
*/

int XmlModule::process_request( void *cls, struct MHD_Connection *connection, const char *url, const char *method, 
			const char *version, const char *upload_data, size_t *upload_data_size, void **con_cls )
{
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
			send_error_response( connection, XML_ERR_NO_HTTP_POST );
		}

		*con_cls = (void *)con_info;
		return MHD_YES;

	}

	if ( strcmp( method, "POST") == 0 )
	{
		struct connection_info *con_info = (struct connection_info *) *con_cls;

		if ( *upload_data_size != 0 )
		{
			MHD_post_process( con_info->post_processor, upload_data, *upload_data_size );
			*upload_data_size = 0;

			return MHD_YES;
		}
		else
		{
			//parsujeme zpravu - ziskani XML struktury
			//TODO: validovat zpravu pri parsovani!!!

			MemBufInputSource *mem_buf = new  MemBufInputSource( (const XMLByte *) con_info->data.c_str(), strlen(con_info->data.c_str()),
					"message", NULL);

			DOMDocument *message;

			try {
				parser->parse( *mem_buf );
				message = parser->getDocument();
			}
			catch ( const XMLException &e )
			{
				log_message( log_file, "Error parsing buffer" );
				log_message( log_file, XMLString::transcode( e.getMessage() ) );
				return MHD_NO;
			}

			//Zjistime, jestli je message root elementem
			DOMElement *msg_root = message->getDocumentElement();

			if ( !msg_root )
			{
				log_message( log_file, "No root element in message" );
				send_error_response( connection, XML_ERR_UNKNOWN );
				message->release();
				return MHD_NO;
			}

			//paklize neni root == message, tak spatnej format zpravy
			if ( !XMLString::equals( msg_root->getTagName(), X("message") ) )
			{
				log_message( log_file, "No message element in message" );
				send_error_response( connection, XML_ERR_WRONG_MSG );
				message->release();
				return MHD_NO;
			}

			//TODO: zde nas zajima attribut password => community pro snmp

			//Nasleduje cyklus pro kazdy podelement rootu -> jeden prikaz na zpracovani
			DOMNodeList *children = msg_root->getChildNodes();

			//TODO: nutno send_response dat az ven z for cyklu a poslat pouze 1 zpravu!!

			for( XMLSize_t i = 0; i < children->getLength(); i++ )
			{
				DOMNode *node = children->item(i);

				
				if ( node->getNodeType() && node->getNodeType() == DOMNode::ELEMENT_NODE)
				{
					DOMElement *elem = dynamic_cast<DOMElement *>(node);

					log_message( log_file, "element");

					struct request_data *xr;

					//zpracovani nejake zpravy
					if ( XMLString::equals( elem->getTagName(), X( "discovery" ) ) )
					{
						xr = process_discovery_message( connection, elem );
					//volani fce na vybudovani odpovedi
					//string *out = build_response_string( xr );

					//send_response( connection, out );
					}
					else if ( XMLString::equals( elem->getTagName(), X( "get" ) ) )
					{
						//process_discovery_message( connection, elem );
					}
					else if ( XMLString::equals( elem->getTagName(), X( "set" ) ) )
					{
						//process_discovery_message( connection, elem );
					}
					else
					{
						//Neznama zprava -> error
						log_message( log_file, "element error");
						//send_error_response( connection, XML_ERR_UNKNOWN_MSG );
						message->release();
						return MHD_NO;
					}


				}
			}

		 message->release();
		 return MHD_NO;
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

	string tmp = data;

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
int XmlModule::send_response( struct MHD_Connection *connection, string *out )
{
	int ret;
	struct MHD_Response *response = NULL;

	log_message( log_file, "Sending response");
	log_message( log_file, out->c_str() );
	response = MHD_create_response_from_data( strlen( out->c_str() ), (void *) out->c_str(), MHD_YES, MHD_NO );

	ret = MHD_queue_response( connection, MHD_HTTP_OK, response );
	MHD_destroy_response( response);

	log_message( log_file, "returning from send response" );
	return ret;
}

/*
Odeslani chybne zpravy zpet
*/
int XmlModule::send_error_response( struct MHD_Connection *connection, int error )
{
	int ret;
	struct MHD_Response *response = NULL;

	const char *wrong_message_type = "Must be POST method";

	switch (error)
	{
		case XML_ERR_NO_HTTP_POST:
			//send back page
			response = MHD_create_response_from_data( strlen (wrong_message_type), (void *)wrong_message_type, MHD_NO, MHD_NO);
			break;

		default:
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
	string *out;
	char *buf;

	if ( data->msg_type == XML_MSG_TYPE_DISCOVERY )
	{
		DOMImplementation *impl;
		DOMWriter *writer;
		XMLCh *xsd = NULL;
		WriterErrHandler *err = new WriterErrHandler( log_file );


		impl = DOMImplementationRegistry::getDOMImplementation( XMLString::transcode("LS") );

		writer = ((DOMImplementationLS*)impl)->createDOMWriter();

		writer->setErrorHandler( err );


		//writer->setFeature( XMLUni::fgDOMWRTFormatPrettyPrint, true );

		//Jestli budeme posilat main xsd, nebo xsd zarizeni
		if ( data->discovery_object_id == -1 )
		{
			//main xsd
			try {
				xsd = writer->writeToString( *main_root );
			}
			catch ( ... )
			{
				log_message( log_file, "Exception writing to string" );
			}
		}
		else
		{
			//xsd daneho zarizeni
		}

		if ( xsd != NULL )
		{
			buf = XMLString::transcode( xsd );
			out = new string( buf );

			//log_message( log_file, out->c_str() );

			delete ( buf );
		
			XMLString::release( &xsd );
		}
		else
		{
			out = new string("");
			log_message( log_file, "write to string failed" );
		}
		writer->release();
	}

	return out;
}


/****************************************************
************** Cast zpracovani zprav*****************
*****************************************************/


/*
DISCOVERY MESSAGE
*/
struct request_data* XmlModule::process_discovery_message( struct MHD_Connection *connection, DOMElement *elem )
{
	struct request_data *xr = new request_data;
	const XMLCh* value;

	/*
	TODO:
	protocolVersion check
	*/

	log_message( log_file, "Dostali jsme DISCOVERY message" );

	xr->msg_type = XML_MSG_TYPE_DISCOVERY;

	value = elem->getAttribute( X( "msgid" ) );
	xr->msgid = atoi( XMLString::transcode( value ) );

	value = elem->getAttribute( X( "objectId" ) );
	if ( !XMLString::equals( value, X( "" ) ) )
		xr->discovery_object_id = atoi( XMLString::transcode( value ) );


	return xr;
}



