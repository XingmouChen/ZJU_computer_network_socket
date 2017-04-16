#include <cstdio>
#include <cstdlib>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

#include <cstring>

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

int getUserInput(int sockfd)
{
    int rtnCode = 1;

    printf("Please enter the message: ");
    char buffer[256];
    bzero(buffer,256);
    fgets(buffer, 255, stdin);
    if (strcmp(buffer, "bye()\n") == 0) {
        rtnCode = 0;
    }

    int n;
    //Notice! the send may not send all of the contents, you should check the n for sure!
    n = send(sockfd, buffer, strlen(buffer), 0);
    if (n < 0) 
         error("ERROR writing to socket");
    bzero(buffer, 256);
    n = recv(sockfd, buffer, 255, 0);
    if (n < 0) 
         error("ERROR reading from socket");
    printf("%s\n",buffer);

    return rtnCode;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
       fprintf(stderr,"usage %s hostname port\n", argv[0]);
       exit(0);
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *res;
    int status;
    if ( (status = getaddrinfo(argv[1], argv[2], &hints, &res)) != 0 ) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }

    if (res->ai_family == AF_INET) {
    	printf("## IPV4 ## \n");
    }
    else {
    	printf("## IPV6 ## \n");
    }
    int sockfd;
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) 
        error("ERROR opening socket");

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0)
        error("ERROR connecting");

    while (getUserInput(sockfd));

    close(sockfd);
    
    return 0;
}