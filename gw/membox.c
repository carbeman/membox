#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/unistd.h>

#include "gw-membox-config.h"
#include "gwlib/gwlib.h"
#include "gw/shared.h"
#include "gwlib/dbpool.h"
#include "gw/msg.h"
#include "gw/bb.h"

#include "membox.h"

static void read_messages_from_bearerbox ( void );
static List *smsbox_requests = NULL;
volatile sig_atomic_t restart = 0;

/* The configuration used by our box. */ 
static Cfg *cfg; 

/* The PID file written by the membox. */ 
static char *pid_file; 

/* Some processing quue */
/*
static List *some_queue;
*/

/* have we received restart cmd from bearerbox? */
static volatile sig_atomic_t restart_membox = 0;

static long bearerbox_port;
static Octstr *bearerbox_host;
static int bearerbox_port_ssl = 0;
/*
static Octstr *box_allow_ip;
static Octstr *box_deny_ip;
*/
static Octstr *global_sender;

Octstr *membox_id;

#define MSGSZ			8192
#define SHM_QUEUE_ID 	2232

#define SLEEP_BETWEEN_SELECTS 1.0

typedef struct _boxc {
    Connection	*smsbox_connection;
    Connection	*bearerbox_connection;
    time_t	connect_time;
    Octstr    	*client_ip;
    volatile sig_atomic_t alive;
    Octstr *boxc_id; /* identifies the connected smsbox instance */
} Boxc;

typedef struct msgbuf {
    long    mtype;
    char    mtext[MSGSZ];
} message_buf;

/********************************************************************
 * Config file handling callbacks.
 */

static int membox_is_allowed_in_group(Octstr *group, Octstr *variable)
{
    Octstr *groupstr;
    
    groupstr = octstr_imm("group");

    #define OCTSTR(name) \
        if (octstr_compare(octstr_imm(#name), variable) == 0) \
        return 1;
    #define SINGLE_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), group) == 0) { \
        if (octstr_compare(groupstr, variable) == 0) \
        return 1; \
        fields \
        return 0; \
    }
    #define MULTI_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), group) == 0) { \
        if (octstr_compare(groupstr, variable) == 0) \
        return 1; \
        fields \
        return 0; \
    }
    #include "membox-cfg.def"

    return 0;
}


static int membox_is_single_group(Octstr *query)
{
    #define OCTSTR(name)
    #define SINGLE_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), query) == 0) \
        return 1;
    #define MULTI_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), query) == 0) \
        return 0;
    #include "membox-cfg.def"
    return 0;
}


/********************************************************************
 * Initialization and Stop functions.
 */

static void write_pid_file(void) 
{ 
    FILE *f; 
 
    if (pid_file != NULL) { 
        f = fopen(pid_file, "w"); 
        fprintf(f, "%d\n", (int)getpid()); 
        fclose(f); 
    } 
} 


static void membox_quit(void) 
{ 
    /*
     * Remove the message producers. This will wake up waiting consumers - 
     * the other threads. 
     */ 
    //gwlist_remove_producer(some_queue); 
    
    debug("membox", 0, "Shutting down " GW_BOX_NAME "...");
    program_status = shutting_down; 
    gwthread_wakeup_all();
}


static void signal_handler(int signum) 
{ 
    /* On some implementations (i.e. linuxthreads), signals are delivered 
     * to all threads.  We only want to handle each signal once for the 
     * entire box, and we let the gwthread wrapper take care of choosing 
     * one. 
     */ 
    if (!gwthread_shouldhandlesignal(signum)) 
        return; 
 
    switch (signum) { 
        case SIGINT:
        case SIGTERM:
            if (program_status != shutting_down) { 
                error(0, "SIGINT or SIGTERM received, shutting down daemon..."); 
                program_status = shutting_down; 
                
                membox_quit();
            } 
            break; 
 
        case SIGHUP: 
            warning(0, "SIGHUP received, catching and re-opening logs"); 
            log_reopen(); 
            alog_reopen();
            break; 
 
        /* 
         * It would be more proper to use SIGUSR1 for this, but on some 
         * platforms that's reserved by the pthread support. 
         */ 
        case SIGQUIT: 
            warning(0, "SIGQUIT received, reporting memory usage."); 
            gw_check_leaks(); 
            break; 

        /*
         * On a SIGSEGV (segmentation fault) signal we only can act within
         * the scope of the function that the signal handler calls, not 
         * outside. This means also we lost int main() scope.
         * At least ensure out logs are flushed before we die.
         */
        case SIGSEGV:
            error(0, "SIGSEGV received, OS layer forces us to die...");
            fflush(NULL);
            program_status = shutting_down;
            break;
    } 
} 
 
 
static void setup_signal_handlers(void) 
{ 
    struct sigaction act, sact; 
 
    /* generic signals */    
    act.sa_handler = signal_handler; 
    sigemptyset(&act.sa_mask); 
    act.sa_flags = 0; 
    sigaction(SIGINT, &act, NULL); 
    sigaction(SIGTERM, &act, NULL); 
    sigaction(SIGQUIT, &act, NULL); 
    sigaction(SIGHUP, &act, NULL); 
    sigaction(SIGPIPE, &act, NULL); 

    /* SEGFAULT signal */
    sact.sa_handler = signal_handler;
    sigfillset(&sact.sa_mask); 
    sigdelset(&sact.sa_mask, SIGBUS);
#if defined(SA_ONESHOT) && defined(SA_NOMASK)     
    sact.sa_flags = SA_ONESHOT | SA_NOMASK;
#else
    sact.sa_flags = 0;
#endif
    sigaction(SIGSEGV, &sact, NULL); 
}


/* 
 * Callback used to handle our own options.
 */ 
static int check_args(int i, int argc, char **argv) 
{ 
    int opt;
    
    while ((opt = getopt(argc, argv, "R")) != EOF) {
        switch (opt) {
            case 'R':
                printf("%s version %s-%s\n", 
                       GW_BOX_NAME, GW_BOX_VERSION, GW_VERSION);
                exit(0);
                break;
            default:
                error(0, "Invalid option %c", opt);
                panic(0, "Stopping.");
        }
    }
          
    return 0; 
}

/*
 * Read our required config file values and initialize here.
 */
static void init_membox(Cfg *cfg)
{
	CfgGroup *grp;
	Octstr *logfile;
	long lvl;
	
	/* some default values */
	//membox_port_ssl = 0;
	bearerbox_port = BB_DEFAULT_SMSBOX_PORT;
	bearerbox_port_ssl = 0;
	logfile = NULL;
	lvl = 0;

	/*
	 * first we take the port number in bearerbox and other values from the
	 * core group in configuration file
	*/

	grp = cfg_get_single_group(cfg, octstr_imm("membox"));
	if (cfg_get_integer(&bearerbox_port, grp, octstr_imm("bearerbox-port")) == -1)
		panic(0, "Missing or bad 'bearerbox-port' in membox group");
/*
#ifdef HAVE_LIBSSL
	cfg_get_bool(&bearerbox_port_ssl, grp, octstr_imm("smsbox-port-ssl"));
	conn_config_ssl(grp);
#endif 
*/
	grp = cfg_get_single_group(cfg, octstr_imm("membox"));
	if (grp == NULL)
		panic(0, "No 'membox' group in configuration");

    bearerbox_host = cfg_get( grp, octstr_imm("bearerbox-host"));
    if (bearerbox_host == NULL)
        bearerbox_host = octstr_create(BB_DEFAULT_HOST);

	membox_id = cfg_get(grp, octstr_imm("smsbox-id"));
	global_sender = cfg_get(grp, octstr_imm("global-sender"));

	logfile = cfg_get(grp, octstr_imm("log-file"));

	cfg_get_integer(&lvl, grp, octstr_imm("log-level"));

	if (logfile != NULL) {
		info(0, "Starting to log to file %s level %ld", 
			octstr_get_cstr(logfile), lvl);
		log_open(octstr_get_cstr(logfile), lvl, GW_NON_EXCL);
		octstr_destroy(logfile);
	}

	program_status = running;
}


/********************************************************************
 * Main function.
 */

int main(int argc, char **argv) 
{ 
    int cf_index; 
    Octstr *cfg_name; 

    gwlib_init(); 

    /* Read commandline arguments. Init debugging. */
    cf_index = get_and_set_debugs(argc, argv, check_args); 
 
    setup_signal_handlers(); 
 
    if (argv[cf_index] == NULL) 
        cfg_name = octstr_create("membox.conf"); 
    else 
        cfg_name = octstr_create(argv[cf_index]); 
 
    /* Read configuration file. */
    cfg = cfg_create(cfg_name); 
    octstr_destroy(cfg_name);
    
    /* add our own config block semantical checkers to the core hook */ 
    cfg_add_hooks(&membox_is_allowed_in_group, &membox_is_single_group);
     
    if (cfg_read(cfg) == -1) 
        panic(0, "Error reading configuration file, can not start."); 

    report_versions("membox");

    init_membox(cfg); 

    /* Show startup info */
    debug("membox", 0, "----------------------------------------------"); 
    debug("membox", 0, "%s version %s starting.", GW_BOX_NAME, GW_BOX_VERSION);
    debug("membox", 0, "Using %s version %s libraries", GW_NAME, GW_VERSION);

    write_pid_file();

	memboxc_run();

    cfg_destroy(cfg);
 
    /* Exit program normally. */
    debug("membox", 0, "Program exiting normally."); 
 
    gwlib_shutdown(); 
    return 0; 
}

/*
 *-------------------------------------------------
 *  receiver thingies
 *-------------------------------------------------
 *
*/

/* read from bearerbox */

static Msg *read_from_box(Connection *conn, Boxc *boxconn)
{
    int ret;
    Octstr *pack;
    Msg *msg;

    pack = NULL;
    while (program_status != shutting_down && boxconn->alive) {
	    pack = conn_read_withlen(conn);
	    gw_claim_area(pack);
	    if (pack != NULL)
	        break;

	    if (conn_eof(conn)) {
	        info(0, "Connection closed by the box <%s>",
		         octstr_get_cstr(boxconn->client_ip));
	        return NULL;
	    }

	    ret = conn_wait(conn, -1.0);
	    if (ret < 0) {
	        error(0, "Connection to box <%s> broke.",
		          octstr_get_cstr(boxconn->client_ip));
	        return NULL;
	    }
    }

    if (pack == NULL)
    	return NULL;

    msg = msg_unpack(pack);
    octstr_destroy(pack);

    if (msg == NULL)
	    error(0, "Failed to unpack data!");
    return msg;
}

/*
 *-------------------------------------------------
 *  sender thingies
 *-------------------------------------------------
 *
*/

/* send to bearerbox */

static int send_msg(Connection *conn, Boxc *boxconn, Msg *pmsg)
{
    Octstr *pack;

    // checking if the message is unicode and converting it to binary for submitting	
    if(pmsg->sms.coding == 2)
	octstr_hex_to_binary(pmsg->sms.msgdata);

    pack = msg_pack(pmsg);

    if (pack == NULL)
        return -1;

    if (conn_write_withlen(conn, pack) == -1) {
    	error(0, "Couldn't write Msg to box <%s>, disconnecting",
	      octstr_get_cstr(boxconn->client_ip));
        octstr_destroy(pack);
        return -1;
    }
    octstr_destroy(pack);
    return 0;
}

static Boxc *boxc_create(int fd, Octstr *ip, int ssl)
{
    Boxc *boxc;

    boxc = gw_malloc(sizeof(Boxc));
    boxc->smsbox_connection = conn_wrap_fd(fd, ssl);
    boxc->bearerbox_connection = NULL;
    boxc->client_ip = ip;
    boxc->alive = 1;
    boxc->connect_time = time(NULL);
    boxc->boxc_id = NULL;
    return boxc;
}

static void boxc_destroy(Boxc *boxc)
{
    if (boxc == NULL)
	    return;

    /* do nothing to the lists, as they are only references */

    if (boxc->smsbox_connection)
	    conn_destroy(boxc->smsbox_connection);
    if (boxc->bearerbox_connection)
	    conn_destroy(boxc->bearerbox_connection);
    octstr_destroy(boxc->client_ip);
    octstr_destroy(boxc->boxc_id);
    gw_free(boxc);
}

static Boxc *accept_boxc(int fd, int ssl)
{
    Boxc *newconn;
    Octstr *ip;

    int newfd;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;

    client_addr_len = sizeof(client_addr);

    newfd = accept(fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (newfd < 0)
	    return NULL;

    ip = host_ip(client_addr);

    // if (is_allowed_ip(box_allow_ip, box_deny_ip, ip) == 0) {
        // info(0, "Box connection tried from denied host <%s>, disconnected",
                // octstr_get_cstr(ip));
        // octstr_destroy(ip);
        // close(newfd);
        // return NULL;
    // }
    newconn = boxc_create(newfd, ip, ssl);

    /*
     * check if the SSL handshake was successfull, otherwise
     * this is no valid box connection any more
     */
#ifdef HAVE_LIBSSL
     if (ssl && !conn_get_ssl(newconn->smsbox_connection))
        return NULL;
#endif

    if (ssl)
        info(0, "Client connected from <%s> using SSL", octstr_get_cstr(ip));
    else
        info(0, "Client connected from <%s>", octstr_get_cstr(ip));


    /* XXX TODO: do the hand-shake, baby, yeah-yeah! */

    return newconn;
}

/*
 * Identify ourself to bearerbox for smsbox-specific routing inside bearerbox.
 * Do this even while no smsbox-id is given to unlock the sender thread in
 * bearerbox.
 */
static void identify_to_bearerbox(Boxc *conn)
{
    Msg *msg;

    msg = msg_create(admin);
    msg->admin.command = cmd_identify;
    msg->admin.boxc_id = octstr_duplicate(conn->boxc_id);
    send_msg(conn->bearerbox_connection, conn, msg);
    msg_destroy(msg);
}

static void memboxc_run()
{
	extern int errno;
    Octstr *os = NULL;
    Msg *msg = NULL;
	Boxc *boxc;
    int msqid;
    message_buf rbuf;

	boxc = gw_malloc(sizeof(Boxc));
	boxc->bearerbox_connection = connect_to_bearerbox_real(bearerbox_host, bearerbox_port, bearerbox_port_ssl, NULL);
	boxc->smsbox_connection = NULL;
	boxc->client_ip = NULL;
	boxc->alive = 1;
	boxc->connect_time = time(NULL);
	boxc->boxc_id = octstr_duplicate(membox_id);
	if (boxc->bearerbox_connection == NULL) {
		boxc_destroy(boxc);
		return;
	}

	identify_to_bearerbox(boxc);

	smsbox_requests = gwlist_create();
	gwlist_add_producer ( smsbox_requests );

	read_messages_from_bearerbox();

	info ( 0, GW_NAME "membox terminating." );


	gwlist_remove_producer ( smsbox_requests );

	gw_assert ( gwlist_len ( smsbox_requests ) == 0 );

    msg_destroy(msg);
	octstr_destroy(os);

	boxc_destroy(boxc);
}

static void read_messages_from_bearerbox ( void )
{
	time_t start, t;
	int secs;
	int total = 0;
	int ret;
	Msg *msg;

	start = t = time ( NULL );
	while ( program_status != shutting_down )
	{
		/* block infinite for reading messages */
		ret = read_from_bearerbox ( &msg, INFINITE_TIME );
		if ( ret == -1 )
			break;
		else if ( ret == 1 ) /* timeout */
			continue;
		else if ( msg == NULL ) /* just to be sure, may not happens */
			break;

		if ( msg_type ( msg ) == admin )
		{

			if ( msg->admin.command == cmd_shutdown )
			{
				info ( 0, "Bearerbox told us to die" );
				program_status = shutting_down;
			}
			else if ( msg->admin.command == cmd_restart )
			{
				info ( 0, "Bearerbox told us to restart" );
				restart = 1;
				program_status = shutting_down;
			}
			/*
			 * XXXX here should be suspend/resume, add RSN
			 */
			msg_destroy ( msg );
		}
		else if ( msg_type ( msg ) == sms )
		{
			if ( total == 0 )
				start = time ( NULL );
			total++;
			gwlist_produce ( smsbox_requests, msg );
		}
		else if ( msg_type ( msg ) == ack )
		{
			msg_destroy ( msg );
		}
		else
		{
			warning ( 0, "Received other message than sms/admin, ignoring!" );
			msg_destroy ( msg );
		}
	}
	secs = (int)difftime ( time ( NULL ), start );
	info ( 0, "Received (and handled?) %d requests in %d seconds "
	       "(%.2f per second)", total, secs, ( float ) total / secs );
}

