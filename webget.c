/*--------------------------------------------------------------------------------
        Multi-threaded Website download tests,
	Using Curl
        Samknows.com Author: Raghu P.V- 03 Feb 2011 - Version 1.15
	History:
	03    Febuary    2011 - Version 1.15 fail the test if xfertime is <=0 
	07    January    2011 - Version 1.14 Fix for parsing of link tag for css files 
	16    November   2010 - Version 1.13 PREFETCH_LOOKUP now applied to baseurl as well 
	10    November   2010 - Version 1.12 Check for 302 code if resource is not followed
	08    November   2010 - Version 1.11 Reduce the timeout for all threads to be
					     ready by 1 sec with 3 retries.
	21    October    2010 - Version 1.10 Ignore badly forms URL resources
	28    September  2010 - Version 1.9 Fail the entire test if any of the objects
				            fail to download or on connection timeouts
	24    September  2010 - Version 1.8 Supports CONNECT_TIMEOUT & TRANSFER_TIMEOUT
	18    September  2010 - Version 1.7 MAX URLs that can be serviced is 200
	10    September  2010 - Version 1.6 Sanity check for bytes,time and speed > 0
	27    August  2010    - Version 1.5 Ignore long URLs of len > 2048
	26    August  2010    - Version 1.4 webget ordering
	23    August  2010    - Version 1.3 Fixed hang issue when tag's attribute value
					 spans mutiple lines 
	18    August  2010    - Version 1.2 SIGALRM is catch and handled so that 
					 any illegal timeouts(<0) dont cause 
					 segmentation faults 
	03    August  2010    - Version 1.1 Upon 301 Redirects the correct contents
					 of html in gzip format in written to file  
					 Also any previous connections made in 
					 main() thread would be reused in child 
					 threads
	28    July    2010    - Version 1.0 Initial Version of Multi-threaded 
                                       Website download tests
----------------------------------------------------------------------------------*/
#define _GNU_SOURCE //added by steffie to remove warning: implicit declaration of strcasestr()
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <pthread.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
/*#include <signal.h> */
#include <curl/curl.h>
//#include <curl/types.h>
#include <curl/easy.h>
#include <errno.h>
/*added by steffie*/
#include <ctype.h> //to remove warning: implicit declaration of isspace()
#include <getopt.h>

/*
 * TEST STRING ID
 */
#define STR(x) #x
#define XSTR(x) STR(x)
#define WEBGETSTRID "WEBGETMT"
#define TESTVERSION "0.2"
#define WEBGETVERSION "WEBGET.0.2"
/*#define SIMWEBVERSION "0.0" */
#define SIMWEBVERSION "SIMWEB.1" //added lock on files while writing; removed WEBGET o/p 


/*
 * IP Version strings and environment variables
 */
int ipfamily = 0;
#define IPSTRING ((AF_INET6 == ipfamily) ? "6" : "")

int IPVERSIONREQUIRED = 0;
/* 0 any version
 * 4 only IPv4
 * 6 only IPv6
 */

char ipvstring[2] = "";


/*
 * DEBUG MACRO 
*/
#ifdef DEBUG
#define DEBUGP(x, args ...) fprintf(stderr, "[%s,%s():%u]: " \
x, __FILE__, __FUNCTION__ ,__LINE__, ## args)
#else
#define DEBUGP(x, args ...)
#endif


/* SK_DEBUG MACRO, allows to specify the debug level for which the message needs to be displayed*/
 
#define SK_DEBUGP(debug_level,x, args ...) if( SK_WEBGET_DEBUG == debug_level) fprintf(stderr, "SK_DEBUG[%d][%s,%s():%u]: "x,debug_level, __FILE__, __FUNCTION__ ,__LINE__, ## args)



# define ISSPACE(x) isspace (x)

#define FORWARD(p) do {                         \
  ++p;                                          \
  if (p >= end)                                 \
    goto finish;                                \
} while (0)

/* Skip whitespace, if any. */

#define SKIPWS(p) do {                         \
  while (ISSPACE (*p)) {                        \
    FORWARD (p);                                \
  }                                             \
} while (0)

/* Skip non-whitespace, if any. */


#define NAMEISCHAR(x) ((x) > 32 && (x) < 127                           \
                        && (x) != '=' && (x) != '>' && (x) != '/')


struct tagdata
{
	char *tagname;
	char *attrname;
};

#define TAGARRAYELEMENTS 5  //changed by steffie from 4 to 5
struct tagdata tagarray[] = 
{
	{"img", "src"},
  {"img", "data-src"}, //added by steffie
	{"script", "src"},
	{"link", "rel"},
	{"link", "href"}
};

struct MemoryStruct
{
	char *memory;
	size_t size;
};

/*typedef struct
{
	int threadnum;
	pthread_t *threadid;
	CURL *curl;
}THREADARGS;
*/

/*#define MAXURL 1000*/
#define MAXURLLEN 2048

#define IPADDRLEN 50
struct threadargs
{
	int threadnum;
	pthread_t *threadid;
	CURL *curl;
	char prevurl[MAXURLLEN];
}__attribute__((__packed__));

typedef struct threadargs  THREADARGS;

typedef struct
{
	double bytes;
	double time;
	double speed;
	int fail;
	char name[MAXURLLEN];
	double resolution_time;
	double connection_time;
	double start_time;
	int new_conn;
	char str_ip[IPADDRLEN];
  char *obj_typ; //added by steffie
  long http_resp_code; //added by steffie
  CURLcode curl_resp_code; //added by steffie
}OBJECTSTATS;
typedef struct
{
	char name[MAXURLLEN];
	char path[MAXURLLEN];
	int serviced;
	int name_serviced;
}URLINFO;

typedef struct
{
	char name[MAXURLLEN];
}DOMAIN;

typedef struct
{
	char name[200];
	char ipaddress[IPADDRLEN];
	double resolution_time;
}IPADD;


typedef	void *(*PF)(void *);

/* abc:char hostname[MAXURLLEN];*/
char *hostname = NULL;
OBJECTSTATS *objstats = NULL;
IPADD *IPADDRESS = NULL;
/*DOMAIN servers[MAXURL];*/

int cururlelem = 0;
int topurlelem = 0;
int topipaddress = 0;
//char URLS[MAXURL][MAXURLLEN];
URLINFO *URLS = NULL;
char targeturl[MAXURLLEN];
int  fail = 0;
int readyfordatatransfer = 0;
int curstreams = 1;
int objcount = 0;
struct MemoryStruct chunk;
/*int nservers = 0;*/

//const int MAXIMUM_STREAMS = 10;
#define MAXIMUM_STREAMS  10
//THREADARGS ta[MAXIMUM_STREAMS];
double	totalbytes = 0;
double	totalspeed = 0;
double	totaltime = 0;
double	totalthreadbytes = 0;
double	totalthreadspeed = 0;
double	totalthreadtime = 0;
double  threadxfertime = 0;
long CONNECT_TIMEOUT = 10L;
long DNS_CACHE_TIMEOUT = 0L;
long TRANSFER_TIMEOUT = 30L;
/*
 * libcurl CURLOPT_MAXCONNECTS sets the number that will be the 
 * persistent connection cache size
 */
long MAXCONNECTS = 5L;
long DNS_USE_GLOBAL_CACHE = 0L;
int SIMWEB_L=0;
int SK_WEBGET_DEBUG = 0;
int NO_CACHE_CONTROL = 0;
int PREFETCH_NAMELOOKUP = 0;
int USE_USER_AGENT = 0;
char USER_AGENT[100];
int USE_ACCEPT_ENCODING = 0;
char ACCEPT_ENCODING[100];
int PRINT_OBJECT_STATS = 0;
int FOLLOWLOCATION = 1;
const double secstousecs = 1e6;
int gzipcontentrecv = 0;

CURL* easyhandlearray[MAXIMUM_STREAMS];
int totaleasyhandles;

pthread_mutex_t xferreadymutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t xferdonemutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t xferinfomutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t urlmutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t objstatsmutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t failmutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t xferdonecond = PTHREAD_COND_INITIALIZER;

#define SETFAIL() pthread_mutex_lock(&failmutex); \
	fail = 1;				  \
   	DEBUGP("setting fail to 1\n"); \
	pthread_mutex_unlock(&failmutex)

struct curl_slist *headerlist=NULL;
/*
void catcher(int signo) 
{
	if(SK_WEBGET_DEBUG >= 1)
        	fprintf(stderr, "Ignoring SIGALRM\n" );
}
*/

/*added by steffie
 * Parses the command line arguments (Replaces the environment varuiables in
 * the webget to command line arguments).
 */

void print_usage_string(){
  const char usage_string[]  =
  "USAGE:"
  " .build/webget [OPTIONS] URL\n"
  "OPTIONS:\n"
  "-h, --help\t\t\t Display this help and exit\n"
  "-V, --version\t\t\t Output version information and exit\n"
  "-n, --number_threads\t\t Set number of threads (1 to 10); Default value is 1\n"
  "-d, --debug_level\t\t Set debug level (0 to 5); Default value is 0\n"
  "-s, --simweb\t\t\t Choose to use simweb; Not chosen by default \n"
  "-c, --no_cache_control\t\t Choose to set no cache control; Not chosen by default\n"
  "-o, --print_object_stats\t Choose to print object statistics; Not chosen by default\n"
  "-r, --on_redirect\t\t Set action on redirect (FOLLOW or SKIP or ROOTONLY); Default action is FOLLOW\n"
  "-a, --user_agent\t\t Set use agent; No default value set\n"
  "-e, --accept_encoding\t\t Set accept encoding; No default value set \n"
  "-t, --connect_timeout\t\t Set timeout (in seconds) for connection phase; Default value is 10\n"
  "-x, --transfer_timeout\t\t Set maximum time (in seconds) the request is allowed to take; Default value is 30\n"
  "-y, --dns_cache_timeout\t\t Set life-time (in seconds) for DNS cache entries; Default value is 0\n"
  "-m, --maxconnects\t\t Set maximum number of simultaneous open persistent connection libcurl may cache; Default value is 5\n"
  "-y, --dns_use_global_cache\t Set global DNS cache; Not enabled by default \n"
  "-p, --prefetch_namelookup\t Choose prefetch name lookup; Not chosen by default\n"
  "-i, --ipversion\t\t\t Choose IP version (4 or 6); Chooses the first IP end-point returned by getaddressinfo() by default\n";

  fprintf(stdout,"%s\n", usage_string);
}

char* parse_cmdline_args(int argc, char** const argv) {

  int                                 opt;
  char*                               shortopts = "hVn:d:scor:a:e:t:x:y:m:gpi:";
  static struct option                longopts[] = {
    { "help",                   no_argument,        NULL,           'h' },
    { "version",                no_argument,        NULL,           'V' },
    { "number_threads",         required_argument,  NULL,           'n' },
    { "debug_level",            required_argument,  NULL,           'd' },
    { "simweb",                 no_argument,        NULL,           's' },
    { "no_cache_control",       no_argument,        NULL,           'c' },
    { "print_object_stats",     no_argument,        NULL,           'o' },
    { "on_redirect",            required_argument,  NULL,           'r' },
    { "user_agent",             required_argument,  NULL,           'a' },
    { "accept_encoding",        required_argument,  NULL,           'e' },
    { "connect_timeout",        required_argument,  NULL,           't' },
    { "transfer_timeout",       required_argument,  NULL,           'x' },
    { "dns_cache_timeout",      required_argument,  NULL,           'y' },
    { "maxconnects",            required_argument,  NULL,           'm' },
    { "dns_use_global_cache",   no_argument,        NULL,           'g' },
    { "prefetch_namelookup",    no_argument,        NULL,           'p' },
    { "ipversion",              required_argument,  NULL,           'i' },
    {  NULL,                    0,                  NULL,            0  }
  };

  while ((opt = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    switch (opt) {
      case 'h': print_usage_string(); exit(EXIT_SUCCESS);

      case 'V': fprintf(stdout, "%s\n%s\n", WEBGETVERSION, SIMWEBVERSION);
                exit(EXIT_SUCCESS);

      case 'n': if(optarg) curstreams = atoi(optarg); break;

      case 'd':
                if(atoi(optarg) >= 0 && atoi(optarg) <= 5)
                  SK_WEBGET_DEBUG = atoi(optarg);
                else {
                  fprintf(stdout, "Valid -d/--debug_level options: (0-5)\n");
                  exit(EXIT_FAILURE);
                }
                break;

      case 's': SIMWEB_L = 1; break;

      case 'c': NO_CACHE_CONTROL = 1; break;

      case 'o': PRINT_OBJECT_STATS = 1; break;

      case 'r':
                if(strcasestr(optarg, "SKIP"))
                  FOLLOWLOCATION = 0;
                else if(strcasestr(optarg, "FOLLOW"))
                  FOLLOWLOCATION = 1;
                else if(strcasestr(optarg, "ROOTONLY"))
                  FOLLOWLOCATION = 2;
                else
                {
                  DEBUGP("Valid -r/--on_redirect options are FOLLOW/SKIP/ROOTONLY.\n");
                  exit(EXIT_FAILURE);
                }
                break;

      case 'a':
                memset(USER_AGENT, 0, 100);
		            USE_USER_AGENT = 1;
		            sprintf(USER_AGENT, "User-Agent: %s", optarg);
                break;

      case 'e':
                memset(ACCEPT_ENCODING, 0, 100);
                USE_ACCEPT_ENCODING = 1;
                sprintf(ACCEPT_ENCODING, "Accept-Encoding: %s", optarg);
                break;

      case 't': if(optarg) CONNECT_TIMEOUT = (long)atoi(optarg); break;

      case 'x': if(optarg) TRANSFER_TIMEOUT = (long)atoi(optarg); break;

      case 'y': if(optarg) DNS_CACHE_TIMEOUT = (long)atoi(optarg); break;

      case 'm': if(optarg) MAXCONNECTS = (long)atoi(optarg); break;

      case 'g': if(optarg) DNS_USE_GLOBAL_CACHE = (long)atoi(optarg); break;

      case 'p': if(optarg) PREFETCH_NAMELOOKUP = atoi(optarg); break;

      case 'i':
                if(atoi(optarg) == 4 || atoi(optarg) == 6)
                  IPVERSIONREQUIRED = atoi(optarg);
                else {
                  fprintf(stdout, "Valid -i/--ipversion options: 4 or 6\n");
                  exit(EXIT_FAILURE);
                }
                break;

      case ':': fprintf(stdout,"Missing argument\n"); exit(EXIT_FAILURE);

      default: exit(EXIT_FAILURE);
    }
  }

  if (argc != optind + 1){ 
    print_usage_string();
    exit(EXIT_FAILURE);
  }
  else {
   hostname = argv[optind];
  }
  return(hostname);
}

//end of 'added by steffie'


void skip(char *str)
{
	char *ptr1 = str, *ptr2 = str;

	while(*ptr1 != '\0')
	{
		if(*ptr1 == ' ' || *ptr1 == '\r' || *ptr1 == '\n')
		{
			++ptr1;
		}
		else
		{
			*ptr2 = *ptr1;
			++ptr1;
			++ptr2;
		}
	}
	*ptr2 = '\0';
	//printf("str = %s\n", str);
}

double difftimeval(struct timeval lhs, struct timeval rhs)
{
	double diff;
	diff = (lhs.tv_sec  - rhs.tv_sec) * secstousecs + (lhs.tv_usec - rhs.tv_usec);
	return diff;
}


char *get_ip_str(const struct sockaddr *sa, char *s, size_t maxlen)
{
	memset(s,0,maxlen);
	switch(sa->sa_family) {
		case AF_INET:
			inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),
			s, maxlen);
		break;
		case AF_INET6:
			s[0]='[';
			inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
			s+1, maxlen-2);
			strcat(s, "]");
		 break;

		default:
			strncpy(s, "Unknown AF", maxlen);
			return NULL;
	}
	return s;
}
int insertipaddress(char* name)
{
	int i = 0;
	struct timeval starttime;
	struct timeval endtime;
	double xfertime = 0;
	struct addrinfo hints, *gai_res;
	int gai_code;
	for(i=0; i < topipaddress; i++)
	{
		if(!strcmp(IPADDRESS[i].name, name))
		{
			SK_DEBUGP(1,"insertipaddress():: IP address %s already exists for Host %s \n",IPADDRESS[i].ipaddress, name );
			return 1;
		}
	}

	memset(&hints, 0, sizeof (struct addrinfo));
	if(IPVERSIONREQUIRED == 4){
		hints.ai_family = AF_INET;
	}else if(IPVERSIONREQUIRED ==6){
		hints.ai_family = AF_INET6;
	}
	hints.ai_socktype = SOCK_STREAM;
	gettimeofday(&starttime,NULL);
  //	struct hostent *hp = gethostbyname(name);
	gai_code = getaddrinfo(name, NULL, &hints, &gai_res);
	gettimeofday(&endtime, NULL);

  /*	if(!hp)
    {
      SK_DEBUGP(1, "gethostbyname name %s errno = %d \n", name, errno );
      return 0;
    }
  */
	if(0 != gai_code)
	{
		SK_DEBUGP(1, "getaddrinfo name %s: %s\n", name, gai_strerror(gai_code));
		return 0;
	}
  //added by steffie
  IPADDRESS = realloc(IPADDRESS,(topipaddress+1)*sizeof(IPADD));
  if(IPADDRESS == NULL){
    exit(EXIT_FAILURE);
  }
  else{//end of addition by steffie
    strcpy(IPADDRESS[topipaddress].name, name);

    SK_DEBUGP(1,"gethostbyname name %s \n", IPADDRESS[topipaddress].name );

    //	strcpy(IPADDRESS[topipaddress].ipaddress, inet_ntoa(*(struct in_addr *)hp->h_addr_list[0]));
    get_ip_str(gai_res->ai_addr, IPADDRESS[topipaddress].ipaddress, IPADDRLEN);
    SK_DEBUGP(1,"gethostbyname ip %s \n", IPADDRESS[topipaddress].ipaddress );
    if(gai_res->ai_family == AF_INET6)
      sprintf(ipvstring,"6");
    //inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr), IPADDRESS[topipaddress].ipaddress, INET_ADDRSTRLEN);
    xfertime = difftimeval(endtime, starttime);
    if(xfertime) 
    {
      xfertime /= 1000000;
    }	
    IPADDRESS[topipaddress].resolution_time = xfertime;

    ++topipaddress;
    return 1;
  }
}


char* lookupipaddress(char* name)
{
	int i = 0;
	for(i=0; i < topipaddress; i++)
	{
		if(!strcmp(IPADDRESS[i].name, name))
		{
			return(IPADDRESS[i].ipaddress);
		}
	}
	return NULL;
}


/* 
 * computes all the stats based on the objstats array
 * NOTE: IT IS NOT THREAD SAFE, IT IS MEANT TO BE USED WHEN ALL THREADS ARE TERMINATED
 */
#define ms(x)((long)( x * 1000000))
void report_stats(char *buff)
{
	int lookup_ctr = 0,conn_ctr = 0, reqex_ctr = 0, req_ctr = 0;
	double time_lookups = 0, lookup_min = 0, lookup_max = 0, lookup_avg = 0;
	double time_ttfbs = 0, ttfb_min = 0, ttfb_max = 0, ttfb_avg = 0;
	/*double time_conns = 0, conn_min = 0, conn_max =0, conn_avg = 0;*/
	double time_reqs = 0, req_min = 0, req_max =0, req_avg = 0;
	double conn_time = 0;
	int i = 0;

	if(0 == objcount)
	{
		return;
	}

	lookup_max = objstats[0].resolution_time;
	lookup_min = objstats[0].resolution_time;
	ttfb_min = objstats[0].start_time;
	ttfb_max = objstats[0].start_time;
	/*conn_min = objstats[0].connection_time;*/
	/*conn_max = objstats[0].connection_time;*/
	req_min = objstats[0].time - conn_time;
	req_max = objstats[0].time - conn_time;

	for(i = 0; i < objcount; ++i)
	{
		if(!objstats[i].fail)
		{
			double req_time = 0;
			++req_ctr;
			if(objstats[i].new_conn)
			{
				++conn_ctr;
				if(objstats[i].resolution_time > lookup_max)
				{
					lookup_max = objstats[i].resolution_time;
				}
				if(objstats[i].resolution_time < lookup_min)
				{				
					lookup_min = objstats[i].resolution_time;	
				}
				time_lookups += objstats[i].resolution_time;
				if(objstats[i].start_time > ttfb_max)
				{
					ttfb_max = objstats[i].start_time;
				}
				if(objstats[i].start_time < ttfb_min)
				{				
					ttfb_min = objstats[i].start_time;	
				}
				time_ttfbs += objstats[i].start_time;
			}
			else
			{
				++reqex_ctr;
			}
			req_time = objstats[i].time - objstats[i].connection_time;
			if(req_time > req_max)
			{
				req_max = req_time;
			}
			if(req_time < req_min)
			{				
				req_min = req_time;	
			}
			time_reqs += req_time;
		}
	}

	lookup_avg = time_lookups / conn_ctr;
	ttfb_avg = time_ttfbs / conn_ctr;
	req_avg = time_reqs / req_ctr;
	lookup_ctr = conn_ctr;

	if(PREFETCH_NAMELOOKUP)
	{
		if(0 == topipaddress)
		{
			return;
		}
		time_lookups = 0;
		lookup_max = IPADDRESS[0].resolution_time;
		lookup_min = IPADDRESS[0].resolution_time;
		for(i = 0; i < topipaddress; ++i)
		{
			if(IPADDRESS[i].resolution_time > lookup_max)
			{
				lookup_max = IPADDRESS[i].resolution_time;
			}
			if(IPADDRESS[i].resolution_time < lookup_min)
			{
				lookup_min = IPADDRESS[i].resolution_time;
			}
			time_lookups += IPADDRESS[i].resolution_time;
		}
		lookup_ctr = topipaddress;
		lookup_avg = time_lookups / lookup_ctr;

	}
	SK_DEBUGP(4,"Lookups %d time %lf min %lf max %lf avg %lf\n", lookup_ctr, time_lookups, lookup_min, lookup_max, lookup_avg);
	SK_DEBUGP(4,"Request using existing connection %d\n",reqex_ctr);
	SK_DEBUGP(4,"Time To First byte %d time %lf min %lf max %lf avg %lf\n", conn_ctr, time_ttfbs, ttfb_min, ttfb_max, ttfb_avg);
	SK_DEBUGP(4,"Reqests %d time %lf min %lf max %lf avg %lf\n", req_ctr, time_reqs, req_min, req_max, req_avg);
	sprintf(buff,"%d;%d;%d;%d;%ld;%ld;%ld;%ld;%ld;%ld;%ld;%ld;%ld;%ld;%ld;%ld",
		req_ctr, conn_ctr, reqex_ctr, lookup_ctr,
		ms(time_reqs),ms(req_min),ms(req_avg),ms(req_max),
		ms(time_ttfbs),ms(ttfb_min),ms(ttfb_avg),ms(ttfb_max),
		ms(time_lookups),ms(lookup_min),ms(lookup_avg),ms(lookup_max));
	SK_DEBUGP(1, "STATS;%s\n",buff);
}
/*
 * prints out the stats in an object
 */
void print_object_stats(OBJECTSTATS obj)
{
	fprintf(stdout,"OBJLONGSTAT: name %s fail %d time %lf bytes %lf type %s speed %lf lookuptime %lf connectiontime %lf starttime %lf newconnection %d url %s ip %s\n",
		obj.name, obj.fail, obj.time, obj.bytes, obj.obj_typ, obj.speed, obj.resolution_time, obj.connection_time, obj.start_time, obj.new_conn, obj.name, obj.str_ip);
}
/*
 * Retrieve all the needed information from a curl handler pointer after a 
 * succesfull transfer and converts the info in objectstat suitable format
 */


OBJECTSTATS get_object_stats(CURL *curl,CURLcode perform_res)
{
	OBJECTSTATS ret = {0};
	long num_conn = 0;
	CURLcode res;
	char *buff;
//curl version on 160nl is too old and does not support 
//CURLINFO_PRIMARY_IP option, hence we try to get server IP
//form the last used socket.
#if LIBCURL_VERSION_MAJOR > 7 || (LIBCURL_VERSION_MAJOR == 7 && LIBCURL_VERSION_MINOR >= 21) 
  ret.curl_resp_code = perform_res;

	res = curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP,&buff);
	if(CURLE_OK != res)
	{
		fprintf(stderr,"%s\n",curl_easy_strerror(res));
		exit(EXIT_FAILURE);
	}	
	strcpy(ret.str_ip,buff);
	/*fprintf(stdout,"%s\n",ret.str_ip);*/
#elif (LIBCURL_VERSION_MAJOR == 7 && LIBCURL_VERSION_MINOR >= 15) 
	long sock;
  res = curl_easy_getinfo(curl, CURLINFO_LASTSOCKET, &sock);
	if(CURLE_OK != res)
  {
    fprintf(stderr,"%s\n",curl_easy_strerror(res));
    exit(EXIT_FAILURE);
  }
	if(socket_to_ip(sock,ret.str_ip) == 0)
	{
		strcpy(ret.str_ip,"");
	}
#else
	strcpy(ret.str_ip,"");
#endif
  /*fprintf(stdout,"%s\n",ret.str_ip);*/
	res = curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &buff);
	if(CURLE_OK != res)
	{
		fprintf(stderr,"%s\n",curl_easy_strerror(res));
		exit(EXIT_FAILURE);
	}
	strcpy(ret.name, buff);
  /*fprintf(stdout, "url name: %s\n",ret.name);*/
	res = curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &(ret.bytes));
	if(CURLE_OK != res)
	{
		fprintf(stderr,"%s\n",curl_easy_strerror(res));
		exit(EXIT_FAILURE);
	}
  /*fprintf(stdout,"BYTES: %lf\n", ret.bytes);*/
  //added by steffie
	res = curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &buff);
	if(CURLE_OK != res)
	{
		fprintf(stderr,"%s\n",curl_easy_strerror(res));
		exit(EXIT_FAILURE);
	}
  if(buff == NULL){
		ret.obj_typ = strdup("");
  }
  else{
    /*strcpy(ret.obj_typ,buff);*/
    ret.obj_typ = strdup(buff);
  }
  /*fprintf(stdout,"CONTENT TYPE: %s\n", ret.obj_typ);*/
  //end of addition by steffie
	res = curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD, &(ret.speed));
	if(CURLE_OK != res)
	{
		fprintf(stderr,"%s\n",curl_easy_strerror(res));
		exit(EXIT_FAILURE);
	}
	res = curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &(ret.time));
	if(CURLE_OK != res)
	{
		fprintf(stderr,"%s\n",curl_easy_strerror(res));
		exit(EXIT_FAILURE);
	}
	  
	res = curl_easy_getinfo(curl,CURLINFO_NUM_CONNECTS, &num_conn);
	if(CURLE_OK != res)
	{
		fprintf(stderr,"%s\n",curl_easy_strerror(res));
		exit(EXIT_FAILURE);
	}
  //added by steffie
  
  res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &(ret.http_resp_code));
	if(CURLE_OK != res)
	{
		fprintf(stderr,"%s\n",curl_easy_strerror(res));
		exit(EXIT_FAILURE);
	}
  /*fprintf(stdout,"HTTP RESPONSE CODE: %ld\n", ret.http_resp_code);*/
  //end of addition by steffie
  //
	//it is a new connection if at least one connection has been initializied to achieve
	//the transfer
	ret.new_conn = (num_conn >  0);
	if(ret.new_conn)
	{
		res = curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &(ret.resolution_time));
		if(CURLE_OK != res)
		{
			fprintf(stderr,"%s\n",curl_easy_strerror(res));
			exit(EXIT_FAILURE);
		}
		res = curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &(ret.connection_time));
		if(CURLE_OK != res)
		{
			fprintf(stderr,"%s\n",curl_easy_strerror(res));
			exit(EXIT_FAILURE);
		}
	}
	res = curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &(ret.start_time));
	if(CURLE_OK != res)
	{
		fprintf(stderr,"%s\n",curl_easy_strerror(res));
		exit(EXIT_FAILURE);
	}
	if(CURLE_OK != perform_res)
	{
		ret.fail = 1;
	}
	if(!((ret.bytes > 0 && ret.time > 0)))
	{
		if(ret.time <= 0)
		{
			ret.time = 0;
			ret.fail = 1;
		}
		long httpcode = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpcode);
		if(!( FOLLOWLOCATION != 1 && httpcode == 302 ))
		{
			ret.fail = 1;
		}
	}
	return ret;
}


/*
 * given a valid socked descriptor and a char buffer
 * copies the ip address on the buffer 
 * returns 1 if success 0 otherwise
 */

int socket_to_ip(int sock, char *buff)
{
	socklen_t len;
	struct sockaddr_storage addr;
	len = sizeof(addr);
	if( getpeername(sock,(struct sockaddr*) &addr, &len) != 0)
	{
		return 0;	
	}
//	memset(&in, 0, sizeof(in));
//	in.s_addr = peeraddr.sin_addr.s_addr;
	if(addr.ss_family == AF_INET)
	{	
		struct sockaddr_in *s =(struct sockaddr_in *)&addr;
		inet_ntop(AF_INET, &s->sin_addr, buff, IPADDRLEN);
	}
	else
	{
		struct sockaddr_in6 *s = (struct sockaddr_in6 *) &addr;
		inet_ntop(AF_INET6, &s->sin6_addr, buff, IPADDRLEN);
	}
//	strcpy(buff, inet_ntoa(in));
//	strncpy(buff, address.sa_data, address_len);
	SK_DEBUGP(1,"Sock %d per ip %s\n", sock, buff);
	return 1;
}



void copy_url_name(OBJECTSTATS *obj_ptr, char *name)
{
	char *oname = obj_ptr->name;
	while(*name != '\0')
        {
                if(*name == ';')
                {
                        *oname = '%';
                        ++oname;
                        *oname = '3';
                        ++oname;
                        *oname = 'b';
                        ++oname;
                }
                else
                {
                        *oname = *name;
                        ++oname;
                }
                ++name;
        }
        *oname = '\0';
}

/*
 * copies an OBJECTSTATS struct in the objstats array in the right position
 */
void insert_object_struct(OBJECTSTATS obj)
{
      /*if( strncmp(obj.obj_typ, "text/html", 9)==0 && strcmp(hostname, obj.name)!=0 ){ //added by steffie: to remove html pags other than root html page*/
        /*[>fprintf(stdout, "%s : %s \n",obj.obj_typ, obj.name);<]*/
        /*return;*/
      /*}*/
      /*else{*/

        if(SK_WEBGET_DEBUG == 1){
          print_object_stats(obj);
        }
        //added by steffie
        objstats = realloc(objstats,(objcount+1)*sizeof(OBJECTSTATS));
        if(objstats == NULL){
          exit(EXIT_FAILURE);
        }
        else{//end of addition by steffie
          pthread_mutex_lock(&objstatsmutex);
          memcpy(&(objstats[objcount]), &obj, sizeof(OBJECTSTATS));
          ++objcount; 
          pthread_mutex_unlock(&objstatsmutex);
        }
      /*}*/
}

/*void insertobjstats(double bytes, double time, double speed, char *name, int failobj, double lookup_time, int new_conn)*/
/*{*/
	/*if(SK_WEBGET_DEBUG == 1)*/
	/*{*/
		/*fprintf(stderr, "insertobjstats():: objcount = %d bytes = %lf time = %lf speed = %lf name = %s lookup_time = %lf new_connection = %d\n", objcount, bytes, time, speed, name, lookup_time, new_conn);*/
	/*}*/
	/*pthread_mutex_lock(&objstatsmutex);*/
	/*objstats[objcount].bytes = bytes;*/
	/*objstats[objcount].time = time;*/
	/*objstats[objcount].speed = speed;*/
	/*objstats[objcount].fail = failobj;*/
	/*objstats[objcount].resolution_time = lookup_time;*/
	/*objstats[objcount].new_conn = new_conn;*/
	
	/*//strcpy(objstats[objcount].name, name);*/
	/*char *oname = objstats[objcount].name;*/
	/*while(*name != '\0')*/
	/*{*/
		/*if(*name == ';')*/
		/*{*/
			/**oname = '%';*/
			/*++oname;*/
			/**oname = '3';*/
			/*++oname;*/
			/**oname = 'b';*/
			/*++oname;*/
		/*}*/
		/*else*/
		/*{*/
			/**oname = *name;*/
			/*++oname;*/
		/*}*/
		/*++name;*/
	/*}*/
	/**oname = '\0';*/
	/*++objcount;*/
	/*pthread_mutex_unlock(&objstatsmutex);*/
/*}*/

void replacehostnamewithip(char *newurl, char *curipaddress, char *url)
{
	char *startptr, *endptr;

	memset(url, 0, MAXURLLEN);
	char *urlstartptr = url;

	startptr = newurl;
	endptr = strstr(newurl, "://");
        if(endptr != NULL)
	{
                endptr += strlen("://");
		strncpy(urlstartptr, newurl, endptr-startptr);
		urlstartptr += endptr-startptr;
	}
        else
                endptr = newurl;


	strcpy(urlstartptr, curipaddress);
	urlstartptr += strlen(curipaddress);

	while(*endptr != '\0' && *endptr != '/')
		++endptr;

	if(*endptr == '/')
		strcpy(urlstartptr, endptr);
	
	SK_DEBUGP(1,"replacehostnamewithip()::url(ip) =  %s url(name) = %s\n", url, newurl);
	
}

void inserturl()
{
	/*int iter = 0;*/ //commented by steffie
	/*int allowdomain = 0;*/ //commented by steffie
	if(*targeturl == '\"')
	{
		SK_DEBUGP(1,"inserturl():: Not Inserting Target Url = %s \n", targeturl);
	}
	char* hostnameptr1 = strstr(targeturl, "://");
        if(hostnameptr1 != NULL)
                hostnameptr1 += strlen("://");
        else
                hostnameptr1 = targeturl;

	/*char* hostnameptr2 = strstr(hostnameptr1, "www.");
        if(hostnameptr2 != NULL)
                hostnameptr2 += strlen("www.");
        else
                hostnameptr2 = hostnameptr1;*/
        char* hostnameptr2 = hostnameptr1;

	char* endptr = hostnameptr2;
	while(*endptr != '\0' && *endptr != '/')
		++endptr;

  //added by steffie
  URLS = realloc(URLS,(cururlelem+1)*sizeof(URLINFO));
  if(URLS == NULL){
    exit(EXIT_FAILURE);
  }
  else{//end of addition by steffie

    pthread_mutex_lock(&urlmutex);

    //commented by steffie to avoid limiting no of resources by MAXURL
    /*if(cururlelem >= MAXURL)*/
    /*{*/
      /*SK_DEBUGP(1,"inserturl():: Not adding Target Url = %s since cururlelem %d >= %d\n", targeturl, cururlelem, MAXURL);*/
      /*pthread_mutex_unlock(&urlmutex);*/
      /*return;*/
    /*}*/

    /*if(nservers)*/
    /*{*/
      /*for(iter = 0; iter < nservers; ++iter)*/
      /*{*/
        /*SK_DEBUGP(1,"inserturl() domain to check = %s targeturl = %s \n", servers[iter].name, targeturl);*/
        /*if(strstr(targeturl, servers[iter].name))*/
        /*{*/
          /*SK_DEBUGP(1,"Domain found name = %s \n", servers[iter].name);*/
          /*allowdomain = 1;*/
          /*break;*/
        /*}*/
      /*}*/
      /*if(!allowdomain)*/
      /*{*/
        /*SK_DEBUGP(1,"inserturl():: Not adding Target url %s not in allowed Domains\n", targeturl);*/
        /*pthread_mutex_unlock(&urlmutex);*/
        /*return;*/
      /*}*/
    /*}*/

    SK_DEBUGP(2,"Targeturl = %s \n", targeturl);
    SK_DEBUGP(2,"URLname = %s \n", hostnameptr2);
    strcpy(URLS[cururlelem].path, targeturl);
    strncpy(URLS[cururlelem].name, hostnameptr2, endptr-hostnameptr2);
    SK_DEBUGP(2,"New Targeturl = %s \n", URLS[cururlelem].path);
    SK_DEBUGP(2,"New URLname = %s \n\n", URLS[cururlelem].name);
    URLS[cururlelem].name[endptr - hostnameptr2 + 1] = '\0';
    URLS[cururlelem].serviced = 0;
    URLS[cururlelem].name_serviced = 0;
    if(PREFETCH_NAMELOOKUP)
    {
      if (!insertipaddress(URLS[cururlelem].name))
      {
        memset(URLS[cururlelem].path, 0, MAXURLLEN);
        memset(URLS[cururlelem].name, 0, MAXURLLEN);
        pthread_mutex_unlock(&urlmutex);
        return;
      }
    }
    ++cururlelem;
    pthread_mutex_unlock(&urlmutex);
  }
}
/*
 * Sets name_serviced=1 for all url with the same name in the URLS array
 */
void set_name_serviced(char *name)
{
	int i=0;
	for(i = 0; i < cururlelem; ++i)
	{
		if(!strcmp(name, URLS[i].name))
		{
			 URLS[i].name_serviced = 1;
		}
	} 
}
/*
 * get the index of the next url that needs to be requested
 * returns -1 if all urls have been already been serviced.
 * To be used inside the critical section
 */
int get_next_url() 
{
	int i = 0;
	int serviced = -1;

	for(i = 0; i < cururlelem; ++i)
	{
		if(URLS[i].serviced == 0 && serviced == -1)
		{
			serviced = i;
		}
		if(URLS[i].name_serviced == 0)
		{
			if(URLS[i].serviced == 0)
		  	{	
				return i;
			}
			else
			{
				SK_DEBUGP(1,"name serviced zero and serviced 1\n");
			}
		}
	}
	return serviced;
}

int pullurl(char *url, char *prevname, char *urlname)
{
	int i = 0;
	if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 2 || SK_WEBGET_DEBUG == 3)
		fprintf(stderr, "Inside pullurl() cururlelem = %d... \n", cururlelem);

	pthread_mutex_lock(&urlmutex);

	while(i < cururlelem && prevname)
	{
		//if(strstr(prevname, URLS[i].name))
		if(!strcmp(urlname, URLS[i].name))
		{
			if(URLS[i].serviced == 0)
			{
				if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 2 || SK_WEBGET_DEBUG == 3)
					fprintf(stderr, "pullurl():: fetching same hostname elem cururlelem = %d prevurl = %s cururl = %s\n", i, prevname, URLS[i].path);

				strcpy(url, URLS[i].path);
				strcpy(urlname, URLS[i].name);
				URLS[i].serviced = 1;
				if( URLS[i].name_serviced == 0)
				{
					SK_DEBUGP(5,"prevname %s name_serviced 0\n",prevname);
					set_name_serviced(urlname);
				}
				pthread_mutex_unlock(&urlmutex);
				return 1;
			}
		}
		++i;
	}

	if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 2 || SK_WEBGET_DEBUG == 3)
		fprintf(stderr, "pullurl():: searching new url element\n");

	i = 0;
	int new_url = get_next_url();
	if(new_url != -1)
/*	{
		while(i < cururlelem)
		{
			if(URLS[i].serviced == 0)
			{
				if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 2 || SK_WEBGET_DEBUG == 3)
					fprintf(stderr, "pullurl():: fetching new url cururlelem = %d prevurl = %s cururl = %s\n", i, prevname, URLS[i].path);
	
				strcpy(url, URLS[i].path);
				strcpy(urlname, URLS[i].name);
				URLS[i].serviced = 1;
				pthread_mutex_unlock(&urlmutex);
				return 1;
			}
			++i;
		}
	}
	else*/
	{
		strcpy(url, URLS[new_url].path);
		strcpy(urlname, URLS[new_url].name);
		URLS[new_url].serviced = 1;
		set_name_serviced(urlname);
		pthread_mutex_unlock(&urlmutex);
		return 1;
	}
	if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 2 || SK_WEBGET_DEBUG == 3)
		fprintf(stderr, "pullurl():: no urls cururlelem = %d prevurl = %s\n", i, prevname);
	pthread_mutex_unlock(&urlmutex);
	return 0;
/*
	if(SK_WEBGET_DEBUG == 2 || SK_WEBGET_DEBUG == 3)
		fprintf(stderr, "pullurl():: topurlelem = %d cururlelem = %d\n", topurlelem, cururlelem);
	if(topurlelem >= cururlelem)
	{
		pthread_mutex_unlock(&urlmutex);
		return 0;
	}
	strcpy(url, URLS[topurlelem]);
	++topurlelem;
*/
}


int skiptag(char *tagname)
{
	int i = 0;
	while(i < TAGARRAYELEMENTS)
	{
		if(!strcasecmp(tagarray[i].tagname, tagname))
		{
			return 0;
		}
		++i;
	}
	return 1;
}

int checkinterestingtag(char *tagname, char *attrname)
{
	int i = 0;
	while(i < TAGARRAYELEMENTS)
	{
		if(!strcasecmp(tagarray[i].tagname, tagname))
		{
			if(!strcasecmp(tagarray[i].attrname, attrname))
			{
				SK_DEBUGP(1,"checkinterestingtag():: Interested tag = %s attr = %s \n", tagname, attrname);
				return 1;
			}
		}
		++i;
	}
	return 0;
}


int generateabspath(char *host, char *attrvalue)
{
	SK_DEBUGP(3," generateabspath host= %s\n", host);
	SK_DEBUGP(3," generateabspath attr= %s\n", attrvalue);
	char *startptrhost = host;
	char *endptrhost = host + (strlen(host) - 1);

	char *startptrattr = attrvalue;
	char *endptrattr = attrvalue + (strlen(attrvalue));

	memset(targeturl, 0, MAXURLLEN);

	SK_DEBUGP(1,"Inside generateabspath() host = %s attrvalue = %s\n", host, attrvalue);
	while(isspace(*startptrhost))
	{
		++startptrhost;
		if(startptrhost > endptrhost)
			return 0;
	}

	//fprintf(stderr, "generateabspath() :: CP1 \n");
	while(isspace(*startptrattr))
	{
		++startptrattr;
		if(startptrattr > endptrattr)
			return 0;
	}

	if(*endptrhost == '/')
		--endptrhost;

	//fprintf(stderr, "generateabspath() :: CP2 \n");
	if(strstr(attrvalue, "http://") || strstr(attrvalue, "www.") || strstr(attrvalue, "https://") )
	{
		strcpy(targeturl, startptrattr);
		SK_DEBUGP(1, "ABS Target = %s \n", targeturl);
		return  1;
	}
	
	if(memcmp(startptrattr, "//", 2) == 0)
	{
		sprintf(targeturl, "http:%s", startptrattr);
		SK_DEBUGP(1, "ABS Target with implicit protocol = %s \n", targeturl);
		return  1;
	}

	//fprintf(stderr, "generateabspath() :: CP3 \n");
	if(*startptrattr == '.')
	{
		while((*startptrattr == '.') && (*(startptrattr + 1) == '.'))
		{
			while(*endptrhost != '/')
			{
				--endptrhost;
				if(endptrhost <= startptrhost)
					return 0;
			}

			if(*endptrhost == '/')
				--endptrhost;
			//fprintf(stderr, "generateabspath() :: CP3a \n");
			while(*startptrattr != '/')
			{
			//	fprintf(stderr, "generateabspath() :: CP3b \n");
				++startptrattr;
				if(startptrattr >= endptrattr)
					return 0;
			}
			++startptrattr;
			if(startptrattr >= endptrattr)
				return 0;
			//fprintf(stderr, "generateabspath() :: CP3c startptrattr = %c \n", *startptrattr);
		}
	}

	if(*startptrattr == '/')
		++startptrattr;

	//if(*endptrhost == '/' && (!(*(endptrhost - 1) == '/')))
	if(*endptrhost == '/')
		--endptrhost;

	//fprintf(stderr, "generateabspath() :: CP4 n = %d\n", endptrhost - startptrhost);
	strncpy(targeturl, startptrhost, endptrhost - startptrhost + 1);
	//fprintf(stderr, "generateabspath() :: CP5 \n");
	strcpy(targeturl + (endptrhost - startptrhost + 1), "/");
	strcpy(targeturl + (endptrhost - startptrhost + 2), startptrattr);
	SK_DEBUGP(1,"generateabspath():: targeturl = %s \n", targeturl);
	SK_DEBUGP(3," generateabspath trgt= %s\n", targeturl);
	return 1;
}

int checkdupurl()
{
	int i = 0;
	for(i = 0; i < cururlelem; ++i)
	{
		if(!strcasecmp(URLS[i].path, targeturl))
			return 1;
	}
	return 0;
}

static const char *
find_comment_end (const char *beg, const char *end)
{
  /* Open-coded Boyer-Moore search for "-->".  Examine the third char;
     if it's not '>' or '-', advance by three characters.  Otherwise,
     look at the preceding characters and try to find a match.  */

	const char *p = beg - 1;

	while ((p += 3) < end)
		switch (p[0])
		{
			case '>':
			if (p[-1] == '-' && p[-2] == '-')
				return p + 1;
			break;

			case '-':
			at_dash:
			if (p[-1] == '-')
			{
				at_dash_dash:
				if (++p == end) return NULL;
				switch (p[0])
				{
					case '>': return p + 1;
					case '-': goto at_dash_dash;
				}
			}
			else
          		{
				if ((p += 2) >= end) return NULL;
				switch (p[0])
				{
					case '>':
					if (p[-1] == '-')
						return p + 1;
					break;
					case '-':
						goto at_dash;
				}
			}
		}
	return NULL;
}

/*
 * returns the distance between endaddress and start address
 * if the distance is bigger than arraymaxlen the data will not fit
 * in the array placed in the stack, hence a debug string will be printed
 * and the program will exit
 */
int checkarraylen(const char *endaddress, const char *startaddress, int arraymaxlen)
{
	int len = endaddress - startaddress;
	if( len >= arraymaxlen)
	{
		fprintf(stderr,"invalid memory access\n");
		exit(EXIT_FAILURE);
	}
	return len;
}

#define MEMLEN 200
void scanhtmltags(char *text, int size)
{
	const char *p = text;
	const char *end = text + size;
	int end_tag;
	const char *tag_name_begin, *tag_name_end;
	const char *tag_start_position;
	int interestingtag;
	char tag[MEMLEN];
	char attr_name[MEMLEN], attr_value[MAXURLLEN], attr_raw_value[MAXURLLEN];
	char temphostname[MAXURLLEN];
  /*chat linkhrefarr[MAXURLLEN]; //commented out by steffie*/
	int extractstylesheet = 0, linkhref = 0;
	int parsetags = 1;
	int len =0;
	memset(tag, 0, MEMLEN);
	memset(attr_name, 0, MEMLEN);
	memset(attr_value, 0, MAXURLLEN);
	memset(attr_raw_value, 0, MAXURLLEN);
	memset(temphostname, 0, MAXURLLEN);
	strcpy(temphostname, hostname);
	if(temphostname[strlen(temphostname)-1] == '/')
		temphostname[strlen(temphostname)-1] = '\0';

	searchtag:
	interestingtag = 0;
	end_tag = 0;
	/*SK_DEBUGP(3,"before memchr\n");*/
	p = memchr(p, '<', end - p);
	
	/*SK_DEBUGP(3,"searchtag p = %p text %p end %p \n", p, text, end);*/

    	if (!p)
		goto finish;

	/*SK_DEBUGP(3, "after memchr\n");*/

	tag_start_position = p;
		FORWARD(p);

	if (*p == '!')
	{
		SK_DEBUGP(3, "before find_comment_end\n");
		if(p < end + 3 && p[1] == '-' && p[2] == '-')
		{
			const char *comment_end = find_comment_end(p + 3, end);
			if (comment_end)
              			p = comment_end;
		}
		SK_DEBUGP(3,"after find_comment_end\n");

		if (p >= end)
			goto finish;
		goto searchtag;
	}
	else if (*p == '/')
	{
		end_tag = 1;
		FORWARD (p);
	}

	tag_name_begin = p;
	while(NAMEISCHAR (*p))
		FORWARD(p);
	if (p == tag_name_begin)
	{
		SK_DEBUGP(3, "goto searchtag p = %p tag_name_begin = %p \n", p, tag_name_begin);
		goto searchtag;
	}
	tag_name_end = p;
	SKIPWS(p);

	if(end_tag && *p != '>')
	{
		SK_DEBUGP(3,"goto backout p = %p *p = %c \n", p, *p);
		goto backout;
	}
	
	len = checkarraylen(tag_name_end,tag_name_begin,MEMLEN);
	memcpy(tag, tag_name_begin,len);
	tag[len] = '\0';

	//if(!end_tag)
	//	printf("tag = %s \n", tag);
	SK_DEBUGP(3, "tag = %s \n", tag);


	if(skiptag(tag))
		goto searchtag;

	if(!strcasecmp(tag, "script"))
	{
		if(end_tag)
			parsetags = 1;
		else
			parsetags = 0;
	}
	else
	{
		if(!parsetags)
			goto searchtag;
	}



	extractstylesheet = 0;
	linkhref = 0;
	/* Find the attributes. */
	while (1)
	{
		const char *attr_name_begin, *attr_name_end;
		const char *attr_value_begin, *attr_value_end;
		const char *attr_raw_value_begin, *attr_raw_value_end;
		if (p >= end)
			goto finish;

		SK_DEBUGP(3, "inside while1 \n");
		SKIPWS (p);

		if (*p == '/')
		{
			parsetags = 1;
			FORWARD (p);
			SKIPWS (p);
			if (*p != '>')
				goto backout;
		}

		if (*p == '>')
			break;

		attr_name_begin = p;
		while (NAMEISCHAR (*p))
			FORWARD (p);
		attr_name_end = p;

		if (attr_name_begin == attr_name_end)
			goto backout;

		memset(attr_name, 0, MEMLEN);
		len = checkarraylen(attr_name_end,attr_name_begin, MEMLEN);
		memcpy(attr_name, attr_name_begin, len);
		attr_name[len] = '\0';

		SK_DEBUGP(3,"before check: interestingtag = %d  tag = %s attr_name = %s\n", interestingtag, tag, attr_name);
		interestingtag = checkinterestingtag(tag, attr_name);
		SK_DEBUGP(3,"interestingtag = %d  tag = %s attr_name = %s\n", interestingtag, tag, attr_name);
		
		

		SKIPWS (p);

		if (NAMEISCHAR (*p) || *p == '/' || *p == '>')
		{
			attr_raw_value_begin = attr_value_begin = attr_name_begin;
			attr_raw_value_end = attr_value_end = attr_name_end;
		}
		else if (*p == '=')
		{
			FORWARD (p);
			SKIPWS (p);
			if (*p == '\"' || *p == '\'')
			{
				int newline_seen = 0;
				char quote_char = *p;
				attr_raw_value_begin = p;
				FORWARD (p);
				attr_value_begin = p; 
				while (*p != quote_char)
				{
					if (!newline_seen && *p == '\n')
					{
						//p = attr_value_begin;
						//newline_seen = 0;
						FORWARD (p);
						newline_seen = 1;
						continue;
					}
					else if (newline_seen && *p == '>')
						break;
					FORWARD (p);
				}
				attr_value_end = p; 
				if (*p == quote_char)
					FORWARD (p);
				else
					goto searchtag;
				attr_raw_value_end = p; /* <foo bar="baz"> */
                                        /*               ^ */

				if( ((attr_raw_value_end - attr_raw_value_begin) > MAXURLLEN) || ((attr_value_end - attr_value_begin) > MAXURLLEN) )
				{
					if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 3)
					{
						fprintf(stderr, "Ignore too long attr_raw_len = %ld attr_len = %ld maxlen = %d \n", attr_raw_value_end - attr_raw_value_begin, attr_value_end - attr_value_begin, MAXURLLEN);
					}
					goto backout;
				}

				memset(attr_value, 0, MAXURLLEN);
				len = checkarraylen(attr_value_end, attr_value_begin, MAXURLLEN);
				memcpy(attr_value, attr_value_begin, len);
			
				attr_value[len] = '\0';

				//fprintf(stderr, "attr_value = %s\n", attr_value);

				memset(attr_raw_value, 0, MAXURLLEN);
				len = checkarraylen(attr_raw_value_end, attr_raw_value_begin, MAXURLLEN);
				memcpy(attr_raw_value, attr_raw_value_begin, len);
				attr_raw_value[len] = '\0';

				//fprintf(stderr, "attr_raw_value = %s\n", attr_value);

			}
			else
			{
				attr_value_begin = p;
				while (!ISSPACE (*p) && *p != '>')
					FORWARD (p);
				attr_value_end = p;
				if (attr_value_begin == attr_value_end)
					goto backout;
				attr_raw_value_begin = attr_value_begin;
				attr_raw_value_end = attr_value_end;

	
				if( ((attr_raw_value_end - attr_raw_value_begin) > MAXURLLEN) || ((attr_value_end - attr_value_begin) > MAXURLLEN) )
				{
					if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 3)
					{
						fprintf(stderr, "Ignore too long attr_raw_len = %ld attr_len = %ld maxlen = %d \n", attr_raw_value_end - attr_raw_value_begin, attr_value_end - attr_value_begin, MAXURLLEN);
					}
					goto backout;
				}

				memset(attr_value, 0, MAXURLLEN);
				len=checkarraylen(attr_value_end, attr_value_begin, MAXURLLEN);
				memcpy(attr_value, attr_value_begin, len);
				attr_value[len] = '\0';

				//fprintf(stderr, "attr_value = %s\n", attr_value);

				memset(attr_raw_value, 0, MAXURLLEN);
				len = checkarraylen(attr_raw_value_end, attr_raw_value_begin, MAXURLLEN);
				memcpy(attr_raw_value, attr_raw_value_begin, attr_raw_value_end - attr_raw_value_begin);
				attr_raw_value[len] = '\0';
				//fprintf(stderr, "attr_raw_value = %s\n", attr_value);
			}
		}
		else
		{
			goto backout;
		}

		/*if( ((attr_raw_value_end - attr_raw_value_begin) > MAXURLLEN) || ((attr_value_end - attr_value_begin) > MAXURLLEN) )
		{
			if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 3)
			{
				fprintf(stderr, "Ignore too long url %s attr_raw_len = %d attr_len = %d maxlen = %d \n", attr_value, attr_raw_value_end - attr_raw_value_begin, attr_value_end - attr_value_begin, MAXURLLEN);
			}
			goto backout;
		}*/

		//if(interestingtag && (!end_tag))
		if(interestingtag)
		{
			skip(attr_name);
			skip(attr_value);
			skip(attr_raw_value);
      if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 3)
			{
				fprintf(stdout, "\ntag = %s \n", tag);
				fprintf(stdout, "attr_name = %s\n", attr_name);
				fprintf(stdout, "attr_value = %s\n", attr_value);
				fprintf(stdout, "attr_raw_value = %s\n", attr_raw_value);
			}

      //commented by steffie to include resources of type .ico, .xml etc
      /*if( (!strcasecmp(tag, "link")) )*/
      /*{*/
        /*if( (!strcasecmp(attr_name, "rel")) && (!strcasecmp(attr_value, "stylesheet")) )*/
        /*{*/
          /*extractstylesheet = 1;*/
          /*SK_DEBUGP(3,"extractstylesheet = %d \n", extractstylesheet);*/
        /*}*/
        /*if(!strcasecmp(attr_name, "href"))*/
        /*{*/
          /*linkhref = 1;*/
          /*memset(linkhrefarr, 0, MAXURLLEN);*/
          /*strcpy(linkhrefarr, attr_value);*/
          /*SK_DEBUGP(3, "linkhrefarr = %s \n", linkhrefarr);*/
        /*}*/
        /*if(linkhref && extractstylesheet)*/
        /*{*/
          /*if(strcasecmp(attr_name, "href"))*/
            /*strcpy(attr_value, linkhrefarr);*/
        /*}*/
        /*else*/
        /*{*/
          /*continue;*/
        /*}*/
      /*}*/
      //end of comment by steffie

      //added by steffie to avoid unnecessary creation of resuorce URLs like
      //hostname/canonical, hostname/alternate etc.
      if( (!strcasecmp(tag, "link"))  && (!strcasecmp(attr_name, "rel"))){
				if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 3)
          fprintf(stdout, "\n\nattr_name: %s\nattr_value: %s\nattr_raw_value: %s", attr_name,attr_value,attr_raw_value);
        continue;
      }
      //end of addition by steffie

			if(!generateabspath(temphostname, attr_value))
			{
				if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 3)
				{
						fprintf(stderr, "generateabspath UNSUCCESSFULL temphostname = %s attr_value = %s\n", temphostname, attr_value);
				}
			}
			else
			{
				if(checkdupurl())
				{
					if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 3)
						fprintf(stderr, "Dup URL = %s\n", targeturl);
					break;	
				}
				else
				{
					if((!strcasecmp(tag, "link")))
					{
						if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 3)
							fprintf(stderr, "Inserting CSS Url %s \n", targeturl);
					}

					inserturl();
					memset(targeturl, 0, MAXURLLEN);
				}
			}
		}

		interestingtag = 0;
		SK_DEBUGP(3, "look for next attr\n");
	}


	end_tag = 0;
	SK_DEBUGP(3, "look for next tag\n");
	goto searchtag;

	backout:
		//++tag_backout_count;
		SK_DEBUGP(3,"backoutag\n");
		p = tag_start_position + 1;
		goto searchtag;

	finish: 
	SK_DEBUGP(3, "finished scanhtmltags()\n");
	
	return;
}


 
static void *myrealloc(void *ptr, size_t size);
 
static void *myrealloc(void *ptr, size_t size)
{
	/* There might be a realloc() out there that doesn't like reallocing
		NULL pointers, so we take care of it here */ 
	if(ptr)
		return realloc(ptr, size);
	else
		return malloc(size);
}
 
static size_t WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)data;
 
	mem->memory = myrealloc(mem->memory, mem->size + realsize + 1);
	if (mem->memory)
	{
		memcpy(&(mem->memory[mem->size]), ptr, realsize);
		mem->size += realsize;
		mem->memory[mem->size] = 0;
	}
	return realsize;
}

 
static size_t WriteMemoryCallback1(void *ptr, size_t size, size_t nmemb, void *data)
{
	size_t realsize = size * nmemb;
	return realsize;
}


static size_t WriteFileCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
	//size_t realsize = size * nmemb;
	static int responserecv = 0;
	static int ok200 = 0;
	size_t realsize = 0;
	SK_DEBUGP(1, "WriteFileCallback size = %zu  nmemb = %zu \n", size, nmemb);
	if(strcasestr((char *) ptr, "200 OK"))
	{
		ok200 = 1;
		SK_DEBUGP(1, "ok200 = %d nmemb = %zu \n", ok200, nmemb);
	}

	if(strcasestr((char *) ptr, "Content-Encoding: gzip"))
	{
		gzipcontentrecv = 1;
		SK_DEBUGP(1, "gzipcontentrecv = %d nmemb = %zu \n", gzipcontentrecv, nmemb);
	}

	if(!responserecv)
	{
		if(ok200 && nmemb == 2)
			responserecv = 1;
		realsize = size * nmemb;
	}
	else
	{
		FILE *fp = fopen("/tmp/webgethtml.gz", "a+");
		realsize = fwrite(ptr, size, nmemb, fp); 
		fclose(fp);
	}

	return realsize;
}

static size_t ReadFileToMemory(char *data)
{
	size_t realsize = 0, size = 0;
	char arr[2048];
	memset(arr, 0, 2048);

	FILE *fp = fopen("/tmp/webgethtml", "r+");
	size = fread(arr, 1, 2048, fp);
	//data = malloc(size);
	memcpy(data, arr, size);
	realsize += size;

	while(size == 2048 && realsize < 2 * 1024 * 1024)
	{
		memset(arr, 0, 2048);
		size = fread(arr, 1, 2048, fp);
		if(size > 0)
		{
			//data = (char *)myrealloc((void *)data, size + realsize + 1);
			memcpy(&(data[realsize]), arr, size);
			realsize += size;
		}
	}

	data[realsize + 1] = '\0';
	fclose(fp);
	return realsize;
}

int new_connection(CURL* curl)
{
	long num_conn = 0;
	CURLcode res = 0;
	res = curl_easy_getinfo(curl, CURLINFO_NUM_CONNECTS, &num_conn);
	if(CURLE_OK != res){
		fprintf(stderr,"%s\n",curl_easy_strerror(res));
		exit(EXIT_FAILURE);
	}
	return num_conn == 0;
}



void *downloaddata(void *args)
{
	THREADARGS *taptr = (THREADARGS *)args;
	//THREADARGS taptr = (THREADARGS)args;
	CURL *curl;
	CURLcode res;
	char *prevurl =  NULL;
	/*double speed = 0, bytes = 0, xfertime = 0, xfertime_curl = 0, speed_curl = 0;*/
	struct timeval starttime, endtime;
	int failobj = 0;

	char url[MAXURLLEN];
	char newurl[MAXURLLEN];
	char urlname[MAXURLLEN];
	struct curl_slist *headerlist1 = NULL;
	/*long httpcode = 0;*/
 
  	chunk.memory=NULL;
  	chunk.size = 0;
	SK_DEBUGP(1, "taptr->curl = %p taptr->threadnum = %d taptr->threadid = %p \n", taptr->curl, taptr->threadnum, taptr->threadid);
  
	memset(url, 0, MAXURLLEN);
	memset(newurl, 0, MAXURLLEN);
	memset(urlname, 0, MAXURLLEN);
	if(taptr->curl)
		prevurl = taptr->prevurl;

	if(!pullurl(url, prevurl, urlname))
	{
		SK_DEBUGP(1,"Thread has No urls to service in the list\n");
		pthread_mutex_lock(&xferreadymutex);
		++readyfordatatransfer;
		pthread_mutex_unlock(&xferreadymutex);
		return NULL;
	}
	SK_DEBUGP(1, "thread get urlpath %s urlname %s\n", url, urlname);

	if(taptr->curl)
	{
		curl = taptr->curl;
		SK_DEBUGP(1,"downloaddata():: Reusing main taptr->curl = 0x%p threadnum = %d \n", taptr->curl, taptr->threadnum);
	}
	else
  		curl = curl_easy_init();
	if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 3)
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

	curl_easy_setopt(curl, CURLOPT_HEADER, 1L);

	//curl_slist_free_all(headerlist1);
	if(NO_CACHE_CONTROL)
		headerlist1 = curl_slist_append(headerlist1, "Cache-control: no-cache");

	if(USE_USER_AGENT)
		headerlist1 = curl_slist_append(headerlist1, USER_AGENT);

	if(USE_ACCEPT_ENCODING)
		headerlist1 = curl_slist_append(headerlist1, ACCEPT_ENCODING);

	char hostheader[50];
	char* curipaddress;

	if(PREFETCH_NAMELOOKUP)
	{
		memset(hostheader, 0, 50);
		sprintf(hostheader, "Host: %s", urlname);
		curipaddress = lookupipaddress(urlname);
		strcpy(newurl, url);
		replacehostnamewithip(newurl, curipaddress, url);
		headerlist1 = curl_slist_append(headerlist1, hostheader);
	}


	if(NO_CACHE_CONTROL || USE_USER_AGENT || USE_ACCEPT_ENCODING || PREFETCH_NAMELOOKUP)
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist1);
	//alarm(150);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	if(FOLLOWLOCATION == 1)
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	else
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	//curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	//curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	if(DNS_CACHE_TIMEOUT)
		curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, DNS_CACHE_TIMEOUT);
	curl_easy_setopt(curl, CURLOPT_MAXCONNECTS, MAXCONNECTS); //default is 5
	if(DNS_USE_GLOBAL_CACHE == 1)
		curl_easy_setopt(curl, CURLOPT_DNS_USE_GLOBAL_CACHE, 1L);
	if(!PREFETCH_NAMELOOKUP)
	{
		curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 0L);
	}
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, TRANSFER_TIMEOUT);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback1);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
	if(IPVERSIONREQUIRED == 4) {
		curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	} else if(IPVERSIONREQUIRED == 6) {
		curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
	}

	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); //added by steffie
  /*fprintf(stdout, "1836: verifyhost set to 0\n");*/
	pthread_mutex_lock(&xferreadymutex);
	readyfordatatransfer++;

	SK_DEBUGP(1, "Waiting for condition signal from main thread: readyfordatatransfer = %d\n",readyfordatatransfer);

	pthread_mutex_unlock(&xferreadymutex);
	pthread_mutex_lock(&xferdonemutex);
        pthread_cond_wait(&xferdonecond, &xferdonemutex);
        pthread_mutex_unlock(&xferdonemutex);

	failobj = 0;
	/*httpcode = 0;*/
	gettimeofday(&starttime, NULL);	
	res = curl_easy_perform(curl);
	gettimeofday(&endtime, NULL);
	OBJECTSTATS obj = get_object_stats(curl,res);
	if(res != 0)
	{
		SETFAIL();
		SK_DEBUGP(1, "curl_easy_perform failed with errorcode %d\n", res);
	}
	
	if(obj.fail)
	{
		SETFAIL();
	}	

	if(PREFETCH_NAMELOOKUP)
	{
		strcpy(url, newurl);
	}
	
	copy_url_name(&obj, url);
	insert_object_struct(obj);
	pthread_mutex_lock(&xferinfomutex);
	totalthreadspeed += obj.speed;
	totalthreadtime += obj.time;
	totalthreadbytes += obj.bytes;
	pthread_mutex_unlock(&xferinfomutex);
  
	prevurl = url;
	while(pullurl(url, prevurl, urlname))
	{
		SK_DEBUGP(1, "thread get urlpath %s urlname %s\n", url, urlname);

		/*speed = bytes = xfertime = xfertime_curl = speed_curl = 0;*/

		curl_slist_free_all(headerlist1);
		headerlist1 = NULL;
		if(NO_CACHE_CONTROL)
			headerlist1 = curl_slist_append(headerlist1, "Cache-control: no-cache");

		if(USE_USER_AGENT)
			headerlist1 = curl_slist_append(headerlist1, USER_AGENT);

		if(USE_ACCEPT_ENCODING)
			headerlist1 = curl_slist_append(headerlist1, ACCEPT_ENCODING);

	//	char hostheader[50];

		if(PREFETCH_NAMELOOKUP)
		{
			memset(hostheader, 0, 50);
			sprintf(hostheader, "Host: %s", urlname);
			curipaddress = lookupipaddress(urlname);
			strcpy(newurl, url);
			replacehostnamewithip(newurl, curipaddress, url);
		
			SK_DEBUGP(3,"Before curl_slist_append() hostheader = %s\n", hostheader);
			headerlist1 = curl_slist_append(headerlist1, hostheader);
			SK_DEBUGP(3, "After curl_slist_append() hostheader = %s\n", hostheader);
		}


		if(NO_CACHE_CONTROL || USE_USER_AGENT || USE_ACCEPT_ENCODING || PREFETCH_NAMELOOKUP)
		{
			SK_DEBUGP(3, "Before curl_easy_setopt hostheader = %s\n", hostheader);
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist1);
			SK_DEBUGP(3, "Before curl_easy_setopt hostheader = %s\n", hostheader);
		}

		SK_DEBUGP(3,"Before curl_easy_setopt url(ip) = %s\n", url);
		curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); //added by steffie
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); //added by steffie

    /*fprintf(stdout, "1922: verifyhost set to 0\n");*/
		SK_DEBUGP(3, "Before curl_easy_perform for url(ip) %s\n", url);
		if(FOLLOWLOCATION == 1)
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		else
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
		

		
/*		if(!PREFETCH_NAMELOOKUP)
		{
			curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 0L);
		}
*/		
		
		failobj = 0;
		/*httpcode = 0;*/
		gettimeofday(&starttime, NULL);	
		res = curl_easy_perform(curl);
		gettimeofday(&endtime, NULL);
	
		//if((res != 0) && (FOLLOWLOCATION == 1))
		if(res != 0)
		{
			failobj = 1;
			fail = 1;
			SK_DEBUGP(1, "curl_easy_perform failed with errorcode %d\n", res);
		}
		OBJECTSTATS obj = get_object_stats(curl,res);
	
		SK_DEBUGP(1, "after curl_easy_perform for url(ip) %s\n", url);

		fail = obj.fail;
		obj.fail = failobj;
	
		if(PREFETCH_NAMELOOKUP)
		{
			strcpy(url, newurl);
		}

		copy_url_name(&obj, url);
       	        insert_object_struct(obj);
	
		pthread_mutex_lock(&xferinfomutex);
		totalthreadspeed += obj.speed;
		totalthreadtime += obj.time;
		totalthreadbytes += obj.bytes;
		pthread_mutex_unlock(&xferinfomutex);
		prevurl = url;
	}

	pthread_mutex_lock(&xferinfomutex);
	SK_DEBUGP(1, "main () :: Adding curl handle 0x%p to easyhandlearray \n", curl);
	easyhandlearray[totaleasyhandles] = curl;
	++totaleasyhandles;
	pthread_mutex_unlock(&xferinfomutex);

	SK_DEBUGP(1, "thread exiting\n");

	return NULL;
}

//added by steffie
void replace_char(char *s, char find, char replace) {
  while(*s != 0) {
    if(*s == find)
      *s = replace;
    s++;
  }

}

int get_ip_address_family(char *ip_address){
  struct addrinfo hint, *res = NULL;
  int ret;
  int ip_af;
  memset(&hint, '\0', sizeof hint);
  hint.ai_family = PF_UNSPEC;
  hint.ai_flags = AI_NUMERICHOST;
  ret = getaddrinfo(ip_address, NULL, &hint, &res);
  if (ret) {
    return(0);
  }
  if(res->ai_family == AF_INET) ip_af=4;
  else if (res->ai_family == AF_INET6) ip_af=6;
  else ip_af=0;
  freeaddrinfo(res);
  return(ip_af);
}


/*taken from happy.c

 * If the file stream is associated with a regular file, lock the file
 * in order coordinate writes to a common file from multiple happy
 * instances. This is useful if, for example, multiple happy instances
 * try to append results to a common file.
 */

static void lock(FILE *f)
{
  int fd;
  struct stat buf;
  static struct flock lock;

  assert(f);

  lock.l_type = F_WRLCK;
  lock.l_start = 0;
  lock.l_whence = SEEK_END;
  lock.l_len = 0;
  lock.l_pid = getpid();

  fd = fileno(f);
  if ((fstat(fd, &buf) == 0) && S_ISREG(buf.st_mode)) {
    if (fcntl(fd, F_SETLKW, &lock) == -1) {
      fprintf(stderr, "simweb: fcntl: %s (ignored)\n", strerror(errno));
    }
  }
}

/*
 * If the file stream is associated with a regular file, unlock the
 * file (which presumably has previously been locked).
 */

static void unlock(FILE *f)
{
  int fd;
  struct stat buf;
  static struct flock lock;

  assert(f);

  lock.l_type = F_UNLCK;
  lock.l_start = 0;
  lock.l_whence = SEEK_END;
  lock.l_len = 0;
  lock.l_pid = getpid();

  fd = fileno(f);
  if ((fstat(fd, &buf) == 0) && S_ISREG(buf.st_mode)) {
  if (fcntl(fd, F_SETLKW, &lock) == -1) {
    fprintf(stderr, "simweb: fcntl: %s (ignored)\n", strerror(errno));
    }
  }
}
//end of addition by steffie

int main(int argc, char **argv)
{
	int i =0, count = 0, error = 0;
	/*double	speed = 0, bytes = 0, xfertime = 0, lookup_time = 0;*/
	struct timeval threadstarttime, threadendtime;
	struct timeval starttimeval, endtimeval;
	int iter = 0;
	long httpcode = 0;
	/*double lookuptime = 0, connecttime = 0, appconnecttime = 0, pretransfertime = 0, starttransfertime = 0, redirecttime = 0;*/
	char ipbuff[IPADDRLEN];

  /*Parsing the command line arguments*/

  hostname = parse_cmdline_args(argc, argv);
  if (hostname == NULL){
    fprintf(stdout,"parse_cmdline_args(...) returned NULL\n");
    exit(EXIT_FAILURE);
  }
  /*if(argc != 3)*/
        /*{*/
                /*fprintf(stderr , "webget.c : Version 1.15\nWrong number of arguments: Usage is : \n'webget numberofthreads url'\n");*/
                /*return(-1);*/
        /*}*/


	/*curstreams = atoi(argv[1]);*/
  if((curstreams > MAXIMUM_STREAMS) || (curstreams < 1))
  {
    fprintf(stderr,"The number of simultaneous threads (option n) should be set to a value between 1 and %d \n", MAXIMUM_STREAMS);
    return(-1);
  }

	for(i=0; i < MAXIMUM_STREAMS; ++i)
		easyhandlearray[i] = 0;

	/*strncpy(hostname, argv[2], MAXURLLEN);*/

/*if(hostname[strlen(hostname)-1] == '/')
		hostname[strlen(hostname)-1] = '\0';
*/

	/*for(i = 0; i < cururlelem; ++i)
	{
		memset(URLS[cururlelem], 0, MAXURLLEN);
	} */
	/*memset(URLS, 0, sizeof(URLS));*/ //commented by steffie


	SK_DEBUGP(1, "hostname = %s\n",hostname);

	/*val = getenv("ALLOW_DOMAINS");*/
	/*if(val)*/
	/*{*/
		/*int k = 0, j = 0;*/
		/*[>char tmpbuf[MAXURLLEN];<]*/
		/*char tmp[strlen(val)];*/
		/*strcpy(tmp, val);*/
		/*for(j = 0; j < strlen(val); j++)*/
		/*{*/
			/*k++;*/
			/*if(val[j] == ' ')*/
			/*{*/
				/*memset(servers[nservers].name, 0, MAXURLLEN );*/
				/*strncpy(servers[nservers].name, tmp, k-1);*/
				/*++nservers;*/
				/*strcpy(tmp, tmp+k);*/
				/*k=0;*/
			/*}*/
		/*}*/
		/*memset(servers[nservers].name, 0, MAXURLLEN );*/
		/*strcpy(servers[nservers].name, tmp);*/
		/*++nservers;*/
	/*}*/

/*#ifdef DEBUG*/
	/*if(nservers)*/
	/*{*/
		/*fprintf(stderr, "List of Allowed Domains \n");*/
		/*for(iter = 0; iter < nservers; ++iter)*/
		/*{*/
			/*fprintf(stderr, "%d name = %s \n", iter, servers[iter].name);*/
		/*}*/
	/*}*/
/*#endif*/


/*
	char *html = "<html><!-- gdslgdfmlmdfmlbdmb<script> --> <body fg=yellow> <img src=\"images/hp0.gif\" fg=blue> </img> <img src=\"../hp1.gif\" fg=blue> <img src=\"../hp1.gif\" fg=blue> </img> <img src=\"../../hp2.gif\" fg=blue> </img> <img src=\"http://www.google.co.uk/images/hp3.gif\" fg=blue> </img> <script src='www.google.co.uk/script.js'></script> <link rel=\"stylesheet\" type=\"text/css\" <link rel=\"stylesheet\" type=\"text/css\" href=\"http://static.bbc.co.uk/homepage/css/bundles/domestic/main.css?553\" media=\"screen,print\" /> <link rel=\"stylesheet\" type=\"text/css\" href=\"http://static.bbc.co.uk/homepage/css/contentblock/promo/mediazone.css?553\" media=\"screen,print\" /> </body> </html>";

//	printf("tagname = %s attrname = %s \n", tagarray[0].tagname, tagarray[0].attrname);


	scanhtmltags(html, strlen(html));
*/


	CURL *curl;
	CURLcode res;

	struct MemoryStruct chunk;

  	chunk.memory=NULL;
  	chunk.size = 0;

	curl_global_init(CURL_GLOBAL_ALL);

	curl = curl_easy_init();

	if(NO_CACHE_CONTROL)
		headerlist = curl_slist_append(headerlist, "Cache-control: no-cache");

	if(USE_USER_AGENT)
		headerlist = curl_slist_append(headerlist, USER_AGENT);

	if(USE_ACCEPT_ENCODING)
		headerlist = curl_slist_append(headerlist, ACCEPT_ENCODING);
	char hostheader[50];

	char *curipaddress;
	char baseurlname[MAXURLLEN];
	char baseurl[MAXURLLEN];
	char basenewurl[MAXURLLEN];
	if(PREFETCH_NAMELOOKUP)
	{
		memset(targeturl, 0, MAXURLLEN);
		memset(baseurlname, 0, MAXURLLEN);
		memset(baseurl, 0, MAXURLLEN);
		memset(basenewurl, 0, MAXURLLEN);
		strcpy(targeturl, hostname);
		inserturl();
		if(0 == pullurl(baseurl, NULL, baseurlname))
		{
			fprintf(stderr,"Fail. No urls to fetch\n");
			exit(EXIT_FAILURE);
		}
		memset(hostheader, 0, 50);
		sprintf(hostheader, "Host: %s", baseurlname);
		curipaddress = lookupipaddress(baseurlname);
		strcpy(basenewurl, baseurl);
		replacehostnamewithip(basenewurl, curipaddress, baseurl);
		headerlist = curl_slist_append(headerlist, hostheader);
	}

	if(curl)
	{
		if(SK_WEBGET_DEBUG == 1){
			curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }
		curl_easy_setopt(curl, CURLOPT_HEADER, 1L);
		if(NO_CACHE_CONTROL || USE_USER_AGENT || USE_ACCEPT_ENCODING)
			curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
		//alarm(150);
		//signal(SIGALRM, catcher);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		if(FOLLOWLOCATION)
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		else
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
		//curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
		//curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
		if(DNS_CACHE_TIMEOUT)
			curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, DNS_CACHE_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_MAXCONNECTS, MAXCONNECTS); //default is 5
		if(DNS_USE_GLOBAL_CACHE == 1)
			curl_easy_setopt(curl, CURLOPT_DNS_USE_GLOBAL_CACHE, 1L);

		if(!PREFETCH_NAMELOOKUP)
		{
			curl_easy_setopt(curl,CURLOPT_DNS_CACHE_TIMEOUT, 0L);
		}
			
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, TRANSFER_TIMEOUT);
		if(PREFETCH_NAMELOOKUP)
			curl_easy_setopt(curl, CURLOPT_URL, baseurl);
		else
			curl_easy_setopt(curl, CURLOPT_URL, hostname);

	
		if(USE_ACCEPT_ENCODING)
		{
			system("rm -rf /tmp/webgethtml.gz");
			system("rm -rf /tmp/webgethtml");
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
		}
		else
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
 
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

		if(IPVERSIONREQUIRED == 4) {
			curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
		} else if(IPVERSIONREQUIRED == 6) {
			curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
		}

		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); //added by steffie
    /*fprintf(stdout, "2308: verifyhost set to 0\n");*/

		httpcode = 0;
		gettimeofday(&starttimeval, NULL);
    /*fprintf(stdout, "curl perform url: %s or %s \n", baseurl, hostname);*/
		res = curl_easy_perform(curl);//prints the curl verbose output
		gettimeofday(&endtimeval, NULL);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpcode);
		//if((res != 0) && (FOLLOWLOCATION))
		if(res != 0)
		{
			fail = 1;
			SK_DEBUGP(1, "curl_easy_perform failed with errorcode %d msg %s\n", res, curl_easy_strerror(res));
		}
		SK_DEBUGP(1, "html content gettimeofday xfertime = %lf \n", difftimeval(endtimeval, starttimeval ));


		OBJECTSTATS obj = get_object_stats(curl,res);
		
		if(PREFETCH_NAMELOOKUP && topipaddress > 0)
		{
			strncpy(ipbuff, IPADDRESS[0].ipaddress, IPADDRLEN);
		}
		else
		{
			strncpy(ipbuff, obj.str_ip,IPADDRLEN);
		}
	
		totalbytes += obj.bytes;
		if(obj.bytes <= 0)
		{
			if(! ((FOLLOWLOCATION == 0) && (httpcode == 302)) )
				obj.fail = 1;
		}


		totalspeed += obj.speed;
	
		if(obj.time <= 0)
		{
			obj.time = 0;
			obj.fail = 1;
		}
		totaltime += obj.time;
		
		copy_url_name(&obj, hostname);
		insert_object_struct(obj);
		// always cleanup
		//curl_easy_cleanup(curl);
	}

	char *data;
	int realsize = 0;
	
	if(USE_ACCEPT_ENCODING)
	{
		if(gzipcontentrecv)
		{
			system("gzip -df /tmp/webgethtml.gz");
		}
		else
		{
			system("cp /tmp/webgethtml.gz /tmp/webgethtml");
			system("rm -rf /tmp/webgethtml.gz");
		}
		data = malloc(2 * 1024 * 1024);
		memset(data, 0, 2 * 1024 * 1024);
		realsize = ReadFileToMemory(data);
		if(SK_WEBGET_DEBUG == 1)
			fprintf(stderr, "Read %d bytes of Html decompressed data \n", realsize);
	}


	if(SK_WEBGET_DEBUG == 1)
	{
		if(USE_ACCEPT_ENCODING && realsize)
		{
			fprintf(stderr, "HTML decompressed chunk = \n");
			int tmp = 0;
			while(tmp < realsize)
			{
				fprintf(stderr, "%c", data[tmp]);
				++tmp;
			}
			fprintf(stderr, "%c\n", data[tmp]);
		}
		else
			fprintf(stderr, "HTML chunk = %s\n", chunk.memory);
	}

	if(USE_ACCEPT_ENCODING)
	{

		SK_DEBUGP(1,"Before scanhtmltags\n");

		scanhtmltags(data, realsize);

		SK_DEBUGP(1,"After scanhtmltags\n");
	}
	else if(chunk.memory)
	{
		SK_DEBUGP(3, "Before scanhtmltags\n");

		scanhtmltags(chunk.memory, chunk.size);

		SK_DEBUGP(3, "After scanhtmltags\n");
		//free(chunk.memory);
	}

	//struct hostent *hp;
	if(SK_WEBGET_DEBUG == 1 ||  SK_WEBGET_DEBUG == 2 || SK_WEBGET_DEBUG == 3)
	{
		fprintf(stderr, "List of inserted URLS: \n");
		for(i = 0; i < cururlelem; ++i)
		{
			//hp = gethostbyname(URLS[i].name);
			fprintf(stderr, "%d :\t%s\t%s \n", i, URLS[i].name, URLS[i].path);
			//fprintf(stderr, "%d :\t%s\t%s\t%s \n", i, URLS[i].name, URLS[i].path, inet_ntoa(*(struct in_addr *)hp->h_addr_list[0]));
		}
	}

	if(PREFETCH_NAMELOOKUP)
	{
		if(SK_WEBGET_DEBUG == 1 ||  SK_WEBGET_DEBUG == 2 || SK_WEBGET_DEBUG == 3)
		{
			fprintf(stderr, "List of inserted Name/Ipaddress: \n");
			for(i = 0; i < topipaddress; ++i)
			{
				fprintf(stderr, "%d :\t%s\t%s \n", i, IPADDRESS[i].name, IPADDRESS[i].ipaddress);
			}
		}
	}

	THREADARGS ta[MAXIMUM_STREAMS];
	PF fptr;
	pthread_t threadid[MAXIMUM_STREAMS];
	fptr = downloaddata;

	ta[count].threadid = threadid;
	for(count = 0;count < curstreams;++count)
        {
		ta[count].threadnum = count;
		if(count == 0)
		{
			ta[count].curl = curl;
			strcpy(ta[count].prevurl, hostname);
			SK_DEBUGP(1, "main():: reusing main() curl = 0x%p to ta.curl = 0x%p  \n", curl, ta[count].curl);
		}
		else
		{
			ta[count].curl = curl_easy_init();
			ta[count].prevurl[0] = '\0';
			SK_DEBUGP(1, "main():: setting new main() ta.curl = 0x%p  \n", ta[count].curl);
		}
			//ta[count].curl = NULL;

                 error = pthread_create(&threadid[count],
                                        NULL, 
                                        fptr,
                                        (void *)&ta[count]);
                if(0 != error)
		{
                        fprintf(stderr, "Couldn't run thread number %d, errno %d\n", count, error);
                	return(-1);
		}
        }

	struct timeval tv;
        struct timespec ts;
	int loopcount = 0;

	pthread_mutex_lock(&xferdonemutex);
        if(gettimeofday(&tv, NULL) < 0)
                fprintf(stderr,"gettimeofday error\n");
        ts.tv_sec = tv.tv_sec + 1;
        ts.tv_nsec = tv.tv_usec * 1000;
        pthread_cond_timedwait(&xferdonecond, &xferdonemutex,&ts);

 
	for(;;)
	{
		pthread_mutex_lock(&xferreadymutex);
		if(readyfordatatransfer == curstreams)
		{
			pthread_mutex_unlock(&xferreadymutex);
			break;
		}
		pthread_mutex_unlock(&xferreadymutex);

		if(loopcount == 3 || fail)
		{
			time_t endtime = time(0);
			static const char zeros_buff[] = "0;0;0;0;0;0;0;0;0;0;0;0;0;0;0;0";
      if(!SIMWEB_L){
  			fprintf(stdout, "%s%s%s;%ld;FAIL;%s;%s;%ld;%ld;%ld;%d;%d;%s\n",WEBGETSTRID,ipvstring, TESTVERSION, endtime, hostname,ipbuff, (long)((totaltime * 1000000)), (long)totalbytes, (long)(totalspeed), objcount, curstreams,zeros_buff);
	  		SK_DEBUGP(1,"%s%s%s;%ld;FAIL;%s;%s;%ld;%ld;%ld;%d;%d;%s\n",WEBGETSTRID,ipvstring, TESTVERSION, endtime, hostname,ipbuff, (long)((totaltime * 1000000)), (long)totalbytes, (long)(totalspeed), objcount, curstreams, zeros_buff); 
      }
			exit(EXIT_FAILURE);
		}
		
		SK_DEBUGP(1, "main(): Still waiting, only %d thread(s) are ready out of %d thread(s).\n",readyfordatatransfer,curstreams);
	        
		if(gettimeofday(&tv, NULL) < 0)
        	        fprintf(stderr,"gettimeofday error\n");
       		ts.tv_sec = tv.tv_sec + 1;
       		ts.tv_nsec = tv.tv_usec * 1000;
		pthread_mutex_lock(&xferdonemutex);
        	pthread_cond_timedwait(&xferdonecond, &xferdonemutex,&ts);
		loopcount++;
	}


	SK_DEBUGP(1, "main(): Just Before conditional broadcast\n");

	gettimeofday(&threadstarttime, NULL);	
        pthread_cond_broadcast(&xferdonecond);
        pthread_mutex_unlock(&xferdonemutex);
        // now wait for all threads to terminate 
        for(count = 0;count < curstreams;++count)
        {
		SK_DEBUGP(1,"waiting for %d thread\n", count);
                error = pthread_join(threadid[count], NULL);
		SK_DEBUGP(1, "Thread %d terminated\n", count);
        }

	gettimeofday(&threadendtime, NULL);	
	threadxfertime = difftimeval( threadendtime, threadstarttime );
	if(threadxfertime) 
		threadxfertime /= 1000000;

	
	SK_DEBUGP(1, "totalbytes = %lf totalspeed = %lf  totaltime = %lf \n", totalbytes, totalspeed, totaltime);
	SK_DEBUGP(1, "totalthreadbytes = %lf totalthreadspeed = %lf  avgthreadtime = %lf totalthreadtime %lf threadxfertime = %lf \n", totalthreadbytes, totalthreadbytes / threadxfertime, totalthreadtime / curstreams, totalthreadtime, threadxfertime);

	totalbytes +=  totalthreadbytes;
	if(totalthreadbytes)
	{
		totalspeed += (totalthreadbytes / threadxfertime);
		totalspeed /= 2;
		totaltime +=  threadxfertime;
	}

	//cleanup
	for(iter = 0; iter < totaleasyhandles; ++iter)
	{
		if(easyhandlearray[iter])
		{
			SK_DEBUGP(1,"cleaningup curl handle 0x%p \n", easyhandlearray[iter]);
			curl_easy_cleanup(easyhandlearray[iter]);
		}
	}
	SK_DEBUGP(1, "calling curl_global_cleanup() \n");

	curl_global_cleanup();
	
	time_t endtime = time(0);

    if(!SIMWEB_L){
        char report_stats_buff[200];
        report_stats(report_stats_buff);
        if(fail == 1 || totaltime <= 0 || totalbytes <= 0 || totalspeed <= 0)
        {
          SK_DEBUGP(1, "WEBGETMT;%ld;FAIL;%s;0;0;0;%d;%d\n", endtime, hostname, objcount, curstreams);
            if(SK_WEBGET_DEBUG==5){
              fprintf(stdout, "Webget version;timestamp;status;service;ip_endpoint;fetch_time;bytes_total;download_speed;no_of_resources;no_of_threads;no_of_http_requests;no_of_connections_established;no_of_tcp_connections_resused;no_of_dns_lookup;total_request_duration;shortest_request_duration;average_request_duration;max_request_duration;total_time_to_first_byte_duration;shortest_time_to_first_byte_duration;avg_time_to_first_byte_duration;longest_time_to_first_byte_duration;total_dns_lookup_duration;shortest_dns_lookup_duration;avg_dns_lookup_duration;longest_dns_lookup_duration\n");
            }
            fprintf(stdout, "%s%s%s;%ld;FAIL;%s;%s;%ld;%ld;%ld;%d; %d;%s\n",
                WEBGETSTRID,ipvstring, TESTVERSION, endtime, hostname,ipbuff,
                (long)((totaltime * 1000000)), (long)totalbytes, (long)(totalspeed),
                objcount, curstreams,report_stats_buff);
          SK_DEBUGP(1,"%s%s%s;%ld;FAIL;%s;%s;%ld;%ld;%ld;%d;%d;%s\n",WEBGETSTRID,ipvstring, TESTVERSION, endtime, hostname,ipbuff, (long)((totaltime * 1000000)), (long)totalbytes, (long)(totalspeed), objcount, curstreams,report_stats_buff);

        }
              else
        {
            if(SK_WEBGET_DEBUG==5){
              fprintf(stdout, "Webget version;timestamp;status;service;ip_endpoint;fetch_time;bytes_total;download_speed;no_of_resources;no_of_threads;no_of_http_requests;no_of_connections_established;no_of_tcp_connections_resused;no_of_dns_lookup;total_request_duration;shortest_request_duration;average_request_duration;max_request_duration;total_time_to_first_byte_duration;shortest_time_to_first_byte_duration;avg_time_to_first_byte_duration;longest_time_to_first_byte_duration;total_dns_lookup_duration;shortest_dns_lookup_duration;avg_dns_lookup_duration;longest_dns_lookup_duration\n");
            }
            fprintf(stdout, "%s%s%s;%ld;OK;%s;%s;%ld;%ld;%ld;%d;%d;%s\n",WEBGETSTRID,
                ipvstring, TESTVERSION, endtime, hostname,ipbuff,
                (long)((totaltime * 1000000)), (long)totalbytes, (long)(totalspeed),
                objcount, curstreams,report_stats_buff);
          SK_DEBUGP(1,"WEBGETMT;%ld;OK;%s;%ld;%ld;%ld;%d;%d\n", endtime, hostname,(long)((totaltime * 1000000)), (long)totalbytes, (long)(totalspeed), objcount, curstreams);
        }
  }
	if(PRINT_OBJECT_STATS)
	{
    if(SK_WEBGET_DEBUG==5){
		  fprintf(stdout, "WEBGETMTOBJ;timestamp;status;service;resource_name;time;bytes;speed\n");
    }
		for(i=0; i < objcount; ++i)
		{
			//if(((!(objstats[i].fail)) && (objstats[i].bytes > 0) && (objstats[i].time > 0) && (objstats[i].speed > 0)) || (FOLLOWLOCATION == 0) || ((i!=0) && (FOLLOWLOCATION == 2)) )
			if(!(objstats[i].fail) )
			{

			//	print_object_stats(objstats[i]);
				fprintf(stdout, "WEBGETMTOBJ;%ld;OK;%s;%s;%ld;%ld;%ld\n", endtime, hostname, objstats[i].name, (long)((objstats[i].time * 1000000)), (long)objstats[i].bytes, (long)(objstats[i].speed));
				SK_DEBUGP(1, "WEBGETMTOBJ;%ld;OK;%s;%s;%ld;%ld;%ld\n", endtime, hostname, objstats[i].name, (long)((objstats[i].time * 1000000)), (long)objstats[i].bytes, (long)(objstats[i].speed));

			}
			else
			{
				fprintf(stdout, "WEBGETMTOBJ;%ld;FAIL;%s;%s;%ld;%ld;%ld\n", endtime, hostname, objstats[i].name, (long)((objstats[i].time * 1000000)), (long)objstats[i].bytes, (long)(objstats[i].speed));
				SK_DEBUGP(1, "WEBGETMTOBJ;%ld;FAIL;%s;%s;%ld;%ld;%ld\n", endtime, hostname, objstats[i].name, (long)((objstats[i].time * 1000000)), (long)objstats[i].bytes, (long)(objstats[i].speed));
			}

		}
	}
  //added by steffie
  if(SIMWEB_L)
  {

    int simweb_obj_index=1;
    time_t now;
    now = time(NULL);
    /*char version[9] = "SIMWEB.0";*/
    char *encoded;
    //array with CURL error code strings corresponding to curl response codes
    const char* curl_response_codes[] = {
      "CURLE_OK" ,
      "CURLE_UNSUPPORTED_PROTOCOL",    /* 1 */
      "CURLE_FAILED_INIT",             /* 2 */
      "CURLE_URL_MALFORMAT",           /* 3 */
      "CURLE_NOT_BUILT_IN",            /* 4 - [was obsoleted in August 2007 for
                                        7.17.0, reused in April 2011 for 7.21.5]*/
      "CURLE_COULDNT_RESOLVE_PROXY",   /* 5 */
      "CURLE_COULDNT_RESOLVE_HOST",    /* 6 */
      "CURLE_COULDNT_CONNECT",         /* 7 */
      "CURLE_FTP_WEIRD_SERVER_REPLY",  /* 8 */
      "CURLE_REMOTE_ACCESS_DENIED",    /* 9 a service was denied by the server
                                        due to lack of access - when login fails
                                        this is not returned. */
      "CURLE_FTP_ACCEPT_FAILED",       /* 10 - [was obsoleted in April 2006 for
                                        7.15.4, reused in Dec 2011 for 7.24.0]*/
      "CURLE_FTP_WEIRD_PASS_REPLY",    /* 11 */
      "CURLE_FTP_ACCEPT_TIMEOUT",      /* 12 - timeout occurred accepting server
                                        [was obsoleted in August 2007 for 7.17.0,
                                        reused in Dec 2011 for 7.24.0]*/
      "CURLE_FTP_WEIRD_PASV_REPLY",    /* 13 */
      "CURLE_FTP_WEIRD_227_FORMAT",    /* 14 */
      "CURLE_FTP_CANT_GET_HOST",       /* 15 */
      "CURLE_HTTP2",                   /* 16 - A problem in the http2 framing layer.
      "                                  [was obsoleted in August 2007 for 7.17.0,
      "                                  reused in July 2014 for 7.38.0] */
      "CURLE_FTP_COULDNT_SET_TYPE",    /* 17 */
      "CURLE_PARTIAL_FILE",            /* 18 */
      "CURLE_FTP_COULDNT_RETR_FILE",   /* 19 */
      "CURLE_OBSOLETE20",              /* 20 - NOT USED */
      "CURLE_QUOTE_ERROR",             /* 21 - quote command failure */
      "CURLE_HTTP_RETURNED_ERROR",     /* 22 */
      "CURLE_WRITE_ERROR",             /* 23 */
      "CURLE_OBSOLETE24",              /* 24 - NOT USED */
      "CURLE_UPLOAD_FAILED",           /* 25 - failed upload "command" */
      "CURLE_READ_ERROR",              /* 26 - couldn't open/read from file */
      "CURLE_OUT_OF_MEMORY",           /* 27 */
      /* Note: CURLE_OUT_OF_MEMORY may sometimes indicate a conversion error
               instead of a memory allocation error if CURL_DOES_CONVERSIONS
               is defined
      */
      "CURLE_OPERATION_TIMEDOUT",      /* 28 - the timeout time was reached */
      "CURLE_OBSOLETE29",              /* 29 - NOT USED */
      "CURLE_FTP_PORT_FAILED",         /* 30 - FTP PORT operation failed */
      "CURLE_FTP_COULDNT_USE_REST",    /* 31 - the REST command failed */
      "CURLE_OBSOLETE32",              /* 32 - NOT USED */
      "CURLE_RANGE_ERROR",             /* 33 - RANGE "command" didn't work */
      "CURLE_HTTP_POST_ERROR",         /* 34 */
      "CURLE_SSL_CONNECT_ERROR",       /* 35 - wrong when connecting with SSL */
      "CURLE_BAD_DOWNLOAD_RESUME",     /* 36 - couldn't resume download */
      "CURLE_FILE_COULDNT_READ_FILE",  /* 37 */
      "CURLE_LDAP_CANNOT_BIND",        /* 38 */
      "CURLE_LDAP_SEARCH_FAILED",      /* 39 */
      "CURLE_OBSOLETE40",              /* 40 - NOT USED */
      "CURLE_FUNCTION_NOT_FOUND",      /* 41 */
      "CURLE_ABORTED_BY_CALLBACK",     /* 42 */
      "CURLE_BAD_FUNCTION_ARGUMENT",   /* 43 */
      "CURLE_OBSOLETE44",              /* 44 - NOT USED */
      "CURLE_INTERFACE_FAILED",        /* 45 - CURLOPT_INTERFACE failed */
      "CURLE_OBSOLETE46",              /* 46 - NOT USED */
      "CURLE_TOO_MANY_REDIRECTS" ,     /* 47 - catch endless re-direct loops */
      "CURLE_UNKNOWN_OPTION",          /* 48 - User specified an unknown option */
      "CURLE_TELNET_OPTION_SYNTAX" ,   /* 49 - Malformed telnet option */
      "CURLE_OBSOLETE50",              /* 50 - NOT USED */
      "CURLE_PEER_FAILED_VERIFICATION", /* 51 - peer's certificate or fingerprint
                                         wasn't verified fine */
      "CURLE_GOT_NOTHING",             /* 52 - when this is a specific error */
      "CURLE_SSL_ENGINE_NOTFOUND",     /* 53 - SSL crypto engine not found */
      "CURLE_SSL_ENGINE_SETFAILED",    /* 54 - can not set SSL crypto engine as
                                        default */
      "CURLE_SEND_ERROR",              /* 55 - failed sending network data */
      "CURLE_RECV_ERROR",              /* 56 - failure in receiving network data */
      "CURLE_OBSOLETE57",              /* 57 - NOT IN USE */
      "CURLE_SSL_CERTPROBLEM",         /* 58 - problem with the local certificate */
      "CURLE_SSL_CIPHER",              /* 59 - couldn't use specified cipher */
      "CURLE_SSL_CACERT",              /* 60 - problem with the CA cert (path?) */
      "CURLE_BAD_CONTENT_ENCODING",    /* 61 - Unrecognized/bad encoding */
      "CURLE_LDAP_INVALID_URL",        /* 62 - Invalid LDAP URL */
      "CURLE_FILESIZE_EXCEEDED",       /* 63 - Maximum file size exceeded */
      "CURLE_USE_SSL_FAILED",          /* 64 - Requested FTP SSL level failed */
      "CURLE_SEND_FAIL_REWIND",        /* 65 - Sending the data requires a rewind
                                        that failed */
      "CURLE_SSL_ENGINE_INITFAILED",   /* 66 - failed to initialise ENGINE */
      "CURLE_LOGIN_DENIED",            /* 67 - user, password or similar was not
                                        accepted and we failed to login */
      "CURLE_TFTP_NOTFOUND",           /* 68 - file not found on server */
      "CURLE_TFTP_PERM",               /* 69 - permission problem on server */
      "CURLE_REMOTE_DISK_FULL",        /* 70 - out of disk space on server */
      "CURLE_TFTP_ILLEGAL",            /* 71 - Illegal TFTP operation */
      "CURLE_TFTP_UNKNOWNID",          /* 72 - Unknown transfer ID */
      "CURLE_REMOTE_FILE_EXISTS",      /* 73 - File already exists */
      "CURLE_TFTP_NOSUCHUSER",         /* 74 - No such user */
      "CURLE_CONV_FAILED",             /* 75 - conversion failed */
      "CURLE_CONV_REQD",               /* 76 - caller must register conversion
                                        callbacks using curl_easy_setopt options
                                        CURLOPT_CONV_FROM_NETWORK_FUNCTION,
                                        CURLOPT_CONV_TO_NETWORK_FUNCTION, and
                                        CURLOPT_CONV_FROM_UTF8_FUNCTION */
      "CURLE_SSL_CACERT_BADFILE",      /* 77 - could not load CACERT file, missing
                                        or wrong format */
      "CURLE_REMOTE_FILE_NOT_FOUND",   /* 78 - remote file not found */
      "CURLE_SSH",                     /* 79 - error from the SSH layer, somewhat
                                        generic so the error message will be of
                                        interest when this has happened */

      "CURLE_SSL_SHUTDOWN_FAILED",     /* 80 - Failed to shut down the SSL
                                        connection */
      "CURLE_AGAIN",                   /* 81 - socket is not ready for send/recv,
                                        wait till it's ready and try again (Added
                                        in 7.18.2) */
      "CURLE_SSL_CRL_BADFILE",         /* 82 - could not load CRL file, missing or
                                        wrong format (Added in 7.19.0) */
      "CURLE_SSL_ISSUER_ERROR",        /* 83 - Issuer check failed.  (Added in
                                        7.19.0) */
      "CURLE_FTP_PRET_FAILED",         /* 84 - a PRET command failed */
      "CURLE_RTSP_CSEQ_ERROR",         /* 85 - mismatch of RTSP CSeq numbers */
      "CURLE_RTSP_SESSION_ERROR",      /* 86 - mismatch of RTSP Session Ids */
      "CURLE_FTP_BAD_FILE_LIST",       /* 87 - unable to parse FTP file list */
      "CURLE_CHUNK_FAILED",            /* 88 - chunk callback reported error */
      "CURLE_NO_CONNECTION_AVAILABLE", /* 89 - No connection available, the
                                        session will be queued */
      "CURLE_SSL_PINNEDPUBKEYNOTMATCH", /* 90 - specified pinned public key did not
                                         match */
      "CURLE_SSL_INVALIDCERTSTATUS",   /* 91 - invalid certificate status */
      "CURL_LAST" /* never use! */
    };

		if(SK_WEBGET_DEBUG == 5)
      fprintf(stdout, "#;version;service;timestamp;af;status;curl_response_code;object_type;http_code;resource_url;ip_endpoint;size_bytes\n");
    for(i=0; i < objcount; ++i)
    {
        //content type string formatting
        replace_char(objstats[i].obj_typ, ';', ':');


        //encode url
        CURL *curl;
        curl = curl_easy_init();
        if (curl)
        {
          encoded = curl_easy_escape(curl,objstats[i].name,0);
          /*urllib.unquote(url).decode('utf8')*/
        }

        //	print_object_stats(objstats[i]);
        if(SK_WEBGET_DEBUG == 1 || SK_WEBGET_DEBUG == 2 || SK_WEBGET_DEBUG == 3 || SK_WEBGET_DEBUG == 4 || SK_WEBGET_DEBUG == 5) {
          fprintf(stdout, "%d;%s;%s;%ld;%d;%s;%s;%s;%ld;%s;%s;%ld\n",
              simweb_obj_index,
              SIMWEBVERSION,
              hostname,
              now,
              IPVERSIONREQUIRED == 0 ? get_ip_address_family(objstats[i].str_ip) : IPVERSIONREQUIRED,
              objstats[i].fail ? "FAIL" : "OK",
              curl_response_codes[objstats[i].curl_resp_code],
              objstats[i].obj_typ,
              objstats[i].http_resp_code,
              /*encoded,*/
              objstats[i].name,
              objstats[i].str_ip,
              (long)objstats[i].bytes);
        }
        else if(SK_WEBGET_DEBUG ==0){
          lock(stdout);
          fprintf(stdout, "%d;%s;%s;%ld;%d;%s;%d;%s;%ld;%s;%s;%ld\n",
              simweb_obj_index,
              SIMWEBVERSION,
              hostname,
              now,
              IPVERSIONREQUIRED == 0 ? get_ip_address_family(objstats[i].str_ip) : IPVERSIONREQUIRED,
              objstats[i].fail ? "FAIL" : "OK",
              objstats[i].curl_resp_code,
              objstats[i].obj_typ,
              objstats[i].http_resp_code,
              encoded,
              /*objstats[i].name,*/
              objstats[i].str_ip,
              (long)objstats[i].bytes);
          unlock(stdout);
        }
        simweb_obj_index++;

        /*freeing memory*/
        free(objstats[i].obj_typ);
        objstats[i].obj_typ = NULL;
        if(curl)
        {
          curl_free(encoded);
          curl_easy_cleanup(curl);
        }
    }
  }
  if(objstats != NULL)
  {
    free(objstats);
    objstats = NULL;
  }
  if(IPADDRESS != NULL){
    free(IPADDRESS);
    IPADDRESS = NULL;
  }
  if(URLS != NULL)
  {
    free(URLS);
    URLS = NULL;
  }
  //end of addition by steffie
		//curl_easy_cleanup(curl);
	if(fail == 1)
		exit(EXIT_FAILURE);
	else
		exit(EXIT_SUCCESS);

}
