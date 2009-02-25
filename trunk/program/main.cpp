#include "header/snmpxmlgate.h"


/*
 Hlavni spousteci funkce
*/
int main( int argc, char** argv)
{
	//Ocekavame config file, jako jediny
	//parametr programu
	if ( argc != 2 )
		exit(1);
	
	//Odtud to bezi jako daemon
	daemonize();

	SnmpXmlGate *gate;

	log_message( (char *)LOG, (char *)"Starting daemon" );

	//TODO: sem inicializace a run hlavni tridy Gate
	try {
		gate = new SnmpXmlGate(argv[1]);
	}
	catch( int ex )
	{
		log_message( (char*) LOG, (char*)"Cannot initialize main class. Aborting run.");
		exit(1);
	}
	catch ( ... )
	{
		log_message( (char *) LOG, (char *)"Unknown error occured while initializing gate." );
	}

	gate->run();

	delete(gate);

	log_message( (char *) LOG, (char *)"Stopping Daemon." );
	return 0;
}
