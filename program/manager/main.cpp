#include <stdio.h>
#include <string.h>
#include <iostream>

#include <curl/curl.h>
#include <curl/types.h>
#include <curl/easy.h>

using namespace std;

struct MemoryStruct {
	char *memory;
	size_t size;

	MemoryStruct()
	{
		memory = NULL;
		size = 0;
	}
};

static char errorBuffer[CURL_ERROR_SIZE];

static size_t write_data( void *ptr, size_t size, size_t nmemb, void *data)
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = ( struct MemoryStruct *) data;
	
	mem->memory = new char[ mem->size + 1 + realsize ];

	if ( mem->memory )
	{
		memcpy( &(mem->memory[mem->size]), ptr, realsize );
		mem->size += realsize;
		mem->memory[mem->size] = 0;
    }
	
	//cout << (char *)ptr <<endl;
	return realsize;

}

const char *msg = "<message password=\"private\"><set msgid=\"1\" objectId=\"0\"><xpath>xsd:element[@name='iso']//xsd:element[@name='org']//xsd:element[@name='dod']//xsd:element[@name='internet']//xsd:element[@name='mgmt']//xsd:element[@name='mib-2']//xsd:element[@name='system']//xsd:element[@name='sysContact']</xpath><value>AAAA</value></set></message>";


int main(int argc, char *argv[])
{
	CURL *curl;
	CURLcode res;

	struct curl_httppost *formpost=NULL;
	struct curl_httppost *lastptr=NULL;
  struct curl_slist *headerlist=NULL;
  static const char buf[] = "Expect:";

	struct MemoryStruct response;

	curl_global_init(CURL_GLOBAL_ALL);

	curl_formadd( &formpost, &lastptr,
					CURLFORM_COPYNAME, "selection",
					CURLFORM_COPYCONTENTS, msg,
					CURLFORM_CONTENTTYPE,"text/xml",
					CURLFORM_END);

	curl = curl_easy_init();
  headerlist = curl_slist_append(headerlist, buf);

	if(curl) {
		/* what URL that receives this POST */
		curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8888");
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
      curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

		curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, write_data );
		curl_easy_setopt( curl, CURLOPT_WRITEDATA, (void *) &response );

		curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
		curl_easy_perform(curl);

		/* always cleanup */
		curl_easy_cleanup(curl);


		/*
		now we output data in respose
		*/
		cout << response.memory << endl;
		cout << "End of response------"<<endl;
    curl_slist_free_all (headerlist);

		/* then cleanup the formpost chain */
		curl_formfree(formpost);
	}
	return 0;
}
