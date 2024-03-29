#ifndef _DEFINITIONS_H_
#define _DEFINITIONS_H_

/*
Standard includes
*/
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <ctype.h>
#include <pthread.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

/*
Timestamp for log
*/
#include <ctime>

/*
STL struktury
*/
#include <list>
#include <vector>


/*
XML Xerces related
*/
#include <xercesc/util/PlatformUtils.hpp>
#include <xercesc/dom/DOM.hpp>
#include <xercesc/dom/DOMDocument.hpp>
#include <xercesc/dom/DOMDocumentType.hpp>
#include <xercesc/dom/DOMElement.hpp>
#include <xercesc/dom/DOMImplementation.hpp>
#include <xercesc/dom/DOMImplementationLS.hpp>
#include <xercesc/dom/DOMNodeIterator.hpp>
#include <xercesc/dom/DOMNodeList.hpp>
#include <xercesc/dom/DOMText.hpp>

#include <xercesc/parsers/XercesDOMParser.hpp>
#include <xercesc/util/XMLUni.hpp>
#include <xercesc/sax/ErrorHandler.hpp>
#include <xercesc/sax/SAXParseException.hpp>
#include <xercesc/framework/LocalFileFormatTarget.hpp>
#include <xercesc/framework/MemBufInputSource.hpp>
#include <xercesc/framework/LocalFileInputSource.hpp>

/*
XML Xalan XPath evaluator
*/
#include <xalanc/PlatformSupport/XSLException.hpp>
#include <xalanc/DOMSupport/XalanDocumentPrefixResolver.hpp>
#include <xalanc/XPath/XObject.hpp>
#include <xalanc/XPath/XPathEvaluator.hpp>
#include <xalanc/XalanSourceTree/XalanSourceTreeDOMSupport.hpp>
#include <xalanc/XalanSourceTree/XalanSourceTreeInit.hpp>
#include <xalanc/XalanSourceTree/XalanSourceTreeParserLiaison.hpp>
#include <xalanc/XalanTransformer/XercesDOMWrapperParsedSource.hpp>
#include <xalanc/XercesParserLiaison/XercesElementWrapper.hpp>


/*
SNMP includy
*/
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

/*
HTTP server
*/
#include <microhttpd.h>

/*
libCURL - pro odesilani distribution a notifications
*/
#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>

/*
Namespaces
*/
using namespace std;
using namespace xercesc;
using namespace xalanc;



/* 
Zakladni hodnoty  logovacich souboru
souboru a pracovniho adresare
*/
//TODO: prehodit na var run snmp
//#define RUN_DIR "/var/run/snmpxmld"
#define RUN_DIR "/tmp"
#define LOCK	"snmpxmld.lock"
#define LOG		"snmpxmld.log"


#define MAIN_XSD "snmpxmld.xsd"
#define DEFAULT_MIB_PATH "/usr/share/snmp/mibs/"
#define DEFAULT_XSD_PATH "/var/run/snmpxmld/"

/*
SNMP specificke defaultni hodnoty
*/
#define SNMP_LISTEN_PORT 3111

#define SNMP_MAX_PACKET_SIZE 64000

/*
XML default hodnoty
*/
#define XML_SEND_PORT 2053
#define XML_LISTEN_PORT 2054

/*
XML error hodnoty
*/
#define XML_ERR_NO_HTTP_POST 		1
#define XML_ERR_UNKNOWN				2
#define XML_ERR_WRONG_MSG			3 //spatny format zpravy
#define XML_ERR_UNKNOWN_MSG			4



/*
XML zpravy
*/
#define XML_MSG_ROOT "<message"
#define XML_MSG_ROOT_END "</message>"


/*
XML typy zprav
*/
#define XML_MSG_TYPE_DISCOVERY 		1
#define XML_MSG_TYPE_GET			2
#define XML_MSG_TYPE_SET			3
#define XML_MSG_TYPE_SUBSCRIBE		4


#define XML_MSG_ERR_INTERNAL		1 //interni chyby
#define XML_MSG_ERR_SNMP			2 //snmp chyby od agenta
#define XML_MSG_ERR_XML				3 //pro chyby v xml message


/*
ACCESS constrol - permissions
*/
#define XML_PERM_READ				1
#define XML_PERM_WRITE				2

/*
SUBSCRIBE default frequency
*/
#define XML_SUBSCRIBE_DEF_FREQUENCY 60

/*
SUBSCRIBE delete after X undelivered messages
*/
#define XML_SUBSCRIBE_MAX_LOST 5

/*
HTTP parametry
*/
#define HTTP_GET 0
#define HTTP_POST 1
#define POSTBUFFERSIZE 4096



/*
Par standardnich funkci, ktere zajisti chovani demona.
Plus logovani zprav do log souboru
*/
extern pthread_mutex_t lg_msg;
inline void log_message(char *filename, const char* message)
{
	pthread_mutex_lock( &lg_msg );
	time_t rawtime;
	struct tm * timeinfo;

	time( &rawtime );
	timeinfo = localtime( &rawtime );

	ofstream outf( filename, ios_base::app | ios_base::out );
	
	if ( !outf.is_open() )
		return;
	
	outf << "----------\n";
	outf << asctime( timeinfo );
	outf << message << endl;
	outf << "----------\n";

	outf.close();
	pthread_mutex_unlock( &lg_msg );
	
}



/*
 signal handler
*/
inline void signal_handler(int sig)
{
	switch( sig )
	{
		case SIGTERM:
			log_message( (char *)LOG, (char *)"killing snmpxmld");
			exit(0);
			break;
	}
}



/*
Zajisti prechod na daemon chovani
*/
inline void daemonize(void)
{
	int i, lfp;
	char str[10];
	int sid;

	//uz je to demon. Pouze 1 instance bezi
	if ( getpid() == 1) 
		return;
	
	i = fork();

	if ( i < 0 ) exit(1); //fork error
	if ( i > 0 ) exit(0); //parent konci
	
	umask(0);

	sid = setsid();
	if ( sid < 0 )	 
		exit(1); //error pri ziskani unikatniho session id
	
	//nastavit pracovni adresar
	if ( chdir( RUN_DIR ) < 0 )
	{
		exit(1);
	}

	//zavrit zdedene filedescriptory
	for ( i = getdtablesize(); i >= 0; i-- )
	{
		close(i);
	}

	lfp = open( LOCK, O_RDWR | O_CREAT, 0640 );
	if ( lfp < 0 )
		exit(1); //neotevreli jsme jej
	
	if ( lockf( lfp, F_TLOCK, 0 ) < 0 )
		exit(0); //uz bezi jina instance demona, koncime
	
	sprintf( str, "%d\n", getpid() );
	write(lfp, str, strlen(str) );

	//budeme chytat signaly
	signal(SIGTERM, signal_handler);

}


/*
Manazer
*/
struct trap_manager {
	char *address;
	char *port;

	trap_manager()
	{
		address = NULL;
		port = NULL;
	}

	~trap_manager()
	{
		if ( address != NULL )
			XMLString::release( &address );
		
		if ( port != NULL )
			XMLString::release( &port );
	}
};


/*
	SNMP device struc
*/
struct SNMP_device {
	int				id;
	char *			snmp_addr;
	char *			protocol_version;
	char *			xml_protocol_version;
	char *			name;
	char *			description;
	list<char *>	mibs;
	list<trap_manager *>	traps;

	//gate specific
	char *			log_file;
	char *			mib_path;
	char *			xsd_path;
	int				snmp_listen_port;
	//int				snmp_trans_port;
	int				xml_listen_port;
	int				xml_trans_port;
	int				active;

	//stejne mib jako jiny device - budou sdilet xml strom
	int				similar_as;

	/*
	Access control
	*/
	char *			snmp_read;
	char *			snmp_write;
	char *			xml_read;
	char *			xml_write;

	/*
	HTTPS key and certificate
	*/
	char *			key;
	char *			certificate;


	SNMP_device()
	{
		similar_as = -1;

		active = 1;
		snmp_addr = NULL;
		protocol_version = NULL;
		name = NULL;
		description = NULL;
		log_file = NULL;
		mib_path = NULL;
		xsd_path = NULL;

		snmp_read = NULL;
		snmp_write = NULL;
		xml_read = NULL;
		xml_write = NULL;
		xml_protocol_version = NULL;
		key = NULL;
		certificate = NULL;

		snmp_listen_port = xml_listen_port = xml_trans_port = 0;
	}

	~SNMP_device()
	{
		if ( snmp_addr != NULL ) 				XMLString::release( &snmp_addr );
		if ( name != NULL ) 					XMLString::release( &name );
		if ( description != NULL ) 				XMLString::release( &description );
		if ( protocol_version != NULL ) 		XMLString::release( &protocol_version );
		if ( log_file != NULL ) 				XMLString::release( &log_file );
		if ( mib_path != NULL )					XMLString::release( &mib_path );
		if ( xsd_path != NULL )					XMLString::release( &xsd_path );

		if ( snmp_read != NULL )				XMLString::release( &xsd_path );
		if ( snmp_write != NULL )				XMLString::release( &xsd_path );
		if ( xml_read != NULL )					XMLString::release( &xsd_path );
		if ( xml_write != NULL )				XMLString::release( &xsd_path );
		if ( xml_protocol_version != NULL )		XMLString::release( &xsd_path );
		if ( key != NULL )						XMLString::release( &key );
		if ( certificate != NULL )				XMLString::release( &certificate );

		
		for ( list<char *>::iterator it = mibs.begin(); it != mibs.end(); it++ )
			XMLString::release( &(*it) );

		list<trap_manager *>::iterator it = traps.begin(); 
		while ( it != traps.end() )
			it = traps.erase( it );
		
	}
	

};

/*
Struktura pouzita http serverem pro predavani informaci
o spojeni a ostatnim
*/
struct connection_info {
	struct MHD_PostProcessor *post_processor;
	string data;
	int conn_type;
	string ip_addr;

	connection_info()
	{
		post_processor = NULL;
		ip_addr = "";
	}
};

/*
Dvojice nazev - hodnota.
*/
struct value_pair
{
	string oid;
	string value;

	value_pair()
	{
		oid = value = "";
	}

	~value_pair()
	{
	}
};

/*
Xml request struktura obsahujici data,
ktera chceme ziskat od agenta
*/
struct request_data {

	pthread_t thread_id;

	//obecne hodnoty
	int msgid;
	string msg_context;
	int msg_type; //for snmp_module
	int msg_type_resp; //for building response string
	int object_id;

	/*
	snmp community
	*/
	string community;

	//GET list
	list<struct value_pair *> request_list;

	//jakykoliv error
	int error;
	string error_str;

	//snmp error odpoved
	int snmp_err;
	string snmp_err_str;

	int snmp_getnext;
	string xpath_end;
	//jako temporary ulozeni jmena do tabulky, ktere u sebe
	//ma mnoho indexu
	string snmp_indexed_name;


	//SUBSCRIBE
	int distr_id;

	/*
	Element found by xpath
	Pouzite pro generovani odpovedi, kdy je vice
	odpovednich hodnot (generujeme xml doc)
	*/
	const DOMElement *found_el;


	request_data()
	{
		msgid = 0;
		msg_type = -1;

		object_id = -1;

		error = 0;
		snmp_err = 0;

		xpath_end = "";
		snmp_getnext = 0;
		snmp_indexed_name = "";

		distr_id = 0;

		found_el = NULL;
		msg_type_resp = 0;
	}

	~request_data()
	{
		if ( !request_list.empty() )
		{
			list<struct value_pair *>::iterator it = request_list.begin();
			while ( it != request_list.end() )
			{
				/*if ( (*it) )
					delete( (*it) );*/

				it = request_list.erase( it );
			}
		}
	}
};


/*
Ulozeni XalanDocumentu s Liaisonem
*/
struct xalan_docs_list
{
	XalanDocument *doc;
	XercesParserLiaison *liaison;

	xalan_docs_list()
	{
		doc = NULL;
		liaison = NULL;
	}

	~xalan_docs_list()
	{
		delete( liaison );
	}
};


/*
Struktura pro distribution handler
*/
struct subscription_element
{
	int object_id;
	string password;
	int distr_id;
	int last_msg_id;
	string manager_ip;

	//nedojite /nepotvrzene zpravy
	int lost_msg_count;

	//timeout
	int frequency;
	int time_remaining;

	//Ukazatel na jeho subscription v XML dokumentu
	DOMElement *subscription;

	//Pri projeti seznamu subscriptions, abychom mohli smazat
	bool in_list;

	//jestli je novy
	bool is_new;

	subscription_element()
	{
		object_id = 0;
		distr_id = 0;
		last_msg_id = 0;
		manager_ip = "";

		lost_msg_count = 0;

		frequency = time_remaining = 0;

		in_list = false;
		is_new = true;
	}
};

/*
Notification element
*/
struct notification_element
{
	SNMP_device *device;
	int last_msg_id;

	list<string> manager_urls;

	notification_element()
	{
		device = NULL;
		last_msg_id = 0;
	}

	
};


#endif

