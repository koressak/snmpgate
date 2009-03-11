#include <iostream>
#include <stdio.h>
#include <fstream>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

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
#include <xercesc/framework/LocalFileFormatTarget.hpp>
#include <list>

using namespace std;
using namespace xercesc;

class XStr
{
public :
    // -----------------------------------------------------------------------
    //  Constructors and Destructor
    // -----------------------------------------------------------------------
    XStr(const char* const toTranscode)
    {
        // Call the private transcoding method
        fUnicodeForm = XMLString::transcode(toTranscode);
    }

    ~XStr()
    {
        XMLString::release(&fUnicodeForm);
    }


    // -----------------------------------------------------------------------
    //  Getter methods
    // -----------------------------------------------------------------------
    const XMLCh* unicodeForm() const
    {
        return fUnicodeForm;
    }

private :
    // -----------------------------------------------------------------------
    //  Private data members
    //
    //  fUnicodeForm
    //      This is the Unicode XMLCh format of the string.
    // -----------------------------------------------------------------------
    XMLCh*   fUnicodeForm;
};

#define X(str) XStr(str).unicodeForm()

DOMDocument *doc;
DOMElement *root;

string el_name;
list<string> curr_oid;

/*
Doda do annotation element appinfo s oid elementem
*/
void write_appinfo( struct tree* node, DOMElement *elem )
{
	DOMElement *app = doc->createElement( X("xsd:appinfo") );
	DOMElement *oid = doc->createElement( X("oid") );

	string tmp = "";
	for ( list<string>::iterator it = curr_oid.begin(); it != curr_oid.end(); it++)
	{
		tmp += *it;
		it++;
		if ( it != curr_oid.end() )
			tmp += ".";
		it--;
	}
	DOMText *txt = doc->createTextNode( X(tmp.c_str()) );

	elem->appendChild(app);
	app->appendChild(oid);
	oid->appendChild(txt);
}


/*
Parsuje node - dle toho zapise do xsd schematu dane veci
*/
void parse_node( struct tree* node, int level, DOMElement *parent )
{
	//if (level == 10) 
	//	return;

	/*
	TODO:
	Check na type a access nodu:
		type=0, access =0 -> bez typu, (iso, org, internet)
		type=0, access = 21 -> sequence of
			-> 1 child!!
			- type 0, access 21

		type x -> simple type, generujeme typ a element

		Notification types - zvlast, az bude fakat cely tohleto svinstvo
	*/

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
			write_appinfo( node, tmel );

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
			/*
			TODO
			check na pouze 1 dite, jinak error
			to dite jest sequence -> vlastni fce
			*/
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
		//asi simple type
		/*
		TODO
		vynechat MODID, group, objid apod

		ALE nutno pak reagovat na notification, trap plus group
		*/
		switch( node->type )
		{
			case TYPE_TRAPTYPE:
				ignore = true;
				cout << "trap type" << endl;
				break;
			case TYPE_NOTIFTYPE:
				cout << "Notif type" << endl;
				ignore = true;
				break;
			case TYPE_OBJGROUP:
				ignore = true;
				break;
			case TYPE_MODID:
				ignore = true;
				break;
			case TYPE_NOTIFGROUP:
				ignore = true;
				cout << "Notif type group " << endl;
				break;
			case TYPE_AGENTCAP:
				ignore = true;
				break;
			case TYPE_MODCOMP:
				ignore = true;
				break;
			case TYPE_OBJIDENTITY:
				ignore = true;
				break;
			default:
				ignore = false;
		}

		if (ignore)
		{
			curr_oid.pop_back();
			return;
		}

		//nyni predpokladame, ze mame simple type
		//nutno vytvorit element Typu a element popisujici zarazeni

		//zarazeni
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

		//TODO pridelat typ pod root element

		
	}


	children = node->child_list;

	while ( children != NULL && next_par != NULL)
	{
		parse_node ( children,  level+1, next_par);
		children = children->next_peer;
	}

	//pri returnu musime pop_back z curr_oid listu
	curr_oid.pop_back();
	
}

/*
Output whole schema to designated file
*/
void output_xml2file( char *file,  DOMDocument *doc )
{
	DOMImplementation *impl;
	DOMWriter *writer;
	XMLFormatTarget	*target;

	impl = DOMImplementationRegistry::getDOMImplementation( XMLString::transcode("LS") );

	writer = ((DOMImplementationLS*)impl)->createDOMWriter();

	writer->setFeature( XMLUni::fgDOMWRTFormatPrettyPrint, true );

	target = new LocalFileFormatTarget( XMLString::transcode(file) );

	writer->writeNode( target, *doc );
}


/*
Zapsani hlavicky dokumentu
*/
void write_header( DOMElement *root )
{
	DOMAttr *attr;

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
	Pridat annotation tag + notification blbce
	Create namespaces??
	*/

}

/*
TODO
pridat vstupni parametr device, ktery zrovna transformujeme
*/
void parse_mib( char * mib_dir )
{
	int mibs;
	struct tree *mib_tree;
	/*ofstream fout("./mib_t", ios_base::out );

	if ( !fout.is_open() )
	{
		cout << "Cannot open mib_t";
		exit(1);
	}*/

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

	//TODO
	//precist vsechny mib definovane v konfiguraku 
	//read mib - pro vsechny mib v listu device
	//mib_tree = read_mib("/var/run/snmpxmld/mib/Modem-MIB");

	//Parsovani celeho stromu
	mib_tree = get_tree_head();

	parse_node( mib_tree, 0, root );

	//END parsing

	//Output to file
	output_xml2file( (char *)"doc.xsd", doc );

	//fout.close();

	shutdown_mib();

	//Ukonceni prace s XML dokumentem. Nutno release
	doc->release();

}

int main( int argc, char **argv )
{
	try {
		XMLPlatformUtils::Initialize();
	}
	catch ( XMLException &e )
	{
		cerr << "Cannot initialize xml utils\n";
		exit (1);
	}

	parse_mib( (char *)"/var/run/snmpxmld/mib");

	try {
		XMLPlatformUtils::Terminate();
	}
	catch ( XMLException &e )
	{
		cerr << "Cannot terminate utils\n";
	}

	return 0;
}
