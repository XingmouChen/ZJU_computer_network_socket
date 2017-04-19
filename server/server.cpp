/* A simple server in the internet domain using TCP
   The port number is passed as an argument */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <signal.h>
#include <unistd.h>

#include <sys/unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <pthread.h>

#include <iostream>
#include <unordered_map>
#include <list>
#include <string>

using namespace std;

const int RECV_BUFFER_SIZE = 2048;

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

//rule of 5 7
class net_packet {
public:
//    net_packet(char* _b, int _l):len(_l) {
//        buf = new char(_l);
//        for (int i = 0; i < _l; ++i)
//            buf[i] = _b[i];
//    }
//    net_packet(const net_packet& p):len(p.len) {
//        buf = new char(p.len);
//        for (int i = 0; i < p.len; ++i)
//            buf[i] = p.buf[i];
//    }
//    ~net_packet() {
//        delete(buf);
//    }
    net_packet(const char* _b, int _l):len(_l) {
        char str[len + 1];
        memcpy(str, _b, len);
        str[len] = '\0';
        msg = string(str);
    }
    int len;
    string msg;
    //char *buf;
};

class host_list_item {
public:
    host_list_item(int _id, int sockfd, struct sockaddr_storage addr) {
        id = _id;
        port = -1;
        bool isIPv4 = true;
        socklen_t len = sizeof(addr);
        getpeername(sockfd, (struct sockaddr*)&addr, &len);

        if (addr.ss_family == AF_INET) { //ipv4
            struct sockaddr_in *s = (struct sockaddr_in *)&addr;
            port = ntohs(s->sin_port);
            inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof(ipstr));
        }
        else if (addr.ss_family == AF_INET6){ //ipv6
            isIPv4 = 0;
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
            port = ntohs(s->sin6_port);
            inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof(ipstr));
        }
        else {
            error("print_ip_port() IP family error! ");
        }

        if (isIPv4) {
            //printf("Peer IP address type is IPV4 \n");
        }
        else {
            //printf("Peer IP address type is IPV6 \n");
        }

        toStr();
    }

    void toStr() {
        sprintf(item_str, "Host ID: %d\nPort number: %d\nIP addr:%s\n", id, port, ipstr);
    }

    void print() {
        printf("## host item print ##\n%s\n", item_str);
    }

    int id;
    int port;
    char ipstr[100];
    char item_str[200];
};

typedef list<net_packet> packet_list_t;
typedef struct thread_attr_t {
    int id;
    int sockfd;
    bool isOnline;
    struct sockaddr_storage client_addr;
    pthread_t thread;
    pthread_mutex_t *send_thrd_mtx;
    pthread_mutex_t *packet_list_mtx;
    packet_list_t list;
} thread_attr_t;

class req_table_item {
public:
    req_table_item() = default;
    req_table_item(
            int _id,
            int _socketfd,
            struct sockaddr_storage _clientaddr,
            thread_attr_t *_attr
    ):
            id(_id), sockfd(_socketfd), client_addr(_clientaddr), attr(_attr)
    {}

    int id;
    int sockfd;
    struct sockaddr_storage client_addr;
    thread_attr_t *attr;
};

typedef unordered_map<int, req_table_item> req_table_t;

//global variables
pthread_mutex_t *req_tab_mtx = new pthread_mutex_t;
req_table_t req_table;

void sendFullMsg(int sockfd, net_packet *p)
{
    printf("## sendFullMsg: ##\n");
    cout << p->msg << endl;
    printf("## sendFullMsg: ##\n");
    int n = 0;
    int sended = 0;
    while (sended < p->len) {
        n = send(sockfd, p->msg.c_str() + sended, p->len, 0);
        if (n < 0)
            error("ERROR writing to socket");
        sended += n;
    }
    printf("## sendFullMsg END: ##\n");
}

int parse_number(const char* buf, int len) {
    char str[len + 1];
    str[len] = '\0';
    for (int i = 0; i < len; i++)
        str[i] = buf[i];
    return atoi(str);
}

net_packet* get_packet(thread_attr_t *t)
{
    static string recv_str;

    //If there are still completely received packets, return one directly
    if (recv_str.length() > 0) {
        cout << "## The recv_str is (length() > 0)" << recv_str << endl;
        int packet_len = parse_number(recv_str.c_str(), 4);
        net_packet *p = new net_packet(recv_str.c_str(), packet_len);
        recv_str = recv_str.substr(packet_len, recv_str.length() - packet_len);
        return p;
    }

    //else receive at least one complete packet
    char recv_buff[RECV_BUFFER_SIZE];
    bzero(recv_buff, RECV_BUFFER_SIZE);
    int packet_len = 0;
    ssize_t n;
    do {
        n = recv(t->sockfd, recv_buff, RECV_BUFFER_SIZE, 0);
        if (n < 0)
            error("ERROR reading from socket");

        //socket offline
        if (n == 0) {
            return nullptr;
        }

        //cout << "n == " << n << endl;
        //recv_buff[n] = '\0';
        recv_str += string(recv_buff);
        packet_len = parse_number(recv_str.c_str(), 4);
    } while (recv_str.length() < packet_len);

    cout << "## The recv_str is " << recv_str << endl;
    net_packet *p = new net_packet(recv_str.c_str(), packet_len);
    recv_str = recv_str.substr(packet_len, recv_str.length() - packet_len);
    return p;
}

void put_packet(const char *header_code, const char *data_buf, int data_len, thread_attr_t *t)
{
    //Construct net_packet's buf
    int packet_len = 7 + data_len;
    char packet_buf[packet_len + 1];
    sprintf(packet_buf, "%04d%s", packet_len, header_code);
    memcpy(packet_buf + 7, data_buf, data_len);

    net_packet send_p = net_packet(packet_buf, packet_len);
    cout << "put_packet msg == " << send_p.msg << endl;
    pthread_mutex_lock(t->packet_list_mtx);
    t->list.push_back(send_p);
    pthread_mutex_unlock(t->packet_list_mtx);
}

int do_receive(thread_attr_t *t) {
    bool isContinue = true;
    while (isContinue) {
        net_packet *p = get_packet(t);

        //socket offline
        if (p == nullptr) {
            cout << "Host " << t->id << " offline!" << endl;
            isContinue = false;
            break;
        }
        //now we got a complete packet
        cout << "## Receiv ## Here is the message: " << p->msg << endl;

        //parse packet
        char req_num = p->msg[4];
        char ins_num = p->msg[5];
        char rep_num = p->msg[6];
        const char *data = p->msg.c_str() + 7;
        int data_len = p->len - 7;

        if (req_num != '0') {
            switch (req_num) {
                case '1': {
                    isContinue = false;
                    t->isOnline = false;
                    put_packet("001", "Bye~", 4, t);

                    break;
                }
                case '2': {
                    time_t rawtime;
                    struct tm *timeinfo;
                    char buff[80];

                    time(&rawtime);
                    timeinfo = localtime(&rawtime);
                    strftime(buff, sizeof(buff), "%d-%m-%Y %I:%M:%S", timeinfo);
                    printf("time: %s\n", buff);
                    int msg_len = strlen(buff);
                    printf("msg_len: %d\n", msg_len);

                    put_packet("002", buff, msg_len, t);

                    break;
                }
                case '3': {
                    char hostname[100];
                    gethostname(hostname, sizeof(hostname));
                    int msg_len = strlen(hostname);

                    printf("hostname %s\n", hostname);
                    put_packet("003", hostname, msg_len, t);

                    break;
                }
                case '4': {
                    char buff[2000];
                    int msg_len = 0;
                    pthread_mutex_lock(req_tab_mtx);
                    for (req_table_t::iterator it = req_table.begin(); it != req_table.end(); ++it) {
                        host_list_item host(
                                it->second.id,
                                it->second.sockfd,
                                it->second.client_addr
                        );
                        int host_len = strlen(host.item_str);
                        for (int i = 0; i < host_len; ++i)
                            buff[msg_len + i] = host.item_str[i];
                        msg_len += host_len;
                    }
                    pthread_mutex_unlock(req_tab_mtx);

                    put_packet("004", buff, msg_len, t);

                    break;
                }
                case '5': {
                    char host_id_str[5];
                    host_id_str[4] = 0;
                    for (int i = 0; i < 4; ++i)
                        host_id_str[i] = data[i];
                    int hostId = parse_number(data, 4);
                    data += 4;
                    cout << "The target msg host id == " << hostId << endl;

                    pthread_mutex_lock(req_tab_mtx);
                    if (req_table.count(hostId) == 1) {
                        put_packet("005", NULL, 0, t);
                        char *msg = new char[4 + data_len];
                        sprintf(msg, "%04d", t->id);
                        for (int i = 0; i < data_len; ++i)
                            msg[i + 4] = data[i];
                        put_packet("010", msg, 4 + data_len, req_table[hostId].attr);
                        delete msg;
                    }
                    else {
                        put_packet("006", "0001HOST NOT FOUND", 18, t);
                    }
                    pthread_mutex_unlock(req_tab_mtx);

                    break;
                }
                case '6': {
                    int hostId = parse_number(data, 4);
                    pthread_mutex_lock(req_tab_mtx);
                    put_packet("020", NULL, 0, req_table[hostId].attr);
                    pthread_mutex_unlock(req_tab_mtx);

                    break;
                }
                default:{
                    error("## Receiv ## Reqeust Number Error!");

                    break;
                }
            }
        }
        else{
            error("## Receiv ## Packet header unkown error!");
        }
        delete(p);
    }
}

void* receive_sock_thread(void* attr)
{
    thread_attr_t *t = (thread_attr_t *)attr;

    host_list_item peer(t->id, t->sockfd, t->client_addr);
    peer.print();
    do_receive(t);
    printf("## Receiv ## End to receive_sock host %d...\n", t->id);

    //wait for send_sock() to exit
    pthread_mutex_lock(t->send_thrd_mtx);

    //clean up
    pthread_mutex_lock(req_tab_mtx);
    req_table.erase(t->id);
    pthread_mutex_unlock(req_tab_mtx);
    close(t->sockfd);
    delete(t);

    pthread_mutex_unlock(t->send_thrd_mtx);
    pthread_exit(NULL);
}

void* send_sock_thread(void* attr)
{
    thread_attr_t *t = (thread_attr_t *)attr;
    pthread_mutex_lock(t->send_thrd_mtx);

    bool isContinue = true;
    while (isContinue) {
        sleep(1);
//        printf("## Send ## WAKE UP!\n");
        pthread_mutex_lock(t->packet_list_mtx);
        for (packet_list_t::iterator it = t->list.begin(); isContinue && it != t->list.end(); ++it) {
            sendFullMsg(t->sockfd, &(*it));
        }
        t->list.clear();
        pthread_mutex_unlock(t->packet_list_mtx);

        if (!t->isOnline) {
            isContinue = false;
        }
    }
    printf("## Send ## End to send_sock host %d...\n", t->id);

    pthread_mutex_unlock(t->send_thrd_mtx);

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
    pthread_mutex_init(req_tab_mtx, NULL);

    while (true) {
        sockfd_new = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_size);

        thread_attr_t *attr = new thread_attr_t;
        attr->isOnline = true;
        attr->id = request_count++;
        attr->sockfd = sockfd_new;
        attr->client_addr = client_addr;
        attr->send_thrd_mtx = new pthread_mutex_t;
        attr->packet_list_mtx = new pthread_mutex_t;
        pthread_mutex_init(attr->send_thrd_mtx, NULL);
        pthread_mutex_init(attr->packet_list_mtx, NULL);

        pthread_mutex_lock(req_tab_mtx);
        req_table.insert(make_pair(
                attr->id,
                req_table_item(attr->id, attr->sockfd, attr->client_addr, attr)
        ));
        pthread_mutex_unlock(req_tab_mtx);

        pthread_create(
                &attr->thread,
                NULL,
                receive_sock_thread,
                (void*)attr
        );
        pthread_create(
                &attr->thread,
                NULL,
                send_sock_thread,
                (void*)attr
        );
    }

    pthread_mutex_destroy(req_tab_mtx);
    close(sockfd);
    close(sockfd_new);

    return 0;
}