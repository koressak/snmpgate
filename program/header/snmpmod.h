#ifndef _SNMP_MODULE_H_
#define _SNMP_MODULE_H_

#include "mib2xsd.h"

/*
	SNMP modul na komunikaci se spravovanymi zarizenimi.
	Ma za ukol overit existenci zarizeni a pritomnych MIB.
	Pak zpracovava vsechny pozadavky od manazeru
*/

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
		Mutexy na pristup do fronty
		*/
		pthread_mutex_t *req_locks;
		pthread_mutex_t cond_lock;

		/*
		Condition pro handlery
		*/
		pthread_cond_t incom_cond;
	
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
		int get_device_count();

		u_char map_type_to_pdu( int );

		/*
		TODO: Thread na poslouchani snmp trapu
		*/

		/*
		Funkce pro XmlModule na posilani zprav a ocekavani odpovedi
		*/
		//int send_request( struct request_data*, const char* password,  int msg_type );
		void request_handler( struct snmp_req_handler* );

		/*
		Inicializace threadu
		*/
		int initialize_threads();

		/*
		Zapsani requestu do fronty a probuzeni daneho threadu pro obsluhu
		*/
		int dispatch_request( struct request_data* );

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
Callbacky startovani threadu.
*/
inline void *start_snmp_req_handler( void *arg )
{
	struct snmp_req_handler *rh = (struct snmp_req_handler *) arg;

	if ( rh->snmpmod != NULL )
		rh->snmpmod->request_handler( rh );

	return 0;
}

#endif
