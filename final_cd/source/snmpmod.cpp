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
	list<SNMP_device*>::iterator it;

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
			//rem = it;
			it = devices.erase( it );
			//delete (*rem);
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
		{
			transform->parse_device_mib( *it );
		}
		else
		{
			/*
			Paklize je stejny jako jiny device, nechame to byt a budeme
			urcovat vyhledavani az u prijimani zprav. Tam zvolime
			vzdy ID elementu, ktery je bud jeho id ci similar_as
			*/
			transform->create_device_element( *it, NULL, true );
		}

	}

	transform->end_main_document();

	//parsovat uplne vsechna MIB, ktera jsou v jednotlivych zarizenich
	//init_snmp( "snmpapp" );
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
Vrati true/false jestli existuje zarizeni s danym id
*/
bool SnmpModule::is_device( int id )
{
	list<SNMP_device *>::iterator it;

	for( it = devices.begin(); it != devices.end(); it++ )
	{
		if ( (*it)->id == id )
			return true;
	}

	return false;

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
vrati pointer na device dle jeho pozice
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
Vrati pointer na device, Porovnavame dle snmp_addr
*/
struct notification_element* SnmpModule::get_notification_by_addr( const char* addr )
{

	list<struct notification_element *>::iterator it;

	for( it = notifications.begin(); it != notifications.end(); it++ )
	{
		if ( strcmp( (*it)->device->snmp_addr, addr ) == 0 )
			return (*it);
	}

	return NULL;
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


	/*
	Inicializace trap threadu
	*/
	thr_ret = pthread_create( &trap_thread, NULL, start_snmp_trap_handler, (void *) this );

	if( thr_ret != 0 )
	{
		log_message( log_file, "cannot start trap handler thread" );
		return thr_ret;
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
	SNMP stuff
	*/
	//int liberr, syserr;
	char		*errstr;
	bool		error = false;
	void 		*ss;
	//netsnmp_session *ss;
	struct 		snmp_session session, *sptr=NULL;
	struct 		snmp_pdu *pdu, *response;
	bool		is_getnext_finished = true;

	ss 			= NULL;
	pdu 		= NULL;
	response 	= NULL;

	oid 		elem[MAX_OID_LEN];
	size_t 		elem_len= MAX_OID_LEN;
	oid 		root[MAX_OID_LEN];
	size_t 		root_len= MAX_OID_LEN;
	int 		status;

	/*
	Ostatni struktury
	*/
	string 		error_msg;
	SNMP_device *dev;

	/*
	Variable list pro odpoved
	*/
	struct 		variable_list *vars;

	/*
	Indexed name pro GETNEXT dotaz
	*/
	string 		indexed_name;
	string		prev_indexed_name;

	/*
	mutex a fronta
	*/
	int 		dev_pos;
	pthread_mutex_t *mutex;
	vector<struct request_data*> *queue;
	list<struct request_data*> requests;

	vector<struct request_data*>::iterator vec_it;

	struct request_data *req_data = NULL;

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
			vec_it = queue->begin();
			while( vec_it < queue->end())
			{
				requests.push_back( (*vec_it) );
				vec_it = queue->erase( vec_it );
			}
		}
		pthread_mutex_unlock( mutex );

		//Paklize nemame zadne pozadavky, jdeme spat
		if ( requests.empty() )
		{
			//Ukoncit snmp session a jit spat
			//log_message( log_file, "request handler going to wait" );

			//Zavreme session
			if ( sptr != NULL )
			{
				snmp_sess_close( ss );
				sptr = NULL;
			}

			pthread_mutex_lock( &cond_lock );
			pthread_cond_wait( &incom_cond, &cond_lock );
			pthread_mutex_unlock( &cond_lock);

			//log_message( log_file, "request_handler awakened" );
			is_getnext_finished = true;
		}
		else
		{
			/*
			Samotne zpracovani pozadavku!!!
			*/
			//Nutno inicializovat session
			if ( sptr == NULL )
			{
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
				

				//session.community = (const u_char *) (req_data->community.c_str());
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

			//log_message( log_file, "SNMP: dalsi getnext" );

			response = NULL;
			error = false;
			

			//nastavime pro nej community string
			delete( sptr->community );
			sptr->community = (u_char *) strdup(req_data->community.c_str());
			sptr->community_len = strlen( (char *)sptr->community ) ;


			/*
			Vytvoreni pdu a naplneni daty
			*/
			switch ( req_data->msg_type )
			{
				case XML_MSG_TYPE_GET:
					if ( req_data->snmp_getnext == 1 )
					{
						pdu = snmp_pdu_create( SNMP_MSG_GETNEXT );
						is_getnext_finished = false;
					}
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
					req_data->error = XML_MSG_ERR_SNMP;
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
					//Nastavime si korenovy node pro getnext zarazku
					memcpy( (char *)root, (char *)elem, elem_len * sizeof(oid) );
					root_len = elem_len;

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

			if( error )
				snmp_free_pdu( pdu );

			if ( !error )
			{
				/*
				Tento cyklus se vykona minimalne 1 - pro klasicke GET/SET
				Pro getnext se musi vykonat nekolikrat
				*/
				do 
				{
					status = snmp_sess_synch_response( ss, pdu, &response );
					

					if ( status != STAT_SUCCESS )
					{
						//snmp_sess_error( &sptr, &liberr, &syserr, &errstr );
							//log_message( log_file, errstr);

						req_data->error = XML_MSG_ERR_SNMP;
						req_data->error_str = "PDU couldn't be delivered";



					}
					else if ( response->errstat != SNMP_ERR_NOERROR )
					{
						/*
						Zjistime jaky error prisel pri odpovedi agenta
						*/
						req_data->error = XML_MSG_ERR_SNMP;
						//req_data->snmp_err = 1;
						req_data->error_str = snmp_errstring( response->errstat ) ;
					}
					else
					{
						//Prisle hodnoty vrazime zpet do request_data struktury
						//a muzeme v klidu vratit

						if ( req_data->msg_type == XML_MSG_TYPE_GET )
						{
							if ( req_data->snmp_getnext == 0 )
							{
								for ( it = req_data->request_list.begin(), vars = response->variables; it != req_data->request_list.end(); vars = vars->next_variable, it++ )
								{
									if ( vars != NULL )
									{
										if ( (vars->type == SNMP_ENDOFMIBVIEW) ||
											( vars->type == SNMP_NOSUCHOBJECT ) ||
											(vars->type == SNMP_NOSUCHINSTANCE ) 
											)
										{
											req_data->error = XML_MSG_ERR_SNMP;
											req_data->error_str = string( "No such instance, object or end of mib view" );
										}
										else
										{
											get_response_value( vars, (*it), head );
										}
									}

								}
							}
							else
							{
								//SNMP GETNEXT
								struct value_pair *vp = new value_pair;

								vars = response->variables;
								
								if ( vars != NULL )
								{
									//if ( indexed_name.compare( prev_indexed_name ) == 0 )
									if ( (vars->type == SNMP_ENDOFMIBVIEW) ||
										( vars->type == SNMP_NOSUCHOBJECT ) ||
										(vars->type == SNMP_NOSUCHINSTANCE ) 
										)
									{
										//log_message( log_file, "ending getnext" );
										is_getnext_finished = true;
										delete ( vp );
									}
									else if ( (vars->name_length < root_len) || 
											( memcmp(root, vars->name, root_len * sizeof(oid)) != 0 ) 
										)
									{
										//log_message( log_file, "we reached end of the subtree" );
										is_getnext_finished = true;
										delete ( vp );
									}
									else
									{
										get_response_value( vars, vp, head );

										//prev_indexed_name = indexed_name;
										memcpy( (char *)elem, (char *)vars->name, vars->name_length * sizeof(oid) );
										elem_len = vars->name_length;


										req_data->request_list.push_back( vp );

										if ( response ) 
											snmp_free_pdu( response );
										response = NULL;

										pdu = create_next_pdu( elem, elem_len );

										if ( pdu == NULL )
										{
											log_message( log_file, "Something went wrong. Next pdu does not exist" );
											is_getnext_finished = true;

											req_data->error = XML_MSG_ERR_SNMP;
											req_data->error_str = string( "Error occured while processing request" );
										}
									}
								}

							} //end of getnext
						} // end of get type message
					} //else zpracovani message

				}//end of do
				while ( is_getnext_finished == false );

			} //end of !error

			if ( response != NULL )
				snmp_free_pdu( response );



			//Nechame otevrenou sessnu, kdyby jeste neco bylo

			xmlmod->enqueue_response( req_data );
			req_data = NULL;


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
int SnmpModule::dispatch_request( list<struct request_data *> *req_data_list )
{
	list<struct request_data *>::iterator it = req_data_list->begin();
	int pos = get_device_position( (*it)->object_id );

	if ( pos == -1 )
		return -1;

	pthread_mutex_lock( &req_locks[ pos ] );

	while ( it != req_data_list->end() )
	{
		request_queue[pos].push_back( (*it) );

		it = req_data_list->erase( it );
	}

	pthread_mutex_unlock( &req_locks[ pos ] );

	pthread_mutex_lock( &cond_lock );
	pthread_cond_broadcast( &incom_cond );
	pthread_mutex_unlock( &cond_lock );
	return 0;
}

/*
Create next pdu vytvori dalsi pdu pro potreby
opakovaneho volani GETNEXT
*/
struct snmp_pdu* SnmpModule::create_next_pdu( oid *elem, size_t elem_len )
{
	struct snmp_pdu *pdu;
	/*oid 		elem[MAX_OID_LEN];
	size_t 		elem_len= MAX_OID_LEN;*/

	pdu = snmp_pdu_create( SNMP_MSG_GETNEXT );

	/*if ( !get_node( curr_node, elem, &elem_len ) )
	{
		snmp_free_pdu( pdu );
		return NULL;
	}*/

	snmp_add_null_var( pdu, elem, elem_len );

	return pdu;

}

/*
Vyparsuje hodnotu navracenou agentem a vlozi ji
do struktury value_pair
*/
void SnmpModule::get_response_value( struct variable_list* vars, struct value_pair *vp, struct tree *head )
{
	char 	*var_buf;
	string 	ret_name = "";
	u_char *buf = NULL;
	size_t out_len, buf_len;

	//dostane z toho ten index
	if ( head != NULL && vars->name_loc )
	{
		struct tree *node = get_tree( vars->name_loc, MAX_OID_LEN, head );
		ret_name = string( node->label );
	}
	else
		ret_name = "";

	vp->oid = ret_name;

	/*sprint_realloc_objid( &buf, &buf_len, &out_len, 1, vars->name, vars->name_length);

	name_p = (char *)buf + 1;
	if ( strchr( name_p, '.') != NULL )
	{
		name_p = strchr( name_p, '.' )+1;
	}*/

	//log_message( log_file, indexed_name.c_str() );
	//delete (buf);

	//buf = NULL;

	sprint_realloc_value( &buf, &buf_len, &out_len, 1, vars->name, vars->name_length, vars);
	vp->value = string( (char *)buf );
	
	
	/*if ( vars->name_loc )
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
				sprint_realloc_objid( &buf, &buf_len, &out_len, 1, vars->val.objid, vars->val_len);
				delete (var_buf);
				var_buf = new char[1 + out_len];
				memcpy( var_buf, buf, out_len );
				var_buf[ out_len ] = '\0';

				break;

			case ASN_INTEGER:
			case ASN_UNSIGNED: //same as GAUGE
				sprintf( var_buf, "%ld", *(vars->val.integer) );
				break;

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

		vp->value = string( var_buf );
		delete ( var_buf );

		if ( buf )
			delete (buf );
	}*/

//	vp->value = string( "" );






	//vp->oid = "";
	//log_message( log_file, vp->value.c_str() );

	//delete( buf );

}



/***********************************
*****	Trap handler
***********************************/

void SnmpModule::trap_handler()
{
	log_message( log_file, "Trap handler started" );

	/*
	Nejprve zpracujeme informace ohledne devices
	a jednotlivych manazeru
	*/
	int devices_no;
	SNMP_device *gate;
	SNMP_device *tmp_dev;

	gate = get_gate_device();
	devices_no = get_device_count();

	list<struct trap_manager*>::iterator it;
	string tmp_str;

	struct notification_element *nelem;

	for ( int i=0; i < devices_no; i++ )
	{
		//zpracovani jednotlivych devices pro trap managery
		tmp_dev = get_device_by_position( i );
		nelem = new notification_element;

		for ( it = tmp_dev->traps.begin(); it != tmp_dev->traps.end(); it++ )
		{
			if ( (*it)->address == NULL || (*it)->port == NULL)
			{
				log_message( log_file, "Address and PORT are NULL" );
				break;
			}
			tmp_str = string( (*it)->address );
			tmp_str += ":";
			tmp_str += string( (*it)->port );

			nelem->manager_urls.push_back( tmp_str );

		}

		nelem->device = tmp_dev;

		//umistime do seznamu notifications
		notifications.push_back( nelem );

	}

	/*
	Vytvoreni snmp session
	*/
	void 				*ss;
	netsnmp_session 	session, *sptr=NULL;
	netsnmp_transport	*transport = NULL;
	int 				fds;
	fd_set				fdset;
	int					block = 1;
	struct timeval 		timeout;
	char 				listen_port[100];
	int					numfds;

	struct snmp_trap_magic *magic_struct = new snmp_trap_magic;

	sprintf( listen_port, "udp:%d", gate->snmp_listen_port );

	//init_agent( "snmptrap" );

	/*
	Start transport and create snmp session
	*/
	transport = netsnmp_tdomain_transport( listen_port, 1, "udp" );
	if ( transport == NULL )
	{
		log_message( log_file, "Cannot start snmp transport. Terminating" );
		return;
	}

	/*
	Inicializace session
	*/
	snmp_sess_init( &session );
	sptr = &session;
	sptr->peername = SNMP_DEFAULT_PEERNAME;
	sptr->version = SNMP_DEFAULT_VERSION;
	sptr->community_len = SNMP_DEFAULT_COMMUNITY_LEN;
	sptr->retries = SNMP_DEFAULT_RETRIES;
	sptr->timeout = SNMP_DEFAULT_TIMEOUT;
	sptr->callback = process_snmp_trap;

	magic_struct->snmpmod = this;
	magic_struct->transport = transport;

	sptr->callback_magic = (void *)magic_struct;
	sptr->authenticator = NULL;
	session.isAuthoritative = SNMP_SESS_UNKNOWNAUTH;


	ss = snmp_add( sptr, transport, NULL, NULL );

	if ( ss == NULL )
	{
		log_message( log_file, "Cannot add snmp session to listen. Terminating" );
		return;
	}

	//nekonecny cyklus poslouchani
	while(1)
	{
		fds = 0;
		FD_ZERO( &fdset );
		snmp_select_info( &fds, &fdset, &timeout, &block );

		//blokujici volani selectu
		numfds = select( fds, &fdset, NULL, NULL, NULL );

		if ( numfds > 0 )
		{
			snmp_read( &fdset );

		}
		else if( numfds == -1 )
		{
			log_message( log_file, "Error while select waiting for incoming data" );
		}
	}


}


/*
Processnuti daneho trapu
*/
int SnmpModule::process_trap( int operation, struct snmp_session *sess, int reqid, struct snmp_pdu *pdu,
								netsnmp_transport *trans )
{
	/*
	TODO:
	DO paketu - dostat i timestamp
	*/
	struct hostent *host = NULL;
	string agent;
	char *cp;
	struct 		variable_list *vars;

	//snmp_device
	struct notification_element *notification = NULL;
	string trap_name = "";


	if ( operation == NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE )
	{
		/*
		Deleni dle verzi
		*/
		if ( pdu->command == SNMP_MSG_TRAP )
		{
			/*
			verze 1
			*/

			//nejprve informace o agentovi
			host = gethostbyaddr( (char*) pdu->agent_addr, 4, AF_INET );
			if ( host != NULL )
			{
				agent = string( host->h_name );
			}
			else
				agent = string( inet_ntoa( *((struct in_addr*) pdu->agent_addr) ) );

			log_message( log_file, agent.c_str() );

			/*
			Vyhledame device dle agenta
			*/
			notification = get_notification_by_addr( agent.c_str() );

			if ( notification == NULL )
			{
				log_message( log_file, "TRAP: no such device available. Dropping packet" );
				return 1;
			}

			/*
			Generic traptype
			*/
			if ( pdu->trap_type != SNMP_TRAP_ENTERPRISESPECIFIC )
			{
				switch ( pdu->trap_type ) {
				case SNMP_TRAP_COLDSTART:
					trap_name = "coldStart";
					break;

				case SNMP_TRAP_WARMSTART:
					trap_name = "warmStart";
					break;

				case SNMP_TRAP_LINKDOWN:
					trap_name = "linkDown";
					break;
					
				case SNMP_TRAP_LINKUP:
					trap_name = "linkUp";
					break;

				case SNMP_TRAP_AUTHFAIL:
					trap_name = "authenticationFailure";
					break;

				case SNMP_TRAP_EGPNEIGHBORLOSS:
					trap_name = "egpNeighborLoss";
					break;

				default:
					trap_name = "";
				}

			}
			else
			{
				oid trap_oid[MAX_OID_LEN + 2] = { 0 };
				int trap_oid_len = pdu->enterprise_length;

                memcpy(trap_oid, pdu->enterprise, sizeof(oid) * trap_oid_len);
                if (trap_oid[trap_oid_len - 1] != 0)
				{
                    trap_oid[trap_oid_len++] = 0;
                }
                trap_oid[trap_oid_len++] = pdu->specific_type;

				u_char *oidbuf = NULL;
				size_t ob_len = 64, oo_len = 0;
				int otrunc = 0;

				oidbuf = new u_char[ob_len];


				otrunc = !sprint_realloc_objid(&oidbuf, &ob_len, &oo_len,
					   1, trap_oid, trap_oid_len);
				if (!otrunc) {
					cp = strrchr((char *) oidbuf, '.');
					if (cp != NULL) {
						cp++;
					} else {
						cp = (char *) oidbuf;
					}
				} else {
					cp = (char *) oidbuf;
				}

				/*
				Ziskame mib node z cp -> tim i jeho nazev
				*/
				if ( ! get_node( (const char*)oidbuf, trap_oid, (size_t *)&trap_oid_len ) )
				{
					trap_name="uknown";
				}
				else
				{
					struct tree* node = get_tree( trap_oid, trap_oid_len, get_tree_head() );
					trap_name = string( node->label );
				}

				delete( oidbuf );


			}


		}
		else if ( pdu->command == SNMP_MSG_TRAP2 ||
				  pdu->command == SNMP_MSG_INFORM )
		{
			/*
			Verze 2c
			*/
			if ( trans != NULL )
			{
				struct sockaddr_in *addr = (struct sockaddr_in *) pdu->transport_data;
				
				if (addr != NULL && pdu->transport_data_length == sizeof(struct sockaddr_in)) {
					host = gethostbyaddr((char *) &(addr->sin_addr), sizeof(struct in_addr), AF_INET);

					if ( host == NULL )
					{
						agent = string( inet_ntoa( addr->sin_addr ) );
					}
					else
					{
						agent = string( host->h_name );
					}


					/*
					Vyhledame device dle agenta
					*/
					notification = get_notification_by_addr( agent.c_str() );

					if ( notification == NULL )
					{
						log_message( log_file, "TRAP: no such device available. Dropping packet" );
						return 1;
					}


					/*
					TODO
					How do we get the fucking timestamp and trap oid???
					*/



				}
			}
			else
			{
				log_message( log_file, "Error while parsing trap message" );
				return 1;
			}
		}


		/*
		Ziskame list varbinds -> jmena a hodnoty
		*/
		vars = pdu->variables;
		struct tree *head = get_tree_head();
		list<struct value_pair *> vp_list;
		struct value_pair * vp;

		while( vars != NULL )
		{
			if ( vars != NULL )
			{

				if ( (vars->type == SNMP_ENDOFMIBVIEW) ||
					( vars->type == SNMP_NOSUCHOBJECT ) ||
					(vars->type == SNMP_NOSUCHINSTANCE ) 
					)
				{
				}
				else
				{
					vp = new struct value_pair;
					get_response_value( vars, vp, head );

					vp_list.push_back( vp );
				}
			}

			vars = vars->next_variable;

		}


		/*
		Vygenerujeme odpovedni zpravu EVENT
		*/
		string message;
		char tmp_id[50];

		notification->last_msg_id++;

		message = "<message>\n";
		message += "<event msgid=\"";
		sprintf( tmp_id, "%d", notification->last_msg_id );
		message += tmp_id;
		message += "\" timestamp=\"\"  senderid=\"";
		sprintf( tmp_id, "%d", notification->device->id);
		message += tmp_id;
		message += "\" eventSpec=\"device/notifications/";
		message += trap_name;
		message += "\">\n<data>\n";

		list<struct value_pair*>::iterator it;

		for ( it = vp_list.begin(); it != vp_list.end(); it++ )
		{
			message += "<value valueLocation=\"";
			message += (*it)->oid;
			message += "\">";
			message += (*it)->value;
			message += "</value>\n";

		}

		message += "</data>\n</event>\n</message>";

		//Vymazeme data tohoto notificationu
		vp_list.clear();


		/*
		Odeslani zpravy vsem managerum pomoci libCUrl
		*/
		/*
		libCUrl section
		*/
		CURL 					*curl;
		CURLcode 				res;
		struct curl_httppost 	*formpost=NULL;
		struct curl_httppost 	*lastptr=NULL;
		struct curl_slist 		*headerlist=NULL;
		static const char 		buf[] = "Expect:";
		string url;

		list<string>::iterator strit;

		headerlist = curl_slist_append(headerlist, buf);

		for ( strit = notification->manager_urls.begin(); strit != notification->manager_urls.end(); strit++ )
		{
		
			curl = curl_easy_init();

			if ( curl )
			{
				curl_formadd( &formpost, &lastptr,
								CURLFORM_COPYNAME, "selection",
								CURLFORM_COPYCONTENTS, message.c_str(),
								CURLFORM_CONTENTTYPE,"text/xml",
								CURLFORM_END);


				url = "http://";
				url += (*strit);


				//nastavime parametry a odesleme
				curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);

				curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
				res = curl_easy_perform(curl);

				if ( res != CURLE_OK )
				{
					log_message( log_file, "NOTIF: could not send the distribution data" );
					log_message( log_file, curl_easy_strerror( res ) );
				}


				/*
				clean up
				*/
				curl_easy_cleanup(curl);
			}
			else
			{
				log_message( log_file, "NOTIF: ERROR, could not initialize curl session" );
			}

			/*
			ostatni delete
			*/
			//curl_slist_free_all (headerlist);
			curl_formfree(formpost);
			lastptr = NULL;
			formpost = NULL;

		}//end of all managers cycle


	} //end of first if


	return 1;
}
