#include "header/snmpmod.h"

#include "header/xmlmod.h"

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
	shutdown_mib();
	delete( transform );

	//delete mutexu
	for ( int a =0; a < get_device_count(); a++ )
	{
		pthread_mutex_destroy( &req_locks[a] );
	}

	pthread_mutex_destroy( &cond_lock );

	pthread_cond_destroy( &incom_cond );
	
	delete( request_queue );
}

/*
Nastaveni zakladnich parametru
*/
void SnmpModule::setParameters( char *log, char *mib, list<DOMElement*>* lis )
{
	log_file = log;
	mib_path = mib;
	devices_root = lis;
}


/*
Ziskani pointeru na XmlModule. Dulezite pro 
prisup k odpovednim frontam.
*/
void SnmpModule::set_xmlmod( XmlModule *mod )
{
	xmlmod = mod;
}

/*
Preda elementy tride transform
*/
void SnmpModule::set_elements( char* xsd_path )
{
	transform = new Mib2Xsd( log_file, devices_root );
	transform->set_dirs( mib_path, xsd_path );
}

/*
Pridani zarizeni z konfiguraku do seznamu
monitorovanych zarizeni
*/
int SnmpModule::addDevice( SNMP_device *dev )
{
	// nejprve zjistime, jestli uz takove zarizeni neni vlozeno
	list<SNMP_device *>::iterator it;
	list<char *>::iterator mib_it_dev;
	list<char *>::iterator mib_it_all;
	unsigned int mib_count = 0;

	for( it = devices.begin(); it != devices.end(); it++ )
	{
		if ( dev->id == (*it)->id )
			return 1;

		//zkontrolujeme MIB, jestli jsou uplne stejne, pak nastavime priznak similar
		if ( dev->similar_as == -1 )
		{
			for ( mib_it_dev = dev->mibs.begin(); mib_it_dev != dev->mibs.end(); mib_it_dev++ )
			{
				for ( mib_it_all = (*it)->mibs.begin(); mib_it_all != (*it)->mibs.end(); mib_it_all++ )
				{
					if ( strcmp( *mib_it_dev, *mib_it_all ) == 0 )
						mib_count++;
				}
			}

			//jestli ma vsechny mib stejne jako ten druhy a zaroven je to rovno poctu
			//definovanych mib, pak jsou stejne
			if ( (( mib_count == dev->mibs.size() ) && ( mib_count == (*it)->mibs.size() )) || ( ( dev->mibs.size() == 0) && ( (*it)->mibs.size() == 0 ) )  )
			{
				dev->similar_as = (*it)->id;
			}
		}
	}
	
	if ( strcmp( dev->snmp_addr, "" )==0 || strcmp( dev->protocol_version, "" )==0 || dev->mibs.empty() )
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

/*
Postupne transformuje vsechny devices mib to xsd
*/
int SnmpModule::start_transform()
{
	//nejprve musime vytvorit hlavni dokument
	transform->create_main_document();

	//nasledne pro vsechny devices zavolame transformaci
	list<SNMP_device *>::iterator it;

	for( it = devices.begin(); it != devices.end(); it++ )
	{
		//pouze paklize je jinej, nez jakekoliv doposud transformovane
		if ( (*it)->similar_as == -1 )
			transform->parse_device_mib( *it );

		/*
		Paklize je stejny jako jiny device, nechame to byt a budeme
		urcovat vyhledavani az u prijimani zprav.
		TODO: paklize je stejny, tak pointer z jeho dat presmerujeme
		na dany device!!!!!!!!!!!!!!!!!!!!!
		*/
	}

	transform->end_main_document();

	//parsovat uplne vsechna MIB, ktera jsou v jednotlivych zarizenich
	init_mib();
	struct tree *tr;
	string mi = "";

	for( it = devices.begin(); it != devices.end(); it++ )
	{
		if ( (*it)->similar_as == -1 )
		{
			list<char *>::iterator mit;
			for ( mit = (*it)->mibs.begin(); mit != (*it)->mibs.end(); mit++)
			{
				mi = mib_path;
				mi += (*mit);

				tr = read_mib( mi.c_str() );
			}
		}

	}

	return 0;
}

/*
vrati seznam vsech devices
*/
list<SNMP_device *> * SnmpModule::get_all_devices()
{
	return &devices;
}

/*
Vrati odkaz na zarizeni brany 
*/
SNMP_device* SnmpModule::get_gate_device()
{
	list<SNMP_device *>::iterator it;

	for( it = devices.begin(); it != devices.end(); it++ )
	{
		if ( (*it)->id == 0 )
			return (*it);
	}

	return NULL;

}

/*
Vrati pozici daneho zarizeni v seznamu devices.
coz se rovna i pozici v seznamu rootu dokumentu
*/
int SnmpModule::get_device_position( int dev_id )
{
	int pos = 0;

	list<SNMP_device *>::iterator it;

	for( it = devices.begin(); it != devices.end(); it++ )
	{
		if ( (*it)->id == dev_id )
			return pos;
		else
			pos++;
	}

	return -1;
}


/*
Vrati pozici daneho zarizeni v seznamu devices.
coz se rovna i pozici v seznamu rootu dokumentu
*/
SNMP_device* SnmpModule::get_device_by_position( int position )
{

	list<SNMP_device *>::iterator it = devices.begin();

	for( int i=0; i < position; i++ )
	{
		it++;
	}

	return (*it);
}

/*
Vrati pozici daneho zarizeni v seznamu devices.
coz se rovna i pozici v seznamu rootu dokumentu
*/
SNMP_device* SnmpModule::get_device_ptr( int dev_id )
{

	list<SNMP_device *>::iterator it;

	for( it = devices.begin(); it != devices.end(); it++ )
	{
		if ( (*it)->id == dev_id )
			return (*it);
	}

	return NULL;
}

/*
Vrati pocet zarizeni
*/
int SnmpModule::get_device_count( )
{
	return devices.size();
}


/*
Inicializace threadu
*/
int SnmpModule::initialize_threads()
{
	int a;
	int thr_ret;
	int devices_no = get_device_count();

	//inicializace mutexu
	req_locks = new pthread_mutex_t[ devices_no ];
	pthread_mutex_init( &cond_lock, NULL );

	for( a = 0; a < devices_no; a++ )
	{
		pthread_mutex_init( &req_locks[a], NULL );
	}

	//inicializace condition
	pthread_cond_init( &incom_cond, NULL );

	//inicializace front
	request_queue = new vector<struct request_data*>[ devices_no ];

	//Startovani threadu
	req_handlers = new pthread_t[ devices_no ];

	for ( a=0; a < devices_no; a++ )
	{
		struct snmp_req_handler *rh = new snmp_req_handler;
		rh->position = a;
		rh->device = get_device_by_position( a );
		rh->snmpmod = this;

		if ( rh->device == NULL )
			return -1;

		thr_ret = pthread_create( &req_handlers[a], NULL, start_snmp_req_handler, (void *) rh );

		if( thr_ret != 0 )
		{
			log_message( log_file, "cannot start new thread" );
			return thr_ret;
		}
	}

	return 0;

}

/*****************************
*********************************/



/*
Zaslani dotazu agentovi - handler, ktery si hlida svoji frontu
*/
void SnmpModule::request_handler( struct snmp_req_handler *hr )
{
	/*
	TODO:
	ziskat poinery na mutex a frontu
	spat na conditione
	delete hr struktury

	zasilat dotazy a odpovedi davat do response queue xmlmodulu
	*/
	
	/*
	SNMP stuff
	*/
	int liberr, syserr;
	char *errstr;
	bool error = false;
	void *ss;
	struct snmp_session session, *sptr=NULL;
	struct snmp_pdu *pdu, *response;

	oid elem[MAX_OID_LEN];
	size_t elem_len= MAX_OID_LEN;
	int status;

	/*
	Ostatni struktury
	*/
	string error_msg;
	SNMP_device *dev;

	/*
	Variable list pro odpoved
	*/
	struct variable_list *vars;
	char *var_buf;

	/*
	mutex a fronta
	*/
	int dev_pos;
	pthread_mutex_t *mutex;
	vector<struct request_data*> *queue;
	list<struct request_data*> requests;

	vector<struct request_data*>::iterator vec_it;

	struct request_data *req_data;

	/*
	Ziskani informaci z parametru
	*/
	dev_pos = hr->position;
	dev = hr->device;

	//smazani nepotrebnych dat.
	delete( hr );

	mutex = &req_locks[ dev_pos ];
	queue = &request_queue[ dev_pos ];

	log_message( log_file, "Thread - snmp request handler - started" );

	/*
	Nekoncici cyklus. Musel by jej zabit hlavni proces pri
	konceni brany
	*/
	while ( 1 )
	{
		pthread_mutex_lock( mutex );
		if ( !queue->empty() )
		{
			//vyjmeme vsechny pozadavky
			for ( vec_it = queue->begin(); vec_it < queue->end(); vec_it++ )
			{
				requests.push_back( (*vec_it) );
				queue->erase( vec_it );
			}


		}
		pthread_mutex_unlock( mutex );

		//Paklize nemame zadne pozadavky, jdeme spat
		if ( requests.empty() )
		{
			//Ukoncit snmp session a jit spat
			log_message( log_file, "request handler going to wait" );
			
			if ( sptr != NULL )
			{
				snmp_sess_close( ss );
				sptr = NULL;
			}

			pthread_mutex_lock( &cond_lock );
			pthread_cond_wait( &incom_cond, &cond_lock );
			pthread_mutex_unlock( &cond_lock);

			log_message( log_file, "request_handler awakened" );
		}
		else
		{
			/*
			Samotne zpracovani pozadavku!!!
			*/
			if ( sptr == NULL )
			{
				//Nutno inicializovat session
				log_message( log_file, "Initializing snmp session" );

				snmp_sess_init( &session );

				session.peername = dev->snmp_addr;

				switch ( atoi( dev->protocol_version ) )
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

				//session.community = (u_char *) password;
				//session.community_len = strlen( (const char *)session.community );

				ss = snmp_sess_open( &session );

				if ( ss == NULL )
				{
					//snmp_error( &session, &liberr, &syserr, &errstr );
					log_message( log_file, "Cannot open snmp session" );
					//delete( errstr );
					errstr = NULL;
					xmlmod->enqueue_response( req_data );
					continue;
				}
				
				sptr = snmp_sess_session( ss );
			}

			//ziskame prvni dotaz
			req_data = requests.front();
			requests.pop_front();

			//nastavime pro nej community string
			delete( sptr->community );
			sptr->community = (u_char *) req_data->community;
			sptr->community_len = strlen( (char *)sptr->community ) ;

			/*
			Vytvoreni pdu a naplneni daty
			*/
			switch ( req_data->msg_type )
			{
				case XML_MSG_TYPE_GET:
					if ( req_data->snmp_getnext == 1 )
						pdu = snmp_pdu_create( SNMP_MSG_GETNEXT );
					else
						pdu = snmp_pdu_create( SNMP_MSG_GET );
					break;

				case XML_MSG_TYPE_SET:
					pdu = snmp_pdu_create( SNMP_MSG_SET );
					break;

				default:
					req_data->error = XML_MSG_ERR_INTERNAL;
					req_data->error_str = "no such message type";

					//vratime response back
					xmlmod->enqueue_response( req_data );					
					error = true;
					continue;

			}

			list<struct value_pair *>::iterator it;

			//Pro pouziti vyhledavani typu a nazvu elementu
			struct tree *head = get_tree_head();

			for ( it = req_data->request_list.begin(); it != req_data->request_list.end(); it++ )
			{
				if ( !get_node( (*it)->oid.c_str(), elem, &elem_len ) )
				{
					//snmp_sess_error( &sptr, &liberr, &syserr, &errstr );
					//log_message( log_file, errstr );
					req_data->error = XML_MSG_ERR_INTERNAL;
					req_data->error_str = "Cannot find node in MIB";
					//delete( errstr );
					error = true;
				}

				/*
				Nejprve je nutne najit ten element podle oid a zjistit jeho typ,
				ktery dosadime do pdu
				*/
				if ( !error )
				{
					struct tree *node = get_tree( elem, elem_len, head );

					switch ( req_data->msg_type )
					{
						case XML_MSG_TYPE_SET:
							snmp_pdu_add_variable( pdu, elem, elem_len, map_type_to_pdu( node->type ), (const u_char*)(*it)->value.c_str(), (*it)->value.size() );
							break;

						case XML_MSG_TYPE_GET:
						default:
							snmp_add_null_var( pdu, elem, elem_len );
					}
				}
			} //end for cycle

			if ( !error )
			{
				status = snmp_sess_synch_response( ss, pdu, &response );
			//status = snmp_sess_send( ss, pdu );

				if ( status != STAT_SUCCESS )
				{
					//snmp_sess_error( &sptr, &liberr, &syserr, &errstr );
						//log_message( log_file, errstr);

					req_data->error = XML_MSG_ERR_INTERNAL;
					req_data->error_str = "PDU couldn't be delivered";


				}
				else if ( response->errstat != SNMP_ERR_NOERROR )
				{
					/*
					Zjistime jaky error prisel pri odpovedi agenta
					*/
					req_data->error = XML_MSG_ERR_SNMP;
					req_data->snmp_err = 1;
					req_data->snmp_err_str = snmp_errstring( response->errstat ) ;
				}
				else
				{
					//Prisle hodnoty vrazime zpet do request_data struktury
					//a muzeme v klidu vratit

					if ( req_data->msg_type == XML_MSG_TYPE_GET )
					{
						for ( it = req_data->request_list.begin(), vars = response->variables; it != req_data->request_list.end(); vars = vars->next_variable, it++ )
						{
							if ( vars != NULL )
							{
								var_buf = new char[ 1 + vars->val_len ];
								switch ( vars->type )
								{
									case ASN_OCTET_STR:
									case ASN_OPAQUE:
										memcpy( var_buf, vars->val.string, vars->val_len );
										var_buf[ vars->val_len ] = '\0';
										break;

									case ASN_BIT_STR:
									case ASN_IPADDRESS:
										memcpy( var_buf, vars->val.bitstring, vars->val_len );
										var_buf[ vars->val_len ] = '\0';
										break;

									case ASN_OBJECT_ID:
										sprintf( var_buf, "%ld", *(vars->val.objid) );
										break;

									case ASN_INTEGER:
									case ASN_UNSIGNED: //same as GAUGE
									case ASN_TIMETICKS:
										sprintf( var_buf, "%ld", *(vars->val.integer) );
										break;

									case ASN_COUNTER:
									case ASN_APP_COUNTER64:
									case ASN_INTEGER64:
									case ASN_UNSIGNED64:
										sprintf( var_buf, "%ld%ld", vars->val.counter64->high, vars->val.counter64->low );
										break;

									case ASN_APP_FLOAT:
										sprintf( var_buf, "%f", *(vars->val.floatVal) );
										break;

									case ASN_APP_DOUBLE:
										sprintf( var_buf, "%f", *(vars->val.doubleVal) );
										break;

								}

								/*
								navratime zaroven i platne jmeno s indexem polozky
								TODO: nevracet jemno s indexem!!!!
								*/
								string ret_name = "";
								u_char *buf = NULL;
								size_t buf_len = 0;
								size_t out_len = 0;
								char *name_p = NULL;

								struct tree *node = get_tree( vars->name_loc, MAX_OID_LEN, head );

								ret_name = string( node->label );
								ret_name += ".";


								//dostane z toho ten index
								sprint_realloc_objid( &buf, &buf_len, &out_len, 1, vars->name, vars->name_length);

								name_p = (char *)buf + 1;
								name_p = strchr( name_p, '.' )+1;

								ret_name += string( name_p );

								log_message( log_file, ret_name.c_str() );


								(*it)->oid = ret_name;
								(*it)->value = string( var_buf );
								log_message( log_file, var_buf );

								delete ( var_buf );
								delete( buf );

							}

						}
					}
				}

			} //end of !error

			if ( response )
				snmp_free_pdu( response );

			//TODO: mel bych mazat i to request PDU???

			xmlmod->enqueue_response( req_data );



		} //end of process requests
	} //end of while(1)




}



/*
Mapuje TYPE_XXX do ASN_TYPE_YYY
Pro spravne nastaveni typu nodu v SET message
*/
u_char SnmpModule::map_type_to_pdu( int node_type )
{
	u_char ret;

	switch( node_type )
	{
		case TYPE_OBJID:
			ret = ASN_OBJECT_ID;
			break;
		case TYPE_OCTETSTR:
			ret = ASN_OCTET_STR;
			break;
		case TYPE_INTEGER:
			ret = ASN_INTEGER;
			break;
		case TYPE_NETADDR:
			ret = ASN_IPADDRESS;
			break;
		case TYPE_IPADDR:
			ret = ASN_IPADDRESS;
			break;
		case TYPE_COUNTER:
			ret = ASN_COUNTER;
			break;
		case TYPE_GAUGE:
			ret = ASN_GAUGE;
			break;
		case TYPE_TIMETICKS:
			ret = ASN_TIMETICKS;
			break;
		case TYPE_OPAQUE:
			ret = ASN_OPAQUE;
			break;
		case TYPE_COUNTER64:
			ret = ASN_COUNTER64;
			break;
		case TYPE_BITSTRING:
			ret = ASN_BIT_STR;
			break;
		case TYPE_UINTEGER:
			ret = ASN_UINTEGER;
			break;
		case TYPE_UNSIGNED32:
			ret = ASN_UINTEGER;
			break;
		case TYPE_INTEGER32:
			ret = ASN_INTEGER;
			break;
		default:
			ret = 0;
	}

	return ret;

}


/*******************************
****Dispatch the request
********************************/
int SnmpModule::dispatch_request( struct request_data *req_data )
{
	//TODO udelat pro vice messages najednou
	int pos = get_device_position( req_data->object_id );

	if ( pos == -1 )
		return -1;
	
	pthread_mutex_lock( &req_locks[ pos ] );

	request_queue[pos].push_back( req_data );

	pthread_mutex_unlock( &req_locks[ pos ] );

	pthread_mutex_lock( &cond_lock );
	pthread_cond_broadcast( &incom_cond );
	pthread_mutex_unlock( &cond_lock );

	return 0;
}


