#include "header/snmpxmlgate.h"

pthread_mutex_t lg_msg;

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
	//daemonize();

	SnmpXmlGate *gate;

	log_message( (char *)LOG, (char *)"Starting daemon" );

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

	pthread_mutex_init( &lg_msg, NULL );

	gate->run();

	pthread_mutex_destroy( &lg_msg );

	delete(gate);

	log_message( (char *) LOG, (char *)"Stopping Daemon." );
	return 0;
}
