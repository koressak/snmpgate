#ifndef _SNMPXMLGATE_H_
#define _SNMPXMLGATE_H_

/*
	Hlavni trida, ktera zajistuje beh serveru.
	Spousti cely transformacni proces, inicializuje struktury,
	implementuje http server a ceka/vyrizuje pozadavky.
*/

#include "definitions.h"


class SnmpXmlGate
{
	private:
		/*
		Konfigurace systemu
		*/
		char *config_file;
		char *log_file;

		/*
		XML
		*/
		XercesDOMParser *conf_parser;


	public:
		SnmpXmlGate(char *);
		~SnmpXmlGate();

		void run(void);
		void initialize_config(void);
};


#endif
