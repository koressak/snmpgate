#include "header/snmpmod.h"

/*
Konstruktor
*/
SnmpModule::SnmpModule()
{
	//inicializace snmp knihovny
	init_snmp( "snmpapp" );
}

/*
Destruktor
*/
SnmpModule::~SnmpModule()
{
}

/*
Nastaveni zakladnich parametru
*/
void SnmpModule::setParameters( char *log, char *mib )
{
	log_file = log;
	mib_path = mib;
}

/*
Pridani zarizeni z konfiguraku do seznamu
monitorovanych zarizeni
*/
int SnmpModule::addDevice( SNMP_device *dev )
{
	// nejprve zjistime, jestli uz takove zarizeni neni vlozeno
	list<SNMP_device *>::iterator it;

	for( it = devices.begin(); it != devices.end(); it++ )
		if ( dev->id == (*it)->id )
			return 1;
	
	if ( ( dev->snmp_addr == "" ) || ( dev->protocol_version == "" ) || dev->mibs.empty() )
		return 1;
	
	devices.push_back( dev );


	return 0;
}

/*
Check devices - zjisti, jestli vsechna zarizeni funguji a 
jestli jsou pritomny jejich MIB soubory
*/
int SnmpModule::checkDevices()
{
	//inicializace snmp session
	struct snmp_session session, *ss;
	struct snmp_pdu *pdu, *response;

	oid elem[MAX_OID_LEN];
	size_t elem_len= MAX_OID_LEN;
	int status;

	string error_msg;


	snmp_sess_init( &session );

	//pro jednotlive devices v seznamu
	list<SNMP_device*>::iterator it, rem;

	for( it = devices.begin(); it != devices.end(); it++ )
	{
		session.peername = (*it)->snmp_addr;

		switch ( atoi( (*it)->protocol_version ) )
		{
			case 1:
				session.version = SNMP_VERSION_1;
				break;
			case 2:
				session.version = SNMP_VERSION_2c;
				break;
			default:
				session.version = SNMP_VERSION_1;
		}

		session.community = (u_char *)"public";
		session.community_len = strlen( (const char *)session.community );

		//otevreme spojeni
		ss = snmp_open( &session );

		if ( !ss )
		{
			error_msg = "Neni mozne navazat spojeni se zarizenim: ";
			error_msg += (*it)->name;
			error_msg += "\n";
			log_message( log_file, error_msg.c_str() );

			//odebereme zarizeni ze seznamu
			(*it)->active = 0;
		}
		else
		{

		/*
			zazadame o sysDesc.0 - popis zarizeni.
			Paklize odpovi, je vsechno v poradku
		*/
		pdu = snmp_pdu_create( SNMP_MSG_GET );
		
		get_node( "sysDescr.0", elem, &elem_len );
		snmp_add_null_var( pdu, elem, elem_len );

		status = snmp_synch_response( ss, pdu, &response );

		if ( status != STAT_SUCCESS || response->errstat != SNMP_ERR_NOERROR )
		{
			//chyba v komunikaci - odebirame z monitorovanych zarizeni
			error_msg = "Zarizeni: ";
			error_msg += (*it)->name;
			error_msg += " neodpovida. Nebude monitorovano.\n";

			log_message( log_file, error_msg.c_str() );
			(*it)->active = 0;
		}
			

		if ( response )
			snmp_free_pdu( response );

		snmp_close( ss );
		}

		/*
		Zjistime, jestli existuji vsechny mib soubory k tomuto zarizeni
		*/
		if ( (*it)->active == 1 )
		{
			list<char *>::iterator mi;
			string path;

			for ( mi = (*it)->mibs.begin(); mi != (*it)->mibs.end(); mi++)
			{
				path = mib_path;
				path += *mi;

				if ( access( path.c_str(), F_OK) == -1 )
				{
					error_msg = "MIB soubor: ";
					error_msg += path;
					error_msg += " neexistuje. Nemuzeme pokracovat v transformaci.";
					log_message( log_file,  error_msg.c_str() );
					return 1;
				}
			}
		}

	} //konec for


	it = devices.begin();
	while ( it != devices.end() )
	{
		if ( (*it)->active == 0 )
		{
			rem = it;
			it = devices.erase( it );
			delete (*rem);
		}
		else
			it++;
	}

	return 0;
}

/*
Vrati jestli je seznam monitorovanych zarizeni prazdny
*/
bool SnmpModule::emptyDevices()
{
	return devices.empty();
}

