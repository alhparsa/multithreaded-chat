//
//  Header.h
//  assignment3
//
//  Created by Parsa Alamzadeh on 2020-07-14.
//  Copyright © 2020 Parsa Alamzadeh. All rights reserved.
//
#ifndef __SENDER_H___
#define __SENDER_H___
#include "list.h"
// void set_cond_variable_send(pthread_cond_t *cond_var);
// void set_mutex_send(pthread_mutex_t *mutex);
// void set_list_send(List *list);
void shutdown_threads_send();
void set_from_sender(struct sockaddr_in in_from);
void set_from_size_sender(int size);
void create_sender_thread(int socket);
void shutdown_sender_thread();
#endif