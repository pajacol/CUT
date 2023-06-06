#pragma once

#include <threads.h>

struct element
{
    struct element *next;
    void *data;
};

struct queue
{
    struct element *head;
    struct element *tail;
    int elements;
    mtx_t lock;
};

extern struct queue *new_queue(void);
extern void delete_queue(struct queue *queue);
extern int enqueue(struct queue *queue, void *data);
extern void *dequeue(struct queue *queue);
