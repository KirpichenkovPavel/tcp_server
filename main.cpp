/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   main.cpp
 * Author: pavel
 *
 * Created on September 13, 2016, 8:01 PM
 */
#include <cstdlib>
#include <cstdio>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <linux/in.h>
#include <errno.h>
#include <pthread.h>
#include <vector>
#include <map>
#include <stdexcept>
#define PSIZE 32
#define FILLER 126
#define PORT 10501
using namespace std;

struct t_arg {
    int sock_d;
    int q_len;
};

struct handle_sockets_t {
    int *sd;
    pthread_t *thread;
    int closed;
};

// basic currency measure
typedef int step_t;

typedef struct currency_t{
    char *name;
    step_t rate;
    step_t change;
    vector<step_t> hist;
} currency_t; 

struct cmp_str
{
   bool operator()(char const *a, char const *b)
   {
      return strcmp(a, b) < 0;
   }
};

map<char*, currency_t, cmp_str> curr_map;
vector<currency_t> curr_list;

vector<handle_sockets_t> clients;

pthread_mutex_t client_lock, currency_lock;


void *t_listen(void* arg);
int error(int fd, char* mes);
int sendall(int s, char *buf, int len, int flags);
int recvall(int sockfd, void *buf, int len, int flags);
void *t_handle(void* arg);
void print_list(void);
void disconnect(int socket);
void list(int socket);
void addcurr(int socket, char *pckg);
void rmcurr(int socket, char *pckg);
void setrate(int socket, char *pckg);
void history(int socket, char *pckg);
void senderr(int socket);
void sendok(int socket);
char *get_sum(char *pckg, int size, int offset);
char* get_code(char* package);
char* get_name(char* package);
char* get_rate(char* package);
char* get_change(char* package);
char *make_package(char type, currency_t c);

int error(int fd, char* mes) {
    printf("%s\n%s\n", mes, strerror(errno));
    if (fd) close(fd);
    //int *code = new int;
//    *code = -1;
    //return code;
    return -1;
}

int sendall(int s, char *buf, int len, int flags) {
    int total = 0;
    int n;

    while (total < len) {
        n = send(s, buf + total, len - total, flags);
        if (n < 1) {
            break;
        }
        total += n;
    }
    return (n < 1 ? -1 : total);
}

int recvall(int sockfd, void *buf, int len, int flags) {
    int total = 0;
    int n;

    while (total < len) {
        n = recv(sockfd, buf + total, len - total, flags);
        if (n < 1) {
            break;
        }
        total += n;
    }
    return (n < 1 ? -1 : total);
}

void sendok(int socket){
    char *pckg = new char[PSIZE];    
    pckg[0]='1';
    for(int i = 1; i < PSIZE; i++)
        pckg[i] = (char)FILLER;
    if (sendall(socket, pckg, PSIZE, 0) < 0)
        error(0, "Send error.\n");
    delete[] pckg;    
}

void senderr(int socket){
    char *pckg = new char[PSIZE];    
    pckg[0]='0';
    for(int i = 1; i < PSIZE; i++)
        pckg[i] = (char)FILLER;
    if (sendall(socket, pckg, PSIZE, 0) < 0)
        error(0, "Send error.\n");
    delete[] pckg;
}

void disconnect(int socket){
    vector<handle_sockets_t>::iterator it;
    pthread_mutex_lock(&client_lock);
    for (it = clients.begin(); it < clients.end(); it++) {
        if (*(it->sd) == socket && it->closed == 0) {
            printf("Kicked client %d with status %d.\n", socket,
            shutdown(*(it->sd), SHUT_RDWR));
            printf("Listening socket is closed with status %i\n",
            close(*(it->sd)));
            it->closed = 1;
            break;
        }
    }
    pthread_mutex_unlock(&client_lock);
}

void addcurr(int socket, char *pckg){
    if (strcmp(get_code(pckg),"2")!=0){
        senderr(socket);
        return;
    }
    currency_t new_c;
    new_c.name=get_name(pckg);
    new_c.rate=0;
    new_c.change=INT32_MIN;
    pthread_mutex_lock(&currency_lock);
    if(curr_map.insert(make_pair(new_c.name,new_c)).second)
        sendok(socket);
    else
        senderr(socket);
    pthread_mutex_unlock(&currency_lock);    
}

void list(int socket){
    map<char*,currency_t>::iterator it;
    char *pckg = new char[PSIZE];
    pthread_mutex_lock(&currency_lock);
    for(it = curr_map.begin(); it != curr_map.end(); it++){
        pckg = make_package('3',it->second);
        if (sendall(socket, pckg, PSIZE, 0) < 0)
            error(0, "Send error.\n");
    }
    pthread_mutex_unlock(&currency_lock);
    sendok(socket);
    delete[] pckg;
}

void rmcurr(int socket, char *pckg){
    if (strcmp(get_code(pckg),"3")!=0){
        senderr(socket);
        return;
    }   
    char *name = get_name(pckg);
    pthread_mutex_lock(&currency_lock);
    if(curr_map.erase(name))
        sendok(socket);
    else
        senderr(socket);
    pthread_mutex_unlock(&currency_lock);
    delete[] name;
}

void setrate(int socket, char *pckg){
    if (strcmp(get_code(pckg),"4")!=0){
        senderr(socket);
        return;
    }
    char *name = get_name(pckg);
    step_t rate = atoi(get_rate(pckg));
    if (rate < 0){
        senderr(socket);
        delete[] name;
        return;
    }
    currency_t *curr;    
    try{
        pthread_mutex_lock(&currency_lock);
        curr = &(curr_map.at(name));        
    }
    catch (out_of_range& e){
        pthread_mutex_unlock(&currency_lock);
        senderr(socket);
        delete[] name;
        return;
    }    
    curr->hist.push_back(curr->rate);
    curr->change = rate - curr->rate;
    curr->rate = rate;
    pthread_mutex_unlock(&currency_lock);
    sendok(socket);
    delete[] name;
}

void history(int socket, char *pckg){
    if (strcmp(get_code(pckg),"5")!=0){
        senderr(socket);
        return;
    }
    char *name = get_name(pckg);
    currency_t curr;
    pthread_mutex_lock(&currency_lock);
    try{
        curr = curr_map.at(name);
    }
    catch(out_of_range){
        pthread_mutex_unlock(&currency_lock);
        senderr(socket);
        return;
    }
    currency_t record;
    char *response;    
    if(!curr.hist.empty()){
        vector<step_t>::iterator it;
        int counter=0;
        for(it = curr.hist.begin(); it < curr.hist.end(); it++){            
            record.name = name;
            record.rate = *it;
            record.change = counter;
            counter++;
            response = make_package('2', record);
            if (sendall(socket, response, PSIZE, NULL) < 0)
                error(0, "Send error.\n");
            delete[] response;
        }        
    }
    response = make_package('2', curr);
    if (sendall(socket, response, PSIZE, NULL) < 0)
        error(0, "Send error.\n");
    delete[] response;
    pthread_mutex_unlock(&currency_lock);
    sendok(socket); 
    delete[] name;    
}

void *t_listen(void* arg) {

    struct t_arg *sock_par = (struct t_arg*) arg;
    // client address length
    socklen_t *cl_addr_len = new socklen_t;
    struct sockaddr_in client_addr;
    int handle_socket, err;
    struct handle_sockets_t *new_rec;
    pthread_t created_thread, *new_thread;
    // set socket to listening mode
    if (listen((*sock_par).sock_d, (*sock_par).q_len) < 0){
        int status = error((*sock_par).sock_d, "Listen error.\n");
        return (void *)&status;
    }
        
    *cl_addr_len = sizeof (struct sockaddr_in);

    while (1) {
        handle_socket = accept(
                (*sock_par).sock_d, (struct sockaddr *) &client_addr, cl_addr_len);
        if (handle_socket <= 0) {
            error((*sock_par).sock_d, "Connection accept error.\n");
            break;
        }
        struct t_arg *arg = (struct t_arg*) malloc(sizeof (struct t_arg));

        arg -> sock_d = handle_socket;
        //arg -> status = sock_par -> status;
        pthread_create(&created_thread, NULL, t_handle, arg);
        new_thread = new pthread_t;
        *new_thread = created_thread;
        new_rec = new handle_sockets_t;
        new_rec->sd = &(arg->sock_d);
        new_rec->thread = new_thread;
        new_rec->closed = 0;
        pthread_mutex_lock(&client_lock);
        clients.push_back(*new_rec);
        pthread_mutex_unlock(&client_lock);

    }
    pthread_mutex_lock(&client_lock);
    for (int i = 0; i < clients.size(); i++) {
        if (clients[i].closed == 0) {
            printf("Shutting up thread %d...\n", i);
            shutdown(*(clients[i].sd), SHUT_RDWR);
            printf("Thread %d is shut.\n", i);
            printf("Handling socket is closed with status %i\n",
                    close(*(clients[i].sd)));
            clients[i].closed = 1;
        } else
            printf("Skipped closing kicked socket %d.\n", i);
    }
    for (int i = 0; i < clients.size(); i++) {
        printf("Waiting thread %d to join...\n", i);
        pthread_join(*(clients[i].thread), NULL);
        printf("Thread %d is joined.\n", i);
    }
    pthread_mutex_unlock(&client_lock);
    printf("All threads are joined\n");
    delete cl_addr_len;
    //delete sock_par;
    return NULL;
}

void *t_handle(void* arg) {
    struct t_arg* _arg = (t_arg*) arg;
    int *pconnected_socket = &(_arg-> sock_d);
    int size = 0;
    char *pckg = new char[PSIZE];
    sendok(*pconnected_socket);
    while (1) {
        if (recvall(*pconnected_socket, pckg, PSIZE, NULL) < 0) {
            error(0, "Receive error.\n");
            break;
        }        
        switch(pckg[0]){
            case '0': disconnect(*pconnected_socket); break;
            case '1': list(*pconnected_socket); break;
            case '2': addcurr(*pconnected_socket, pckg); break;
            case '3': rmcurr(*pconnected_socket, pckg); break;
            case '4': setrate(*pconnected_socket, pckg); break;
            case '5': history(*pconnected_socket, pckg); break;
            default: senderr(*pconnected_socket);
        }
    }
    delete[] pckg;
    return NULL;
}

/*
 * 
 */
int main(int argc, char** argv) {

    // listening socket descriptor, handling socket descriptor
    // and error variable
    int server_socket, err, stat;
    pthread_t thr_listen;
    // server port
    unsigned short int port = (unsigned short int) PORT;
    // socket address struct for server and client
    struct sockaddr_in server_addr;
    // length of client request queue
    int len = 10;
    // fill server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    // create server socket descriptor
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if ((pthread_mutex_init(&client_lock, NULL) != 0)
            || (pthread_mutex_init(&currency_lock, NULL) != 0)) {
        return error(0, "Mutex init error.\n");
    }
    if (server_socket < 0) {
        printf("Server socket init error.\n");
        close(server_socket);
        return -1;
    }
    // assign ip:port pair to the socket descriptor
    if ((err = bind(server_socket, (struct sockaddr *) &server_addr,
            sizeof (server_addr))) < 0) {
        return error(server_socket, "Bind error.\n");
    }
    struct t_arg arg;
    arg.q_len = len;
    arg.sock_d = server_socket;
    stat = pthread_create(&thr_listen, NULL, t_listen, &arg);
    char *command = new char[10];
    while (1 > 0) {
        printf("command: ");
        scanf("%s", command);
        if (strcmp(command, "quit") == 0) {
            printf("Exiting...\n");
            printf("Listening stopped with status %d.\n",
                    shutdown(server_socket, SHUT_RDWR));
            printf("Listening socket is closed with status %i\n",
                    close(server_socket));
            break;
        } else if (strcmp(command, "list") == 0) {
            print_list();
        } else if (strcmp(command, "kick") == 0) {
            int descr = -1;
            printf("Enter socket descriptor.\n");
            scanf("%d", &descr);                             
            if (descr >= 0){
                disconnect(descr);                
            }
                
        } else
            printf("Unknown command.\n");
    }
    pthread_join(thr_listen, NULL);
    printf("Exited.\n");
    
    pthread_mutex_destroy(&client_lock);
    pthread_mutex_destroy(&currency_lock);
    delete[] command;
    vector<handle_sockets_t>::iterator it;
    for (it = clients.begin(); it!= clients.end(); it++){
        delete it->sd;
        //delete it->thread;
    }
    return 0;
}

void print_list(void) {
    printf("#### Connections list ####\n");
    pthread_mutex_lock(&client_lock);
    for (int i = 0; i < clients.size(); i++) {
        printf("SD[%d]: descriptor %d; closed %d\n", i, *clients[i].sd, clients[i].closed);
    }
    pthread_mutex_unlock(&client_lock);
}
char *get_sum(char *pckg, int size, int offset){
    char *_ret = new char[size+1];
    //bzero(_ret, size+1);
    int i;
    for (i=0; i<size; i++){
        if(pckg[i+offset] == FILLER) break;
        _ret[i] = pckg[i+offset];
    }
    _ret[i]='\0';
    return _ret;
}

char *get_code(char *pckg){
    return get_sum(pckg, 1, 0);
}

char *get_name(char *pckg){
    return get_sum(pckg, 10, 1);
}

char *get_rate(char *pckg){
    return get_sum(pckg, 10, 11);
}

char *get_change(char *pckg){
    return get_sum(pckg, 11, 21);
}

char *make_package(char type, currency_t c){
    char *package = new char[PSIZE];
    package[0] = type;
    for(int i=0; i<10; i++){
        if(c.name[i]!= 0)
            package[i+1]=c.name[i];
        else
            package[i+1]=FILLER;
    }
    char* rate = new char[10];
    sprintf(rate, "%d", c.rate);
    for(int i=0; i<10; i++){
        if(rate[i]!= 0)
            package[i+11]=rate[i];
        else
            package[i+11]=FILLER;        
    }
    char* change = new char[11];
    sprintf(change, "%d", c.change);
    for(int i=0; i<11; i++){
        if(change[i] != 0)
            package[i+21]=change[i];
        else
            package[i+21]=FILLER;
    }
    return package;
}