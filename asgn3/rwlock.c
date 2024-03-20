#include "rwlock.h"
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct rwlock {
    int readersCount;
    int writersCount;
    int n;
    bool readers_turn;
    int n_way_count;
    int activeReadersCount;
    int activeWritersCount;
    pthread_mutex_t rwlock;
    pthread_mutex_t cslock;
    pthread_cond_t read;
    pthread_cond_t write;
    PRIORITY priority;
};

rwlock_t *rwlock_new(PRIORITY p, uint32_t n) {
    rwlock_t *rw = (rwlock_t *) malloc(sizeof(rwlock_t));
    if (rw == NULL) {
        return NULL;
    }

    pthread_mutex_init(&(rw->rwlock), NULL);
    pthread_mutex_init(&(rw->cslock), NULL);
    pthread_cond_init(&(rw->read), NULL);
    pthread_cond_init(&(rw->write), NULL);

    rw->readersCount = 0;
    rw->writersCount = 0;
    rw->n = n;
    rw->activeReadersCount = 0;
    rw->activeWritersCount = 0;
    rw->readers_turn = true;
    rw->n_way_count = n;
    rw->priority = p;

    return rw;
}

void rwlock_delete(rwlock_t **rw) {
    if (rw == NULL || *rw == NULL) {
        return;
    }
    pthread_mutex_destroy(&(*rw)->rwlock);
    pthread_mutex_destroy(&(*rw)->cslock);
    pthread_cond_destroy(&(*rw)->read);
    pthread_cond_destroy(&(*rw)->write);

    free(*rw);
}

void reader_lock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->cslock);
    rw->readersCount++;
    if (rw->priority == N_WAY) {
        while ((rw->activeWritersCount != 0) || (!rw->readers_turn)
               || (rw->n - rw->activeReadersCount <= 0 && rw->writersCount != 0)
               || (rw->n_way_count == 0 && rw->writersCount != 0)) {
            pthread_cond_wait(&rw->read, &rw->cslock);
            if (rw->readers_turn && (rw->writersCount != 0 && rw->n_way_count - 1 >= 0))
                break;
        }
        if (rw->writersCount == 0) {
            if (rw->activeReadersCount == 0)
                pthread_mutex_lock(&rw->rwlock);
            rw->activeReadersCount++;
            if (rw->n_way_count - 1 >= 0)
                rw->n_way_count--;
        } else {
            if (rw->n_way_count == rw->n)
                pthread_mutex_lock(&rw->rwlock);
            rw->activeReadersCount++;
            rw->n_way_count--;
        }
        pthread_mutex_unlock(&rw->cslock);
    } else if (rw->priority == READERS) {
        while (rw->activeWritersCount != 0)
            pthread_cond_wait(&rw->read, &rw->cslock);
        rw->activeReadersCount++;
        if (rw->readersCount == 1)
            pthread_mutex_lock(&rw->rwlock);
        pthread_mutex_unlock(&rw->cslock);
    } else {
        while (rw->writersCount != 0)
            pthread_cond_wait(&rw->read, &rw->cslock);
        if (rw->readersCount == 1)
            pthread_mutex_lock(&rw->rwlock);
        rw->activeReadersCount++;
        pthread_mutex_unlock(&rw->cslock);
    }
}

void reader_unlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->cslock);
    rw->readersCount--;
    if (rw->priority == N_WAY) {
        rw->activeReadersCount--;
        if (rw->activeReadersCount == 0) {
            pthread_mutex_unlock(&rw->rwlock);
            rw->n_way_count = rw->n;
            if (rw->writersCount != 0) {
                rw->readers_turn = false;
                pthread_cond_signal(&rw->write);
            }
        }
        pthread_mutex_unlock(&rw->cslock);
    } else if (rw->priority == READERS) {
        rw->activeReadersCount--;
        if (rw->readersCount == 0) {
            pthread_mutex_unlock(&rw->rwlock);
            pthread_mutex_unlock(&rw->cslock);
            pthread_cond_signal(&rw->write);
        } else
            pthread_mutex_unlock(&rw->cslock);
    } else {
        rw->activeReadersCount--;
        if (rw->writersCount != 0) {
            if (rw->activeReadersCount == 0) {
                pthread_mutex_unlock(&rw->rwlock);
                pthread_mutex_unlock(&rw->cslock);
                pthread_cond_signal(&rw->write);
            } else
                pthread_mutex_unlock(&rw->cslock);
        } else if (rw->readersCount != 0)
            pthread_mutex_unlock(&rw->cslock);
        else {
            pthread_mutex_unlock(&rw->rwlock);
            pthread_mutex_unlock(&rw->cslock);
        }
    }
}

void writer_lock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->cslock);
    rw->writersCount++;
    if (rw->priority == N_WAY) {
        while (rw->activeReadersCount != 0 || rw->activeWritersCount != 0 || rw->readers_turn)
            pthread_cond_wait(&rw->write, &rw->cslock);
        rw->activeWritersCount++;
        pthread_mutex_lock(&rw->rwlock);
        pthread_mutex_unlock(&rw->cslock);
    } else if (rw->priority == READERS) {
        while (rw->readersCount != 0 || rw->activeWritersCount != 0)
            pthread_cond_wait(&rw->write, &rw->cslock);
        rw->activeWritersCount++;
        pthread_mutex_lock(&rw->rwlock);
        pthread_mutex_unlock(&rw->cslock);
    } else {
        while (rw->activeReadersCount != 0 || rw->activeWritersCount != 0)
            pthread_cond_wait(&rw->write, &rw->cslock);
        rw->activeWritersCount++;
        pthread_mutex_lock(&rw->rwlock);
        pthread_mutex_unlock(&rw->cslock);
    }
}

void writer_unlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->cslock);
    rw->writersCount--;
    if (rw->priority == N_WAY) {
        rw->activeWritersCount--;
        pthread_mutex_unlock(&rw->rwlock);
        if (rw->readersCount != 0) {
            rw->readers_turn = true;
            pthread_mutex_unlock(&rw->cslock);
            pthread_cond_broadcast(&rw->read);
        } else if (rw->writersCount != 0) {
            pthread_mutex_unlock(&rw->cslock);
            pthread_cond_signal(&rw->write);
        } else
            pthread_mutex_unlock(&rw->cslock);
    } else if (rw->priority == READERS) {
        pthread_mutex_unlock(&rw->rwlock);
        rw->activeWritersCount--;
        if (rw->readersCount != 0) {
            pthread_mutex_unlock(&rw->cslock);
            pthread_cond_broadcast(&rw->read);
        } else if (rw->writersCount != 0) {
            pthread_mutex_unlock(&rw->cslock);
            pthread_cond_signal(&rw->write);
        } else {
            pthread_mutex_unlock(&rw->cslock);
        }
    } else {
        pthread_mutex_unlock(&rw->rwlock);
        rw->activeWritersCount--;
        if (rw->writersCount != 0) {
            pthread_mutex_unlock(&rw->cslock);
            pthread_cond_signal(&rw->write);
        } else if (rw->readersCount != 0) {
            pthread_mutex_unlock(&rw->cslock);
            pthread_cond_broadcast(&rw->read);
        } else
            pthread_mutex_unlock(&rw->cslock);
    }
}
