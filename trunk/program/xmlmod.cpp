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

			//Zakladni promenne
			DOMDocument *message;
			const XMLCh* value;
			char* msg_context;
			string *response_string;
			char* password;

			try {
				parser->parse( *mem_buf );
				message = parser->getDocument();
			}
			catch ( const XMLException &e )
			{
				log_message( log_file, "Error parsing buffer" );
				log_message( log_file, XMLString::transcode( e.getMessage() ) );
				send_error_response( connection, XML_ERR_UNKNOWN );
				delete( mem_buf );
				return MHD_NO;
			}

			//Zjistime, jestli je message root elementem
			DOMElement *msg_root = message->getDocumentElement();

			if ( !msg_root )
			{
				log_message( log_file, "No root element in message" );
				//odesilame error
				send_error_response( connection, XML_ERR_UNKNOWN );
				message->release();
				delete( mem_buf );
				return MHD_NO;
			}

			//paklize neni root == message, tak spatnej format zpravy
			if ( !XMLString::equals( msg_root->getTagName(), X("message") ) )
			{
				log_message( log_file, "No message element in message" );
				//spatny format zpravy
				send_error_response( connection, XML_ERR_WRONG_MSG );
				message->release();
				delete( mem_buf );
				return MHD_NO;
			}

			//Ulozime password = community string
			value = msg_root->getAttribute( X( "password" ) );
			password = XMLString::transcode( value );

			//Ulozime si Message Context atribut
			value = msg_root->getAttribute( X( "context" ) );
			msg_context = XMLString::transcode( value );

			response_string = new string;;

			//Nasleduje cyklus pro kazdy podelement rootu -> jeden prikaz na zpracovani
			DOMNodeList *children = msg_root->getChildNodes();


			for( XMLSize_t i = 0; i < children->getLength(); i++ )
			{
				DOMNode *node = children->item(i);

				
				if ( node->getNodeType() && node->getNodeType() == DOMNode::ELEMENT_NODE)
				{
					DOMElement *elem = dynamic_cast<DOMElement *>(node);

					struct request_data *xr;

					//zpracovani nejake zpravy
					if ( XMLString::equals( elem->getTagName(), X( "discovery" ) ) )
					{
						xr = process_discovery_message( connection, elem );
						//volani fce na vybudovani odpovedi

					//send_response( connection, out );
					}
					else if ( XMLString::equals( elem->getTagName(), X( "get" ) ) )
					{
						xr = process_get_message( elem, password );
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
						delete( mem_buf );
						return MHD_NO;
					}

					string *out = build_response_string( xr );
					*response_string += *out;
					delete( out );
					delete( xr );

				}

			}

		 message->release();

		 //odeslani odpovedi
		 int ret =  send_response( connection, msg_context,  response_string );
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
int XmlModule::send_error_response( struct MHD_Connection *connection, int error )
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

	//TODO: nejprve check, jestli data neobsahuji nastaveny error

	/*
	DISCOVERY message response
	*/
	if ( data->msg_type == XML_MSG_TYPE_DISCOVERY )
	{
		DOMImplementation *impl;
		DOMWriter *writer;
		XMLCh *xsd = NULL;
		WriterErrHandler *err = new WriterErrHandler( log_file );


		impl = DOMImplementationRegistry::getDOMImplementation( XMLString::transcode("LS") );

		writer = ((DOMImplementationLS*)impl)->createDOMWriter();

		writer->setErrorHandler( err );


		writer->setFeature( XMLUni::fgDOMWRTFormatPrettyPrint, true );

		//Jestli budeme posilat main xsd, nebo xsd zarizeni
		if ( data->object_id == -1 )
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
			int position = snmpmod->get_device_position( data->object_id );
			if ( position != -1  )
			{
				DOMElement *ro = get_device_document( position );

				xsd = writer->writeToString( *ro );
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

			//TODO: zamyslet se, jestli budeme posilat i neco s xquery nebo nechame xpath
			*out += "<info><xpath>1.0</xpath></info>\n";
			*out += "<dataModel>";


			buf = XMLString::transcode( xsd );
			*out += buf;

			*out += "</datamodel>\n";

			//log_message( log_file, out->c_str() );

			delete ( buf );
		
			XMLString::release( &xsd );
		}
		else
		{
			*out += "<error code=\"2\">Cannot generate data model</error>\n";
			log_message( log_file, "Cannot generate data model in response to DISCOVERY message" );
		}

		//uzavreni vystupni zpravy
		*out += "</publication>\n";

		writer->release();
	}
	/*
	GET message response
	*/
	else if ( data->msg_type == XML_MSG_TYPE_GET )
	{
		//check na error

		//vyrizeni odpovedi
	}

	return out;
}


/*
Vrati odkaz na DOMElement z devices_root
*/
DOMElement* XmlModule::get_device_document( int position )
{
	list<DOMElement *>::iterator it = devices_root->begin();

	for ( int a=0; a < position; a++ )
		it++;
	
	return *it;
}

/*
Nalezne element dle xpath vyrazu a vrati na nej odkaz
*/
string XmlModule::find_element( const XMLCh* name, DOMElement* doc_root )
{
	string ret_str = "";
	char *ret_buf;
	const XMLCh* value;

	log_message( log_file, "Starting find of oid" );

	/*
	TODO:
	doosetrit spatne xpath vyrazy -> nutno dat try catch elementy
	*/


	//Ziskame pointer na dokument daneho zarizeni
	DOMDocument *doc = doc_root->getOwnerDocument();

	//Pripravime struktury k vyhledani
	XercesDOMSupport		theDOMSupport;
	XercesParserLiaison	theLiaison(theDOMSupport);
	XalanDocument* const	theDocument =
			theLiaison.createDocument(doc, true, true, true);
	
	if ( theDocument == NULL )
	{
		log_message( log_file, "cannot create document" );
	}
	

	XalanDocumentPrefixResolver		thePrefixResolver(theDocument);
	XPathEvaluator	theEvaluator;

	XalanNode* const	theContextNode =
			theEvaluator.selectSingleNode(
				theDOMSupport,
				theDocument,
				XalanDOMString("xsd:schema").c_str(),
				thePrefixResolver);

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
		return ret_str;
	}
	else if ( nodeset.getLength() == 1 )
	{
		/*
		Nasli jsme pomoci XPath vyrazu dany element
		Nyni z neho dostaneme atribut name a ten navratime
		*/
		XalanNode *n = nodeset.item(0);

		if ( n->getNodeType() && n->getNodeType() != XalanNode::ELEMENT_NODE)
		{
			log_message( log_file, "Cannot get the element node" );
			return ret_str;
		}

		XalanElement *elm = dynamic_cast<XalanElement *>(n);

		XalanDOMString dom_str("name");

		value = (elm->getAttribute( dom_str )).c_str();
		ret_buf = XMLString::transcode( value );
		ret_str = string(ret_buf);

		XMLString::release( &ret_buf );
		return ret_str;
	}
	else
	{
		/*
		TODO paklize jsme nic nenasli. Zkusime odparsovat posledni cast - asi tabulka
		a zkusime to zavolat znovu. Paklize nic, tak to neexistuje
		*/
		log_message( log_file, "No element found");
		return ret_str;
	}

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
	protocolVersion check - nastavi error v request datech

	proc mame v parametrech to connection???
	*/

	log_message( log_file, "Dostali jsme DISCOVERY message" );

	xr->msg_type = XML_MSG_TYPE_DISCOVERY;

	value = elem->getAttribute( X( "msgid" ) );
	xr->msgid = atoi( XMLString::transcode( value ) );

	value = elem->getAttribute( X( "objectId" ) );
	if ( !XMLString::equals( value, X( "" ) ) )
	{
		xr->object_id = atoi( XMLString::transcode( value ) );
	}


	return xr;
}


/*
GET MESSAGE
*/
struct request_data* XmlModule::process_get_message( DOMElement *elem, const char *password )
{
	struct request_data *xr = new request_data;
	const XMLCh* value;

	log_message( log_file, "Dostali jsme GET message" );

	xr->msg_type = XML_MSG_TYPE_GET;

	value = elem->getAttribute( X( "msgid" ) );
	xr->msgid = atoi( XMLString::transcode( value ) );

	value = elem->getAttribute( X( "objectId" ) );
	if ( !XMLString::equals( value, X( "" ) ) )
	{
		xr->object_id = atoi( XMLString::transcode( value ) );
	}
	else xr->object_id = 0;

	//Parsovani <xpath> elementu
	DOMNodeList *children = elem->getChildNodes();

	for( XMLSize_t i = 0; i < children->getLength(); i++ )
	{
		DOMNode *node = children->item(i);
		
		if ( node->getNodeType() && node->getNodeType() == DOMNode::ELEMENT_NODE)
		{
			DOMElement *el = dynamic_cast<DOMElement *>(node);

			struct value_pair *vp = new value_pair;

			//paklize je to <xpath>
			if ( XMLString::equals( el->getTagName(), X( "xpath" ) ) )
			{
				value = el->getTextContent();
				//Nalezeni OID hledaneho objektu
				int pos = snmpmod->get_device_position( xr->object_id );
				vp->oid = find_element( value, get_device_document( pos ) );

				log_message( log_file, vp->oid.c_str() );

				/*
				TODO 
				find oid, dat do vp a pokracovat dal
				*/

				/*if ( strcmp( vp->oid.c_str(), "" ) == 0 )
				{
					xr->error = 1;
					xr->error_str = "Such name cannot be found in managed data tree.";
					return xr;
				}*/

				xr->request_list.push_back( vp );
			}
			else
			{
				xr->error = 1;
				xr->error_str = "Unknown element in GET message";
				return xr;
			}
		}
	}

	//TODO zavolat snmpmod funkci na ziskani danych dat!!



	return xr;
}

