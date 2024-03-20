#include "queue.h"
#include <stdlib.h>
#include <semaphore.h>

typedef struct queue_node {
    void *data;
    struct queue_node *next;
} queue_node_t;

typedef struct queue {
    queue_node_t *head;
    queue_node_t *tail;
    int size;
    int capacity;
    sem_t sem_mutex;
    sem_t sem_empty;
    sem_t sem_full;
} queue_t;

queue_t *queue_new(int size) {
    if (size <= 0) {
        return NULL;
    }
    queue_t *q = (queue_t *) malloc(sizeof(queue_t));
    if (q == NULL) {
        return NULL;
    }
    q->head = q->tail = NULL;
    q->size = 0;
    q->capacity = size;
    sem_init(&q->sem_mutex, 0, 1);
    sem_init(&q->sem_empty, 0, size);
    sem_init(&q->sem_full, 0, 0);

    return q;
}

void queue_delete(queue_t **q) {
    if (q == NULL || *q == NULL) {
        return;
    }

    queue_t *queue = *q;
    while (queue->head != NULL) {
        queue_node_t *temp = queue->head;
        queue->head = queue->head->next;
        free(temp);
    }

    sem_destroy(&queue->sem_mutex);
    sem_destroy(&queue->sem_empty);
    sem_destroy(&queue->sem_full);

    free(queue);
    *q = NULL;
}

bool queue_push(queue_t *q, void *elem) {
    sem_wait(&q->sem_empty);
    sem_wait(&q->sem_mutex);

    queue_node_t *new_node = (queue_node_t *) malloc(sizeof(queue_node_t));
    if (new_node == NULL) {
        sem_post(&q->sem_mutex);
        sem_post(&q->sem_empty);
        return false;
    }

    new_node->data = elem;
    new_node->next = NULL;

    if (q->head == NULL) {
        q->head = q->tail = new_node;
    } else {
        q->tail->next = new_node;
        q->tail = new_node;
    }

    q->size++;

    sem_post(&q->sem_mutex);
    sem_post(&q->sem_full);

    return true;
}

bool queue_pop(queue_t *q, void **elem) {
    sem_wait(&q->sem_full);
    sem_wait(&q->sem_mutex);

    queue_node_t *temp = q->head;
    *elem = temp->data;

    if (q->head == q->tail) {
        q->head = q->tail = NULL;
    } else {
        q->head = q->head->next;
    }

    free(temp);
    q->size--;

    sem_post(&q->sem_mutex);
    sem_post(&q->sem_empty);

    return true;
}
