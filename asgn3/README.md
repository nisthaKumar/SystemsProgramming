# Assignment 3 Readme
# rwlock.c

Functions implemented:
rwlock_t *rwlock_new(PRIORITY p, uint32_t n);
- Allocates and initializes a new rwlock with priority 'p' and value 'n' for N_WAY priority

rwlock is a structure which has the following attributes:
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

All these get initialized when rwlock_new() is called.

void rwlock_delete(rwlock_t **rw);
- Deletes the rwlock once the read/write has been completed. Also, any allocated memor is freed in this function.

void reader_lock(rwlock_t *rw);
- This function implements accquiring the lock functionality for reader. Multiple threads can read simultaneously,
but only one can be present in the critical section at a time. Thus I have used a shared mutex: 'cslock' which is shared by both read and write.
So, we need to release the lock after every reader as well and in various scenarios it should give the lock to the waiting writer instead of the reader. This would be when priority is WRITERS. Also for N_WAY when n readers are already done. There can be multiple scenarios and all needs to be handled by properly sending a signal or broadcast based on if its writer or reader.

void reader_unlock(rwlock_t *rw);
- Once the readers are done reading, they need to release both the cslock and rwlock. Here again we need to check if the lock has to be given to writer or the nect reader. Based on valid conditions, a signal is sent.

void writer_lock(rwlock_t *rw);
- This would lock the shared resource for writing. It allows a single writer to access the critical section at time, but no other writers or reades. It will wait for signal from reader in scenarios its readers turn to take the lock.

void writer_unlock(rwlock_t *rw);
- This would release the lock which was accquired by writer. In case all the readers have to be informed, a broadcast is sent. If its just releasing the lock for next writer, its a signal.

All the lock/unlock functions have the checks on priority(READERS, WRITERS and N_WAY). Based on the priority and various scenarios, the implementation is done.

