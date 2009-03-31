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
	//TODO delete all xalan documents
	list< struct xalan_docs_list *>::iterator it;

	for( it = xalan_docs.begin(); it != xalan_docs.end(); it++ )
		delete ((*it));
	
	delete( main_xa_doc );
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


	/*
	Build Xalan Documents
	*/
	list<DOMElement *>::iterator it; 

	DOMDocument *doc;
	XercesDOMSupport		theDOMSupport;
	XercesParserLiaison	*theLiaison;
	XalanDocument *theDocument;
	

	for ( it= devices_root->begin(); it != devices_root->end(); it++ )
	{
		doc = (*it)->getOwnerDocument();
		struct xalan_docs_list *l = new xalan_docs_list;

		theLiaison = new XercesParserLiaison( theDOMSupport );
		theDocument =
			theLiaison->createDocument(doc, true, true, true);

		l->doc = theDocument;
		l->liaison = theLiaison;

		xalan_docs.push_back( l );
	}

	/*
	Jeste pro hlavni dokument
	*/
	doc = main_root->getOwnerDocument();
	main_xa_doc = new xalan_docs_list;

	theLiaison = new XercesParserLiaison( theDOMSupport );
	theDocument = theLiaison->createDocument( doc, true, true, true );

	main_xa_doc->doc = theDocument;
	main_xa_doc->liaison = theLiaison;

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
			send_error_response( connection, XML_ERR_NO_HTTP_POST, NULL );
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
				send_error_response( connection, XML_ERR_UNKNOWN, NULL );
				delete( mem_buf );
				return MHD_NO;
			}

			//Zjistime, jestli je message root elementem
			DOMElement *msg_root = message->getDocumentElement();

			if ( !msg_root )
			{
				log_message( log_file, "No root element in message" );
				//odesilame error
				send_error_response( connection, XML_ERR_UNKNOWN, NULL );
				message->release();
				delete( mem_buf );
				return MHD_NO;
			}

			//paklize neni root == message, tak spatnej format zpravy
			if ( !XMLString::equals( msg_root->getTagName(), X("message") ) )
			{
				log_message( log_file, "No message element in message" );
				//spatny format zpravy
				send_error_response( connection, XML_ERR_WRONG_MSG, NULL );
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
					}
					else if ( XMLString::equals( elem->getTagName(), X( "get" ) ) )
					{
						xr = process_get_set_message( elem, password, XML_MSG_TYPE_GET );
					}
					else if ( XMLString::equals( elem->getTagName(), X( "set" ) ) )
					{
						xr = process_get_set_message( elem, password, XML_MSG_TYPE_SET );
					}
					else if ( XMLString::equals( elem->getTagName(), X( "subscribe" ) ) )
					{
						xr = process_subscribe_message( elem, password );
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

					/*
					Jestli zjistime nejaky error pri parsovani ci provadeni prikazu
					je nutne ukoncit prubeh a poslat zpet zpravu o chybe.
					Pouze INTERNAL error. Klasicke SNMP errory handlujeme jako normalni odpoved
					*/
					if ( xr->error == XML_MSG_ERR_INTERNAL )
					{
						int ret = send_error_response( connection, 0, xr->error_str.c_str() );
						XMLString::release ( &password );
						XMLString::release ( &msg_context );
						delete( response_string );
						delete( mem_buf );
						delete( xr );

						return ret;
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
		/*
		Odpoved - RESPONSE
		*/
		*out = "<response msgid=\"";
		sprintf( tmpid, "%d", data->msgid );
		*out += tmpid;
		*out += "\">\n";

		list<struct value_pair *>::iterator rit;

		for ( rit = data->request_list.begin(); rit != data->request_list.end(); rit++ )
		{
			*out += "<value name=\"";
			*out += (*rit)->oid;
			*out += "\">\n";
			*out += (*rit)->value;
			*out += "</value>\n";
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
		if ( data->snmp_err )
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

		list<struct value_pair *>::iterator rit;

		for ( rit = data->request_list.begin(); rit != data->request_list.end(); rit++ )
		{
			*out += "<value name=\"";
			*out += (*rit)->oid;
			*out += "\">\n";
			*out += (*rit)->value;
			*out += "</value>\n";
		}

		*out += "</distribution>\n";

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
Vrati odkaz na XalanDokument z xalan_docs
*/
XalanDocument* XmlModule::get_device_xalan_document( int position )
{
	list<struct xalan_docs_list *>::iterator it = xalan_docs.begin();

	for ( int a=0; a < position; a++ )
		it++;
	
	return (*it)->doc;
}

/*
Nalezne element dle xpath vyrazu a vrati na nej odkaz
*/
const DOMElement* XmlModule::find_element( const XMLCh* name, XalanDocument* theDocument, struct request_data* req_data, bool deep = true )
{
	const DOMElement *ret_elm = NULL;

	//Ziskame pointer na dokument daneho zarizeni

	//Pripravime struktury k vyhledani
	XercesDOMSupport		theDOMSupport;
	
	if ( theDocument == NULL )
	{
		log_message( log_file, "cannot create document" );
		return ret_elm;
	}	


	XalanDocumentPrefixResolver		thePrefixResolver(theDocument);
	XPathEvaluator	theEvaluator;
	XalanNode* theContextNode;

	try {
		theContextNode =
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
				return ret_elm;
			}

			//Z XalanElementu musime udelat DOMElement a ten vratit
			XalanElement *elm = dynamic_cast<XalanElement *>(n);
			XercesElementWrapper *wrap = dynamic_cast<XercesElementWrapper *>(elm);
			ret_elm = wrap->getXercesNode();

			return ret_elm;
		}
		else
		{
			//TODO dodelat odsekavani cisilek v jmenu elementu
			//Nenasli jsme, jdeme separovat zpet
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
				pos1 = xp.rfind( "name='" );
				if ( pos1 != string::npos )
				{
					pos2 = xp.find( "']", pos1+1 );

					name = xp.substr( pos1+6, pos2 - (pos1+6) );

					pos3 = name.find( "." );
					if ( pos3 != string::npos )
					{
						req_data->snmp_indexed_name = name;

						name = name.substr( 0, pos3 );

						tmp_xp = xp.replace( pos1+6, pos2 - (pos1+6), name, 0, name.size() );

						tmp_find = find_element( X( tmp_xp.c_str() ), theDocument, req_data, false );

						if ( tmp_find != NULL )
						{
							req_data->xpath_end = "";
							return tmp_find;
						}
					}

				}

				pos1 = xp.rfind("//");
				//xpath string jde rozdelit na elementy. 
				if ( pos1 != string::npos )
				{
					to_search = xp;
					leftover = to_search.substr( pos1, to_search.size() );
					to_search = to_search.substr( 0, pos1 );

					if ( leftover.compare( "//next" ) == 0 )
					{
						req_data->snmp_getnext = 1;
					}
					else
						req_data->xpath_end = leftover + req_data->xpath_end;

					return find_element( X( to_search.c_str() ), theDocument, req_data, true );


				}
				//dosli jsme na zacatek stringu a uz nemame nic, kde bychom hledali
				else
					return ret_elm;
			}
			else 
				return ret_elm;

		}
	}
	catch ( ... )
	{
		log_message( log_file, "Exception while evaluating XPath expression" );
		return ret_elm;
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
GET / SET MESSAGE
*/
struct request_data* XmlModule::process_get_set_message( DOMElement *elem, const char *password, int msg_type )
{
	struct request_data *xr = new request_data;
	const XMLCh* value;
	struct value_pair *vp;
	DOMNodeList *children;
	DOMElement *el;
	DOMNode *node;

	const DOMElement *found_el;
	const DOMElement *type_element;

	string tmp_str_xpath = "";
	char *tmp_buf;
	bool create_vp = true;



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
	else xr->object_id = 0;

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


				int pos = snmpmod->get_device_position( xr->object_id );
				found_el = find_element( value, get_device_xalan_document( pos ), xr, true );

				if ( found_el == NULL )
				{
					log_message( log_file, "NULL found element" );
					xr->error = XML_MSG_ERR_INTERNAL;
					xr->error_str = "Such element cannot be found in managed data tree.";
					
					delete( vp );
					return xr;
				}

				/*
				Paklize jeste mame neco k vyhledani
				Je to vetsinou u tabulek, kdy se musime podivat do typu elementu, jestli
				to neni v nem
				*/
				if ( xr->xpath_end.compare( "" ) != 0 )
				{
					//Nutno najit element typu a pak vyhledat znova
					tmp_buf = XMLString::transcode( found_el->getAttribute( X("type") ) );

					tmp_str_xpath = "xsd:complexType[@name='";
					tmp_str_xpath += string( tmp_buf );
					tmp_str_xpath += "']";

					type_element = find_element( X( tmp_str_xpath.c_str() ), get_device_xalan_document( pos ), xr, false );

					if ( type_element == NULL )
					{
						log_message( log_file, "NULL found element" );
						xr->error = XML_MSG_ERR_INTERNAL;
						xr->error_str = "Such element cannot be found in managed data tree.";
						
						delete( vp );
						return xr;
					}

					//Nasli jsme typ elementu. Nyni prohledame jeho zbytek, jestli je tam ten element uvnitr

					xr->xpath_end = tmp_str_xpath + xr->xpath_end;

					found_el = find_element( X( xr->xpath_end.c_str() ), get_device_xalan_document( pos ), xr, true );

					if ( found_el == NULL )
					{
						log_message( log_file, "NULL found element" );
						xr->error = XML_MSG_ERR_INTERNAL;
						xr->error_str = "Such element cannot be found in managed data tree.";
						
						delete( vp );
						return xr;
					}

				}

				/*
				Paklize je element korenovym elementem, je to zatim v TODO diskusi.

				Jestli je to koncovym elementem, nastavime jmeno a hodnotu.
				*/

				if ( found_el->hasChildNodes() )
				{
					//TODO Prodiskutovat tuto cast s Macejkem. Na tohle se proste ptat nemuze
					log_message( log_file, "This is not a leaf node. Canceling request" );
					xr->error = XML_MSG_ERR_INTERNAL;
					xr->error_str = "Cannot request this non-simple value node";
					delete( vp );
					return xr;
				}
				else
				{
					if ( xr->snmp_indexed_name.compare( "" ) == 0 )
					{
						// ziskame nazev typu elementu
						value = found_el->getAttribute( X( "name" ) );
						tmp_buf = XMLString::transcode( value );

						vp->oid = string( tmp_buf );
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
				xr->error = XML_MSG_ERR_INTERNAL;
				xr->error_str = "Unknown element in GET/SET message";
				delete ( vp );
				return xr;
			}
		}
	}

	/*
	Zavolame snmp funkci, ktera dana data zpracuje a odesle agentovi
	zaroven prijme odpoved a zpracuje data zpet
	*/
	if ( snmpmod->send_request( xr, password,  msg_type ) != 0 )
	{
		log_message( log_file, "XmlMod: snmp module returned error" );
	}


	return xr;
}


/*
SUBSCRIBE message
*/
struct request_data* XmlModule::process_subscribe_message( DOMElement *elem, const char* password )
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
	DOMElement *subscriptions;
	



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
			xr->msg_type = XML_MSG_TYPE_SUBSCRIBE;
			xr->error = XML_MSG_ERR_INTERNAL;
			xr->error_str = "Such subscription does not exist.";

			return xr;
		}

		//TODO:  delete! 


	}


	/*
	Subscribe ma stejnou strukturu jako get message.
	Nechame tedy ziskat data pomoci GET message, cimz se overi, ze je vse v poradku
	a teprve pote budeme s daty vice manipulovat.
	*/
	xr = process_get_set_message( elem, password, XML_MSG_TYPE_GET );

	//Nyni mame data ziskana.
	if ( xr->error == XML_MSG_ERR_INTERNAL || xr->error == XML_MSG_ERR_SNMP )
	{
		/*
		Error. Nektera data nemusi existovat. 
		Posleme zpet error
		*/
		xr->msg_type = XML_MSG_TYPE_SUBSCRIBE;
		return xr;
	}

	//Data jsou v poradku, nez je vratime zpet, zapiseme tento element do
	//hlavniho stromu

	if ( distr_id > 0 )
	{
		//Alter

		//subscr uz je handle na ten element. Muzeme pracovat s nim
	}
	else
	{
		//Pridavame

		//Nejrpve najdeme posledni distr_id
		log_message( log_file, "a" );
		DOMNodeList *sl = main_root->getElementsByTagName( X( "subscriptions" ) );
		subscriptions = dynamic_cast<DOMElement *> (sl->item(0));

		DOMNode *lch = subscription->getLastChild();
		log_message( log_file, "b" );
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
			XMLString::release( &tmp_buf );

		}





		log_message( log_file, "c" );
		subscription = doc->createElement( X( "subscription" ) );
		attr = doc->createAttribute( X( "distrid" ) );
		sprintf( tmpid, "%d", distr_id );
		attr->setNodeValue( X( tmpid ) );

		log_message( log_file, "d" );

		subscription->setAttributeNode( attr );
		log_message( log_file, "e" );

		attr = doc->createAttribute( X( "frequency" ) );
		value = elem->getAttribute( X( "frequency" ) );

		//paklize tam neni frekvence - dame to na minutu
		if ( XMLString::equals( value, X("") ) == 0 )
		{
			value = X( "60" );
		}
		log_message( log_file, "f" );

		attr->setNodeValue( value );
		subscription->setAttributeNode( attr );


		//TODO: manager

		subscriptions->appendChild( subscription );
		//Vypis vysledku
		Mib2Xsd::output_xml2file( "zkuska.xsd", doc );

		log_message( log_file, "g" );
		
	}

	//TODO probudit thread, aby si to zpracoval

	return xr;
}


