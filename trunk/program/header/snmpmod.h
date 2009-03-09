#ifndef _SNMP_MODULE_H_
#define _SNMP_MODULE_H_

#include "definitions.h"

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

/*
	SNMP modul na komunikaci se spravovanymi zarizenimi.
	Ma za ukol overit existenci zarizeni a pritomnych MIB.
	Pak zpracovava vsechny pozadavky od manazeru
*/

class SnmpModule {
	private:
		list<SNMP_device *> devices;
		char * log_file;
		char * mib_path;
	
	public:
		SnmpModule();
		~SnmpModule();

		int addDevice( SNMP_device * );
		int checkDevices();
		void setParameters( char* , char* );
		bool emptyDevices();
		/*
		TODO: vratit device brany - kvuli poslouchacim portum
		*/

		
};



#endif
