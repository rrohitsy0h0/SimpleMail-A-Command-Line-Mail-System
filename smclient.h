// Rohit Ranjeet Satpute
// 23CS10060

#ifndef SM_CLIENT_H
#define SM_CLIENT_H

#include<errno.h>
#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/types.h>
#include<stdbool.h>
#include<fcntl.h>
#include<sys/select.h>

#define MAXN 4096
#define MAX_SIZE 2048
#define BUFFER_SIZE 512
#define DELIM "\r\n"

unsigned long djb2(const char*);

void check_mailbox(char*,int);
void set_unblocking(int);
void server_read(int);
void cli_read(int);
void reconn();
void mail_menu(const char *);
void menu();

int read_line(int,char *,int);
int send_t(int,const char *,int);
static int send_all(int,const char *,int);

#define CLIENT_MENU_STATE 0
#define CLIENT_SEND_STATE 1
#define CLIENT_RECV_STATE 2
#define CLIENT_QUIT_STATE 3

#define SENDSTEP_IDLE 0
#define SENDSTEP_FROM 1
#define SENDSTEP_TO 2
#define SENDSTEP_SUBJECT 3
#define SENDSTEP_BODY_BEGIN 4
#define SENDSTEP_BODY_DATA 5

#define RECVSTEP_USER 0
#define RECVSTEP_PASS 1
#define RECVSTEP_MENU 2
#define RECVSTEP_READ_ID 3
#define RECVSTEP_DEL_ID 4

extern int client_mode;
extern int send_step;
extern int recv_step;


#endif /* SM_CLIENT_H */