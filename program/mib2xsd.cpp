#include "header/mib2xsd.h"

/*
Konstruktor
*/
Mib2Xsd::Mib2Xsd( char* log, list<DOMElement*> *lis )
{
	main_doc = NULL;
	main_root = NULL;
	log_file = log;
	devices_root = lis;
}

/*
Destruktor
*/
Mib2Xsd::~Mib2Xsd()
{
	main_doc->release();

	//smazani jednotlivych device dokumentu
	list<DOMElement *>::iterator it;

	for ( it = devices_root->begin(); it != devices_root->end(); it++ )
		(*it)->release();
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
	char output[100];

	/*
	XML inicializace dokumentu
	*/
	DOMImplementation *impl;
	
	impl = DOMImplementationRegistry::getDOMImplementation( XMLString::transcode("core") );

	doc =  impl->createDocument() ;
	root = doc->createElement( X("xsd:schema") );
	doc->appendChild( root );

	xml_root = main_doc->createElement( X("data") );

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

	parse_node( mib_tree, root, xml_root );

	//END parsing

	//Output to file
	mi = xsd_dir;
	sprintf( output, "%d.xsd", dev->id );
	mi += output;
	output_xml2file( mi.c_str() , doc );

	create_device_element( dev, root, false );

	shutdown_mib();

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
POZN: neni mozne ziskat odkazy na objekty, na ktere linkuje. Tohle se nejak snmp nepodarilo
*/
void Mib2Xsd::write_object_group( struct tree* node, DOMElement *parent, DOMElement *xml_par )
{
	DOMElement* elem;
	DOMAttr*	attr;
	DOMElement* annot;
	DOMElement* appinfo;
	DOMElement* tmp;
	DOMElement* xtmp;
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

	//xml
	xtmp = main_doc->createElement( X( node->label ) );
	xml_par->appendChild( xtmp );

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

	//xml
	attr = main_doc->createAttribute( X( "oid" ) );
	attr->setNodeValue( X(tmpstr.c_str()) );
	xtmp->setAttributeNode( attr );


}


/*
Zapis object Identity
*/
void Mib2Xsd::write_object_identity( struct tree* node, DOMElement *parent, DOMElement *xml_par )
{
	DOMElement* elem;
	DOMAttr*	attr;
	DOMElement* annot;
	DOMElement* appinfo;
	DOMElement* tmp;
	DOMElement* xtmp;
	DOMText*	txt;
	string tmpstr;

	elem = doc->createElement( X("xsd:element") );
	parent->appendChild( elem );

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X( node->label ) );
	elem->setAttributeNode( attr );

	//xml
	xtmp = main_doc->createElement( X( node->label ) );
	xml_par->appendChild( xtmp );

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

	//xml
	attr = main_doc->createAttribute( X( "oid" ) );
	attr->setNodeValue( X(tmpstr.c_str()) );
	xtmp->setAttributeNode( attr );
}

/*
Notification group type
*/
void Mib2Xsd::write_notif_group( struct tree* node, DOMElement *parent, DOMElement *xml_par )
{
	DOMElement* elem;
	DOMAttr*	attr;
	DOMElement* annot;
	DOMElement* appinfo;
	DOMElement* tmp;
	DOMText*	txt;
	DOMElement*	restr;
	DOMElement* xtmp;
	string tmpstr;

	elem = doc->createElement( X("xsd:element") );
	parent->appendChild( elem );

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X( node->label ) );
	elem->setAttributeNode( attr );

	//xml
	xtmp = main_doc->createElement( X( node->label ) );
	xml_par->appendChild( xtmp );

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

	//xml
	attr = main_doc->createAttribute( X( "oid" ) );
	attr->setNodeValue( X(tmpstr.c_str()) );
	xtmp->setAttributeNode( attr );

	appinfo->appendChild( tmp );

	restr = doc->createElement( X("xsd:restriction") );
	elem->appendChild( restr );

	attr = doc->createAttribute( X("base") );
	attr->setNodeValue( X("xsd:integer") );
	restr->setAttributeNode( attr );

	//xml
	attr = main_doc->createAttribute( X( "restriction" ) );
	attr->setNodeValue( X( "xsd:integer" ) );
	xtmp->setAttributeNode( attr );
}

/*
vytvoreni obecneho complex typu
*/
list<DOMElement*> Mib2Xsd::write_complex_type( struct tree* node, DOMElement *parent, DOMElement *xml_par )
{
	DOMElement* tmel;
	DOMElement* next_par;
	DOMElement* xtmp;
	DOMAttr* attr;

	list<DOMElement *> mod_elements;

	tmel = doc->createElement( X("xsd:element"));
	parent->appendChild( tmel );

	parent = tmel;

	attr = doc->createAttribute( X("minOccurs") );
	attr->setNodeValue( X("0") );
	tmel->setAttributeNode( attr );

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X(node->label) );
	tmel->setAttributeNode( attr );

	//xml
	xtmp = main_doc->createElement( X( node->label ) );
	xml_par->appendChild( xtmp );

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

	//xml
	attr = main_doc->createAttribute( X( "oid" ) );
	attr->setNodeValue( X( tmp.c_str() ) );
	xtmp->setAttributeNode( attr );

	mod_elements.push_back( xtmp );

	//pridat complex type def
	tmel = doc->createElement( X("xsd:complexType") );
	parent->appendChild( tmel );

	//pridat complex type def
	next_par = doc->createElement( X("xsd:sequence") );
	tmel->appendChild( next_par );

	mod_elements.push_back( next_par );


	//return next_par;
	return mod_elements;
}


/*
Notification type
*/
void Mib2Xsd::write_notif_type( struct tree* node, DOMElement *parent, DOMElement *xml_par )
{
	DOMElement* tmel;
	DOMElement* elem;
	DOMElement* restr;
	DOMElement* xtmp;
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

	//xml
	xtmp = main_doc->createElement( X( node->label ) );
	xml_par->appendChild( xtmp );

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
void Mib2Xsd::write_annotation_part( struct tree* node, DOMElement *annot, DOMElement *xml_par )
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

	//xml
	attr = main_doc->createAttribute( X( "access" ) );
	attr->setNodeValue( X( tmpstr.c_str() ) );
	xml_par->setAttributeNode( attr );

	tmp = doc->createElement( X("oid") );
	tmpstr = get_current_oid();
	txt = doc->createTextNode( X( tmpstr.c_str() ) );
	tmp->appendChild( txt );
	appinfo->appendChild( tmp );

	//xml
	attr = main_doc->createAttribute( X( "oid" ) );
	attr->setNodeValue( X( tmpstr.c_str() ) );
	xml_par->setAttributeNode( attr );
}

/*
Zapise definici typu zakladniho elementu
*/
void Mib2Xsd::write_typeof_simple( struct tree* node, DOMElement *xml_par )
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

	write_annotation_part( node,  annot, xml_par );

	//nyni mame annotation hotovy
	//zacina restriction
	restr = doc->createElement( X("xsd:restriction") );
	tmel->appendChild( restr );

	attr = doc->createAttribute( X("base") );
	restr->setAttributeNode( attr );

	tmpstr = get_node_type( node );
	attr->setNodeValue( X( tmpstr.c_str() ));

	//xml
	attr = main_doc->createAttribute( X("restriction") );
	attr->setNodeValue( X( tmpstr.c_str() ) );
	xml_par->setAttributeNode( attr );

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
POZN: nechodi description u elementu. Proste ten pointer je prazdny
*/
void Mib2Xsd::write_simple_type( struct tree* node, DOMElement *parent, DOMElement *xml_par )
{
	DOMElement* tmel;
	DOMElement* xtmp;
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

	//xml
	xtmp = main_doc->createElement( X( node->label ) );
	xml_par->appendChild( xtmp );

	attr = doc->createAttribute( X("type") );
	tmpstr = node->label;
	tmpstr += "Type";
	attr->setNodeValue( X(tmpstr.c_str()) );
	tmel->setAttributeNode( attr );

	write_typeof_simple( node, xtmp );

}


/*
Element SEQUENCE
*/
void Mib2Xsd::write_sequence( struct tree *node, DOMElement *xml_par )
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

	write_annotation_part( node,  annot, xml_par );

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
		parse_node ( children, sequence, xml_par);
		children = children->next_peer;
	}

}



/*
Zapise element SEQUENCE OF
*/
void Mib2Xsd::write_sequence_of( struct tree *node, DOMElement* parent, DOMElement *xml_par )
{
	DOMElement* elem;
	DOMElement* subel;

	DOMElement* xtmp;
	DOMElement* xsubtmp;

	DOMAttr* attr;
	string tmpstr;
	char oid_tmp[10];

	elem = doc->createElement( X("xsd:element") );
	parent->appendChild( elem );

	attr = doc->createAttribute( X("name") );
	attr->setNodeValue( X( node->label) );
	elem->setAttributeNode( attr );

	//xml
	xtmp = main_doc->createElement( X( node->label ) );
	xml_par->appendChild( xtmp );

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

	xsubtmp = main_doc->createElement( X( entry->label ) );
	xtmp->appendChild( xsubtmp );

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

	write_sequence( entry, xsubtmp );

	curr_oid.pop_back();

	
}


/*
Parsuje node - dle toho zapise do xsd schematu dane veci
*/
void Mib2Xsd::parse_node( struct tree* node, DOMElement *parent, DOMElement *xml_par )
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

	DOMElement *xtmp = NULL;
	DOMElement *next_xml_par = NULL;

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

			//XML dokument
			xtmp = main_doc->createElement( X( node->label ) );
			xml_par->appendChild( xtmp );


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

			//xml doc oid
			attr = main_doc->createAttribute( X( "oid" ) );
			attr->setNodeValue( X( tmp.c_str() ) );
			xtmp->setAttributeNode( attr );

			if ( node->child_list != NULL )
			{
				//pridat complex type def
				tmel = doc->createElement( X("xsd:complexType") );
				parent->appendChild( tmel );

				//pridat complex type def
				next_par = doc->createElement( X("xsd:sequence") );
				tmel->appendChild( next_par );

				//xml next par
				next_xml_par = xtmp;
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
				write_sequence_of( node, parent, xml_par);
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
		list<DOMElement *> mod_elements;
		switch( node->type )
		{
			case TYPE_TRAPTYPE:
				ignore = true;
				write_notif_type( node, parent, xml_par );
				break;
			case TYPE_NOTIFTYPE:
				ignore = true;
				write_notif_type( node, parent, xml_par );
				break;
			case TYPE_OBJGROUP:
				ignore = false;
				write_object_group( node, parent, xml_par );
				break;
			case TYPE_MODID:
				ignore = false;
				mod_elements = write_complex_type( node, parent, xml_par );
				next_xml_par = mod_elements.front();
				mod_elements.pop_front();
				next_par = mod_elements.front();
				mod_elements.pop_front();
				break;
			case TYPE_NOTIFGROUP:
				ignore = true;
				write_notif_group( node, parent, xml_par );
				break;
			case TYPE_AGENTCAP:
				ignore = true;
				break;
			case TYPE_MODCOMP:
				ignore = true;
				break;
			case TYPE_OBJIDENTITY:
				ignore = false;
				write_object_identity( node, parent, xml_par );
				break;
			default:
				ignore = false;
				write_simple_type( node, parent, xml_par );
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
		if ( next_xml_par != NULL )
			parse_node ( children, next_par, next_xml_par);
		else
			parse_node( children, next_par, xml_par );

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
	DOMElement *sequence;
	DOMAttr* attr;

	string tmpstr;
	
	impl = DOMImplementationRegistry::getDOMImplementation( XMLString::transcode("core") );

	main_doc =  impl->createDocument() ;
	main_root = main_doc->createElement( X("device") );
	main_doc->appendChild( main_root );

	
	info = main_doc->createElement( X("info") );
	main_root->appendChild( info );

	services = main_doc->createElement( X("services") );
	main_root->appendChild( services );

	tmel = main_doc->createElement( X("xmlbnmgate") );
	services->appendChild( tmel );

	services = tmel;
	info = main_doc->createElement( X("info") );
	services->appendChild( info );

	device = main_doc->createElement( X("devices") );
	services->appendChild( device );
	main_devices = device;

	/*
	XSD popis brany
	*/
	xsd_main_doc = impl->createDocument();
	xsd_main_root = xsd_main_doc->createElement( X("xsd:schema" ) );
	xsd_main_doc->appendChild( xsd_main_root );

	attr = xsd_main_doc->createAttribute( X("attributeFromDefault") );
	attr->setNodeValue( X("unqualified") );
	xsd_main_root->setAttributeNode( attr );

	attr = xsd_main_doc->createAttribute( X("elementFormDefault") );
	attr->setNodeValue( X("qualified") );
	xsd_main_root->setAttributeNode( attr );

	attr = xsd_main_doc->createAttribute( X("xmlns:xsd") );
	attr->setNodeValue( X("http://www.w3.org/2001/XMLSchema") );
	xsd_main_root->setAttributeNode( attr );

	attr = xsd_main_doc->createAttribute( X("xmlns:xsi") );
	attr->setNodeValue( X("http://www.w3.org/2001/XMLSchema-instance") );
	xsd_main_root->setAttributeNode( attr );

	//xsd popis device
	device = xsd_main_doc->createElement( X("xsd:element") );
	attr = xsd_main_doc->createAttribute( X("name") );
	attr->setNodeValue( X("device") );
	device->setAttributeNode( attr );
	xsd_main_root->appendChild( device );
	xsd_main_root = device;

	tmel = xsd_main_doc->createElement( X("xsd:complexType") );
	device->appendChild( tmel );
	sequence = xsd_main_doc->createElement( X("xsd:sequence") );
	tmel->appendChild( sequence );
	xsd_main_root = sequence;

	info = xsd_main_doc->createElement( X("xsd:element") );
	attr = xsd_main_doc->createAttribute( X("name") );
	attr->setNodeValue( X("info") );
	info->setAttributeNode( attr );
	xsd_main_root->appendChild( info );

	services = xsd_main_doc->createElement( X("xsd:element") );
	attr = xsd_main_doc->createAttribute( X("name") );
	attr->setNodeValue( X("services") );
	services->setAttributeNode( attr );
	xsd_main_root->appendChild( services );

	tmel = xsd_main_doc->createElement( X("xsd:complexType") );
	services->appendChild( tmel );
	sequence = xsd_main_doc->createElement( X("xsd:sequence") );
	tmel->appendChild( sequence );
	services = sequence;

	tmel = xsd_main_doc->createElement( X("xsd:element") );
	attr = xsd_main_doc->createAttribute( X("name") );
	attr->setNodeValue( X("xmlbnmgate") );
	tmel->setAttributeNode( attr );
	services->appendChild( tmel );

	services = tmel;

	tmel = xsd_main_doc->createElement( X("xsd:complexType") );
	services->appendChild( tmel );
	sequence = xsd_main_doc->createElement( X("xsd:sequence") );
	tmel->appendChild( sequence );
	services = sequence;

	info = xsd_main_doc->createElement( X("xsd:element") );
	attr = xsd_main_doc->createAttribute( X("name") );
	attr->setNodeValue( X("info") );
	info->setAttributeNode( attr );
	services->appendChild( info );

	device = xsd_main_doc->createElement( X("xsd:element") );
	attr = xsd_main_doc->createAttribute( X("name") );
	attr->setNodeValue( X("devices") );
	device->setAttributeNode( attr );
	services->appendChild( device );

	tmel = xsd_main_doc->createElement( X("xsd:complexType") );
	device->appendChild( tmel );
	sequence = xsd_main_doc->createElement( X("xsd:sequence") );
	tmel->appendChild( sequence );

	xsd_main_devices = sequence;


}

/*
Zapsani informaci o device do hlavniho dokumentu
*/
void Mib2Xsd::create_device_element( SNMP_device *dev, DOMElement *r, bool similar )
{
	DOMElement *tmel;
	DOMElement *info;
	DOMElement *device;
	DOMElement *notifications;

	DOMElement *sequence;

	DOMAttr *attr;
	DOMText *txt;

	string tmpstr;
	char out[100];

	/*
	Nejprve XSD popis zarzeni
	*/
	device = xsd_main_doc->createElement( X("xsd:element") );
	attr = xsd_main_doc->createAttribute( X("name") );
	attr->setNodeValue( X("device") );
	device->setAttributeNode( attr );
	xsd_main_devices->appendChild( device );

	attr = xsd_main_doc->createAttribute( X("id") );
	sprintf( out, "%d", dev->id );
	attr->setNodeValue( X( out ) );
	device->setAttributeNode( attr );

	tmel = xsd_main_doc->createElement( X("xsd:complexType") );
	device->appendChild( tmel );
	sequence = xsd_main_doc->createElement( X("xsd:sequence") );
	tmel->appendChild( sequence );
	device = sequence;

	info = xsd_main_doc->createElement( X("xsd:element") );
	attr = xsd_main_doc->createAttribute( X("name") );
	attr->setNodeValue( X("info") );
	info->setAttributeNode( attr );
	device->appendChild( info );

	info = xsd_main_doc->createElement( X("xsd:element") );
	attr = xsd_main_doc->createAttribute( X("name") );
	attr->setNodeValue( X("name") );
	info->setAttributeNode( attr );
	device->appendChild( info );

	txt = xsd_main_doc->createTextNode( X( dev->name ) );
	info->appendChild( txt );

	info = xsd_main_doc->createElement( X("xsd:element") );
	attr = xsd_main_doc->createAttribute( X("name") );
	attr->setNodeValue( X("description") );
	info->setAttributeNode( attr );
	device->appendChild( info );

	txt = xsd_main_doc->createTextNode( X( dev->description ) );
	info->appendChild( txt );

	info = xsd_main_doc->createElement( X("xsd:element") );
	attr = xsd_main_doc->createAttribute( X("name") );
	attr->setNodeValue( X("data") );
	info->setAttributeNode( attr );
	device->appendChild( info );
	
	info = xsd_main_doc->createElement( X("xsd:element") );
	attr = xsd_main_doc->createAttribute( X("name") );
	attr->setNodeValue( X("notifications") );
	info->setAttributeNode( attr );
	device->appendChild( info );

	/*
	XML struktura pro interni potreby
	*/
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

	/*
	Zde nacteme znova celej dokument pomoci parseru
	a setDoNamespaces(true)
	Pouze neni-li stejny jako neco jineho
	*/
	//Nutno nejprve releasnout ten hlavni dokument

	if ( !similar )
	{
		doc->release();

		XercesDOMParser *conf_parser = new XercesDOMParser;

		conf_parser->setValidationScheme( XercesDOMParser::Val_Never );
		conf_parser->setDoNamespaces( true );
		conf_parser->setDoSchema( false );
		conf_parser->setLoadExternalDTD( false );

		
		sprintf( out, "%s%d.xsd", xsd_dir, dev->id );

		try {

			conf_parser->parse( out );

			DOMDocument *xmlDoc = conf_parser->getDocument();
			DOMElement *rootElem = xmlDoc->getDocumentElement();

			if ( !rootElem )
				throw (char *)"Empty config file.";

			/*
			Budovani XML casti Notifications
			*/
			 notifications =  create_xml_from_xsd( rootElem );
			 device->appendChild( notifications );

			//devices_root->push_back(rootElem);

			//Nyni muzeme dokument znicit, jiz jej nepotrebujeme.
			xmlDoc->release();
		}
		catch ( ... )
		{
			log_message( log_file, "Cannot parse the xsd file" );
		}

		device->appendChild( xml_root );
	}
	

}


/*
Konecne zapsani hlavniho dokumentu do souboru
*/
void Mib2Xsd::end_main_document()
{
	string str;

	str = xsd_dir;
	str += MAIN_XSD;

	output_xml2file( str.c_str(), xsd_main_doc );

	//xsd popis uz nebudeme potrebovat
	xsd_main_doc->release();

}


/*
Create XML from XSD!!!!
*/
DOMElement * Mib2Xsd::create_xml_from_xsd( DOMElement *xsd_root )
{
	DOMDocument *doc = main_doc;

	DOMNode *node;

	DOMElement *element;
	DOMElement *notification;

	DOMNodeList *children;

	string tmp_str;

	/*
	Vytvarime Xalan Document na xpath dotazy na typy
	*/
	XercesDOMSupport		theDOMSupport;
	XercesParserLiaison	*theLiaison;
	XalanDocument *theDocument;

	theLiaison = new XercesParserLiaison( theDOMSupport );
	theDocument = theLiaison->createDocument( xsd_root->getOwnerDocument(), true, true, true );


	children = xsd_root->getElementsByTagName( X( "xsd:complexType" ) );

	//jediny dite je hlavni "iso" element
	for ( unsigned int a = 0; a < children->getLength(); a++ )
	{
		node = children->item( a );
		element = dynamic_cast<DOMElement *>(node);
		
		if ( XMLString::equals( element->getAttribute( X("name") ), X("MainNotificationType") ) )
			break;
	}

	//nyni zahajime rekurzivni budovani xml stromu
	notification = doc->createElement( X( "notifications") );

	children = element->getChildNodes();
	for ( unsigned int i=0; i < children->getLength(); i++ )
	{
		node = children->item( i );
		if ( node->getNodeType() && node->getNodeType() == DOMNode::ELEMENT_NODE )
		{
			DOMElement *tmel = dynamic_cast<DOMElement *>(node);
			create_xml_element( tmel, notification, theDocument );
		}
	}

	delete( theLiaison );

	return notification;

}

/*
Vytvori element a pripoji jej do XML stromu
*/
void Mib2Xsd::create_xml_element( const DOMElement *xsd, DOMElement *xml, XalanDocument *theDoc )
{
	DOMDocument *doc = main_doc;

	const DOMElement *type_el;
	DOMElement *new_xml;
	DOMElement *xtmp;

	DOMAttr *attr;

	DOMNodeList *children;
	DOMNode *node;

	string tmp_str;
	const XMLCh* value;


	if ( XMLString::equals( xsd->getTagName(), X( "oid" ) ) )
	{
		attr = doc->createAttribute( X( "oid" ) );
		attr->setNodeValue( xsd->getTextContent() );
		xml->setAttributeNode( attr );
	}
	else if ( XMLString::equals( xsd->getTagName(), X( "access" ) ) )
	{
		attr = doc->createAttribute( X( "access" ) );
		attr->setNodeValue( xsd->getTextContent() );
		xml->setAttributeNode( attr );
	}
	else if ( XMLString::equals( xsd->getTagName(), X( "xsd:restriction" ) ) )
	{
		attr = doc->createAttribute( X( "restriction" ) );
		attr->setNodeValue( xsd->getAttribute( X("base") ) );
		xml->setAttributeNode( attr );
	}
	else if ( XMLString::equals( xsd->getTagName(), X( "variable" ) ) )
	{
		xtmp = doc->createElement( xsd->getTextContent() );

		attr = doc->createAttribute( X( "oid" ) );
		attr->setNodeValue( xsd->getAttribute( X("oid") ) );
		xtmp->setAttributeNode( attr );

		xml->appendChild( xtmp );
	}
	else if ( XMLString::equals( xsd->getTagName(), X( "xsd:element" ) ) )
	{
		
		//Nejprve vytvorime novy element
		new_xml = doc->createElement( xsd->getAttribute( X( "name" ) ) );
		xml->appendChild( new_xml );

		/*
		Jestli nema deti, tak zkusime typ.
		Paklize je typ, tak to uz se projede samo
		*/
		value = xsd->getAttribute( X( "type" ) );

		if ( !XMLString::equals( value, X("") ) )
		{
			//process type
			type_el = find_xsd_type( theDoc, value );

			if ( type_el != NULL )
			{
				create_xml_element( type_el, new_xml, theDoc );
			}
		}
		else
		{
			children = xsd->getChildNodes();
			for ( unsigned int i = 0; i < children->getLength(); i++ )
			{
				node = children->item( i );
				if ( node->getNodeType() && node->getNodeType() == DOMNode::ELEMENT_NODE )
				{
					DOMElement *tmel = dynamic_cast<DOMElement *>(node);

					//rekurzivni fce
					create_xml_element( tmel, new_xml, theDoc );
				}

			}
		}


	}
	else
	{
		/*
		Paklize je to cokoliv jineho, tak akorat projdeme o uroven nize
		*/
		//Process children
		children = xsd->getChildNodes();
		for ( unsigned int i = 0; i < children->getLength(); i++ )
		{
			node = children->item( i );
			if ( node->getNodeType() && node->getNodeType() == DOMNode::ELEMENT_NODE )
			{
				DOMElement *tmel = dynamic_cast<DOMElement *>(node);

				//rekurzivni fce
				create_xml_element( tmel, xml, theDoc );
			}

		}
	}

}


/*
Nalezne xsd typ v XSD dokumentu a vrati tento element zpet ke zpracovani
*/
const DOMElement* Mib2Xsd::find_xsd_type( XalanDocument *theDocument, const XMLCh* name )
{
	const DOMElement *ret_elm;

	XercesDOMSupport				theDOMSupport;
	XalanDocumentPrefixResolver		thePrefixResolver(theDocument);
	XPathEvaluator					theEvaluator;
	XalanNode*						theContextNode;

	if ( theDocument == NULL )
	{
		log_message( log_file, "Document is NULL. WTF???" );
		return NULL;
	}

	try {
	theContextNode = theDocument->getDocumentElement();

	if ( theContextNode == NULL )
		return NULL;
	
	XalanDOMString search = XalanDOMString("xsd:simpleType[@name='");
	search  += XalanDOMString( name );
	search	+= XalanDOMString("']");


	//Vyhledani daneho elementu
	 const XObjectPtr result( theEvaluator.evaluate(
				   theDOMSupport,        // DOMSupport
				  theContextNode,       // context node
				  search.c_str(),  // XPath expr
				   thePrefixResolver));     // Namespace resolver

	const NodeRefListBase&        nodeset = result->nodeset( );

	if ( nodeset.getLength() == 1 )
	{
		/*
		Nasli jsme pomoci XPath vyrazu dany element
		*/
		XalanNode *n = nodeset.item(0);

		if ( n->getNodeType() && n->getNodeType() != XalanNode::ELEMENT_NODE)
		{
			log_message( log_file, "Cannot get the element node" );
			return ret_elm;
		}

		//Z XalanElementu musime udelat DOMElement a ten vratit
		XalanElement *elm = dynamic_cast<XalanElement *>(n);
		XercesElementWrapper *wrap = dynamic_cast<XercesElementWrapper *>(elm);
		ret_elm = wrap->getXercesNode();

		return ret_elm;
	}
	else
	{
		XalanDOMString search2 = XalanDOMString("xsd:complexType[@name='");
		search2  += XalanDOMString( name );
		search2	+= XalanDOMString("']");


		//Vyhledani daneho elementu
		 const XObjectPtr result2( theEvaluator.evaluate(
					   theDOMSupport,        // DOMSupport
					  theContextNode,       // context node
					  search2.c_str(),  // XPath expr
					   thePrefixResolver));     // Namespace resolver

		const NodeRefListBase&        nodeset2 = result2->nodeset( );
		if ( nodeset2.getLength() == 1 )
		{
			/*
			Nasli jsme pomoci XPath vyrazu dany element
			*/
			XalanNode *n = nodeset2.item(0);

			if ( n->getNodeType() && n->getNodeType() != XalanNode::ELEMENT_NODE)
			{
				log_message( log_file, "Cannot get the element node" );
				return ret_elm;
			}

			//Z XalanElementu musime udelat DOMElement a ten vratit
			XalanElement *elm = dynamic_cast<XalanElement *>(n);
			XercesElementWrapper *wrap = dynamic_cast<XercesElementWrapper *>(elm);
			ret_elm = wrap->getXercesNode();

			return ret_elm;
		}
	}

	}
	catch ( ... )
	{
		log_message( log_file, "Exception while finding node" );
	}

	return NULL;

}
