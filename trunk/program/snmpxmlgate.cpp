#include "header/snmpxmlgate.h"

/*
	Konstruktor
*/
SnmpXmlGate::SnmpXmlGate( char *conf_file )
{
	/*
	TODO:
	inicializace xml
	inicializace snmp
	*/
	config_file = conf_file;
	log_file = (char *)LOG;


	try {
		XMLPlatformUtils::Initialize();
	}
	catch ( const XMLException& ex )
	{
		//Nepodarilo se inicializovat Xerces. Finishing
		char *message = XMLString::transcode( ex.getMessage() );
		log_message( log_file , message );
		XMLString::release( &message );
		throw -1;
	}

	tagDevice = XMLString::transcode( "device" );
}

/*
	Destruktor
*/
SnmpXmlGate::~SnmpXmlGate()
{
	try {
		XMLString::release( &tagDevice );
	}
	catch (...)
	{
		log_message( log_file, (char*)"unknown error while deleting structures" );
	}


	//uzavreni Xercesu
	try {
		XMLPlatformUtils::Terminate();
	}
	catch( const XMLException& ex )
	{
		log_message( log_file , XMLString::transcode( ex.getMessage() ) );
	}
}



/*
	Hlavni vykonna funkce
*/
void SnmpXmlGate::run()
{
	/*
	TODO
	nacteni configu a parsovani optionu
	transformace
	check na devices
	inicializace http serveru, poslech
	*/

	try {
		initialize_config();
	}
	catch ( int e )
	{
		log_message( log_file, (char *)"Device initialization failed. Exitting." );
		exit(1);
	}
	catch ( ... )
	{
		log_message( log_file, (char *)"Unknown error while device initialization." );
		exit(1);

	}


	while(1) sleep(1);
}



/*
	Precteni konfiguracniho souboru
*/
void SnmpXmlGate::initialize_config()
{
	conf_parser = new XercesDOMParser;
	ConfigErrorHandler *err = new ConfigErrorHandler;

	conf_parser->setValidationScheme( XercesDOMParser::Val_Never );
	conf_parser->setDoNamespaces( false );
	conf_parser->setDoSchema( false );
	conf_parser->setLoadExternalDTD( false );
	conf_parser->setErrorHandler( err );

	try {

		conf_parser->parse( config_file );

		DOMDocument *xmlDoc = conf_parser->getDocument();
		DOMElement *rootElem = xmlDoc->getDocumentElement();

		if ( !rootElem )
			throw (char *)"Empty config file.";

		DOMNodeList *children = rootElem->getChildNodes();
		XMLSize_t	nodeCount = children->getLength();
		SNMP_device *dev;

		//pro vsechny elementy v hlavnim elementu devices
		for ( XMLSize_t x = 0; x < nodeCount; x++ )
		{

			DOMNode* currNode = children->item(x);
			if ( currNode->getNodeType() && currNode->getNodeType() == DOMNode::ELEMENT_NODE )
			{
				DOMElement *currElem = dynamic_cast<DOMElement *>( currNode );
				
				//paklize je to element device
				if ( XMLString::equals( currElem->getTagName(), tagDevice ) )
				{

					dev = new SNMP_device;
					
					getDeviceInfo( currElem, dev );

					//TODO paklize je to Gate, tak bychom meli nastavit interni promenne!

					/*
					TODO : TEMP Zalogujeme informace o spravovanem zarizeni
					*/
					string log_msg = "Monitored device:";
					log_msg += dev->name;
					log_msg += "\n";
					log_msg += "description: ";
					log_msg += dev->description;
					log_msg += "\n";

					log_message( log_file, log_msg.c_str() );
				}
				else
					throw (char *)"Unknown element in config file.";
			}
		}

	}
	catch ( char *e )
	{
		log_message( log_file, e );
		throw -1;
	}
	catch ( const XMLException *e )
	{
		char *message = XMLString::transcode( e->getMessage() );
		log_message( log_file, message );
		XMLString::release(&message);

		throw -1;
	}
	

}


/*
	getDeviceInfo - zjisti informace z XML elementu
	a vrati strukturu SNMP_device
*/
void SnmpXmlGate::getDeviceInfo( DOMElement *device, SNMP_device *info )
{
	const XMLCh* value;
	char id[20];

	//ziskame potomky elementu device
	DOMNodeList *children = device->getChildNodes();
	XMLSize_t nodeCount = children->getLength();

	value = device->getAttribute( XMLString::transcode("id") );
	if ( XMLString::equals(value, XMLString::transcode("") ) )
	{
		throw (char *)"Device with no id found in config file. Aborting.";
	}

	info->id = atoi( XMLString::transcode( value ) );

	
	//projit vsechny podnody uzlu device
	for ( XMLSize_t x = 0; x < nodeCount; x++ )
	{
		DOMNode* currNode = children->item(x);
		if ( currNode->getNodeType() && currNode->getNodeType() == DOMNode::ELEMENT_NODE )
		{
			DOMElement *currElem = dynamic_cast<DOMElement *>( currNode );
			
			//nyni nasleduje vyber jednotlivych podelementu
			if ( XMLString::equals( currElem->getTagName(), XMLString::transcode( "name" ) ) )
			{
				info->name = XMLString::transcode( currElem->getTextContent() );
			}
			else if ( XMLString::equals( currElem->getTagName(), XMLString::transcode( "description" ) ) )
			{
				info->description = XMLString::transcode( currElem->getTextContent() );
			}
			else if ( XMLString::equals( currElem->getTagName(), XMLString::transcode( "snmpAddr" ) ) )
			{
				info->snmp_addr = XMLString::transcode( currElem->getTextContent() );
			}
			else if ( XMLString::equals( currElem->getTagName(), XMLString::transcode( "protocolVersion" ) ) )
			{
				info->protocol_version = XMLString::transcode( currElem->getTextContent() );
			}
			else if ( XMLString::equals( currElem->getTagName(), XMLString::transcode( "mibs" ) ) )
			{
				//Vyber vsech mib souboru
				DOMNodeList *mibs = currElem->getElementsByTagName( XMLString::transcode( "mib" ) );
				XMLSize_t mibCount = mibs->getLength();

				for ( XMLSize_t m = 0; m < mibCount; m++ )
				{
					DOMNode* mibNode  = mibs->item(m);
					if ( mibNode->getNodeType() && mibNode->getNodeType() == DOMNode::ELEMENT_NODE )
					{
						DOMElement *mibElem = dynamic_cast<DOMElement *>( mibNode );

						info->mibs.push_back( XMLString::transcode( mibElem->getTextContent() ) );
					}

				}
			}
			else if ( XMLString::equals( currElem->getTagName(), XMLString::transcode( "traps" ) ) )
			{
				//Vyber vsech trap manageru
				DOMNodeList *traps = currElem->getElementsByTagName( XMLString::transcode( "manager" ) );
				XMLSize_t trapCount = traps->getLength();

				for ( XMLSize_t m = 0; m < trapCount; m++ )
				{
					DOMNode* trapNode  = traps->item(m);
					if ( trapNode->getNodeType() && trapNode->getNodeType() == DOMNode::ELEMENT_NODE )
					{
						DOMElement *trapElem = dynamic_cast<DOMElement *>( trapNode );

						info->traps.push_back( XMLString::transcode( trapElem->getTextContent() ) );
					}

				}
			}
			else if ( XMLString::equals( currElem->getTagName(), XMLString::transcode( "logFile" ) ) && info->id == 0 )
			{
				info->log_file = XMLString::transcode( currElem->getTextContent() );
			}
			else if ( XMLString::equals( currElem->getTagName(), XMLString::transcode( "snmp" ) )  && info->id == 0 )
			{
				//Vyber vsech elementu snmp
				DOMNodeList *nol = currElem->getChildNodes();
				XMLSize_t nolCount = nol->getLength();

				for ( XMLSize_t m = 0; m < nolCount; m++ )
				{
					DOMNode* node  = nol->item(m);
					if ( node->getNodeType() && node->getNodeType() == DOMNode::ELEMENT_NODE )
					{
						DOMElement *elem = dynamic_cast<DOMElement *>( node );

						if ( XMLString::equals( elem->getTagName(), XMLString::transcode( "mibPath" )) )
						{
							info->mib_path = XMLString::transcode( elem->getTextContent() );
						}
						else if ( XMLString::equals( elem->getTagName(), XMLString::transcode( "listenPort" )) )
						{
							info->snmp_listen_port = atoi(XMLString::transcode( elem->getTextContent() ) );
							//info->snmp_listen_port = 0;
						}
						else if ( XMLString::equals( elem->getTagName(), XMLString::transcode( "transmitPort" )) )
						{
							info->snmp_trans_port = atoi( XMLString::transcode( elem->getTextContent() ) );
							//info->snmp_trans_port = 0;
						}

					}

				}
			}
			else if ( XMLString::equals( currElem->getTagName(), XMLString::transcode( "xml" ) )  && info->id == 0 )
			{
				//Vyber vsech elementu xml
				DOMNodeList *nol = currElem->getChildNodes();
				XMLSize_t nolCount = nol->getLength();

				for ( XMLSize_t m = 0; m < nolCount; m++ )
				{
					DOMNode* node  = nol->item(m);
					if ( node->getNodeType() && node->getNodeType() == DOMNode::ELEMENT_NODE )
					{
						DOMElement *elem = dynamic_cast<DOMElement *>( node );

						if ( XMLString::equals( elem->getTagName(), XMLString::transcode( "xsdPath" )) )
						{
							info->xsd_path = XMLString::transcode( elem->getTextContent() );
						}
						else if ( XMLString::equals( elem->getTagName(), XMLString::transcode( "listenPort" )) )
						{
							info->xml_listen_port = atoi(XMLString::transcode( elem->getTextContent() ) );
							//info->xml_listen_port = 0;
						}
						else if ( XMLString::equals( elem->getTagName(), XMLString::transcode( "transmitPort" )) )
						{
							info->xml_trans_port = atoi( XMLString::transcode( elem->getTextContent() ) );
							//info->xml_trans_port = 0;
						}

					}

				}
			}
			else
			{
				sprintf( id, "%d", info->id );
				string message = "Undefined or illegal element ";
				message += XMLString::transcode( currElem->getTagName() );
				message += " for the device id: ";
				message += id;
				message += " detected.";

				throw message.c_str();
			}
		}
	}


	//return info;	

}


