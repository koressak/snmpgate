#include <iostream>
#include <stdio.h>
#include <fstream>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

using namespace std;


void parse_node( struct tree* node, ofstream &out, int level )
{
	//if (level == 10) 
	//	return;

	int i;
	struct tree *children;

	for ( i=0; i < level; i++ )
		out << " ";
	
	out << node->label;
	out << " type: " << node->type;
	out << " subid: " << node->subid;
	out << " access: " << node->access;
	out << endl;

	children = node->child_list;

	while ( children != NULL )
	{
		parse_node ( children, out, level+1);
		children = children->next_peer;
	}
	
}

/*
TODO
pridat vstupni parametr device, ktery zrovna transformujeme
*/
void parse_mib( char * mib_dir )
{
	int mibs;
	struct tree *mib_tree;
	ofstream fout("./mib_t", ios_base::out );

	if ( !fout.is_open() )
	{
		cout << "Cannot open mib_t";
		exit(1);
	}


	mibs = add_mibdir(mib_dir);
	cout << "Pocet MIB v adresari: " << mibs << endl;

	init_mib();

	//read mib - pro vsechny mib v listu device
	//mib_tree = read_mib("/var/run/snmpxmld/mib/Modem-MIB");

	//Parsovani celeho stromu
	mib_tree = get_tree_head();

	parse_node( mib_tree, fout, 0 );

	fout.close();

	shutdown_mib();

}

int main( int argc, char **argv )
{
	parse_mib( (char *)"/var/run/snmpxmld/mib");

	return 0;
}
