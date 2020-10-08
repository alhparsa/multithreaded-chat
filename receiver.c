#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include "receiver.h"
#include "sender.h"
#include "list.h"

// Static global variables
static int BUFFER_MAX_LEN = 512;
static int recvValue = 0;
static int error = 0;
static struct sockaddr *tmp = NULL;
static struct sockaddr from;
static int from_size = 0;
static pthread_t recv_thread = 0;
static pthread_t print_thread = 0;
static int localSocket = 0;
static pthread_cond_t okayToReadList_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t okayToReadList_mutex = PTHREAD_MUTEX_INITIALIZER;
static List *sharedList = NULL;
static char *keyboardBuffer = NULL;
static char *printBuffer = NULL;

// setters for static variables
void set_from_recv(struct sockaddr_in in_from)
{
    from = *(struct sockaddr *)&in_from;
}
void set_from_size_recv(int in_size)
{
    from_size = in_size;
}

// function used to free all items
static void freeItems(void *pItem)
{
    free(pItem);
}

// function used to free all the variables and mutexes and cond vars
static void freeMemory() // free memory function
{
    if (sharedList)
        List_free(sharedList, freeItems);
    if (keyboardBuffer)
    {
        free(keyboardBuffer);
        keyboardBuffer = NULL;
    }
    if (printBuffer)
    {
        free(printBuffer);
        printBuffer = NULL;
    }
    pthread_mutex_destroy(&okayToReadList_mutex);
    pthread_mutex_unlock(&okayToReadList_mutex);
    pthread_cond_destroy(&okayToReadList_cond);
}

//function used to receive items from the socket
static void *receiveMessage(void *unused)
{
    // making sure that the thread's cancellation is async
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    from_size = sizeof(from);
    int addItem;
    while (1)
    {
        // creating the buffer
        keyboardBuffer = (char *)malloc(sizeof(char) * BUFFER_MAX_LEN);
        memset(keyboardBuffer, 0, sizeof(char) * BUFFER_MAX_LEN);
        // waiting for a message
        recvValue = recvfrom(localSocket,
                             keyboardBuffer,
                             sizeof(char) * BUFFER_MAX_LEN,
                             0,
                             tmp,
                             (unsigned int *)&from_size);
        if (recvValue < 0) // error checking for recieving
        {
            error = errno;
            fprintf(stderr, "Recv error: %s\n", strerror(error));
            // exit(1);
        }
        // Critical section
        pthread_mutex_lock(&okayToReadList_mutex);
        {
            if (List_count(sharedList) == LIST_MAX_NUM_NODES)
                pthread_cond_wait(&okayToReadList_cond, &okayToReadList_mutex);
            {
                addItem = List_prepend(sharedList, (void *)keyboardBuffer);
                // in case it can't add any items just ignore it.
                if (addItem != 0)
                {
                    printf("COUNT LIST%d\n", List_count(sharedList));
                    printf("ADD ITEM FAILED!!!!\n");
                    free(keyboardBuffer);
                    keyboardBuffer = NULL;
                }
            }
        }
        // once done with CS unlock mutex and signal the cond variable
        pthread_mutex_unlock(&okayToReadList_mutex);
        pthread_cond_signal(&okayToReadList_cond);
    }
    return NULL;
}

static void *printToScreen(void *unused)
{
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    while (1)
    {
        // Critical section
        pthread_mutex_lock(&okayToReadList_mutex);
        // making sure there are items available to consume
        if (List_count(sharedList) == 0)
        {
            pthread_cond_wait(&okayToReadList_cond, &okayToReadList_mutex);
        }
        {
            printBuffer = (char *)List_trim(sharedList);
            if (printBuffer == NULL)
            {
                printf("FAILED getting items from list\n");
                // exit(1);
            }
            if (printBuffer)
            {
                fputs(printBuffer, stdout);
                // termination clause
                if (printBuffer[0] == '!' && printBuffer[1] == '\n')
                // if (strstr(printBuffer, "!\n") != NULL)
                {
                    free(printBuffer);
                    printBuffer = NULL;
                    close(localSocket);
                    freeMemory();
                    shutdown_threads_send();
                    shutdown_sender_thread();
                    shutdown_threads_recv();
                    shutdown_recv_thread();
                    return NULL;
                }
            }
            free(printBuffer);
            printBuffer = NULL;
        }
        pthread_mutex_unlock(&okayToReadList_mutex);
        pthread_cond_signal(&okayToReadList_cond);
    }
}

// create thread for send part
void create_recv_thread(int socket)
{
    sharedList = List_create();
    assert(sharedList != NULL);
    localSocket = socket;
    int rcv_thread_val = pthread_create(&recv_thread, NULL, receiveMessage, NULL);
    int print_thread_val = pthread_create(&print_thread, NULL, printToScreen, NULL);
    if (rcv_thread_val != 0 || print_thread_val != 0)
    {
        int error = errno;
        printf("pthread creation error: %s\n", strerror(error));
        exit(0);
    }
}

// used to avoid recurssion
void shutdown_threads_recv()
{
    pthread_cancel(recv_thread);
    pthread_cancel(print_thread);
}

// used to do final clean up
void shutdown_recv_thread()
{
    pthread_join(recv_thread, NULL);
    pthread_cancel(print_thread);
    pthread_join(print_thread, NULL);
    shutdown_threads_send();
    freeMemory();
}