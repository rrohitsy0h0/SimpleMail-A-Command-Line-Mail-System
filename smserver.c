// Rohit Ranjeet Satpute
// 23CS10060

#include "smserver.h"

smtp2_state smtp2_states[MAXN];
smp_state smp_states[MAXN];

char username[MAX_USERS][MAX_LEN];
char password[MAX_USERS][MAX_LEN];

int next_id[MAX_USERS];
int active_clients[MAXN];
int mode_received[MAXN]; // 0:none,1:smtp2,2:smp
time_t connect_time[MAXN];
int drop_oversize_line[MAXN];

char linebuf[MAXN][LINE_BUF];
int linebuf_len[MAXN];

int user_count=0;
int PORT;
int listen_fd;
char userfile_name[256]; // store filename for logging
volatile sig_atomic_t shutdown_req=0;

int main(int argc,char *argv[]){
    if(argc!=3){
        fprintf(stderr,"Enter in the format :- ./smserver <port> <userfile>\n");
        exit(EXIT_FAILURE);
    }

    PORT=atoi(argv[1]);
    strncpy(userfile_name,argv[2],sizeof(userfile_name)-1);
    userfile_name[sizeof(userfile_name)-1]=0;
    srand(time(NULL));

    signal(SIGINT,handle_signal);
    signal(SIGQUIT,handle_signal);

    FILE *details=fopen(argv[2],"r");
    if(!details){
        perror("File not found");
        exit(errno);
    }

    memset(username,0,sizeof(username));
    memset(password,0,sizeof(password));

    check_and_make_directory(details);
    fclose(details);

    // init next_id by scanning existing mailboxes
    for(int i=0;i<user_count;i++){
        char path[256];
        snprintf(path,sizeof(path),"%s/%s",BASE_DIR,username[i]);
        DIR *d=opendir(path);
        
        int maxid=0;
        if(d){
            struct dirent *ent;
            while((ent=readdir(d))!=NULL){
                int id;
                if(sscanf(ent->d_name,"%d.txt",&id)==1){
                    if(id>maxid) maxid=id;
                }
            }
            closedir(d);
        }
        next_id[i]=maxid+1;
    }

    listen_fd=socket(AF_INET,SOCK_STREAM,0);
    if(listen_fd<0){
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt=1;
    setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family=AF_INET;
    server_addr.sin_addr.s_addr=INADDR_ANY;
    server_addr.sin_port=htons(PORT);

    if(bind(listen_fd,(struct sockaddr*)&server_addr,sizeof(server_addr))<0){
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if(listen(listen_fd,10)<0){
        perror("listen");
        exit(EXIT_FAILURE);
    }

    set_nonblocking(listen_fd);

    char logmsg[512];
    snprintf(logmsg,sizeof(logmsg),"Server started on port %d",PORT);
    print_to_terminal(logmsg);
    snprintf(logmsg,sizeof(logmsg),"Loaded %d users from %s",user_count,userfile_name);
    print_to_terminal(logmsg);

    fd_set master_set,read_set;
    FD_ZERO(&master_set);
    FD_SET(listen_fd,&master_set);
    int max_fd=listen_fd;

    memset(active_clients,0,sizeof(active_clients));
    memset(mode_received,0,sizeof(mode_received));
    memset(linebuf_len,0,sizeof(linebuf_len));
    memset(drop_oversize_line,0,sizeof(drop_oversize_line));

    while(true){
        read_set=master_set;

        struct timeval tv;
        tv.tv_sec=1;
        tv.tv_usec=0;

        int ret=select(max_fd+1,&read_set,NULL,NULL,&tv);
        if(ret<0){
            if(errno==EINTR){
                if(shutdown_req) break;
                continue;
            }
            perror("select");
            break;
        }

        if(shutdown_req) break;

        // accept new connections
        if(FD_ISSET(listen_fd,&read_set)){
            struct sockaddr_in cli_addr;
            socklen_t cli_len=sizeof(cli_addr);

            int new_fd=accept(listen_fd,(struct sockaddr*)&cli_addr,&cli_len);
            if(new_fd>=0){
                set_nonblocking(new_fd);
                FD_SET(new_fd,&master_set);
                if(new_fd>max_fd) max_fd=new_fd;

                for(int i=0;i<MAXN;i++){
                    if(active_clients[i]==0){
                        active_clients[i]=new_fd;
                        connect_time[i]=time(NULL);
                        mode_received[i]=0;
                        linebuf_len[i]=0;
                        drop_oversize_line[i]=0;
                        break;
                    }
                }

                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET,&cli_addr.sin_addr,ip,sizeof(ip));
                snprintf(logmsg,sizeof(logmsg),"New connection from %s:%d",ip,ntohs(cli_addr.sin_port));
                print_to_terminal(logmsg);

                greet(new_fd);
            }
        }

        // handle client data
        for(int i=0;i<MAXN;i++){
            int fd=active_clients[i];
            if(fd<=0||!FD_ISSET(fd,&read_set)) continue;

            if(drop_oversize_line[i]){
                char tmp[512];
                int n=read(fd,tmp,sizeof(tmp));
                if(n==0||(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK)){
                    print_to_terminal("Client disconnected");
                    disconnect_client(i,&master_set);
                }else if(n>0){
                    for(int k=0;k<n-1;k++){
                        if(tmp[k]=='\r' && tmp[k+1]=='\n'){
                            drop_oversize_line[i]=0;
                            break;
                        }
                    }
                }
                continue;
            }

            int n=read(fd,linebuf[i]+linebuf_len[i],LINE_BUF-linebuf_len[i]-1);

            if(n==0||(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK)){
                // client disconnected
                print_to_terminal("Client disconnected");
                disconnect_client(i,&master_set);
                continue;
            }
            if(n<0) continue; // EAGAIN

            linebuf_len[i]+=n;

            if(linebuf_len[i]>=LINE_BUF-1 && find_crlf(linebuf[i],linebuf_len[i])<0){
                write(fd,"ERR Line too long\r\n",19);
                linebuf_len[i]=0;
                drop_oversize_line[i]=1;
                continue;
            }

            char line[LINE_BUF];
            while(extract_line(i,line,sizeof(line))>=0){

                if(strlen(line)>510){
                    write(fd,"ERR Line too long\r\n",19);
                    continue;
                }

                if(mode_received[i]==0){
                    // mode selection
                    if(strcmp(line,"MODE SEND")==0){
                        write(fd,"OK\r\n",4);
                        mode_received[i]=1;
                        memset(&smtp2_states[i],0,sizeof(smtp2_state));

                        print_to_terminal("Client selected MODE SEND");
                    }else if(strcmp(line,"MODE RECV")==0){
                        write(fd,"OK\r\n",4);
                        mode_received[i]=2;
                        memset(&smp_states[i],0,sizeof(smp_state));
                        smp_states[i].uidx=-1;

                        random_nonce(smp_states[i].nonce);
                        char authline[64];
                        snprintf(authline,sizeof(authline),"AUTH REQUIRED %s\r\n",smp_states[i].nonce);
                        write(fd,authline,strlen(authline));
                        
                        print_to_terminal("Client selected MODE RECV");
                    }else write(fd,"ERR Unknown mode\r\n",18);
                }else if(mode_received[i]==1){
                    if(handle_smtp2(fd,&smtp2_states[i],line)){
                        disconnect_client(i,&master_set);
                        break;
                    }
                }else if(mode_received[i]==2){
                    if(handle_smp(fd,&smp_states[i],line)){
                        disconnect_client(i,&master_set);
                        break;
                    }
                }
            }
        }

        check_timeout(&master_set);
    }

    cleanup_on_shutdown();

    return 0;
}

void handle_signal(int sig){
    shutdown_req=sig;

    return;
}

void cleanup_on_shutdown(){
    for(int i=0;i<MAXN;i++){
        if(active_clients[i]>0){
            close(active_clients[i]);
            active_clients[i]=0;
        }
    }

    if(listen_fd>0){
        close(listen_fd);
        listen_fd=-1;
    }

    print_to_terminal("Server shutting down");

    return;
}

void disconnect_client(int slot,fd_set *master){
    int fd=active_clients[slot];
    if(fd>0){
        close(fd);
        FD_CLR(fd,master);
        active_clients[slot]=0;
        mode_received[slot]=0;
        linebuf_len[slot]=0;
        drop_oversize_line[slot]=0;
    }

    return;
}

void print_to_terminal(const char *msg){
    time_t now=time(NULL);
    struct tm *t=localtime(&now);
    printf("[%04d-%02d-%02d %02d:%02d:%02d] %s\n",t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec,msg);
    fflush(stdout);

    return;
}

void set_nonblocking(int fd){
    int flags=fcntl(fd,F_GETFL,0);
    fcntl(fd,F_SETFL,flags|O_NONBLOCK);

    return;
}

void greet(int fd){
    const char *msg="WELCOME SimpleMail v1.0\r\n";
    write(fd,msg,strlen(msg));

    return;
}

void check_timeout(fd_set *master){
    time_t now=time(NULL);
    for(int i=0;i<MAXN;i++){
        if(active_clients[i]>0 && mode_received[i]==0){
            if(now-connect_time[i]>=30){
                print_to_terminal("Client timed out (no mode selected)");
                disconnect_client(i,master);
            }
        }
    }

    return;
}

void check_and_make_directory(FILE *detail){
    char line[256];

    create_dir_if_not_exists(BASE_DIR);

    while(fgets(line,sizeof(line),detail)){
        char user_temp[MAX_LEN]={0};
        char pass_temp[MAX_LEN]={0};
        char extra[MAX_LEN]={0};

        line[strcspn(line,"\r\n")]=0;
        if(strlen(line)==0) continue;

        if(sscanf(line,"%99s %99s %99s",user_temp,pass_temp,extra)!=2){
            fprintf(stderr,"Malformed userfile\n");
            exit(EXIT_FAILURE);
        }

        int ulen=strlen(user_temp);
        int plen=strlen(pass_temp);
        if(ulen<1||ulen>20||plen<1||plen>30){
            fprintf(stderr,"Malformed userfile\n");
            exit(EXIT_FAILURE);
        }

        for(int i=0;i<ulen;i++){
            if(user_temp[i]<'a'||user_temp[i]>'z'){
                fprintf(stderr,"Malformed userfile\n");
                exit(EXIT_FAILURE);
            }
        }
        for(int i=0;i<plen;i++){
            if(!isalnum((unsigned char)pass_temp[i])){
                fprintf(stderr,"Malformed userfile\n");
                exit(EXIT_FAILURE);
            }
        }

        if(find_user(user_temp)>=0){
            fprintf(stderr,"Malformed userfile\n");
            exit(EXIT_FAILURE);
        }

        if(user_count>=MAX_USERS){
            fprintf(stderr,"Too many users\n");
            exit(EXIT_FAILURE);
        }

        strncpy(username[user_count],user_temp,MAX_LEN-1);
        strncpy(password[user_count],pass_temp,MAX_LEN-1);
        user_count++;

        char path[256];
        snprintf(path,sizeof(path),"%s/%s",BASE_DIR,user_temp);
        create_dir_if_not_exists(path);
    }

    return;
}

void create_dir_if_not_exists(char *path){
    struct stat st={0};
    if(stat(path,&st)==-1) mkdir(path,0700);

    return;
}

int find_user(const char *name){
    for(int i=0;i<user_count;i++){
        if(strcasecmp(name,username[i])==0) return i;
    }

    return -1;
}

int find_crlf(char *buf,int len){
    for(int i=0;i<len-1;i++){
        if(buf[i]=='\r' && buf[i+1]=='\n') return i;
    }

    return -1;
}

int extract_line(int slot,char *line,int maxlen){
    int pos=find_crlf(linebuf[slot],linebuf_len[slot]);
    if(pos<0) return -1;

    int len=pos;
    if(len>=maxlen) len=maxlen-1;
    
    memcpy(line,linebuf[slot],len);
    line[len]=0;
    
    int consumed=pos+2;
    memmove(linebuf[slot],linebuf[slot]+consumed,linebuf_len[slot]-consumed);
    linebuf_len[slot]-=consumed;

    return len;
}

// SMTP2 protocol handler (one line at a time, non-blocking)
int handle_smtp2(int fd,smtp2_state *s,char *line){
    if(s->state==0){
        // expecting FROM or QUIT
        if(strncmp(line,"FROM ",5)==0){
            strncpy(s->from,line+5,sizeof(s->from)-1);
            s->from[sizeof(s->from)-1]=0;

            write(fd,"OK Sender accepted\r\n",20);

            s->state=1;
            s->to_sent=0;
        }else if(strcmp(line,"QUIT")==0){
            write(fd,"BYE\r\n",5);
            print_to_terminal("Client disconnected (QUIT)");
            return 1;
        }else write(fd,"ERR Bad sequence\r\n",18);

    }else if(s->state==1){
        // expecting TO, SUB, or QUIT
        if(strncmp(line,"TO ",3)==0){
            char *user=line+3;
            int idx=find_user(user);

            if(idx>=0){
                // check for duplicate recipients
                int dup=0;
                for(int d=0;d<s->rec_count;d++){
                    if(strcasecmp(s->recipients[d],username[idx])==0){
                        dup=1;
                        break;
                    }
                }
                if(dup){
                    write(fd,"OK Recipient accepted\r\n",23);
                    return 0;
                }

                if(s->rec_count>=MAX_USERS){
                    write(fd,"ERR Too many recipients\r\n",25);
                    return 0;
                }
                strcpy(s->recipients[s->rec_count++],username[idx]);
                s->to_sent=1;
                write(fd,"OK Recipient accepted\r\n",23);

            }else write(fd,"ERR No such user\r\n",18);
        }else if(strncmp(line,"SUB ",4)==0||strcmp(line,"SUB")==0){
            if(!s->to_sent){
                write(fd,"ERR Bad sequence\r\n",18);
                return 0;
            }

            if(strncmp(line,"SUB ",4)==0 && strlen(line+4)>0){
                strncpy(s->subject,line+4,sizeof(s->subject)-1);
                s->subject[sizeof(s->subject)-1]=0;
            }else strcpy(s->subject,"(no subject)");

            write(fd,"OK Subject accepted\r\n",21);
            s->state=2;
        }else if(strcmp(line,"QUIT")==0){
            write(fd,"BYE\r\n",5);
            print_to_terminal("Client disconnected (QUIT)");

            return 1;
        }else write(fd,"ERR Bad sequence\r\n",18);
    }else if(s->state==2){
        // expecting BODY or QUIT
        if(strcmp(line,"BODY")==0){
            if(s->rec_count==0){
                write(fd,"ERR No valid recipients\r\n",25);
                s->state=0;
                return 0;
            }

            write(fd,"OK Send body, end with CRLF.CRLF\r\n",34);

            s->state=3;
            s->total_body=0;
            s->body_overflow=0;
        }else if(strcmp(line,"QUIT")==0){
            write(fd,"BYE\r\n",5);
            print_to_terminal("Client disconnected (QUIT)");

            return 1;
        }else write(fd,"ERR Bad sequence\r\n",18);
    }else if(s->state==3){
        // body lines
        if(s->body_overflow){
            // drain remaining body lines after overflow until dot
            if(strcmp(line,".")==0){
                s->body_overflow=0;
                s->state=0;
                s->to_sent=0;
                s->rec_count=0;
                s->total_body=0;
                memset(s->from,0,sizeof(s->from));
                memset(s->subject,0,sizeof(s->subject));
                memset(s->body,0,sizeof(s->body));
            }
            // else: silently discard lines until dot
        }else if(strcmp(line,".")==0){
            deliver_mail(fd,s);
        }else{
            char *lptr=line;
            if(lptr[0]=='.') lptr++; // de-stuffing

            int l=strlen(lptr);
            if(s->total_body+l+1>MAX_BODY){
                write(fd,"ERR Body too large\r\n",20);
                s->body_overflow=1;
                return 0;
            }

            memcpy(s->body+s->total_body,lptr,l);
            s->total_body+=l;
            s->body[s->total_body++]='\n';
        }
    }
    return 0;
}

void deliver_mail(int fd,smtp2_state *s){
    char logmsg[512];
    time_t now=time(NULL);
    struct tm *t=localtime(&now);
    char date[64];
    strftime(date,sizeof(date),"%Y-%m-%d %H:%M:%S",t);

    // remove trailing newline from body if present
    if(s->total_body>0 && s->body[s->total_body-1]=='\n'){
        s->total_body--;
        s->body[s->total_body]=0;
    }

    int delivered=0;
    for(int i=0;i<s->rec_count;i++){
        int uidx=find_user(s->recipients[i]);
        if(uidx<0) continue;

        char mbox[256];
        snprintf(mbox,sizeof(mbox),"%s/%s",BASE_DIR,s->recipients[i]);

        int id=next_id[uidx];
        next_id[uidx]++;

        char fname[300];
        snprintf(fname,sizeof(fname),"%s/%d.txt",mbox,id);

        FILE *f=fopen(fname,"w");
        if(!f) continue;

        fprintf(f,"From: %s\n",s->from);
        fprintf(f,"To: ");

        for(int j=0;j<s->rec_count;j++){
            fprintf(f,"%s",s->recipients[j]);
            if(j!=s->rec_count-1) fprintf(f,",");
        }

        fprintf(f,"\nSubject: %s\n",s->subject);
        fprintf(f,"Date: %s\n",date);
        fprintf(f,"---\n");
        fwrite(s->body,1,s->total_body,f);
        fprintf(f,"\n");
        fclose(f);
        delivered++;
    }

    char okmsg[128];
    snprintf(okmsg,sizeof(okmsg),"OK Delivered to %d mailboxes\r\n",delivered);
    write(fd,okmsg,strlen(okmsg));

    // build recipient list string for log
    char reclist[512];
    reclist[0]=0;
    for(int i=0;i<s->rec_count;i++){
        if(i>0) strcat(reclist,",");
        strcat(reclist,s->recipients[i]);
    }
    snprintf(logmsg,sizeof(logmsg),"Mail delivered from \"%s\" to [%s] (%d recipient%s)",
        s->from,reclist,delivered,delivered==1?"":"s");
    print_to_terminal(logmsg);

    // reset for next mail in same session
    s->state=0;
    s->to_sent=0;
    s->rec_count=0;
    s->total_body=0;
    s->body_overflow=0;
    memset(s->from,0,sizeof(s->from));
    memset(s->subject,0,sizeof(s->subject));
    memset(s->body,0,sizeof(s->body));

    return;
}

// SMP protocol handler (one line at a time, non-blocking)
int handle_smp(int fd,smp_state *s,char *line){
    char logmsg[256];

    if(s->state==0){
        // authentication phase
        if(strncmp(line,"AUTH ",5)!=0){
            write(fd,"ERR Authentication failed\r\n",27);
            print_to_terminal("Authentication failed");
            s->attempts++;

            if(s->attempts>=3){
                write(fd,"ERR Too many failures\r\n",23);
                print_to_terminal("Authentication failed (too many failures)");
                return 1;
            }
            return 0;
        }

        char user[32],hashstr[32];
        if(sscanf(line+5,"%31s %31s",user,hashstr)!=2){
            write(fd,"ERR Authentication failed\r\n",27);
            print_to_terminal("Authentication failed");
            s->attempts++;

            if(s->attempts>=3){
                write(fd,"ERR Too many failures\r\n",23);
                print_to_terminal("Authentication failed (too many failures)");
                return 1;
            }
            return 0;
        }

        int uidx=find_user(user);
        if(uidx<0){
            write(fd,"ERR Authentication failed\r\n",27);
            print_to_terminal("Authentication failed");
            s->attempts++;

            if(s->attempts>=3){
                write(fd,"ERR Too many failures\r\n",23);
                print_to_terminal("Authentication failed (too many failures)");
                return 1;
            }
            return 0;
        }

        char combo[128];
        snprintf(combo,sizeof(combo),"%s%s",password[uidx],s->nonce);

        unsigned long hval=djb2(combo);
        unsigned long clientval=strtoul(hashstr,NULL,10);

        if(hval!=clientval){
            write(fd,"ERR Authentication failed\r\n",27);
            print_to_terminal("Authentication failed");
            s->attempts++;

            if(s->attempts>=3){
                write(fd,"ERR Too many failures\r\n",23);
                print_to_terminal("Authentication failed (too many failures)");
                return 1;
            }
            return 0;
        }

        // auth success
        s->uidx=uidx;
        s->state=1;

        char okmsg[64];
        snprintf(okmsg,sizeof(okmsg),"OK Welcome %s\r\n",username[uidx]);
        write(fd,okmsg,strlen(okmsg));

        snprintf(logmsg,sizeof(logmsg),"Authentication successful for user %s",username[uidx]);
        print_to_terminal(logmsg);

        return 0;
    }else if(s->state==1){
        // mailbox commands
        char mbox[256];
        snprintf(mbox,sizeof(mbox),"%s/%s",BASE_DIR,username[s->uidx]);
        int maxid=next_id[s->uidx];

        if(strcmp(line,"LIST")==0){
            // count first
            int count=0;
            for(int j=1;j<maxid;j++){
                char fname[300];
                snprintf(fname,sizeof(fname),"%s/%d.txt",mbox,j);
                struct stat st;
                if(stat(fname,&st)==0) count++;
            }

            char okmsg[64];
            snprintf(okmsg,sizeof(okmsg),"OK %d messages\r\n",count);
            write(fd,okmsg,strlen(okmsg));

            // send each mail summary
            for(int j=1;j<maxid;j++){
                char fname[300];
                snprintf(fname,sizeof(fname),"%s/%d.txt",mbox,j);
                FILE *f=fopen(fname,"r");
                if(!f) continue;

                char fromline[256],toline[256],subline[256],dateline[128];
                fgets(fromline,sizeof(fromline),f);
                fgets(toline,sizeof(toline),f);
                fgets(subline,sizeof(subline),f);
                fgets(dateline,sizeof(dateline),f);
                fclose(f);

                fromline[strcspn(fromline,"\n")]=0;
                subline[strcspn(subline,"\n")]=0;
                dateline[strcspn(dateline,"\n")]=0;

                char *fdisp=strchr(fromline,':');
                char *sdisp=strchr(subline,':');
                char *ddisp=strchr(dateline,':');

                fdisp=fdisp?fdisp+2:fromline;
                sdisp=sdisp?sdisp+2:subline;
                ddisp=ddisp?ddisp+2:dateline;

                char msg[512];
                snprintf(msg,sizeof(msg),"%d\t%s\t%s\t%s\r\n",j,fdisp,sdisp,ddisp);
                write(fd,msg,strlen(msg));
            }
            write(fd,".\r\n",3);

            snprintf(logmsg,sizeof(logmsg),"User %s LIST mailbox",username[s->uidx]);
            print_to_terminal(logmsg);

        }else if(strncmp(line,"READ ",5)==0){
            int id=atoi(line+5);
            char fname[300];
            snprintf(fname,sizeof(fname),"%s/%d.txt",mbox,id);

            FILE *f=fopen(fname,"r");
            if(!f){
                write(fd,"ERR No such message\r\n",21);
                return 0;
            }

            write(fd,"OK\r\n",4);
            char fline[1024];
            while(fgets(fline,sizeof(fline),f)){
                fline[strcspn(fline,"\n")]=0;
                // dot-stuffing: if line starts with '.', prepend extra '.'
                if(fline[0]=='.'){
                    char dotline[1028];
                    snprintf(dotline,sizeof(dotline),".%s\r\n",fline);
                    write(fd,dotline,strlen(dotline));
                }else{
                    char outline[1028];
                    snprintf(outline,sizeof(outline),"%s\r\n",fline);
                    write(fd,outline,strlen(outline));
                }
            }

            fclose(f);
            write(fd,".\r\n",3);

            snprintf(logmsg,sizeof(logmsg),"User %s READ message %d",username[s->uidx],id);
            print_to_terminal(logmsg);

        }else if(strncmp(line,"DELETE ",7)==0){
            int id=atoi(line+7);
            char fname[300];
            snprintf(fname,sizeof(fname),"%s/%d.txt",mbox,id);

            if(remove(fname)==0){
                write(fd,"OK Deleted\r\n",12);
                snprintf(logmsg,sizeof(logmsg),"User %s DELETE message %d",username[s->uidx],id);
                print_to_terminal(logmsg);
            }else write(fd,"ERR No such message\r\n",21);

        }else if(strcmp(line,"COUNT")==0){
            int count=0;
            for(int j=1;j<maxid;j++){
                char fname[300];
                snprintf(fname,sizeof(fname),"%s/%d.txt",mbox,j);
                struct stat st;
                if(stat(fname,&st)==0) count++;
            }

            char okmsg[64];
            snprintf(okmsg,sizeof(okmsg),"OK %d\r\n",count);
            write(fd,okmsg,strlen(okmsg));

        }else if(strcmp(line,"QUIT")==0){
            write(fd,"BYE\r\n",5);
            print_to_terminal("Client disconnected (QUIT)");

            return 1;
        }else write(fd,"ERR Unknown command\r\n",21);
    }

    return 0;
}

unsigned long djb2(const char *str){
    unsigned long hash=5381;
    int c;
    while((c=*str++)) hash=((hash<<5)+hash)+c;
    
    return hash;
}

void random_nonce(char *nonce){
    static const char alphanum[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for(int i=0;i<8;i++) nonce[i]=alphanum[rand()%62];
    nonce[8]=0;

    return;
}