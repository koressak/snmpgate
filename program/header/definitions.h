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

/*
Timestamp for log
*/
#include <ctime>


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


/*
Namespaces
*/
using namespace xercesc;
using namespace std;



/* 
Zakladni hodnoty  logovacich souboru
souboru a pracovniho adresare
*/
#define RUN_DIR "/tmp"
#define LOCK	"snmpxmld.lock"
#define LOG		"snmpxmld.log"


/*
Par standardnich funkci, ktere zajisti chovani demona.
Plus logovani zprav do log souboru
*/
inline void log_message(char *filename, string message)
{
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
}



/*
 signal handler
*/
inline void signal_handler(int sig)
{
	switch( sig )
	{
		case SIGTERM:
			log_message( (char *)LOG, string("killing snmpxmld"));
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


#endif

