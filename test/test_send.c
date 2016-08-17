/*
 * test_send.c
 * 
 * Simulate a client injecting a bunch of messages
 * 
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdio.h>
#include <string.h>
#include "gwlib/gwlib.h"
#include "gwlib/octstr.h"
#include "gw/shared.h"
#include "gw/msg.h"
#include "gw/bb.h"

#define MSGSZ     		8192
#define SHM_QUEUE_ID	2232
#define MAX_ERRORS		1000	//max errors before exit
#define ERROR_SLEEP		100000  //microseconds to sleep when getting an error

/*
 * Declare the message structure.
 */

typedef struct msgbuf {
         long    mtype;
         char    mtext[MSGSZ];
         } message_buf;

int init_queue() {
    return msgget(SHM_QUEUE_ID, IPC_CREAT | 0666 );
}

int main() 
{
    int msqid;
    message_buf sbuf;
    size_t buf_length;
    Msg *msg;
    Octstr *os;
    int i, err;
    gwlib_init();

    msqid = init_queue();

    if ( msqid < 0 ) {
        perror("msgget");
        return 1;
    }
    msg = msg_create(sms);
	msg->sms.sender = octstr_create("12345678");
	msg->sms.receiver = octstr_create("87654321");
	msg->sms.smsc_id = octstr_create("test");
	for( i=1; i<=10000; i++) {
		msg->sms.msgdata = octstr_format("This is message #%d", i);	

		os = msg_pack(msg);
		octstr_binary_to_hex(os,1);

		sbuf.mtype = 1;
		octstr_get_many_chars(sbuf.mtext, os, 0, MSGSZ);
		buf_length = strlen(sbuf.mtext) + 1 ;
		err = 0;
		while(msgsnd(msqid, &sbuf, buf_length, IPC_NOWAIT) < 0) {
			if ( err++ > MAX_ERRORS ) {
		        perror("msgsnd");
				return 1;
			}
	        usleep(ERROR_SLEEP);
		}
	    printf("Message: \"%s\" Sent\n", octstr_get_cstr( msg->sms.msgdata ));
	}
	octstr_destroy(os);
	msg_destroy(msg);
    gwlib_shutdown(); 

    return 0;
}

