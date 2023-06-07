#include <threads.h>
#include <stdlib.h>
#include <stdio.h>
#include "queue.h"

static struct queue *queue;
static mtx_t mutex;

static int add(void *arg)
{
    int *x;
    while(1)
    {
        x = malloc(sizeof(int));
        *x = rand();
        mtx_lock(&mutex);
        if(enqueue(queue, x))
        {
            // printf("Adding thread %ld, adding failed\n", (long int)arg);
            free(x);
        }
        else
        {
            printf("Adding thread %ld, added value %d\n", (long int)arg, *x);
        }
        mtx_unlock(&mutex);
    }
    return 0;
}

static int take(void *arg)
{
    int *x;
    while(1)
    {
        mtx_lock(&mutex);
        if((x = dequeue(queue)))
        {
            printf("Taking thread %ld, taken value %d\n", (long int)arg, *x);
            free(x);
        }
        else
        {
            // printf("Taking thread %ld, taking failed\n", (long int)arg);
        }
        mtx_unlock(&mutex);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    long int i;
    thrd_t adding[10];
    thrd_t taking[10];

    queue = new_queue();
    mtx_init(&mutex, mtx_plain);

    for(i = 0; i < 10; ++i)
    {
        thrd_create(&adding[i], add, (void*)i);
    }

    for(i = 0; i < 10; ++i)
    {
        thrd_create(&taking[i], take, (void*)i);
    }

    for(i = 0; i < 10; ++i)
    {
        thrd_join(adding[i], NULL);
    }

    for(i = 0; i < 10; ++i)
    {
        thrd_join(taking[i], NULL);
    }

    mtx_destroy(&mutex);

    return 0;
    (void)argc;
    (void)argv;
}
