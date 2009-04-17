#ifndef _SNMP_MODULE_H_
#define _SNMP_MODULE_H_

#include "mib2xsd.h"

/*
	SNMP modul na komunikaci se spravovanymi zarizenimi.
	Ma za ukol overit existenci zarizeni a pritomnych MIB.
	Pak zpracovava vsechny pozadavky od manazeru
*/
extern pthread_mutex_t lg_msg;

class XmlModule;

struct snmp_req_handler;

class SnmpModule {
	private:
		list<SNMP_device *> devices;
		list<DOMElement *> *devices_root;
		char * log_file;
		char * mib_path;

		Mib2Xsd *transform;

		/*
		XmlModule pro vraceni odpovedi
		*/
		XmlModule *xmlmod;

		/*
		thready na obsluhu pozadavku
		*/
		pthread_t *req_handlers;

		/*
		Fronty na zpracovani pozadavku
		*/
		vector<struct request_data *> *request_queue;

		/*
		thread na odchytavani trapu
		*/
		pthread_t trap_thread;

		/*
		Mutexy na pristup do fronty
		*/
		pthread_mutex_t *req_locks;
		pthread_mutex_t cond_lock;

		/*
		Condition pro handlery
		*/
		pthread_cond_t incom_cond;

		/*
		Seznam notification elementu
		*/
		list<struct notification_element *> notifications;
	
	public:
		SnmpModule();
		~SnmpModule();

		int addDevice( SNMP_device * );
		int checkDevices();
		void setParameters( char* , char*, list<DOMElement*>* );
		void set_xmlmod( XmlModule *mod );
		void set_elements( char* );
		bool emptyDevices();

		int start_transform();
		list<SNMP_device*>* get_all_devices();
		SNMP_device* get_gate_device();
		int get_device_position( int );
		SNMP_device* get_device_ptr( int );
		SNMP_device* get_device_by_position( int );
		struct notification_element* get_notification_by_addr( const char* );
		int get_device_count();

		u_char map_type_to_pdu( int );


		/*
		Funkce pro XmlModule na posilani zprav a ocekavani odpovedi
		*/
		//int send_request( struct request_data*, const char* password,  int msg_type );
		void request_handler( struct snmp_req_handler* );

		/*
		Odchytavani trapu a posilani eventu managerovi
		*/
		void trap_handler();

		/*
		Processing trap pdu
		*/
		int process_trap( int, struct snmp_session *, int , struct snmp_pdu*, netsnmp_transport * );

		/*
		Inicializace threadu
		*/
		int initialize_threads();

		/*
		Zapsani requestu do fronty a probuzeni daneho threadu pro obsluhu
		*/
		int dispatch_request( list<struct request_data*> * );

		/*
		Vytvori dalsi getnext snmp pdu
		*/
		struct snmp_pdu* create_next_pdu( oid *, size_t );

		/*
		Dostane z response vars hodnotu a nazev
		*/
		void get_response_value( struct variable_list*, struct value_pair *, struct tree * );

		DOMDocument *get_doc()
		{
			return transform->get_main_doc();
		}

		DOMElement* get_root()
		{
			return transform->get_main_root();
		}

		
};

/*
Informace pro snmp request handler
*/
struct snmp_req_handler
{
	int position;
	SNMP_device *device;
	SnmpModule *snmpmod;
};

/*
SNMP trap blbec
*/
struct snmp_trap_magic
{
	SnmpModule *snmpmod;
	netsnmp_transport *transport;
};
  
/*
Callbacky startovani threadu.
*/
inline void *start_snmp_req_handler( void *arg )
{
	struct snmp_req_handler *rh = (struct snmp_req_handler *) arg;

	if ( rh->snmpmod != NULL )
		rh->snmpmod->request_handler( rh );

	return 0;
}

/*
pro trap handler
*/
inline void *start_snmp_trap_handler( void *arg )
{
	SnmpModule *snmpmod = (SnmpModule *) arg;
	if ( snmpmod != NULL )
	{
		snmpmod->trap_handler();
	}

	return 0;
}

/*
Callback pro cteni a processing prijatych trapu
*/
inline int process_snmp_trap( int operation, struct snmp_session *sess, int reqid , struct snmp_pdu* pdu, void *magic)
{
	//SnmpModule *snmpmod = (SnmpModule *) magic;
	struct snmp_trap_magic *mag = (struct snmp_trap_magic *) magic;
	if ( mag->snmpmod != NULL )
	{
		mag->snmpmod->process_trap( operation, sess, reqid, pdu, mag->transport );
	}

	return 0;
}

#endif
