#ifndef _SNMPXMLGATE_H_
#define _SNMPXMLGATE_H_

/*
	Hlavni trida, ktera zajistuje beh serveru.
	Spousti cely transformacni proces, inicializuje struktury,
	implementuje http server a ceka/vyrizuje pozadavky.
*/

#include "snmpmod.h"

class ConfigErrorHandler : public ErrorHandler
{
	private:
		char *message;
	public:
		void warning( const SAXParseException &e )
		{
			message = XMLString::transcode( e.getMessage() );
			log_message( (char *) LOG, message);
			XMLString::release(&message);
		}
		void error( const SAXParseException &e )
		{
			log_message( (char*) LOG, XMLString::transcode( e.getMessage() ) );
		}
		void fatalError( const SAXParseException &e )
		{
			log_message( (char*) LOG, XMLString::transcode( e.getMessage() ) );
		}

		void resetErrors()
		{
			log_message( (char *)LOG, (char*)"reseting error");
		}
};


class SnmpXmlGate
{
	private:
		/*
		Konfigurace systemu
		*/
		char *config_file;
		char *log_file;

		char *mib_path;
		char *xsd_path;

		/*
		XML
		*/
		XercesDOMParser *conf_parser;
		XMLCh* tagDevice;

		/*
		SNMP
		*/
		SnmpModule *snmpmod;


	public:
		SnmpXmlGate(char *);
		~SnmpXmlGate();

		void run(void);
		void initialize_config(void);
		void getDeviceInfo( DOMElement *, SNMP_device * );

};


#endif
