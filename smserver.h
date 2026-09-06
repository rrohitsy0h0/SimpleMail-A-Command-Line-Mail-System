// Rohit Ranjeet Satpute
// 23CS10060

#ifndef SM_SERVER_H
#define SM_SERVER_H

#include<errno.h>
#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<sys/stat.h>
#include<arpa/inet.h>
#include<sys/types.h>
#include<stdbool.h>
#include<fcntl.h>
#include<sys/select.h>
#include<time.h>
#include<dirent.h>
#include<ctype.h>
#include<signal.h>

#define MAXN 1024
#define MAX_USERS 100
#define MAX_LEN 100
#define BASE_DIR "mailboxes"
#define LINE_BUF 4096
#define MAX_BODY 65536

typedef struct {
    int state; // 0:FROM,1:TO/SUB,2:BODY cmd,3:body lines
    int to_sent;
    int body_overflow; // 1 if body too large, drain until dot
    char from[512];
    char subject[512];
    char body[MAX_BODY+10];
    char recipients[MAX_USERS][MAX_LEN];
    int rec_count;
    int total_body;
}smtp2_state;

typedef struct {
    int state; // 0:auth,1:commands
    int attempts;
    int uidx;
    char nonce[9];
}smp_state;

void check_and_make_directory(FILE*);
void create_dir_if_not_exists(char*);
void print_to_terminal(const char*);
void set_nonblocking(int);
void greet(int);
void check_timeout(fd_set*);
void deliver_mail(int,smtp2_state*);
void disconnect_client(int,fd_set*);
void handle_signal(int);
void cleanup_on_shutdown();

int handle_smtp2(int,smtp2_state*,char*);
int handle_smp(int,smp_state*,char*);
int find_user(const char*);
int find_crlf(char*,int);
int extract_line(int,char*,int);

unsigned long djb2(const char*);
void random_nonce(char*);

#endif /* SM_SERVER_H */