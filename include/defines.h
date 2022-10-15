#ifndef __DEFINES_H_
#define __DEFINES_H_

#define MAX_PENDING_CLIENTS 5
#define MAX_IP_LEN 15
#define MAX_ID_LEN 10
#define MAX_COMM_LEN 11
#define MAX_TOPIC_LEN 50
#define MAX_CONTENT_LEN 1500
#define UDP_HDR_LEN (MAX_TOPIC_LEN + 9) 
#define BUFLEN 1600

#define UDP_INT 0
#define UDP_SHORT_REAL 1
#define UDP_FLOAT 2
#define UDP_STRING 3

const char EXIT_CMD[5] = "exit";
const char SUB_CMD[10] = "subscribe";
const char SH_SUB_CMD[10] = "s";
const char UNSUB_CMD[12] = "unsubscribe";
const char SH_UNSUB_CMD[12] = "u";
const char WHITESPACE[] = " \n\t";

const char UDP_INT_STR[] = "INT";
const char UDP_SHORT_REAL_STR[] = "SHORT_REAL";
const char UDP_FLOAT_STR[] = "FLOAT";
const char UDP_STRING_STR[] = "STRING";

#endif
