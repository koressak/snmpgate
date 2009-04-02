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

		//pro budovani xml dokumentu z XSD 
		DOMElement *xml_root;

		DOMDocument *xsd_main_doc;
		DOMElement *xsd_main_root;
		DOMElement *xsd_main_devices;


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
		Mib2Xsd( char *, list<DOMElement*>* );
		~Mib2Xsd();

		void set_dirs( char *, char * );
		void parse_device_mib( SNMP_device *dev );

		string get_current_oid();
		string get_element_oid( char* );
		string get_node_type( struct tree* );
		void create_docum_tag( struct tree*, DOMElement* );
		void create_main_notif_element();
		void add_notif_element( string );
		void write_object_group( struct tree*, DOMElement*, DOMElement * );
		void write_object_identity( struct tree*, DOMElement*, DOMElement * );
		void write_notif_group( struct tree*, DOMElement*, DOMElement * );
		list<DOMElement*> write_complex_type( struct tree*, DOMElement*, DOMElement * );
		void write_notif_type( struct tree*, DOMElement*, DOMElement * );
		void write_annotation_part( struct tree*, DOMElement*, DOMElement * );
		void write_typeof_simple( struct tree*, DOMElement * );
		void write_simple_type( struct tree*, DOMElement*, DOMElement* );
		void write_sequence( struct tree *, DOMElement *);
		void write_sequence_of( struct tree*, DOMElement*, DOMElement * );
		void parse_node( struct tree*, DOMElement*, DOMElement * );
		static void output_xml2file( const char*, DOMDocument* );
		void write_header( DOMElement* );

		/*
		Vytvoreni hlavniho dokumentu pro celou gate 
		*/
		void create_main_document();
		void create_device_element( SNMP_device *, DOMElement* );
		void end_main_document();

		/*
		Vytvoreni XML dokumentu z XSD
		*/
		DOMElement* create_xml_from_xsd( DOMElement * );
		void create_xml_element( const DOMElement *, DOMElement *, XalanDocument * );
		const DOMElement* find_xsd_type( XalanDocument *, const XMLCh* );
		DOMNodeList *get_child_elements( DOMElement * );
		//TODO: nova fce na pridani pointeru na data mezi souhlasnymi devices

		DOMDocument* get_main_doc()
		{
			return main_doc;
		}

		DOMElement* get_main_root()
		{
			return main_root;
		}

};

#endif
