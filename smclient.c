// Rohit Ranjeet Satpute
// 23CS10060

#include "smclient.h"

int sockfd;
int maxfd;
int PORT;

fd_set masterfd;

char ip_addr[BUFFER_SIZE];
char to_last[BUFFER_SIZE];
char del_last[BUFFER_SIZE];
char username[BUFFER_SIZE];
char noncee[BUFFER_SIZE];

int auth_attempts=0;
int to_count=0;
int client_mode;
int send_step;
int recv_step;

int main(int argc,char *argv[]){
    if(argc!=3){
        fprintf(stderr,"Usage: %s <server_ip> <port>\n",argv[0]);
        exit(EXIT_FAILURE);
    }

    PORT=atoi(argv[2]);
    strncpy(ip_addr,argv[1],sizeof(ip_addr)-1);
    ip_addr[sizeof(ip_addr)-1]='\0';

    struct sockaddr_in server_add;
    socklen_t sock_len=sizeof(struct sockaddr_in);

    sockfd=socket(AF_INET,SOCK_STREAM,0);
    if(sockfd==-1){
        fprintf(stderr,"ERROR: socket\n");
        exit(EXIT_FAILURE);
    }

    memset(&server_add,0,sizeof(server_add));
    server_add.sin_family=AF_INET;
    server_add.sin_port=htons(PORT);

    if(inet_pton(AF_INET,ip_addr,&server_add.sin_addr)<=0){
        fprintf(stderr,"ERROR: inet_pton\n");
        exit(EXIT_FAILURE);
    }

    if(connect(sockfd,(struct sockaddr*)&server_add,sock_len)==-1){
        fprintf(stderr,"ERROR: connect\n");
        perror("connect");
        exit(EXIT_FAILURE);
    }

    char greet[BUFFER_SIZE];
    if(read_line(sockfd,greet,BUFFER_SIZE)<=0){
        fprintf(stderr,"ERROR: failed to read greeting\n");
        exit(EXIT_FAILURE);
    }

    printf("Connected to SimpleMail server.\n");

    client_mode=CLIENT_MENU_STATE;
    send_step=SENDSTEP_IDLE;
    recv_step=RECVSTEP_USER;

    menu();

    set_unblocking(sockfd);

    FD_ZERO(&masterfd);
    FD_SET(sockfd,&masterfd);
    FD_SET(STDIN_FILENO,&masterfd);

    maxfd=sockfd;

    while(1){
        fd_set read_set=masterfd;

        if(select(maxfd+1,&read_set,NULL,NULL,NULL)==-1) continue;

        for(int i=0;i<=maxfd;i++){
            if(!FD_ISSET(i,&read_set)) continue;

            if(i==STDIN_FILENO) cli_read(sockfd);
            else if(i==sockfd) server_read(sockfd);
        }
    }

    return 0;
}

void set_unblocking(int fd){
    int flags=fcntl(fd,F_GETFL,0);
    if(flags==-1){
        fprintf(stderr,"ERROR: F_GETFL error\n");
        exit(EXIT_FAILURE);
    }

    if(fcntl(fd,F_SETFL,flags|O_NONBLOCK)==-1){
        fprintf(stderr,"ERROR: F_SETFL error\n");
        exit(EXIT_FAILURE);
    }

    return;
}

void server_read(int connect_fd){
    // OUTPUT HANDLING
    char buf[MAX_SIZE];
    int r=read_line(connect_fd,buf,MAX_SIZE);

    if(r<=0){
        printf("Server disconnected.\n");
        exit(EXIT_FAILURE);
    }

    if(client_mode==CLIENT_SEND_STATE){
        if(send_step==SENDSTEP_FROM){
            if(strcmp(buf,"OK")==0){
                printf("From (your name): ");
                fflush(stdout);
            }else if(strcmp(buf,"OK Sender accepted")==0){
                send_step=SENDSTEP_TO;
                printf("To (recipient username, empty line to finish): ");
                fflush(stdout);
            }else{
                printf("Error: %s\n",buf);
                send_t(connect_fd,"QUIT",4);
                send_step=SENDSTEP_IDLE;
            }
        }else if(send_step==SENDSTEP_TO){
            if(strncmp(buf,"OK Recipient accepted",21)==0){
                to_count++;
                printf("-> Recipient '%s' accepted.\nTo (recipient username, empty line to finish): ",to_last);
                fflush(stdout);
            }else if(strncmp(buf,"ERR No such user",16)==0){
                printf("-> Error: user '%s' does not exist on this server.\nTo (recipient username, empty line to finish): ",to_last);
                fflush(stdout);
            }else{
                printf("Error: %s\n",buf);
                send_t(connect_fd,"QUIT",4);
                send_step=SENDSTEP_IDLE;
            }
        }else if(send_step==SENDSTEP_SUBJECT){
            if(strncmp(buf,"OK Subject accepted",19)==0){
                send_t(connect_fd,"BODY",4);
                send_step=SENDSTEP_BODY_BEGIN;
            }else{
                printf("Error: %s\n",buf);
                send_t(connect_fd,"QUIT",4);
                send_step=SENDSTEP_IDLE;
            }
        }else if(send_step==SENDSTEP_BODY_BEGIN){
            if(strncmp(buf,"OK Send body",12)==0){
                printf("Body (type '.' on a line by itself to finish):\n");
                fflush(stdout);
                send_step=SENDSTEP_BODY_DATA;
            }else{
                printf("Error: %s\n",buf);
                send_t(connect_fd,"QUIT",4);
                send_step=SENDSTEP_IDLE;
            }
        }else if(send_step==SENDSTEP_IDLE){
            if(strncmp(buf,"OK Delivered",12)==0){
                int n=0;
                sscanf(buf,"OK Delivered to %d",&n);
                printf("Mail delivered to %d recipient%s.\n",n,n==1?"":"s");
                send_t(connect_fd,"QUIT",4);
            }else if(strncmp(buf,"BYE",3)==0){
                reconn();
                client_mode=CLIENT_MENU_STATE;
                send_step=SENDSTEP_IDLE;
                menu();
            }else{
                printf("%s\n",buf);
                send_t(connect_fd,"QUIT",4);
            }
        }
    }else if(client_mode==CLIENT_RECV_STATE){
        if(recv_step==RECVSTEP_USER){
            if(strncmp(buf,"OK",2)==0){
                r=read_line(connect_fd,buf,MAX_SIZE);
                if(r>0&&strncmp(buf,"AUTH REQUIRED ",14)==0){
                    strncpy(noncee,buf+14,sizeof(noncee)-1);
                    noncee[sizeof(noncee)-1]='\0';
                    printf("Username: ");
                    fflush(stdout);
                }
            }else{
                printf("Error: %s\n",buf);
                reconn();
                client_mode=CLIENT_MENU_STATE;
                menu();
            }
        }else if(recv_step==RECVSTEP_MENU){
            if(strncmp(buf,"OK Welcome",10)==0){
                printf("Welcome, %s!\n",username);
                mail_menu(username);
            }else if(strncmp(buf,"ERR Authentication failed",25)==0){
                printf("Authentication failed.\n");
                auth_attempts++;
                if(auth_attempts>=3){
                    printf("Too many failures.\n");
                    reconn();
                    client_mode=CLIENT_MENU_STATE;
                    menu();
                }else{
                    printf("Username: ");
                    fflush(stdout);
                    recv_step=RECVSTEP_USER;
                }
            }else if(strncmp(buf,"ERR Too many failures",21)==0){
                printf("Too many failures.\n");
                reconn();
                client_mode=CLIENT_MENU_STATE;
                menu();
            }else if(strncmp(buf,"OK",2)==0&&strstr(buf,"messages")!=NULL){
                printf("\n%-6s%-20s%-30s%-20s\n","ID","From","Subject","Date");
                printf("%-6s%-20s%-30s%-20s\n","---","----","-------","----");
                while(1){
                    r=read_line(connect_fd,buf,MAX_SIZE);
                    if(r<0 || strcmp(buf,".")==0) break;

                    char tmp[MAX_SIZE];
                    strncpy(tmp,buf,sizeof(tmp)-1);
                    tmp[sizeof(tmp)-1]=0;

                    char *id=strtok(tmp,"\t");
                    char *from=strtok(NULL,"\t");
                    char *sub=strtok(NULL,"\t");
                    char *date=strtok(NULL,"\t");

                    printf("%-6s%-20s%-30s%-20s\n",id?id:"",from?from:"",sub?sub:"",date?date:"");
                }
                mail_menu(username);
            }else if(strncmp(buf,"OK Deleted",10)==0){
                printf("Message %s deleted.\n",del_last);
                mail_menu(username);
            }else if(strncmp(buf,"ERR No such message",19)==0){
                printf("Error: No such message.\n");
                mail_menu(username);
            }else if(strncmp(buf,"OK",2)==0){
                while(1){
                    r=read_line(connect_fd,buf,MAX_SIZE);
                    if(r<0 || strcmp(buf,".")==0) break;
                    if(buf[0]=='.'&&buf[1]=='.') printf("%s\n",buf+1);
                    else printf("%s\n",buf);
                }
                mail_menu(username);
            }else if(strncmp(buf,"BYE",3)==0){
                reconn();
                client_mode=CLIENT_MENU_STATE;
                menu();
            }else{
                printf("%s\n",buf);
                mail_menu(username);
            }
        }
    }

    return;
}

void cli_read(int connect_fd){
    // INPUT HANDLING
    char ch_buf[MAX_SIZE];
    if(!fgets(ch_buf,MAX_SIZE,stdin)) return;
    ch_buf[strcspn(ch_buf,"\n")]='\0';

    if(client_mode==CLIENT_MENU_STATE){
        if(strcmp(ch_buf,"1")==0){
            client_mode=CLIENT_SEND_STATE;
            send_step=SENDSTEP_FROM;
            to_count=0;
            send_t(connect_fd,"MODE SEND",9);
        }else if(strcmp(ch_buf,"2")==0){
            client_mode=CLIENT_RECV_STATE;
            recv_step=RECVSTEP_USER;
            auth_attempts=0;
            send_t(connect_fd,"MODE RECV",9);
        }else if(strcmp(ch_buf,"3")==0){
            client_mode=CLIENT_QUIT_STATE;
            printf("Goodbye.\n");
            exit(EXIT_SUCCESS);
        }else menu();
    }else if(client_mode==CLIENT_SEND_STATE){
        if(send_step==SENDSTEP_IDLE){
            ;
        }else if(send_step==SENDSTEP_FROM){
            char from_msg[MAX_SIZE];
            snprintf(from_msg,MAX_SIZE,"FROM %s",ch_buf);
            send_t(connect_fd,from_msg,strlen(from_msg));
        }else if(send_step==SENDSTEP_TO){
            if(strlen(ch_buf)==0){
                if(to_count==0){
                    printf("To (recipient username, empty line to finish): ");
                    fflush(stdout);
                }else{
                    printf("Subject: ");
                    fflush(stdout);
                    send_step=SENDSTEP_SUBJECT;
                }
            }else{
                char to_msg[MAX_SIZE];
                snprintf(to_msg,MAX_SIZE,"TO %s",ch_buf);
                send_t(connect_fd,to_msg,strlen(to_msg));
                strncpy(to_last,ch_buf,sizeof(to_last)-1);
                to_last[sizeof(to_last)-1]='\0';
            }
        }else if(send_step==SENDSTEP_SUBJECT){
            if(strlen(ch_buf)==0){
                send_t(connect_fd,"SUB",3);
            }else{
                char sub_msg[MAX_SIZE];
                snprintf(sub_msg,MAX_SIZE,"SUB %s",ch_buf);
                send_t(connect_fd,sub_msg,strlen(sub_msg));
            }
        }else if(send_step==SENDSTEP_BODY_BEGIN){
            send_t(connect_fd,"BODY",4);
        }else if(send_step==SENDSTEP_BODY_DATA){
            if(strcmp(ch_buf,".")==0){
                send_t(connect_fd,".",1);
                send_step=SENDSTEP_IDLE;
            }else{
                if(ch_buf[0]=='.'){
                    char stuffed[MAX_SIZE];
                    snprintf(stuffed,MAX_SIZE,".%s",ch_buf);
                    send_t(connect_fd,stuffed,strlen(stuffed));
                }else send_t(connect_fd,ch_buf,strlen(ch_buf));
            }
        }
    }else if(client_mode==CLIENT_RECV_STATE){
        if(recv_step==RECVSTEP_USER){
            strncpy(username,ch_buf,sizeof(username)-1);
            username[sizeof(username)-1]='\0';
            printf("Password: ");
            fflush(stdout);
            recv_step=RECVSTEP_PASS;
        }else if(recv_step==RECVSTEP_PASS){
            char concat[MAX_SIZE];
            snprintf(concat,sizeof(concat),"%s%s",ch_buf,noncee);
            unsigned long h=djb2(concat);
            char msg[MAX_SIZE];
            snprintf(msg,MAX_SIZE,"AUTH %s %lu",username,h);
            send_t(connect_fd,msg,strlen(msg));
            recv_step=RECVSTEP_MENU;
        }else if(recv_step==RECVSTEP_MENU){
            if(strcmp(ch_buf,"1")==0){
                send_t(connect_fd,"LIST",4);
            }else if(strcmp(ch_buf,"2")==0){
                printf("Enter message ID: ");
                fflush(stdout);
                recv_step=RECVSTEP_READ_ID;
            }else if(strcmp(ch_buf,"3")==0){
                printf("Enter message ID: ");
                fflush(stdout);
                recv_step=RECVSTEP_DEL_ID;
            }else if(strcmp(ch_buf,"4")==0){
                send_t(connect_fd,"QUIT",4);
                printf("Logged out.\n");
            }else mail_menu(username);
        }else if(recv_step==RECVSTEP_READ_ID){
            char msg[128];
            snprintf(msg,sizeof(msg),"READ %s",ch_buf);
            send_t(connect_fd,msg,strlen(msg));
            recv_step=RECVSTEP_MENU;
        }else if(recv_step==RECVSTEP_DEL_ID){
            strncpy(del_last,ch_buf,sizeof(del_last)-1);
            del_last[sizeof(del_last)-1]='\0';
            char msg[128];
            snprintf(msg,sizeof(msg),"DELETE %s",ch_buf);
            send_t(connect_fd,msg,strlen(msg));
            recv_step=RECVSTEP_MENU;
        }
    }

    return;
}

void reconn(){
    close(sockfd);
    FD_CLR(sockfd,&masterfd);

    sockfd=socket(AF_INET,SOCK_STREAM,0);
    if(sockfd<0){
        fprintf(stderr,"ERROR: socket\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_port=htons(PORT);
    inet_pton(AF_INET,ip_addr,&addr.sin_addr);

    if(connect(sockfd,(struct sockaddr*)&addr,sizeof(addr))==-1){
        fprintf(stderr,"ERROR: reconnect\n");
        exit(EXIT_FAILURE);
    }

    char buf[BUFFER_SIZE];
    if(read_line(sockfd,buf,BUFFER_SIZE)<=0){
        fprintf(stderr,"ERROR: reconnect greeting\n");
        exit(EXIT_FAILURE);
    }

    set_unblocking(sockfd);
    FD_SET(sockfd,&masterfd);

    if(sockfd>maxfd) maxfd=sockfd;

    return;
}

void mail_menu(const char *user){
    printf("Mailbox for %s\n1. List all messages\n2. Read a message\n3. Delete a message\n4. Logout\n> ",user);
    fflush(stdout);

    return;
}

void menu(){
    printf("1. Send a mail\n2. Check my mailbox\n3. Quit\n> ");
    fflush(stdout);

    return;
}

unsigned long djb2(const char *str){
    unsigned long hash=5381;
    int c;
    while((c=*str++)) hash=((hash<<5)+hash)+c;

    return hash;
}

int read_line(int fd,char *inbuf,int max_len){
    if(max_len<=1) return 0;

    int i=0;
    char prev_c='\0';
    int got_data=0;

    while(i<max_len-1){
        char c;
        int n=recv(fd,&c,1,0);

        if(n==-1){
            if(errno==EAGAIN || errno==EWOULDBLOCK) continue;
            return -1;
        }else if(n==0){
            if(!got_data) return -1;
            break;
        }

        got_data=1;

        if(c==DELIM[1]&&prev_c==DELIM[0]){
            if(i>0) i--;
            break;
        }

        prev_c=c;
        inbuf[i++]=c;
    }
    inbuf[i]='\0';

    return i;
}

int send_t(int fd,const char *msg,int max_len){
    if(send_all(fd,msg,max_len)<0) return -1;
    if(send_all(fd,DELIM,strlen(DELIM))<0) return -1;

    return 0;
}

static int send_all(int fd,const char *buf,int len){
    int sent=0;

    while(sent<len){
        int n=send(fd,buf+sent,len-sent,0);
        if(n==-1){
            if(errno==EAGAIN || errno==EWOULDBLOCK) continue;
            return -1;
        }
        sent+=n;
    }

    return 0;
}