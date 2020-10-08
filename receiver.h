#ifndef __RECEIVER_H__
#define __RECEIVER_H__
#include "list.h"

void shutdown_threads_recv();
void set_from_recv(struct sockaddr_in in_from);
void set_from_size_recv(int in_size);
void create_recv_thread(int socket);
void shutdown_recv_thread();
#endif
