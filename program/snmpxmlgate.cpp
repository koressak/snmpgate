#include "header/snmpxmlgate.h"

/*
	Konstruktor
*/
SnmpXmlGate::SnmpXmlGate( char *conf_file )
{
	config_file = conf_file;
	log_file = (char *)LOG;
	gate_set = false;

	devices_root = new list<DOMElement*>;


	try {
		XMLPlatformUtils::Initialize();
		XPathEvaluator::initialize();
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

	/*
	Inicializace snmp a xml modulu
	*/
	try {
		snmpmod = new SnmpModule();
		xmlmod = new XmlModule();
		xm = xmlmod;
	}
	catch ( char * e )
	{
		log_message( log_file, e );
		throw -1;
	}
}

/*
	Destruktor
*/
SnmpXmlGate::~SnmpXmlGate()
{
	try {
		XMLString::release( &tagDevice );

		doc->release();
	}
	catch (...)
	{
		log_message( log_file, (char*)"unknown error while deleting structures" );
	}


	//uzavreni Xercesu
	try {
		XPathEvaluator::terminate();
		XMLPlatformUtils::Terminate();
	}
	catch( const XMLException& ex )
	{
		log_message( log_file , XMLString::transcode( ex.getMessage() ) );
	}

	//zavreme snmp modul
	delete(snmpmod);
	delete(xmlmod);

}



/*
	Hlavni vykonna funkce
*/
void SnmpXmlGate::run()
{

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

	//nastaveni trid a vytvoreni transformatoru
	snmpmod->setParameters( log_file, mib_path, devices_root );
	snmpmod->set_elements(  xsd_path );

	//check na funkcnost zarizeni
	log_message( log_file, (char *)"GATE: Checking devices");
	if  ( snmpmod->checkDevices() != 0 )
	{
		exit(1);
	}
	log_message( log_file, (char *)"GATE: Checking done");

	if ( snmpmod->emptyDevices() )
	{
		log_message( log_file, (char *)"GATE: No devices left to monitor. Exitting.");
		exit(1);
	}

	/*
	Transformacni faze
	*/
	log_message( log_file, (char *)"GATE: Starting transformation");
	if ( snmpmod->start_transform() != 0 )
	{
		log_message( log_file, (char *)"error during transformation. Exitting." );
		exit(1);
	}
	log_message( log_file, (char *)"GATE: End of transformation");

	//ziskame odkaz na seznam zarizeni (pro dalsi pouziti)
	devices_list = snmpmod->get_all_devices();

	
	/*
	Nejprve zkontrolujeme gate device, jestli je mezi monitorovanymi
	zarizenimi a pak postupujeme k nastartovani vsech threadu
	*/
	SNMP_device *gate = snmpmod->get_gate_device();

	if ( gate == NULL )
	{
		log_message( log_file, (char *)"Gate is not in th edevice list. Some error with translation.");
		exit(1);
	}

	//spustime handlovaci thready
	if ( snmpmod->initialize_threads() != 0 )
	{
		log_message( log_file, (char *)"Error during snmp thread handlers initialization. Exitting." );
		exit(1);
	}

	//nastavime parametry xmlmodulu
	doc = snmpmod->get_doc();
	root = snmpmod->get_root();
	xmlmod->set_parameters( root, devices_root, log_file, gate->xsd_path, snmpmod );
	snmpmod->set_xmlmod( xmlmod );

	if ( xmlmod->initialize_threads() != 0 )
	{
		log_message( log_file, (char *)"Error during XmlModule thread startup. Terminating.");
		exit(1);
	}

	/*
	Pustime http server
	*/
	

	if ( gate->key != NULL && gate->certificate != NULL )
	{
		char * key_pem;
		char *cert_pem;

		key_pem = load_file( gate->key );
		cert_pem = load_file( gate->certificate);

		if ( key_pem == NULL || cert_pem == NULL )
		{
			log_message( log_file, "Cannot load certificate and key file. Terminating" );
			exit(1);
		}


		http_server = MHD_start_daemon( MHD_USE_THREAD_PER_CONNECTION | MHD_USE_SSL, gate->xml_listen_port,
						NULL, NULL, &process_request, NULL,
						MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL, 
						MHD_OPTION_HTTPS_MEM_KEY, key_pem,
						MHD_OPTION_HTTPS_MEM_CERT, cert_pem,
						MHD_OPTION_END);

	}
	else
	{
	
		http_server = MHD_start_daemon( MHD_USE_THREAD_PER_CONNECTION, gate->xml_listen_port,
						NULL, NULL, &process_request, NULL,
						MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL, MHD_OPTION_END);
	}

	if ( http_server == NULL )
	{
		log_message( log_file, "Cannot start HTTP server. Program terminating." );
		exit(1);
	}


	Mib2Xsd::output_xml2file( "zk.xml", doc );

	/*
	Cekame na ukonceni threadu - ciste jenom pro to, abychom neskoncili program.
	*/
	pthread_join( *(xmlmod->get_distr_thread_id()), NULL );

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


					//Jestli je to brana, nastavime zakladni promenne
					if ( dev->id == 0 )
					{
						if ( !gate_set )
							gate_set = true;
						else
						{
							throw (char *)"Gate element already set. You have an error in config file";
						}

						/*
						porty - kdyz nic, tak defaultni
						*/

						//defaultne je log file nastaven
						if ( dev->log_file )
							log_file = dev->log_file;
						
						if ( dev->mib_path )
							mib_path = dev->mib_path;
						else
							mib_path = (char *)DEFAULT_MIB_PATH;

						if ( dev->xsd_path )
							xsd_path = dev->xsd_path;
						else
							xsd_path = (char *)DEFAULT_XSD_PATH;

						if ( dev->snmp_listen_port == 0 )
							dev->snmp_listen_port = SNMP_LISTEN_PORT;

						if ( dev->xml_listen_port == 0 )
							dev->xml_listen_port = XML_LISTEN_PORT;

						if ( dev->xml_trans_port == 0 )
							dev->xml_trans_port = XML_SEND_PORT;

					}

					string log_msg = "Monitored device:";
					log_msg += dev->name;
					log_msg += "\n";
					log_msg += "description: ";
					log_msg += dev->description;
					log_msg += "\n";

					log_message( log_file, log_msg.c_str() );

					//Vlozeni do snmp modulu - do seznamu zarizeni
					if ( snmpmod->addDevice( dev ) != 0 )
					{
						char dev_id[10];
						log_msg = "Error adding device id: ";
						sprintf( dev_id, "%d", dev->id );
						log_msg += dev_id;
						log_msg += "\n";
						log_message( log_file, log_msg.c_str() );

						throw (char *)"Device configuration error. Check config file.";
					}

				}
				else
					throw (char *)"Unknown element in config file.";
			}
		}

		delete( xmlDoc );

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

						trap_manager *manager = new trap_manager;

						//vybirame dve deti - address a port
						DOMNodeList* man_list = trapElem->getChildNodes();

						for ( XMLSize_t man = 0; man < man_list->getLength(); man++ )
						{
							DOMNode* tmp  = man_list->item(man);
							if ( tmp->getNodeType() && tmp->getNodeType() == DOMNode::ELEMENT_NODE )
							{
								DOMElement *tmp2 = dynamic_cast<DOMElement *>( tmp );
								if ( XMLString::equals( tmp2->getTagName(), XMLString::transcode( "address" ) ) )
								{
									manager->address = XMLString::transcode( tmp2->getTextContent() );
								}
								else if ( XMLString::equals( tmp2->getTagName(), XMLString::transcode( "port" ) ) )
								{
									manager->port = XMLString::transcode( tmp2->getTextContent() );
								}
								/*else
								{
									log_message( log_file, XMLString::transcode(tmp2->getTextContent()) );
									throw (char *)"Illegal element in trap definition";
								}*/

							}
						}

						info->traps.push_back( manager );
					}

				}
			}
			else if ( XMLString::equals( currElem->getTagName(), XMLString::transcode( "access" ) ) )
			{
				//Vyber vsech mib souboru
				DOMNodeList *ach = currElem->getChildNodes();
				XMLSize_t count = ach->getLength();

				for ( XMLSize_t m = 0; m < count; m++ )
				{
					DOMNode* achNode  = ach->item(m);
					if ( achNode->getNodeType() && achNode->getNodeType() == DOMNode::ELEMENT_NODE )
					{
						DOMElement *achElem = dynamic_cast<DOMElement *>( achNode );

						if ( XMLString::equals( achElem->getTagName(), XMLString::transcode( "read" ) ) )
						{
							info->snmp_read = XMLString::transcode( achElem->getTextContent() );
						}
						else if ( XMLString::equals( achElem->getTagName(), XMLString::transcode( "write" ) ) )
						{
							info->snmp_write = XMLString::transcode( achElem->getTextContent() );
						}
						/*else
						{
							//Error
							throw (char *)"Unknown access element in configuration file";
						}*/

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
						/*else if ( XMLString::equals( elem->getTagName(), XMLString::transcode( "transmitPort" )) )
						{
							info->snmp_trans_port = atoi( XMLString::transcode( elem->getTextContent() ) );
							//info->snmp_trans_port = 0;
						}*/
						/*else
						{
							throw (char *)"Illegal element in SNMP configuration of the gate";
						}*/

					}

				}
			}
			else if ( XMLString::equals( currElem->getTagName(), XMLString::transcode( "security" ) )  && info->id == 0 )
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

						if ( XMLString::equals( elem->getTagName(), XMLString::transcode( "key" )) )
						{
							info->key = XMLString::transcode( elem->getTextContent() );
						}
						else if ( XMLString::equals( elem->getTagName(), XMLString::transcode( "certificate" )) )
						{
							info->certificate = XMLString::transcode( elem->getTextContent() );
						}
						/*else
						{
							throw (char *)"Illegal element in SNMP configuration of the gate";
						}*/

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
						else if ( XMLString::equals( elem->getTagName(), XMLString::transcode( "protocolVersion" )) )
						{
							info->xml_protocol_version = XMLString::transcode( elem->getTextContent() ) ;
							//info->xml_trans_port = 0;
						}
						else if ( XMLString::equals( elem->getTagName(), XMLString::transcode( "access" ) ) )
						{
							//Vyber vsech mib souboru
							DOMNodeList *ach = elem->getChildNodes();
							XMLSize_t count = ach->getLength();

							for ( XMLSize_t m = 0; m < count; m++ )
							{
								DOMNode* achNode  = ach->item(m);
								if ( achNode->getNodeType() && achNode->getNodeType() == DOMNode::ELEMENT_NODE )
								{
									DOMElement *achElem = dynamic_cast<DOMElement *>( achNode );

									if ( XMLString::equals( achElem->getTagName(), XMLString::transcode( "read" ) ) )
									{
										info->xml_read = XMLString::transcode( achElem->getTextContent() );
									}
									else if ( XMLString::equals( achElem->getTagName(), XMLString::transcode( "write" ) ) )
									{
										info->xml_write = XMLString::transcode( achElem->getTextContent() );
									}
									/*else
									{
										//Error
										throw (char *)"Unknown access element in configuration file";
									}*/

								}

							}
						} //end of last else if
						/*else
						{
							throw (char *)"Illegal element in XML configuration of the gate";
						}*/

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

/*
Zastavi http demona a ukonci beh ostatnich threadu (skonci praci)
*/
void SnmpXmlGate::stop()
{
	MHD_stop_daemon( http_server );

	//TODO: zastavit ostatni thready
}


/*
Nacte obsah souboru do bufferu
Pro nacitani certifikatu!
*/
char *
 SnmpXmlGate::load_file (const char *filename)
{
	FILE *fp;
	char *buffer;
	long size;
	unsigned int s;

	fp = fopen (filename, "rb");
	if (fp)
	{
		if ((0 != fseek (fp, 0, SEEK_END)) || (-1 == (size = ftell (fp))))
			size = 0;

		fclose (fp);

	}

	if (size == 0)
		return NULL;

	fp = fopen (filename, "rb");
	if (!fp)
		return NULL;

	buffer = new char[size];
	if (!buffer)
	{
		fclose (fp);
		return NULL;
	}

	s = fread (buffer, 1, size, fp);

	if ( (unsigned long) size != s)
	{
		free (buffer);
		buffer = NULL;
	}

	fclose (fp);
	return buffer;
}




