#include <stdlib.h>
#include <threads.h>
#include "queue.h"

#define MAX_ITEMS 32

struct queue *new_queue(void)
{
    struct queue *queue = calloc(1, sizeof(struct queue));
    mtx_init(&queue->lock, mtx_plain);
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
    mtx_destroy(&queue->lock);
    free(queue);
    return;
}

int enqueue(struct queue *queue, void *data)
{
    struct element *element;
    mtx_lock(&queue->lock);
    if(queue->elements < MAX_ITEMS)
    {
        element = malloc(sizeof(struct element));
        element->next = NULL;
        element->data = data;
        if(queue->head != NULL)
        {
            queue->head->next = element;
        }
        else
        {
            queue->tail = element;
        }
        queue->head = element;
        ++queue->elements;
        mtx_unlock(&queue->lock);
        return 0;
    }
    else
    {
        mtx_unlock(&queue->lock);
        return 1;
    }
}

void *dequeue(struct queue *queue)
{
    void *data;
    struct element *element;
    mtx_lock(&queue->lock);
    element = queue->tail;
    if(element != NULL)
    {
        data = element->data;
        queue->tail = element->next;
        free(element);
        --queue->elements;
        if(queue->elements == 0)
        {
            queue->head = NULL;
        }
    }
    else
    {
        data = NULL;
    }
    mtx_unlock(&queue->lock);
    return data;
}
