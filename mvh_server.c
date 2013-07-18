#define     __USE_GNU     1
#define     _GNU_SOURCE   1

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> 
#include <pthread.h>
#include <assert.h> 
#include <stdbool.h> 
#include <fcntl.h> 
#include <poll.h> 

#include "common.h" 
#include "handler.h"
#include "syscall_x64.h"
#include "color.h" 
#include "server_handler.h"
#include "mvh_server.h"

#define     MAX_LISTENER_SOCKET 10 

/* PRINT FUNCTIONS */ 
void print_thread_info(const struct thread_info * info, int fd){
    DPRINT(DEBUG_INFO, "%s process %d, %s thread %d, Cookie %d Monitored thread %d, Group %d, Session %d\n Connected over %d\n",  
                         info->visibility == PUBLIC ? "Public" : "Private", info->pid,
                         info->type == TRUSTED_THREAD ? "Trusted" : "Untrusted",info->tid, info->cookie,
                         info->monitored_thread_id, info->gid, info->sid, fd);  
}
void print_thread_pair(const struct thread_pair * pair){
    print_thread_info(&pair->trusted, pair->trusted_fd); 
    print_thread_info(&pair->untrusted, pair->untrusted_fd); 
}
void print_thread_group(const struct thread_group * group){
    print_thread_pair(&group->public); 
    print_thread_pair(&group->private);
}

/* foir the time being I leave the server single thread 
   if the list is empty I initialize a new one
    if ( syncronisation_group_ == NULL) {
       syncronisation_group_ = malloc(sizeof( struct syncronisation_group));
       memset(syncronisation_group_, 0 , sizeof( struct syncronisation_group));
       INIT_LIST_HEAD(&syncronisation_group_->list);
    } else {
         We are already dealing with some connection
         the connection starts always with an untrusted thread 
        if ( info.type= UNTRUSTED_THREAD && (

    }
*/ 

struct thread_group * syncronisation_group_;
struct thread_group connection;

int make_socket_non_blocking (int sfd){
  int flags, s;

  flags = fcntl (sfd, F_GETFL, 0);
  if (flags == -1)
    die("fcntl");

  flags |= O_NONBLOCK;
  s = fcntl (sfd, F_SETFL, flags);
  if (s == -1)
      die("fcntl");

  return 0;
}
static void start_application( int fd) {
    int res =-1;
    char buf[COMMAND] = {0}; 
    
    memset(buf, 0, COMMAND); 
    strncpy(buf, START_COMMAND, sizeof(START_COMMAND)); 
   
    INTR_RES(write(fd, buf, COMMAND), res); 
    if (res < COMMAND) 
          die("start process");
}
int receive_syscall_header( int fd, struct syscall_header * header) { 
    int res = -1; 
    struct iovec io[1];
    struct msghdr msg; 
  
    memset(header, 0, sizeof(header)); 
    memset((void*)&msg, 0, sizeof(msg));  
   
    // set header 
    io[0].iov_base = header; 
    io[0].iov_len = SIZE_HEADER; 

    msg.msg_iov=io; 
    msg.msg_iovlen=1; 
   
    res = recvmsg(fd, &msg, 0);
  
    if( res < 0)
       die("Error sending registers");

   assert(res ==  (SIZE_HEADER));
   return res; 
}

void  * handle_thread_pair(void * arg) {

    int fds[NFDS]={0}; 
    struct pollfd pollfds[NFDS]; 
    int res =-1; 

    fds[PUBLIC_TRUSTED]     = connection.public.trusted_fd; 
    fds[PUBLIC_UNTRUSTED]   = connection.public.untrusted_fd; 
    fds[PRIVATE_TRUSTED]    = connection.private.trusted_fd; 
    fds[PRIVATE_UNTRUSTED]  = connection.private.untrusted_fd; 
    
     /*
      * I must make the socket non-blocking 
      * because I don't know from which unstrused 
      * thread I wiil receive the first request
      */ 

    memset(pollfds, 0, sizeof(pollfds)); 
    for (int i=0; i < NFDS; i++){
        make_socket_non_blocking(fds[i]); 
        printf("%d : %d\n",i, fds[i]); 
        pollfds[i].fd = fds[i]; 
        pollfds[i].events =  POLLIN; /* there is data to read */ 
    }

    printf("Public untrusted %d  private untrusted %d\n", fds[PUBLIC_UNTRUSTED],fds[PRIVATE_UNTRUSTED]);  
    printf("Public trusted   %d  private trusted   %d\n", fds[PUBLIC_TRUSTED], fds[PRIVATE_TRUSTED]);  
    
    start_application(fds[PUBLIC_UNTRUSTED]); 
    start_application(fds[PRIVATE_UNTRUSTED]); 

    bool pub_req=false, priv_req=false; 
    
    do {
 
    struct syscall_header private_header, public_header; 

    /* 
     * = Read from the system call requests and call the correct handler 
     */ 
    
    res=poll(pollfds,NFDS,SERVER_TIMEOUT); 

    if (res == 0)
        irreversible_error("Connection Time out"); 
    else if ( res < 0 )
        die("pool"); 
    // there must be at maximun two fd ready  
    assert( res <= 2 ); 

    if ( !pub_req && pollfds[PUBLIC_UNTRUSTED].revents) {
        pub_req = true; 
        receive_syscall_header(fds[PUBLIC_UNTRUSTED], &public_header); 
        DPRINT(DEBUG_INFO, "Received request %d from %d for system call < %s > over %d\n", public_header.cookie, connection.public.untrusted.tid, 
                                                                                      syscall_names[public_header.syscall_num], fds[PUBLIC_UNTRUSTED]);
    }

    if (!priv_req && pollfds[PRIVATE_UNTRUSTED].revents) {
        priv_req = true; 
        receive_syscall_header(fds[PRIVATE_UNTRUSTED], &private_header); 
        DPRINT(DEBUG_INFO, "Received request %d from %d for system call < %s > over %d\n", private_header.cookie, connection.private.untrusted.tid, 
                                                                                      syscall_names[private_header.syscall_num], fds[PRIVATE_UNTRUSTED]);
    }

   if(pub_req && priv_req) {
  
        DPRINT(DEBUG_INFO, "Received an header pair\n");
       // we must call the handler 
        assert( private_header.syscall_num ==  public_header.syscall_num);
        int syscall_num = private_header.syscall_num; 
        syscall_table_server_[syscall_num].handler(fds, pollfds, &public_header, &private_header); 
       
        CLEAN_HEA(&public_header);
        CLEAN_HEA(&private_header);
        pub_req    =false; 
        priv_req   =false; 
        DPRINT(DEBUG_INFO, "Back to main server function \n");
    }
      
  } while(ALWAYS); 

     return NULL;
}

void update_thread_group(struct  thread_group *group,
                         struct thread_info * info,
                         int sockfd,
                         process_visibility visibility )
{
    struct thread_pair * pair = (visibility == PUBLIC ) ?
                                &(group->public) : &(group->private); 

    if(info->type == UNTRUSTED_THREAD) { 
          memcpy(&(pair->untrusted) ,info, SIZE_THREAD_INFO);
          pair->untrusted_fd = sockfd;
          pair->cookie = info->cookie; 
       } else  if(info->type == TRUSTED_THREAD) { 
          memcpy(&(pair->trusted) ,info, SIZE_THREAD_INFO);
          pair->trusted_fd = sockfd;
          assert(pair->cookie == info->cookie); 
      } else {
          printf("%d",info->type);
          die("Error unkown public thread"); 
    }
}
void handle_connection(int sockfd)
{

    struct thread_info info;
    int bytes_transfered = -1; 
    char buf[ACKNOWLEDGE]={0}; 
    pthread_t tid; 

    // get information about the untrusted process; 
    INTR_RES(read(sockfd, (char *)&info, sizeof(info)), bytes_transfered); 

    if (bytes_transfered < (int)sizeof(info))
        die("Read (thread info)"); 

    print_thread_info(&info, sockfd); 

    strncpy(buf, ACCEPTED, sizeof(ACCEPTED));
    // send acknowledge 
    INTR_RES(write(sockfd, buf, ACKNOWLEDGE), bytes_transfered); 

    if (bytes_transfered != ACKNOWLEDGE)
        die("Read (waiting for acknowledge)"); 
 
    update_thread_group(&connection,&info, sockfd, info.visibility);  

    // all threads are connected 
  if ( connection.public.trusted_fd   && connection.public.untrusted_fd && 
       connection.private.trusted_fd  && connection.private.untrusted_fd){
       pthread_create(&tid, NULL, handle_thread_pair, NULL); 
    }
}
void  run_mvh_server(int port) 
{
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr; 
    struct sockaddr_in client;
    int size_client= sizeof(struct sockaddr_in); 
   
    memset(&serv_addr, 0, sizeof(serv_addr));
    syncronisation_group_ = NULL; 

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port); 

    initialize_server_handler();
        

    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        die("Socket"); 

    /* Enable address reuse */
    int on = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
        die("SetSockOpt"); 
    
    
    if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0 )
        die("Bind"); 

    if (listen(listenfd, MAX_LISTENER_SOCKET) < 0) 
        die("Listen"); 

    while(ALWAYS)
    {
        memset((void *)&client, 0, sizeof(client));
        connfd = accept(listenfd, (struct sockaddr *) &client, (socklen_t *)&size_client); 
       
        if (connfd < 0 )
            die("Accept");
        
        DPRINT(DEBUG_INFO, "Accepted connection from %s \n", inet_ntoa(client.sin_addr));
        handle_connection(connfd);
           
    }
}
