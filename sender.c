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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "sender.h"
#include "receiver.h"
#include "list.h"

// static global variables

static char *keyboardBuffer = NULL;
static char *sendBuffer = NULL;
static bool active = true;
static int BUFFER_MAX_LEN = 512;
static pthread_t keyboard_thread = 0;
static pthread_t print_thread = 0;
static int from_size = 0;
static struct sockaddr from;
static int localSocket = 0;
static pthread_cond_t okayToReadList_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t okayToJoin_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t okayToReadList_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t okayToJoin_mutex = PTHREAD_MUTEX_INITIALIZER;
static List *sharedList = NULL;
static char *fgetsReturned = NULL;

// function used to free items
static void freeItems(void *pItem)
{
    free(pItem);
}

// frees and destroys mutex and cond variables
static void freeMemory()
{
    List_free(sharedList, freeItems);
    if (keyboardBuffer)
    {
        free(keyboardBuffer);
        keyboardBuffer = NULL;
    }
    if (sendBuffer)
    {
        free(sendBuffer);
        sendBuffer = NULL;
    }
    int mutex_destory = pthread_mutex_destroy(&okayToReadList_mutex);
    pthread_mutex_unlock(&okayToReadList_mutex);
    if (mutex_destory != 0)
    {
        int error = errno;
        // printf("mutex   variable!\n"); for debugging
        fprintf(stderr, "sending error: %s\n", strerror(error));
    }
    int cond_destory = pthread_cond_destroy(&okayToReadList_cond);
    if (cond_destory != 0)
    {
        int error = errno;
        // printf("condition variable!\n"); for debugging
        fprintf(stderr, "sending error: %s\n", strerror(error));
    }
}

// setter for from and from_size
void set_from_sender(struct sockaddr_in in_from)
{
    from = *(struct sockaddr *)&in_from;
}
void set_from_size_sender(int size)
{
    from_size = size;
}

// thread that is going to read from user/cat
static void *readFromKeyboard(void *unused)
{
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL); // making sure that pthread_cancellation is async
    int list_add_ret;
    while (1)
    {
        keyboardBuffer = (char *)malloc(sizeof(char) * BUFFER_MAX_LEN); // setting up the buffer for input
        memset(keyboardBuffer, 0, sizeof(char) * BUFFER_MAX_LEN);
        fgetsReturned = fgets(keyboardBuffer, BUFFER_MAX_LEN, stdin); // reads upto n-1 characters
        pthread_mutex_lock(&okayToReadList_mutex);
        if (List_count(sharedList) == LIST_MAX_NUM_NODES) // if list is full then wait
            pthread_cond_wait(&okayToReadList_cond, &okayToReadList_mutex);
        // Critical section
        {
            list_add_ret = List_prepend(sharedList, (void *)keyboardBuffer);
            // in case it can't add any items just ignore it.
            if (list_add_ret != 0)
            {
                printf("COUNT LIST%d\n", List_count(sharedList));
                printf("ADD ITEM FAILED!!!!\n");
                free(keyboardBuffer);
                keyboardBuffer = NULL;
                // exit(1);
            }
        }
        // once done with CS unlock mutex and signal the cond variable
        pthread_mutex_unlock(&okayToReadList_mutex);
        pthread_cond_signal(&okayToReadList_cond);
    }
    if (!active)
        return NULL;
}

// function used to send messages to other socket
static void *sendMessage(void *unused)
{
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    int sentValue = 0;

    // making sure that the from_size is set, not really needed just in case
    while (1)
    {
        if (from_size != 0)
        {
            break;
        }
    }
    while (1)
    {
        // Critical section
        pthread_mutex_lock(&okayToReadList_mutex);
        if (List_count(sharedList) == 0)
        {
            pthread_cond_wait(&okayToReadList_cond, &okayToReadList_mutex);
        }
        {
            sendBuffer = (char *)List_trim(sharedList);
            if (sendBuffer == NULL)
            {
                printf("FAILED getting items from list");
                // exit(1);
            }
            else
            {
                // Sending a msg to from
                sentValue = sendto(localSocket,
                                   (void *)sendBuffer,
                                   BUFFER_MAX_LEN,
                                   0,
                                   &from,
                                   from_size);
                if (sentValue < 0)
                {
                    // if it fails to send anything just crash
                    int error = errno;
                    fprintf(stderr, "sending error: %s\n", strerror(error));
                    exit(1);
                }
                // termination clause
                if (sendBuffer[0] == '!' && sendBuffer[1] == '\n')
                // if (strstr(sendBuffer, "!\n") != NULL)
                {
                    free(sendBuffer);
                    sendBuffer = NULL;
                    fputs("sender shutting down!\n", stdout);
                    close(localSocket);
                    freeMemory();
                    shutdown_threads_recv();
                    shutdown_recv_thread();
                    shutdown_threads_send();
                    shutdown_sender_thread();
                    return NULL;
                }
            }
            free(sendBuffer);
            sendBuffer = NULL;
        }
        pthread_mutex_unlock(&okayToReadList_mutex);
        pthread_cond_signal(&okayToReadList_cond);
    }
    return NULL;
}
// create thread for send part
void create_sender_thread(int socket)
{
    sharedList = List_create(); // creating a local list for items read
    assert(sharedList != NULL);
    localSocket = socket; // using the socket returned from main function
    int send_keyboard_thread = pthread_create(&keyboard_thread, NULL, readFromKeyboard, NULL);
    int send_print_thread = pthread_create(&print_thread, NULL, sendMessage, NULL);
    if (send_keyboard_thread != 0 || send_print_thread != 0) // if it fails to create a thread
    {
        int error = errno;
        printf("pthread creation error: %s\n", strerror(error));
        exit(0);
    }
    // waiting for join, instead of using a busy while loop i used a condition var
    active = true;
    pthread_mutex_lock(&okayToJoin_mutex);
    {
        if (active)
            pthread_cond_wait(&okayToJoin_cond, &okayToJoin_mutex);
        pthread_join(keyboard_thread, NULL);
    }
    pthread_mutex_unlock(&okayToJoin_mutex);
}

// used to avoid recurssion
void shutdown_threads_send()
{
    pthread_cancel(keyboard_thread);
    pthread_cond_signal(&okayToJoin_cond);
    pthread_cancel(print_thread);
}
// used to do final clean up
void shutdown_sender_thread()
{
    pthread_cancel(print_thread);
    pthread_join(print_thread, NULL);
    shutdown_threads_send();
    freeMemory();
}
