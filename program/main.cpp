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

	//TODO: sem inicializace a run hlavni tridy Gate
	try {
		gate = new SnmpXmlGate(argv[1]);
	}
	catch( int ex )
	{
		log_message( (char*) LOG, string("Cannot initialize main class. Aborting run."));
		exit(1);
	}

	gate->run();

	delete(gate);

	return 0;
}
