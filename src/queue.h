#pragma once

#include <threads.h>

struct element
{
    struct element *next;
    void *data;
    int len;
};

struct queue
{
    struct element *head;
    struct element *tail;
    mtx_t lock;
    cnd_t empty;
    cnd_t full;
    volatile int elements;
};

extern struct queue *new_queue(void);
extern void delete_queue(struct queue *queue);
extern void enqueue(struct queue *queue, const void *data, int len);
extern void dequeue(struct queue *queue, void *data, int *len);
extern int queue_length(struct queue *queue);
