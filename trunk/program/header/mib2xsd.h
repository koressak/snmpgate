#ifndef _MIB2XSD_H_
#define _MIB2XSD_H_

#include "definitions.h"

/*
Pomocna trida pro prevod XML stringu na char *
*/
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


/*
Hlavni formatovaci trida
*/
class Mib2Xsd
{
	private:
		//hlavni dokuemnt
		DOMDocument *main_doc;
		DOMElement *main_root;
		DOMElement *main_devices;

		//pracovni pro jednotlive zarizeni
		DOMDocument *doc;
		DOMElement *root;

		//Notifications elements
		DOMElement *main_notif;
		DOMElement *main_notif_seq;

		//pomocne promenne
		string el_name;
		list<string> curr_oid;

		//snmp mib
		char *mib_dir;
		char *xsd_dir;

		//log
		char *log_file;

		//seznam dokumentu pro devices
		list<DOMElement *> *devices_root;
	
	public:
		Mib2Xsd( DOMDocument*, DOMElement*, char *, list<DOMElement*>* );
		~Mib2Xsd();

		void set_dirs( char *, char * );
		void parse_device_mib( SNMP_device *dev );

		string get_current_oid();
		string get_element_oid( char* );
		string get_node_type( struct tree* );
		void create_docum_tag( struct tree*, DOMElement* );
		void create_main_notif_element();
		void add_notif_element( string );
		void write_object_group( struct tree*, DOMElement* );
		void write_object_identity( struct tree*, DOMElement* );
		void write_notif_group( struct tree*, DOMElement* );
		DOMElement* write_complex_type( struct tree*, DOMElement* );
		void write_notif_type( struct tree*, DOMElement* );
		void write_annotation_part( struct tree*, DOMElement* );
		void write_typeof_simple( struct tree* );
		void write_simple_type( struct tree*, DOMElement* );
		void write_sequence( struct tree *);
		void write_sequence_of( struct tree*, DOMElement* );
		void parse_node( struct tree*, DOMElement* );
		void output_xml2file( const char*, DOMDocument* );
		void write_header( DOMElement* );

		/*
		Vytvoreni hlavniho dokumentu pro celou gate 
		*/
		void create_main_document();
		void create_device_element( SNMP_device *, DOMElement* );
		void end_main_document();

};

#endif
