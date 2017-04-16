/* A simple server in the internet domain using TCP
   The port number is passed as an argument */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <pthread.h>

#include <unordered_map>

using namespace std;

const int RECV_BUFFER_SIZE = 2048;
typedef struct thread_attr_t {
    int id;
    int sockfd;
    struct sockaddr_storage client_addr;
    pthread_t thread;
} thread_attr_t;

//global variables
pthread_mutex_t req_list_mtx;
typedef unordered_map<int, thread_attr_t> req_list_t;
req_list_t req_table;

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

void print_ip_port(int sockfd, struct sockaddr_storage *addr)
{
    char ipstr[100];
    int port = -1;
    int isIPv4 = 1;
    socklen_t len = sizeof(*addr);
    getpeername(sockfd, (struct sockaddr*)addr, &len);

    if (addr->ss_family == AF_INET) { //ipv4
        struct sockaddr_in *s = (struct sockaddr_in *)addr;
        port = ntohs(s->sin_port);
        inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof(ipstr));
    }
    else if (addr->ss_family == AF_INET6){ //ipv6
        isIPv4 = 0;
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)addr;
        port = ntohs(s->sin6_port);
        inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof(ipstr));
    }
    else {
        error("print_ip_port() IP family error! ");
    }

    if (isIPv4) {
        printf("Peer IP address type is IPV4 \n");
    }
    else {
        printf("Peer IP address type is IPV6 \n");
    }
    printf("Peer IP address: %s\n", ipstr);
    printf("Peer port          : %d\n", port);
}

void sendFullMsg(int sockfd, const char* msg, int len) {
    int n = 0;
    int sended = 0;
    while (sended < len) {
        n = send(sockfd, msg + sended, len, 0);
        if (n < 0)
            error("ERROR writing to socket");
        sended += n;
    }
}

int talk(int sockfd) {
    int rtnCode = 1;

    ssize_t n;
    char recv_buff[RECV_BUFFER_SIZE];
    bzero(recv_buff, RECV_BUFFER_SIZE);
    n = recv(sockfd, recv_buff, RECV_BUFFER_SIZE, 0);
    if (n < 0)
        error("ERROR reading from socket");

    printf("Here is the message: %s\n", recv_buff);
    if (strcmp(recv_buff, "bye()\n") == 0) {
        rtnCode = 0;
        sendFullMsg(sockfd, "Got", 3);
    }
    else if (strcmp(recv_buff, "getTime()")) {
        time_t rawtime;
        struct tm *timeinfo;
        char buff[80];

        time(&rawtime);
        timeinfo = localtime(&rawtime);

        strftime(buff, sizeof(buff), "%d-%m-%Y %I:%M:%S", timeinfo);
        sendFullMsg(sockfd, buff, strlen(buff));
    }

    return rtnCode;
}

void* serve_request(void* attr)
{
    thread_attr_t *t = (thread_attr_t *)attr;

    printf("Begin to serve host %d...\n", t->id);
    print_ip_port(t->sockfd, &(t->client_addr));

    while (talk(t->sockfd));
    printf("End to serve host %d...\n", t->id);

    //clean up
    pthread_mutex_lock(&req_list_mtx);
    req_table.erase(t->sockfd);
    pthread_mutex_unlock(&req_list_mtx);
    close(t->sockfd);
    delete(t);

    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    //argv[1] points to the string of port number
    if (argc < 2) {
     fprintf(stderr,"ERROR, no port provided\n");
     exit(1);
    }

    //Print hostname
    char hostname[128];
    gethostname(hostname, sizeof(hostname));
    printf("My hostname is : %s\n", hostname);

    //
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *res;
    int status;
    if ( (status = getaddrinfo(NULL, argv[1], &hints, &res)) != 0 ) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }

    //make a socket
    int sockfd;
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) 
    error("ERROR opening socket");

    // struct sockaddr_in serv_addr;
    // bzero((char *) &serv_addr, sizeof(serv_addr));
    // serv_addr.sin_family = AF_INET;
    // serv_addr.sin_addr.s_addr = INADDR_ANY;
    // serv_addr.sin_port = htons(portno);

    if (bind(sockfd, res->ai_addr, res->ai_addrlen) < 0)
          error("ERROR on binding");
    listen(sockfd, 10);

    struct sockaddr_storage client_addr;
    socklen_t client_addr_size;
    int sockfd_new;
    int request_count = 0;
    pthread_mutex_init(&req_list_mtx, NULL);

    while (true) {
        sockfd_new = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_size);

        thread_attr_t *attr = new thread_attr_t;
        attr->id = request_count++;
        attr->sockfd = sockfd_new;
        attr->client_addr = client_addr;

        pthread_mutex_lock(&req_list_mtx);
        req_table.insert(make_pair(attr->sockfd, *attr));
        pthread_mutex_unlock(&req_list_mtx);

        pthread_create(
                &attr->thread,
                NULL,
                serve_request,
                (void*)attr
        );
    }

    pthread_mutex_destroy(&req_list_mtx);
    close(sockfd);
    close(sockfd_new);

    return 0;
}