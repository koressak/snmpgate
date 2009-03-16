#include "header/mib2xsd.h"

/*
Konstruktor
*/
Mib2Xsd::Mib2Xsd( DOMDocument *d, DOMElement *r, char* log, list<DOMElement*> *lis )
{
	main_doc = d;
	main_root = r;
	log_file = log;
	devices_root = lis;
}

/*
Destruktor
*/
Mib2Xsd::~Mib2Xsd()
{
}

/*
Nastaveni pracovnich adresaru
*/
void Mib2Xsd::set_dirs( char *md, char *xd )
{
	mib_dir = md;
	xsd_dir = xd;
}

/*
Startovaci funkce transformace
*/
void Mib2Xsd::parse_device_mib( SNMP_device *dev )
{
	int mibs;
	struct tree *mib_tree;
	string mi="";
	char output[20];

	/*
	XML inicializace dokumentu
	*/
	DOMImplementation *impl;
	
	impl = DOMImplementationRegistry::getDOMImplementation( XMLString::transcode("core") );

	doc =  impl->createDocument() ;
	root = doc->createElement( X("xsd:schema") );
	doc->appendChild( root );

	//get headers
	write_header( root );

	/*
	SNMP - inicializace MIB
	*/
	mibs = add_mibdir(mib_dir);
	init_mib();

	/*
	Cteme vsechny MIB prislusejici k zarizeni
	*/
	list<char *>::iterator it;
	for ( it = dev->mibs.begin(); it != dev->mibs.end(); it++ )
	{
		mi = mib_dir;
		mi += (*it);

		mib_tree = read_mib( mi.c_str() );
	}

	//mib_tree = read_mib("/usr/share/snmp/mibs/OSPF-MIB.txt");
	//mib_tree = read_mib("/usr/share/snmp/mibs/OSPF-TRAP-MIB.txt");

	//Parsovani celeho stromu
	mib_tree = get_tree_head();

	parse_node( mib_tree, root );

	//END parsing

	//Output to file
	mi = xsd_dir;
	sprintf( output, "%d.xsd", dev->id );
	mi += output;
	output_xml2file( mi.c_str() , doc );

	create_device_element( dev, root );

	shutdown_mib();

	//Ukonceni prace s XML dokumentem. Nutno release
	//TODO nereleasovat -> predat zpet do drzeni hlavni tride
	//TODO nutno pridat tento device do hlavniho elementu
	//doc->release();
}


/*
Create oid
*/
string Mib2Xsd::get_current_oid()
{
	string tmp = "";
	for ( list<string>::iterator it = curr_oid.begin(); it != curr_oid.end(); it++)
	{
		tmp += *it;
		it++;
		if ( it != curr_oid.end() )
			tmp += ".";
		it--;
	}

	return tmp;
}

/*
Get element oid
*/
string Mib2Xsd::get_element_oid( char *element )
{
	string ret = "";
	string tmp;
	char oid[10];

	struct tree* node = find_tree_node( element, -1 );
	tmp = ".";
	sprintf( oid, "%d", (int)node->subid);
	tmp += oid;
	ret = tmp;

	node = node->parent;

	while ( node != NULL )
	{
		tmp = ".";
		sprintf( oid, "%d", (int)node->subid);
		tmp += oid;

		ret = tmp + ret;

		node = node->parent;
	}

	return ret.substr(1);
}

/*
Zjisti a vrati typ uzlu
*/
string Mib2Xsd::get_node_type( struct tree* node )
{
	string ret = "";

	switch( node->type )
	{
		case TYPE_OBJID:
			ret = "OID";
			break;
		case TYPE_OCTETSTR:
			ret = "xsd:string";
			break;
		case TYPE_INTEGER:
			ret = "xsd:integer";
			break;
		case TYPE_NETADDR:
			ret = "NetworkAddress";
			break;
		case TYPE_IPADDR:
			ret = "IpAddress";
			break;
		case TYPE_COUNTER:
			ret = "Counter";
			break;
		case TYPE_GAUGE:
			ret = "Gauge";
			break;
		case TYPE_TIMETICKS:
			ret = "TimeTicks";
			break;
		case TYPE_OPAQUE:
			ret = "Opaque";
			break;
		case TYPE_COUNTER64:
			ret = "Counter64";
			break;
		case TYPE_BITSTRING:
			ret = "xsd:string";
			break;
		case TYPE_UINTEGER:
			ret = "xsd:unsignedInt";
			break;
		case TYPE_UNSIGNED32:
			ret = "Unsigned32";
			break;
		case TYPE_INTEGER32:
			ret = "xsd:integer";
			break;
		default:
			ret = "unkn";
			cout << "found uknown type" << endl;
	}

	return ret;
}


void Mib2Xsd::create_docum_tag( struct tree* node, DOMElement *parent )
{
	DOMElement *docum;
	DOMAttr*	attr;

	docum = doc->createElement( X("xsd:documentation") );
	parent->appendChild( docum );

	attr = doc->createAttribute( X("xml:lang") );
	attr->setNodeValue( X("en") );

	docum->setAttributeNode( attr );
}

/*------------------------------------------
	Write notification types
--------------------------------------------*/

/*
	Vytvoreni hlavniho elementu shrnujiciho vsechny
	notification
*/
void Mib2Xsd::create_main_notif_element()
{
	DOMAttr* attr;
	DOMElement *tmel;

	main_notif = doc->createElement( X("xsd:complexType") );

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X("MainNotificationType") );
	main_notif->setAttributeNode( attr );

	tmel = doc->createElement( X("xsd:sequence") );
	main_notif->appendChild( tmel );

	main_notif_seq = tmel;

}

/*
	Pridani elementu do main notification
*/
void Mib2Xsd::add_notif_element( string name )
{
	DOMElement*	tmel;
	DOMAttr*	attr;
	string tmpstr;

	tmpstr = name;
	tmpstr += "Type";

	tmel = doc->createElement( X("xsd:element") );
	main_notif_seq->appendChild( tmel );

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X( name.c_str() ) );
	tmel->setAttributeNode( attr );

	attr = doc->createAttribute( X("type") );
	attr->setNodeValue( X( tmpstr.c_str() ) );
	tmel->setAttributeNode( attr );

}



/*
Zapis skupiny objektu
TODO : jak sakra najit ty objecty, na ktery linkuje?? ve strukture to vubec neni
*/
void Mib2Xsd::write_object_group( struct tree* node, DOMElement *parent )
{
	DOMElement* elem;
	DOMAttr*	attr;
	DOMElement* annot;
	DOMElement* appinfo;
	DOMElement* tmp;
	DOMText*	txt;
	string tmpstr;

	elem = doc->createElement( X("xsd:element") );
	parent->appendChild( elem );

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X( node->label ) );
	elem->setAttributeNode( attr );

	elem = doc->createElement( X("xsd:simpleType") );
	root->appendChild( elem );

	attr = doc->createAttribute( X("name") );
	tmpstr = node->label;
	tmpstr += "Type";
	attr->setNodeValue( X( tmpstr.c_str() ) );
	elem->setAttributeNode( attr );


	annot = doc->createElement( X("xsd:annotation") );
	appinfo = doc->createElement( X("appinfo") );

	elem->appendChild( annot );
	annot->appendChild( appinfo );

	create_docum_tag( node, annot );

	tmp = doc->createElement( X("oid") );
	tmpstr = get_current_oid();
	txt = doc->createTextNode( X( tmpstr.c_str() ) );
	tmp->appendChild(txt);

	appinfo->appendChild( tmp );


}


/*
Zapis object Identity
*/
void Mib2Xsd::write_object_identity( struct tree* node, DOMElement *parent )
{
	DOMElement* elem;
	DOMAttr*	attr;
	DOMElement* annot;
	DOMElement* appinfo;
	DOMElement* tmp;
	DOMText*	txt;
	string tmpstr;

	elem = doc->createElement( X("xsd:element") );
	parent->appendChild( elem );

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X( node->label ) );
	elem->setAttributeNode( attr );

	attr = doc->createAttribute( X("type") );
	tmpstr = node->label;
	tmpstr += "Type";
	attr->setNodeValue( X( tmpstr.c_str() ) );
	elem->setAttributeNode( attr );

	elem = doc->createElement( X("xsd:simpleType") );
	root->appendChild( elem );

	attr = doc->createAttribute( X("name") );
	tmpstr = node->label;
	tmpstr += "Type";
	attr->setNodeValue( X( tmpstr.c_str() ) );
	elem->setAttributeNode( attr );

	annot = doc->createElement( X("xsd:annotation") );
	appinfo = doc->createElement( X("appinfo") );

	elem->appendChild( annot );
	annot->appendChild( appinfo );

	create_docum_tag( node, annot );

	tmp = doc->createElement( X("oid") );
	tmpstr = get_current_oid();
	txt = doc->createTextNode( X( tmpstr.c_str() ) );
	tmp->appendChild(txt);

	appinfo->appendChild( tmp );
}

/*
Notification group type
*/
void Mib2Xsd::write_notif_group( struct tree* node, DOMElement *parent )
{
	DOMElement* elem;
	DOMAttr*	attr;
	DOMElement* annot;
	DOMElement* appinfo;
	DOMElement* tmp;
	DOMText*	txt;
	DOMElement*	restr;
	string tmpstr;

	elem = doc->createElement( X("xsd:element") );
	parent->appendChild( elem );

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X( node->label ) );
	elem->setAttributeNode( attr );

	attr = doc->createAttribute( X("type") );
	tmpstr = node->label;
	tmpstr += "Type";
	attr->setNodeValue( X( tmpstr.c_str() ) );
	elem->setAttributeNode( attr );

	elem = doc->createElement( X("xsd:simpleType") );
	root->appendChild( elem );

	attr = doc->createAttribute( X("name") );
	tmpstr = node->label;
	tmpstr += "Type";
	attr->setNodeValue( X( tmpstr.c_str() ) );
	elem->setAttributeNode( attr );

	annot = doc->createElement( X("xsd:annotation") );
	appinfo = doc->createElement( X("appinfo") );

	elem->appendChild( annot );
	annot->appendChild( appinfo );

	create_docum_tag( node, annot );

	tmp = doc->createElement( X("oid") );
	tmpstr = get_current_oid();
	txt = doc->createTextNode( X( tmpstr.c_str() ) );
	tmp->appendChild(txt);

	appinfo->appendChild( tmp );

	restr = doc->createElement( X("xsd:restriction") );
	elem->appendChild( restr );

	attr = doc->createAttribute( X("base") );
	attr->setNodeValue( X("xsd:integer") );
	restr->setAttributeNode( attr );
}

/*
vytvoreni obecneho complex typu
*/
DOMElement* Mib2Xsd::write_complex_type( struct tree* node, DOMElement *parent)
{
	DOMElement* tmel;
	DOMElement* next_par;
	DOMAttr* attr;

	tmel = doc->createElement( X("xsd:element"));
	parent->appendChild( tmel );

	parent = tmel;

	attr = doc->createAttribute( X("minOccurs") );
	attr->setNodeValue( X("0") );
	tmel->setAttributeNode( attr );

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X(node->label) );
	tmel->setAttributeNode( attr );

	//pridat anotaci
	tmel = doc->createElement( X("xsd:annotation") );
	parent->appendChild( tmel );

	DOMElement *app = doc->createElement( X("xsd:appinfo") );
	DOMElement *oid = doc->createElement( X("oid") );

	string tmp = "";
	tmp = get_current_oid();
	DOMText *txt = doc->createTextNode( X(tmp.c_str()) );

	tmel->appendChild(app);
	app->appendChild(oid);
	oid->appendChild(txt);

	//pridat complex type def
	tmel = doc->createElement( X("xsd:complexType") );
	parent->appendChild( tmel );

	//pridat complex type def
	next_par = doc->createElement( X("xsd:sequence") );
	tmel->appendChild( next_par );

	return next_par;
}


/*
Notification type
*/
void Mib2Xsd::write_notif_type( struct tree* node, DOMElement *parent )
{
	DOMElement* tmel;
	DOMElement* elem;
	DOMElement* restr;
	DOMAttr*	attr;
	DOMElement* annot;
	DOMElement* appinfo;
	DOMText*	txt;
	string tmpstr;

	//Nejprve vytvorime element pod parenta
	tmel = doc->createElement( X("xsd:element") );
	parent->appendChild( tmel );

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X( node->label) );
	tmel->setAttributeNode( attr );

	//Pak definujeme typ trapu a zaclenime jej do main_notif
	add_notif_element( string(node->label) );

	elem = doc->createElement( X("xsd:simpleType") );
	root->appendChild( elem );

	attr = doc->createAttribute( X("name") );
	tmpstr = node->label;
	tmpstr += "Type";
	attr->setNodeValue( X( tmpstr.c_str() ) );
	elem->setAttributeNode( attr );

	annot = doc->createElement( X("xsd:annotation") );
	appinfo = doc->createElement( X("appinfo") );

	elem->appendChild( annot );
	annot->appendChild( appinfo );

	create_docum_tag( node, annot );

	restr = doc->createElement( X("xsd:restriction") );
	elem->appendChild( restr );

	attr = doc->createAttribute( X("base") );
	attr->setNodeValue( X("xsd:dateTime") );
	restr->setAttributeNode( attr );

	/*
	variables pro trap
	*/
	struct varbind_list* var = node->varbinds;

	while( var != NULL )
	{
		tmel = doc->createElement( X("variable") );
		appinfo->appendChild( tmel );

		attr = doc->createAttribute( X("oid") );
		tmpstr = get_element_oid( var->vblabel );
		attr->setNodeValue( X( tmpstr.c_str() ) );
		tmel->setAttributeNode( attr );
		
		txt = doc->createTextNode( X( var->vblabel ) );
		tmel->appendChild( txt );

		var = var->next;
	}

}



/*---------------------------------------------
	Write normal types
-----------------------------------------------*/

/*
Zapise spolecnou cast annotation elementu do definice typu
*/
void Mib2Xsd::write_annotation_part( struct tree* node, DOMElement *annot )
{
	DOMElement* appinfo;
	DOMElement* tmp;
	DOMAttr* attr;
	DOMText* txt;
	string tmpstr;

	tmp = doc->createElement( X("xsd:documentation") );
	annot->appendChild( tmp );

	attr = doc->createAttribute( X("xsd:lang") );
	attr->setNodeValue( X("en") );
	tmp->setAttributeNode( attr );
	
	if ( node->description != NULL )
	{
		txt = doc->createTextNode( X(node->description) );
		tmp->appendChild( txt );
	}

	appinfo = doc->createElement( X("xsd:appinfo") );
	annot->appendChild( appinfo );

	tmp = doc->createElement( X("status") );
	
	switch( node->status )
	{
		case MIB_STATUS_MANDATORY:
			tmpstr = "mandatory";
			break;
		case MIB_STATUS_OPTIONAL:
			tmpstr = "optional";
			break;
		case MIB_STATUS_OBSOLETE:
			tmpstr = "obsolete";
			break;
		case MIB_STATUS_DEPRECATED:
			tmpstr = "deprecated";
			break;
		case MIB_STATUS_CURRENT:
			tmpstr = "current";
			break;
		default:
			tmpstr = "unkn";
	}

	txt = doc->createTextNode( X( tmpstr.c_str() ) );
	tmp->appendChild( txt );
	appinfo->appendChild( tmp );

	tmp = doc->createElement( X("access") );

	switch( node->access )
	{
		case MIB_ACCESS_READONLY:
			tmpstr = "read-only";
			break;
		case MIB_ACCESS_READWRITE:
			tmpstr = "read-write";
			break;
		case MIB_ACCESS_WRITEONLY:
			tmpstr = "write-only";
			break;
		case MIB_ACCESS_NOACCESS:
			tmpstr = "not-accessible";
			break;
		case MIB_ACCESS_NOTIFY:
			tmpstr = "accessible-for-notify";
			break;
		case MIB_ACCESS_CREATE:
			tmpstr = "read-create";
			break;
		default:
			tmpstr = "unkn";
	}

	txt = doc->createTextNode( X( tmpstr.c_str() ) );
	tmp->appendChild( txt );
	appinfo->appendChild( tmp );

	tmp = doc->createElement( X("oid") );
	tmpstr = get_current_oid();
	txt = doc->createTextNode( X( tmpstr.c_str() ) );
	tmp->appendChild( txt );
	appinfo->appendChild( tmp );
}

/*
Zapise definici typu zakladniho elementu
*/
void Mib2Xsd::write_typeof_simple( struct tree* node )
{
	DOMElement* tmel;
	DOMElement* annot;
	DOMElement* restr;
	DOMElement* tmp;
	DOMAttr* attr;
	string tmpstr;


	/*
	Definice typu s restriction
	*/
	tmel = doc->createElement( X("xsd:simpleType") );
	tmpstr = node->label;
	tmpstr += "Type";
	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X( tmpstr.c_str() ) );
	tmel->setAttributeNode( attr );
	root->appendChild( tmel );

	annot = doc->createElement( X("xsd:annotation") );
	tmel->appendChild( annot );

	write_annotation_part( node,  annot );

	//nyni mame annotation hotovy
	//zacina restriction
	restr = doc->createElement( X("xsd:restriction") );
	tmel->appendChild( restr );

	attr = doc->createAttribute( X("base") );
	restr->setAttributeNode( attr );

	tmpstr = get_node_type( node );
	attr->setNodeValue( X( tmpstr.c_str() ));

	//Typ je slozen z nekolika definovanych elementu
	if ( node->enums != NULL )
	{
		struct enum_list *en = node->enums;
		char value[10];

		while ( en != NULL )
		{
			tmp = doc->createElement( X("xsd:enumeration") );
			attr = doc->createAttribute( X("value") );

			sprintf( value, "%d", en->value);
			attr->setNodeValue( X( value ) );
			tmp->setAttributeNode( attr );
			restr->appendChild( tmp );

			en = en->next;
		}

	}
	//Typ ma definovany rozsah hodnot
	else if ( node->ranges != NULL )
	{
		struct range_list *ran = node->ranges;

		if ( node->type == TYPE_OCTETSTR )
			tmp = doc->createElement( X("xsd:minLength") );
		else
			tmp = doc->createElement( X("xsd:minInclusive") );
		attr = doc->createAttribute( X("value") );

		char low[10];
		sprintf( low, "%d", ran->low);
		attr->setNodeValue( X( low ) );
		tmp->setAttributeNode( attr );
		restr->appendChild( tmp );

		if ( node->type == TYPE_OCTETSTR )
			tmp = doc->createElement( X("xsd:maxLength") );
		else
			tmp = doc->createElement( X("xsd:maxInclusive") );
		attr = doc->createAttribute( X("value") );

		char high[10];
		sprintf( high, "%d", ran->high);
		attr->setNodeValue( X( high ) );
		tmp->setAttributeNode( attr );
		restr->appendChild( tmp );

	}


}

/*
Doda do annotation element appinfo s oid elementem
TODO
nechce chodit description?!?!?!?!
*/
void Mib2Xsd::write_simple_type( struct tree* node, DOMElement *parent )
{
	DOMElement* tmel;
	DOMAttr* attr;
	string tmpstr;

	/*
	Zarazeni elementu do stromove struktury
	*/
	tmel = doc->createElement( X("xsd:element") );
	parent->appendChild(tmel);

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X(node->label) );
	tmel->setAttributeNode( attr );

	attr = doc->createAttribute( X("type") );
	tmpstr = node->label;
	tmpstr += "Type";
	attr->setNodeValue( X(tmpstr.c_str()) );
	tmel->setAttributeNode( attr );

	write_typeof_simple( node );

}


/*
Element SEQUENCE
*/
void Mib2Xsd::write_sequence( struct tree *node )
{
	DOMElement* tmel;
	DOMElement* annot;
	DOMElement* appinfo;
	DOMElement* tmp;
	DOMElement* sequence;
	DOMAttr* attr;
	DOMText* txt;
	string tmpstr;
	char oid[10];

	//nejprve je nutno vytvorit complex typ - table entry
	tmel = doc->createElement( X("xsd:complexType") );
	tmpstr = node->label;
	tmpstr += "Type";

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X( tmpstr.c_str() ) );
	tmel->setAttributeNode( attr );

	//annotation part
	annot = doc->createElement( X("xsd:annotation") );
	tmel->appendChild( annot );

	root->appendChild( tmel );

	write_annotation_part( node,  annot );

	//indexes
	struct index_list *index = node->indexes;
	struct tree* elem_oid;

	//najdeme appinfo element annotationu
	DOMNodeList *applist = annot->getElementsByTagName( X("xsd:appinfo") );
	DOMNode *appinf;

	for ( XMLSize_t m = 0; m < applist->getLength(); m++ )
	{
		appinf = applist->item(m);

		if ( appinf->getNodeType() && appinf->getNodeType() == DOMNode::ELEMENT_NODE )
		{
			appinfo = dynamic_cast<DOMElement *>(appinf);
		}
	}

	while ( index != NULL )
	{
		
		tmp = doc->createElement( X("index") );
		appinfo->appendChild( tmp );

		attr = doc->createAttribute( X("oid") );

		elem_oid = find_tree_node( index->ilabel, -1 );
		tmpstr = get_current_oid();
		tmpstr += ".";
		sprintf( oid, "%d", (int)elem_oid->subid );
		tmpstr += oid;

		attr->setNodeValue( X( tmpstr.c_str() ) );
		tmp->setAttributeNode( attr );

		txt = doc->createTextNode( X( index->ilabel ) );
		tmp->appendChild( txt );

		index = index->next;
	}


	//nasleduje definice typu jednotlivych elementru table entry ( simple type )
	sequence = doc->createElement( X("xsd:sequence") );
	tmel->appendChild( sequence );
	
	struct tree* children = node->child_list;

	while ( children != NULL )
	{
		parse_node ( children, sequence);
		children = children->next_peer;
	}

}



/*
Zapise element SEQUENCE OF
*/
void Mib2Xsd::write_sequence_of( struct tree *node, DOMElement* parent )
{
	DOMElement* elem;
	DOMElement* subel;
	DOMAttr* attr;
	string tmpstr;
	char oid_tmp[10];

	elem = doc->createElement( X("xsd:element") );
	parent->appendChild( elem );

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X( node->label) );
	elem->setAttributeNode( attr );

	subel = doc->createElement( X("xsd:complexType") );
	elem->appendChild( subel );

	elem = doc->createElement( X("xsd:sequence") );
	subel->appendChild( elem );

	//prechazime k diteti - TableEntry
	struct tree* entry = node->child_list;


	subel = doc->createElement( X("xsd:element") );
	elem->appendChild( subel );

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X( entry->label ) );
	subel->setAttributeNode( attr );

	attr = doc->createAttribute( X("type") );
	tmpstr = entry->label;
	tmpstr += "Type";
	attr->setNodeValue( X( tmpstr.c_str() ) );
	subel->setAttributeNode( attr );

	attr = doc->createAttribute( X("minOccurs") );
	attr->setNodeValue( X("0") );
	subel->setAttributeNode( attr );

	attr = doc->createAttribute( X("maxOccurs") );
	attr->setNodeValue( X("unbounded") );
	subel->setAttributeNode( attr );

	//nutno myslet na dalsi level of oid
	sprintf( oid_tmp, "%d",(int) entry->subid );
	curr_oid.push_back( oid_tmp );

	write_sequence( entry );

	curr_oid.pop_back();

	
}


/*
Parsuje node - dle toho zapise do xsd schematu dane veci
*/
void Mib2Xsd::parse_node( struct tree* node, DOMElement *parent )
{
	struct tree *children;
	char oid_tmp[10];
	bool ignore = false;
	string tmpstr;
	DOMElement *next_par= NULL;

	/*
	current oid
	*/
	sprintf( oid_tmp, "%d", (int)node->subid );
	curr_oid.push_back( (string)oid_tmp );

	DOMElement *tmel = NULL;
	DOMAttr *attr = NULL;

	//Zjistime, jaky typ nodu to je
	if ( node->type == TYPE_OTHER )
	{
		if ( node->access == 0 )
		{
			//root elemen, without type
			tmel = doc->createElement( X("xsd:element"));
			parent->appendChild( tmel );

			parent = tmel;

			attr = doc->createAttribute( X("minOccurs") );
			attr->setNodeValue( X("0") );
			tmel->setAttributeNode( attr );

			attr = doc->createAttribute( X("name") );
			attr->setNodeValue( X(node->label) );
			tmel->setAttributeNode( attr );

			//pridat anotaci
			tmel = doc->createElement( X("xsd:annotation") );
			parent->appendChild( tmel );

			DOMElement *app = doc->createElement( X("xsd:appinfo") );
			DOMElement *oid = doc->createElement( X("oid") );

			string tmp = "";
			tmp = get_current_oid();
			DOMText *txt = doc->createTextNode( X(tmp.c_str()) );

			tmel->appendChild(app);
			app->appendChild(oid);
			oid->appendChild(txt);

			if ( node->child_list != NULL )
			{
				//pridat complex type def
				tmel = doc->createElement( X("xsd:complexType") );
				parent->appendChild( tmel );

				//pridat complex type def
				next_par = doc->createElement( X("xsd:sequence") );
				tmel->appendChild( next_par );
			}

		}
		else if ( node->access == MIB_ACCESS_NOACCESS ) //NOACCESS
		{
			//tabulka, neboli SEQUENCE OF
			int childCount = 0;
			struct tree *child = node->child_list;
			while ( child != NULL )
			{
				childCount++;
				child = child->next_peer;
			}

			//Paklize je vice nebo zadne dite, chyba
			//tabulka musi mit 1 dite -> Sequence
			if ( childCount != 1 )
			{
				cout << "ERROR: table has more than one child\n";
			}
			else
			{
				write_sequence_of( node, parent );
			}

		}
		else
		{
			//debug vystup, ze je neco spatne
			//to by nastat nemelo
			cout << "Chybny element - access wrong: " << node->label << endl;
		}
	}
	else
	{
		switch( node->type )
		{
			case TYPE_TRAPTYPE:
				ignore = true;
				write_notif_type( node, parent );
				break;
			case TYPE_NOTIFTYPE:
				ignore = true;
				write_notif_type( node, parent );
				break;
			case TYPE_OBJGROUP:
				ignore = false;
				write_object_group( node, parent );
				break;
			case TYPE_MODID:
				ignore = false;
				next_par = write_complex_type( node, parent );
				break;
			case TYPE_NOTIFGROUP:
				ignore = true;
				write_notif_group( node, parent );
				break;
			case TYPE_AGENTCAP:
				ignore = true;
				break;
			case TYPE_MODCOMP:
				ignore = true;
				break;
			case TYPE_OBJIDENTITY:
				ignore = false;
				write_object_identity( node, parent );
				break;
			default:
				ignore = false;
				write_simple_type( node, parent );
		}

		if (ignore)
		{
			curr_oid.pop_back();
			return;
		}

	}


	children = node->child_list;

	while ( children != NULL && next_par != NULL)
	{
		parse_node ( children, next_par);
		children = children->next_peer;
	}

	//pri returnu musime pop_back z curr_oid listu
	curr_oid.pop_back();
	
}

/*
Output whole schema to designated file
*/
void Mib2Xsd::output_xml2file( const char *file,  DOMDocument *doc )
{
	DOMImplementation *impl;
	DOMWriter *writer;
	XMLFormatTarget	*target;

	impl = DOMImplementationRegistry::getDOMImplementation( XMLString::transcode("LS") );

	writer = ((DOMImplementationLS*)impl)->createDOMWriter();

	writer->setFeature( XMLUni::fgDOMWRTFormatPrettyPrint, true );

	target = new LocalFileFormatTarget( XMLString::transcode(file) );

	writer->writeNode( target, *doc );

	target->flush();
	writer->release();
	delete(target);
}

/*------------------------------------------
	Document header
---------------------------------------------*/

/*
Zapsani hlavicky dokumentu
*/
void Mib2Xsd::write_header( DOMElement *root )
{
	DOMAttr *attr;
	DOMElement* tmel;
	DOMElement*	notif;
	DOMText* txt;

	attr = doc->createAttribute( X("attributeFromDefault") );
	attr->setNodeValue( X("unqualified") );
	root->setAttributeNode( attr );

	attr = doc->createAttribute( X("elementFormDefault") );
	attr->setNodeValue( X("qualified") );
	root->setAttributeNode( attr );

	attr = doc->createAttribute( X("xmlns:xsd") );
	attr->setNodeValue( X("http://www.w3.org/2001/XMLSchema") );
	root->setAttributeNode( attr );

	attr = doc->createAttribute( X("xmlns:xsi") );
	attr->setNodeValue( X("http://www.w3.org/2001/XMLSchema-instance") );
	root->setAttributeNode( attr );

	/*
	TODO
	Pridat core type dokument/ nebo na nej pouze linkovat
	Create namespaces??
	*/

	tmel = doc->createElement( X("xsd:appinfo") );
	root->appendChild( tmel );

	notif = doc->createElement( X("notificationType") );
	tmel->appendChild( notif );

	txt = doc->createTextNode( X("MainNotificationType") );
	notif->appendChild( txt );

	attr = doc->createAttribute( X("defaultMappingPath") );
	attr->setNodeValue( X("/notifications/") );
	notif->setAttributeNode( attr );

	create_main_notif_element();
	root->appendChild( main_notif );

}


/*
Vytvori hlavni dokument se seznamem vsech 
*/
void Mib2Xsd::create_main_document()
{
	/*
	XML inicializace dokumentu
	*/
	DOMImplementation *impl;
	DOMElement *tmel;
	DOMElement *services;
	DOMElement *info;
	DOMElement *device;

	string tmpstr;
	
	impl = DOMImplementationRegistry::getDOMImplementation( XMLString::transcode("core") );

	main_doc =  impl->createDocument() ;
	main_root = main_doc->createElement( X("device") );
	main_doc->appendChild( main_root );
	
	info = main_doc->createElement( X("info") );
	main_root->appendChild( info );
	//TODO: dodat nejake info do elementu info

	services = main_doc->createElement( X("services") );
	main_root->appendChild( services );

	tmel = main_doc->createElement( X("xmlbnmgate") );
	services->appendChild( tmel );

	services = tmel;
	info = main_doc->createElement( X("info") );
	services->appendChild( info );
	//TODO: nejake info tady

	device = main_doc->createElement( X("devices") );
	services->appendChild( device );
	main_devices = device;

}

/*
Zapsani informaci o device do hlavniho dokumentu
*/
void Mib2Xsd::create_device_element( SNMP_device *dev, DOMElement *r )
{
	DOMElement *tmel;
	DOMElement *info;
	DOMElement *device;

	DOMAttr *attr;
	DOMText *txt;

	string tmpstr;
	char out[20];

	device = main_doc->createElement( X("device") );
	main_devices->appendChild( device );

	attr = main_doc->createAttribute( X("id") );
	sprintf( out, "%d", dev->id );
	attr->setNodeValue( X( out ) );
	device->setAttributeNode(attr);

	info = main_doc->createElement( X("info") );
	device->appendChild( info );

	tmel = main_doc->createElement( X("name") );
	txt = main_doc->createTextNode( X( dev->name ) );
	tmel->appendChild( txt );
	info->appendChild( tmel );

	tmel = main_doc->createElement( X("description") );
	txt = main_doc->createTextNode( X( dev->description ) );
	tmel->appendChild( txt );
	info->appendChild( tmel );

	tmel = main_doc->createElement( X("subscriptions") );
	device->appendChild( tmel );

	tmel = main_doc->createElement( X("notifications") );
	device->appendChild( tmel );
	//TODO: sem dodat info z notifications,

	tmel = main_doc->createElement( X("data") );
	device->appendChild( tmel );

	devices_root->push_back(r);

}


/*
Konecne zapsani hlavniho dokumentu do souboru
*/
void Mib2Xsd::end_main_document()
{
	string str;

	str = xsd_dir;
	str += "snmpxmld.xsd";

	output_xml2file( str.c_str(), main_doc );

}
