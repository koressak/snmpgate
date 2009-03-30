#ifndef _XML_MODULE_H
#define _XML_MODULE_H

#include "definitions.h"
#include "snmpmod.h"


class WriterErrHandler : public DOMErrorHandler
{
	private:
		char *log_file;
	public:
		WriterErrHandler( char * lg )
		{
			log_file = lg;
		}

		bool handleError( const DOMError &err )
		{
			log_message( log_file, XMLString::transcode( err.getMessage() ) ); 

			return 0;
		}
};

/*
XML module pro zpracovavani pozadavku od manageru a
posilani odpovedi.
*/

class XmlModule
{
	private:
		/*
		Basic variables
		*/
		char *log_file;
		char *xsd_dir;

		/*
		XML specific
		*/

		XercesDOMParser *parser;
		DOMImplementation *impl;
		DOMElement *main_root;

		list<DOMElement *> *devices_root;

		//xalanDocuments
		list<xalan_docs_list *> xalan_docs;

		/*
		Pointer to SNMP module - to send requests
		*/
		SnmpModule *snmpmod;


	public:
		XmlModule();
		~XmlModule();

		/*
		nastaveni parametru
		*/
		void set_parameters( DOMElement *, list<DOMElement *> *, char *, char * , SnmpModule *);

		/*
		metody na parsovani pozadavku
		*/
		int process_request( void *cls, MHD_Connection *connection, const char *url, const char *method, const char *version, const char *upload_data, size_t *upload_data_size, void **con_cls);

		//uvolneni POST processoru po ukonceni parsovani
		void request_completed( void *, struct MHD_Connection *, void **, enum MHD_RequestTerminationCode );

		int post_iterate( void *coninfo_cls, enum MHD_ValueKind kind, const char *key, 
			const char *filename, const char *content_type, const char *transfer_encoding, 
			const char *data, size_t off, size_t size);

		int send_response( struct MHD_Connection *, const char*,  string *);

		int send_error_response( struct MHD_Connection *, int error, const char *err_msg );

		string * build_response_string( struct request_data* );

		DOMElement* get_device_document( int );
		XalanDocument* get_device_xalan_document( int );
		const DOMElement* find_element( const XMLCh*, XalanDocument*, struct request_data*, bool );

		/*
		Funkce na handlovani jednotlivych typu zprav
		*/
		struct request_data* process_discovery_message( struct MHD_Connection *, DOMElement * );
		struct request_data* process_get_set_message( DOMElement *, const char*, int );


};





/*
Callback funkce pro zavolani parsovani requestu
Plus globalni promenna drzici odkaz na xmlmod strukturu :((
Neprijemny obejiti C omezeni
*/
extern XmlModule *xm;

inline int process_request( void *cls, struct MHD_Connection *connection, const char *url, const char *method,
			const char *version, const char *upload_data, size_t *upload_data_size, void **con_cls)
{
	if ( xm != NULL )
		return xm->process_request( cls, connection, url, method, version, upload_data, upload_data_size, con_cls);
	else
		return MHD_NO;
}

inline void request_completed( void *cls, struct MHD_Connection *connection, void **con_cls, enum MHD_RequestTerminationCode toe)
{
	if ( xm != NULL )
		xm->request_completed( cls, connection, con_cls, toe );
}

inline int iterate_post( void *coninfo_cls, enum MHD_ValueKind kind, const char *key, 
			const char *filename, const char *content_type, const char *transfer_encoding, 
			const char *data, size_t off, size_t size)
{
	if ( xm != NULL )
		return xm->post_iterate( coninfo_cls, kind, key, filename, content_type, transfer_encoding, data, off, size);
	else
		return MHD_NO;
}


#endif
