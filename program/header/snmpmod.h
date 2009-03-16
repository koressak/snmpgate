#ifndef _SNMP_MODULE_H_
#define _SNMP_MODULE_H_

#include "mib2xsd.h"

/*
	SNMP modul na komunikaci se spravovanymi zarizenimi.
	Ma za ukol overit existenci zarizeni a pritomnych MIB.
	Pak zpracovava vsechny pozadavky od manazeru
*/

class SnmpModule {
	private:
		list<SNMP_device *> devices;
		list<DOMElement *> *devices_root;
		char * log_file;
		char * mib_path;

		Mib2Xsd *transform;
	
	public:
		SnmpModule();
		~SnmpModule();

		int addDevice( SNMP_device * );
		int checkDevices();
		void setParameters( char* , char*, list<DOMElement*>* );
		void set_elements( DOMDocument*, DOMElement*, char* );
		bool emptyDevices();

		int start_transform();
		/*
		TODO: vratit device brany - kvuli poslouchacim portum
		*/
		list<SNMP_device*>* get_all_devices();

		
};



#endif
