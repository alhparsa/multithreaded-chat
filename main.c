#include <netdb.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include "sender.h"
#include "receiver.h"
#include "list.h"

// inspired by https://support.sas.com/documentation/onlinedoc/sasc/doc700/html/lr2/ztbyname.htm
// Given a hostname it will return it's ipv4 address
void getipaddress(char *server, char *buffer)
{
    struct hostent *addr;
    struct in_addr ip_addr;
    char *ipaddr_str;
    addr = gethostbyname(server);
    if (!addr)
    {
        int error = h_errno;
        fprintf(stderr, "Failed to resolve to hostname: %s\n", strerror(error));
        exit(1);
    }
    ip_addr = *(struct in_addr *)(addr->h_addr_list[0]);
    ipaddr_str = inet_ntoa(ip_addr);
    if (ipaddr_str == 0)
    {
        printf("Invalid address");
        exit(1);
    }
    memcpy(buffer, ipaddr_str, strlen(ipaddr_str) + 1);
}

// main function to initialize the local sockets and bind them
int socket_init(char *port, char *server)
{
    // inspired by beej's guide
    int status;                // used for error checking
    struct addrinfo hints;     // struct used as a hint for getaddrinfo
    struct addrinfo *servinfo; // will point to the results
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;                  // only accepting IPv4
    hints.ai_socktype = SOCK_DGRAM;             // making sure it's type udp
    hints.ai_flags = (server) ? 0 : AI_PASSIVE; // if server is available set to 0 otherwise it's local
    status = getaddrinfo(server, port, &hints, &servinfo);
    if (status != 0) // error checking for getaddrinfo
    {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }
    int localSocket = socket(servinfo->ai_family,
                             servinfo->ai_socktype,
                             servinfo->ai_protocol); // creating a local socket
    if (localSocket == 1)
    {
        printf("Invalid socket!\n");
        exit(1);
    }

    if (server) // if we are setting up the remote server
    {
        char buffer[1025];
        getipaddress(server, buffer);
        struct sockaddr_in remote;
        remote.sin_family = AF_INET;
        remote.sin_port = htons(atoi(port));
        int inet_val = inet_aton(buffer, &remote.sin_addr);
        if (inet_val == 0)
        {
            printf("invalid ip\n");
            exit(1);
        }
        printf("Connecting server is: %s, port number is: %d\n",
               buffer, ntohs(remote.sin_port));
        set_from_sender(remote);
        set_from_recv(remote);
        set_from_size_recv(sizeof(remote));
        set_from_size_sender(sizeof(remote));
    }
    else
    {
        int bindValue = bind(localSocket,
                             servinfo->ai_addr,
                             servinfo->ai_addrlen); //binding the local socket with local port
        if (bindValue < 0)                          // error checking for binding
        {
            int error = errno;
            fprintf(stderr, "binding error: %s\n", strerror(error));
            exit(1);
        }
    }
    freeaddrinfo(servinfo); // freeing addrinfo
    // upon successful binding, it will return the socket descriptor for the local socket
    return localSocket;
}

// creating threads
int main(int args, char *argsc[])
{
    // args are going to be provided as follows:
    // filename local_port remote_ip remote_port
    assert(args == 4); // making sure the provided args match the requirements
    int local_port = atoi(argsc[1]);
    int remort_port = atoi(argsc[3]);
    assert(local_port > 1024 && local_port < 65536);
    assert(remort_port > 1024 && remort_port < 65536);
    int localSocket = socket_init(argsc[1], NULL);
    int remoteSocket = socket_init(argsc[3], argsc[2]);

    // setting local variables in receive module
    create_recv_thread(localSocket);
    create_sender_thread(remoteSocket);

    // just in case, for redundancy
    shutdown_recv_thread();
    shutdown_sender_thread();
    return 0;
}
