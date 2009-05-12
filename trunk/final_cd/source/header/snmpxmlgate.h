#ifndef _SNMPXMLGATE_H_
#define _SNMPXMLGATE_H_

/*
	Hlavni trida, ktera zajistuje beh serveru.
	Spousti cely transformacni proces, inicializuje struktury,
	implementuje http server a ceka/vyrizuje pozadavky.
*/

#include "snmpmod.h"
#include "xmlmod.h"

extern XmlModule *xm;
extern pthread_mutex_t lg_msg;


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

		bool gate_set;

		/*
		XML
		*/
		XercesDOMParser *conf_parser;
		XMLCh* tagDevice;

		DOMDocument *doc;
		DOMElement *root;

		//seznam dokumentu jednotlivych devices
		list<DOMElement *> *devices_root;
		list<SNMP_device*> *devices_list;

		/*
		XML module
		*/
		XmlModule *xmlmod;

		/*
		SNMP
		*/
		SnmpModule *snmpmod;

		/*
		HTTP Daemon
		*/
		MHD_Daemon *http_server;


	public:
		SnmpXmlGate(char *);
		~SnmpXmlGate();

		void run(void);
		void initialize_config(void);
		void getDeviceInfo( DOMElement *, SNMP_device * );

		char *load_file( const char * );

		void stop(void);

		int process_message( void *cls, MHD_Connection *connection, const char *url, const char *method, const char *version, const char *upload_data, size_t *dsize, void **con_cls)
		{
			return 1;
		}

};


#endif
