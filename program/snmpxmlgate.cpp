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
}

/*
	Destruktor
*/
SnmpXmlGate::~SnmpXmlGate()
{
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
	initialize_config();

	while(1) sleep(1);
}



/*
	Precteni konfiguracniho souboru
*/
void SnmpXmlGate::initialize_config()
{
	conf_parser = new XercesDOMParser;

	conf_parser->setValidationScheme( XercesDOMParser::Val_Never );
	conf_parser->setDoNamespaces( false );
	conf_parser->setDoSchema( false );
	conf_parser->setLoadExternalDTD( false );
	

}
