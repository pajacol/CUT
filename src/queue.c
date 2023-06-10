#include <stdlib.h>
#include <threads.h>
#include "queue.h"

#include <stdio.h>

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

int enqueue(struct queue *queue, void *data)
{
    struct element *element;
    mtx_lock(&queue->lock);
fprintf(stderr, "Try adding, queue elements % 2d, queue: %p\n", queue->elements, (void*)queue);
// Spurious wakeup on 'while' changed to 'if'
    while(queue->elements == MAX_ITEMS)
    {
fprintf(stderr, "Blocked adding, queue: %p\n", (void*)queue);
        cnd_wait(&queue->full, &queue->lock);
fprintf(stderr, "Unlocked adding, queue: %p\n", (void*)queue);
    }
    element = malloc(sizeof(struct element));
    element->next = NULL;
    element->data = data;
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
fprintf(stderr, "Adding, queue elements % 2d, queue: %p\n", queue->elements, (void*)queue);
    cnd_signal(&queue->empty);
    mtx_unlock(&queue->lock);
    return 0;
}

void *dequeue(struct queue *queue)
{
    void *data;
    struct element *element;
    mtx_lock(&queue->lock);
fprintf(stderr, "Try taking, queue elements % 2d, queue: %p\n", queue->elements, (void*)queue);
// Spurious wakeup on 'while' changed to 'if'
    while(queue->elements == 0)
    {
fprintf(stderr, "Blocked taking, queue: %p\n", (void*)queue);
        cnd_wait(&queue->empty, &queue->lock);
fprintf(stderr, "Unlocked taking, queue: %p\n", (void*)queue);
    }
    element = queue->tail;
    data = element->data;
    queue->tail = element->next;
    free(element);
    --queue->elements;
    if(queue->elements == 0)
    {
        queue->head = NULL;
    }
fprintf(stderr, "Taking, queue elements % 2d, queue: %p\n", queue->elements, (void*)queue);
    cnd_signal(&queue->full);
    mtx_unlock(&queue->lock);
    return data;
}
