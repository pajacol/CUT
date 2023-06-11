#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include "queue.h"

#define MAX_ITEMS 32

struct queue *new_queue(void)
{
    struct queue *queue = calloc(1, sizeof(struct queue));
    mtx_init(&queue->lock, mtx_plain);
    cnd_init(&queue->empty);
    cnd_init(&queue->full);
    return queue;
}

void delete_queue(struct queue *queue)
{
    struct element *element = queue->tail, *next_element;
    while(element != NULL)
    {
        next_element = element->next;
        free(element->data);
        free(element);
        element = next_element;
    }
    cnd_destroy(&queue->full);
    cnd_destroy(&queue->empty);
    mtx_destroy(&queue->lock);
    free(queue);
    return;
}

void enqueue(struct queue *queue, const void *data, int len)
{
    struct element *element;
    mtx_lock(&queue->lock);
    while(queue->elements == MAX_ITEMS)
    {
        cnd_wait(&queue->full, &queue->lock);
    }
    element = malloc(sizeof(struct element));
    element->next = NULL;
    element->len = len;
    element->data = malloc((unsigned long)len);
    memcpy(element->data, data, (unsigned long)len);
    if(queue->elements == 0)
    {
        queue->tail = element;
    }
    else
    {
        queue->head->next = element;
    }
    queue->head = element;
    ++queue->elements;
    cnd_signal(&queue->empty);
    mtx_unlock(&queue->lock);
    return;
}

void dequeue(struct queue *queue, void *data, int *len)
{
    struct element *element;
    mtx_lock(&queue->lock);
    while(queue->elements == 0)
    {
        cnd_wait(&queue->empty, &queue->lock);
    }
    element = queue->tail;
    memcpy(data, element->data, (unsigned long)element->len);
    free(element->data);
    *len = element->len;
    queue->tail = element->next;
    free(element);
    --queue->elements;
    if(queue->elements == 0)
    {
        queue->head = NULL;
    }
    cnd_signal(&queue->full);
    mtx_unlock(&queue->lock);
    return;
}

int queue_length(struct queue *queue)
{
    return queue->elements;
}
