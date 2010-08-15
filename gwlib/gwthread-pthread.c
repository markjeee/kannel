/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2009 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

/*
 * gwthread-pthread.c - implementation of gwthread.h using POSIX threads.
 *
 * Richard Braakman
 */

#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>

#include "gwlib/gwlib.h"

#ifdef HAVE_LIBSSL
#include <openssl/err.h>
#endif /* HAVE_LIBSSL */

/* Maximum number of live threads we can support at once.  Increasing
 * this will increase the size of the threadtable.  Use powers of two
 * for efficiency. */
#define THREADTABLE_SIZE 1024

struct threadinfo
{
    pthread_t self;
    const char *name;
    gwthread_func_t *func;
    long number;
    int wakefd_recv;
    int wakefd_send;
    /* joiners may be NULL.  It is not allocated until a thread wants
     * to register.  This is safe because the thread table is always
     * locked when a thread accesses this field. */
    List *joiners;
    pid_t pid;
};

struct new_thread_args
{
    gwthread_func_t *func;
    void *arg;
    struct threadinfo *ti;
    /* signals already started thread to die */
    int failed;
};

/* The index is the external thread number modulo the table size; the
 * thread number allocation code makes sure that there are no collisions. */
static struct threadinfo *threadtable[THREADTABLE_SIZE];
#define THREAD(t) (threadtable[(t) % THREADTABLE_SIZE])

/* Number of threads currently in the thread table. */
static long active_threads = 0;

/* Number to use for the next thread created.  The actual number used
 * may be higher than this, in order to avoid collisions in the threadtable.
 * Specifically, (threadnumber % THREADTABLE_SIZE) must be unique for all
 * live threads. */
static long next_threadnumber;

/* Info for the main thread is kept statically, because it should not
 * be deallocated even after the thread module shuts down -- after all,
 * the main thread is still running, and in practice, it can still
 * output debug messages which will require the thread number. */
static struct threadinfo mainthread;

/* Our key for accessing the (struct gwthread *) we stash in the
 * thread-specific-data area.  This is much more efficient than
 * accessing a global table, which we would have to lock. */
static pthread_key_t tsd_key;

static pthread_mutex_t threadtable_lock;

static void inline lock(void)
{
    int ret;

    ret = pthread_mutex_lock(&threadtable_lock);
    if (ret != 0) {
        panic(ret, "gwthread-pthread: could not lock thread table");
    }
}

static void inline unlock(void)
{
    int ret;

    ret = pthread_mutex_unlock(&threadtable_lock);
    if (ret != 0) {
        panic(ret, "gwthread-pthread: could not unlock thread table");
    }
}

/* Empty the wakeup pipe, in case we got several wakeup signals before
 * noticing.  We want to wake up only once. */
static void flushpipe(int fd)
{
    unsigned char buf[128];
    ssize_t bytes;

    do {
        bytes = read(fd, buf, sizeof(buf));
    } while (bytes > 0);
}

/* Allocate and fill a threadinfo structure for a new thread, and store
 * it in a free slot in the thread table.  The thread table must already
 * be locked by the caller.  Return the thread number chosen for this
 * thread.  The caller must make sure that there is room in the table. */
static long fill_threadinfo(pthread_t id, const char *name,
                            gwthread_func_t *func,
                            struct threadinfo *ti)
{
    int pipefds[2];
    long first_try;

    gw_assert(active_threads < THREADTABLE_SIZE);

    /* initialize to default values */
    ti->self = id;
    ti->name = name;
    ti->func = func;
    ti->pid = -1;
    ti->wakefd_recv = -1;
    ti->wakefd_send = -1;
    ti->joiners = NULL;
    ti->number = -1;

    if (pipe(pipefds) < 0) {
        error(errno, "cannot allocate wakeup pipe for new thread");
        return -1;
    }
    ti->wakefd_recv = pipefds[0];
    ti->wakefd_send = pipefds[1];
    socket_set_blocking(ti->wakefd_recv, 0);
    socket_set_blocking(ti->wakefd_send, 0);

    /* Find a free table entry and claim it. */
    first_try = next_threadnumber;
    do {
        ti->number = next_threadnumber++;
        /* Check if we looped all the way around the thread table. */
        if (ti->number == first_try + THREADTABLE_SIZE) {
            error(0, "Cannot have more than %d active threads", THREADTABLE_SIZE);
            ti->number = -1;
            return -1;
        }
    } while (THREAD(ti->number) != NULL);
    THREAD(ti->number) = ti;

    active_threads++;

    return ti->number;
}

/* Look up the threadinfo pointer for the current thread */
static struct threadinfo *getthreadinfo(void)
{
    struct threadinfo *threadinfo;

    threadinfo = pthread_getspecific(tsd_key);
    if (threadinfo == NULL) {
        panic(0, "gwthread-pthread: pthread_getspecific failed");
    } else {
        gw_assert(pthread_equal(threadinfo->self, pthread_self()));
    }
    return threadinfo;
}

/*
 * Go through the list of threads waiting for us to exit, and tell
 * them that we're exiting.  The joiner_cond entries are registered
 * by those threads, and will be cleaned up by them.
 */
static void alert_joiners(void)
{
    struct threadinfo *threadinfo;
    pthread_cond_t *joiner_cond;

    threadinfo = getthreadinfo();
    if (!threadinfo->joiners)
        return;
    while ((joiner_cond = gwlist_extract_first(threadinfo->joiners))) {
        pthread_cond_broadcast(joiner_cond);
    }
}

static void delete_threadinfo(void)
{
    struct threadinfo *threadinfo;

    threadinfo = getthreadinfo();
    gwlist_destroy(threadinfo->joiners, NULL);
    if (threadinfo->wakefd_recv != -1)
        close(threadinfo->wakefd_recv);
    if (threadinfo->wakefd_send != -1)
        close(threadinfo->wakefd_send);
    if (threadinfo->number != -1) {
        THREAD(threadinfo->number) = NULL;
        active_threads--;
    }
    gw_assert(threadinfo != &mainthread);
    gw_free(threadinfo);
}

void gwthread_init(void)
{
    int ret;
    int i;

    pthread_mutex_init(&threadtable_lock, NULL);

    ret = pthread_key_create(&tsd_key, NULL);
    if (ret != 0) {
        panic(ret, "gwthread-pthread: pthread_key_create failed");
    }

    for (i = 0; i < THREADTABLE_SIZE; i++) {
        threadtable[i] = NULL;
    }
    active_threads = 0;

    /* create main thread info */
    if (fill_threadinfo(pthread_self(), "main", NULL, &mainthread) == -1)
        panic(0, "gwthread-pthread: unable to fill main threadinfo.");

    ret = pthread_setspecific(tsd_key, &mainthread);
    if (ret != 0)
        panic(ret, "gwthread-pthread: pthread_setspecific failed");
}

/* Note that the gwthread library can't shut down completely, because
 * the main thread will still be running, and it may make calls to
 * gwthread_self(). */
void gwthread_shutdown(void)
{
    int ret;
    int running;
    int i;

    /* Main thread must not have disappeared */
    gw_assert(threadtable[0] != NULL);
    lock();

    running = 0;
    /* Start i at 1 to skip the main thread, which is supposed to be
     * still running. */
    for (i = 1; i < THREADTABLE_SIZE; i++) {
        if (threadtable[i] != NULL) {
            debug("gwlib", 0, "Thread %ld (%s) still running",
                  threadtable[i]->number,
                  threadtable[i]->name);
            running++;
        }
    }
    unlock();

    /* We can't do a full cleanup this way */
    if (running)
        return;

    ret = pthread_mutex_destroy(&threadtable_lock);
    if (ret != 0) {
        warning(ret, "cannot destroy threadtable lock");
    }

    /* We can't delete the tsd_key here, because gwthread_self()
     * still needs it to access the main thread's info. */
}

static void *new_thread(void *arg)
{
    int ret;
    struct new_thread_args *p = arg;

    /* Make sure we don't start until our parent has entered
     * our thread info in the thread table. */
    lock();
    /* check for initialization errors */
    if (p->failed) {
        /* Must free p before signaling our exit, otherwise there is
        * a race with gw_check_leaks at shutdown. */
        gw_free(p);
        delete_threadinfo();
        unlock();
        return NULL;
    }
    unlock();

    /* This has to be done here, because pthread_setspecific cannot
     * be called by our parent on our behalf.  That's why the ti
     * pointer is passed in the new_thread_args structure. */
    /* Synchronization is not a problem, because the only thread
     * that relies on this call having been made is this one --
     * no other thread can access our TSD anyway. */
    ret = pthread_setspecific(tsd_key, p->ti);
    if (ret != 0) {
        panic(ret, "gwthread-pthread: pthread_setspecific failed");
    }

    p->ti->pid = getpid();
    debug("gwlib.gwthread", 0, "Thread %ld (%s) maps to pid %ld.",
          p->ti->number, p->ti->name, (long) p->ti->pid);

    (p->func)(p->arg);

    lock();
    debug("gwlib.gwthread", 0, "Thread %ld (%s) terminates.",
          p->ti->number, p->ti->name);
    alert_joiners();
#ifdef HAVE_LIBSSL
    /* Clear the OpenSSL thread-specific error queue to avoid
     * memory leaks. */
    ERR_remove_state(gwthread_self());
#endif /* HAVE_LIBSSL */
    /* Must free p before signaling our exit, otherwise there is
     * a race with gw_check_leaks at shutdown. */
    gw_free(p);
    delete_threadinfo();
    unlock();

    return NULL;
}

/*
 * Change this thread's signal mask to block user-visible signals
 * (HUP, TERM, QUIT, INT), and store the old signal mask in
 * *old_set_storage.
 * Return 0 for success, or -1 if an error occurred.
 */
 
 /* 
  * This does not work in Darwin alias MacOS X alias Mach kernel,
  * however. So we define a dummy function doing nothing.
  */
#if defined(DARWIN_OLD)
    static int pthread_sigmask();
#endif
  
static int block_user_signals(sigset_t *old_set_storage)
{
    int ret;
    sigset_t block_signals;

    ret = sigemptyset(&block_signals);
    if (ret != 0) {
        error(errno, "gwthread-pthread: Couldn't initialize signal set");
	    return -1;
    }
    ret = sigaddset(&block_signals, SIGHUP);
    ret |= sigaddset(&block_signals, SIGTERM);
    ret |= sigaddset(&block_signals, SIGQUIT);
    ret |= sigaddset(&block_signals, SIGINT);
    if (ret != 0) {
        error(0, "gwthread-pthread: Couldn't add signal to signal set");
	    return -1;
    }
    ret = pthread_sigmask(SIG_BLOCK, &block_signals, old_set_storage);
    if (ret != 0) {
        error(ret, 
            "gwthread-pthread: Couldn't disable signals for thread creation");
        return -1;
    }
    return 0;
}

static void restore_user_signals(sigset_t *old_set)
{
    int ret;

    ret = pthread_sigmask(SIG_SETMASK, old_set, NULL);
    if (ret != 0) {
        panic(ret, "gwthread-pthread: Couldn't restore signal set.");
    }
}


static long spawn_thread(gwthread_func_t *func, const char *name, void *arg)
{
    int ret;
    pthread_t id;
    struct new_thread_args *p = NULL;
    long new_thread_id;

    /* We want to pass both these arguments to our wrapper function
     * new_thread, but the pthread_create interface will only let
     * us pass one pointer.  So we wrap them in a little struct. */
    p = gw_malloc(sizeof(*p));
    p->func = func;
    p->arg = arg;
    p->ti = gw_malloc(sizeof(*(p->ti)));
    p->failed = 0;

    /* Lock the thread table here, so that new_thread can block
     * on that lock.  That way, the new thread won't start until
     * we have entered it in the thread table. */
    lock();

    if (active_threads >= THREADTABLE_SIZE) {
        unlock();
        warning(0, "Too many threads, could not create new thread.");
        gw_free(p);
        return -1;
    }

    ret = pthread_create(&id, NULL, &new_thread, p);
    if (ret != 0) {
        unlock();
        error(ret, "Could not create new thread.");
        gw_free(p);
        return -1;
    }
    ret = pthread_detach(id);
    if (ret != 0) {
        error(ret, "Could not detach new thread.");
    }

    new_thread_id = fill_threadinfo(id, name, func, p->ti);
    if (new_thread_id == -1)
        p->failed = 1;
    unlock();
    
    if (new_thread_id != -1)
        debug("gwlib.gwthread", 0, "Started thread %ld (%s)", new_thread_id, name);
    else
        debug("gwlib.gwthread", 0, "Failed to start thread (%s)", name);

    return new_thread_id;
}

long gwthread_create_real(gwthread_func_t *func, const char *name, void *arg)
{
    int sigtrick = 0;
    sigset_t old_signal_set;
    long thread_id;

    /*
     * We want to make sure that only the main thread handles signals,
     * so that each signal is handled exactly once.  To do this, we
     * make sure that each new thread has all the signals that we
     * handle blocked.  To avoid race conditions, we block them in 
     * the spawning thread first, then create the new thread (which
     * inherits the settings), and then restore the old settings in
     * the spawning thread.  This means that there is a brief period
     * when no signals will be processed, but during that time they
     * should be queued by the operating system.
     */
    if (gwthread_self() == MAIN_THREAD_ID)
	    sigtrick = block_user_signals(&old_signal_set) == 0;

    thread_id = spawn_thread(func, name, arg);

    /*
     * Restore the old signal mask.  The new thread will have
     * inherited the resticted one, but the main thread needs
     * the old one back.
     */
    if (sigtrick)
 	    restore_user_signals(&old_signal_set);
    
    return thread_id;
}

void gwthread_join(long thread)
{
    struct threadinfo *threadinfo;
    pthread_cond_t exit_cond;
    int ret;

    gw_assert(thread >= 0);

    lock();
    threadinfo = THREAD(thread);
    if (threadinfo == NULL || threadinfo->number != thread) {
        /* The other thread has already exited */
        unlock();
        return;
    }

    /* Register our desire to be alerted when that thread exits,
     * and wait for it. */

    ret = pthread_cond_init(&exit_cond, NULL);
    if (ret != 0) {
        warning(ret, "gwthread_join: cannot create condition variable.");
        unlock();
        return;
    }

    if (!threadinfo->joiners)
        threadinfo->joiners = gwlist_create();
    gwlist_append(threadinfo->joiners, &exit_cond);

    /* The wait immediately releases the lock, and reacquires it
     * when the condition is satisfied.  So don't worry, we're not
     * blocking while keeping the table locked. */
    ret = pthread_cond_wait(&exit_cond, &threadtable_lock);
    unlock();

    if (ret != 0)
        warning(ret, "gwthread_join: error in pthread_cond_wait");

    pthread_cond_destroy(&exit_cond);
}

void gwthread_join_all(void)
{
    long i;
    long our_thread = gwthread_self();

    for (i = 0; i < THREADTABLE_SIZE; ++i) {
        if (THREAD(our_thread) != THREAD(i))
            gwthread_join(i);
    }
}

void gwthread_wakeup_all(void)
{
    long i;
    long our_thread = gwthread_self();

    for (i = 0; i < THREADTABLE_SIZE; ++i) {
        if (THREAD(our_thread) != THREAD(i))
            gwthread_wakeup(i);
    }
}

void gwthread_join_every(gwthread_func_t *func)
{
    struct threadinfo *ti;
    pthread_cond_t exit_cond;
    int ret;
    long i;

    ret = pthread_cond_init(&exit_cond, NULL);
    if (ret != 0) {
        warning(ret, "gwthread_join_every: cannot create condition variable.");
        unlock();
        return;
    }

    /*
     * FIXME: To be really safe, this function should keep looping
     * over the table until it does a complete run without having
     * to call pthread_cond_wait.  Otherwise, new threads could
     * start while we wait, and we'll miss them.
     */
    lock();
    for (i = 0; i < THREADTABLE_SIZE; ++i) {
        ti = THREAD(i);
        if (ti == NULL || ti->func != func)
            continue;
        debug("gwlib.gwthread", 0,
              "Waiting for %ld (%s) to terminate",
              ti->number, ti->name);
        if (!ti->joiners)
            ti->joiners = gwlist_create();
        gwlist_append(ti->joiners, &exit_cond);
        ret = pthread_cond_wait(&exit_cond, &threadtable_lock);
        if (ret != 0)
            warning(ret, "gwthread_join_all: error in pthread_cond_wait");
    }
    unlock();

    pthread_cond_destroy(&exit_cond);
}

/* Return the thread id of this thread. */
long gwthread_self(void)
{
    struct threadinfo *threadinfo;
    threadinfo = pthread_getspecific(tsd_key);
    if (threadinfo) 
        return threadinfo->number;
    else
        return -1;
}

/* Return the thread pid of this thread. */
long gwthread_self_pid(void)
{
    struct threadinfo *threadinfo;
    threadinfo = pthread_getspecific(tsd_key);
    if (threadinfo && threadinfo->pid != -1) 
        return (long) threadinfo->pid;
    else
        return (long) getpid();
}

void gwthread_self_ids(long *tid, long *pid)
{
    struct threadinfo *threadinfo;
    threadinfo = pthread_getspecific(tsd_key);
    if (threadinfo) {
        *tid = threadinfo->number;
        *pid = (threadinfo->pid != -1) ? threadinfo->pid : getpid();
    } else {
        *tid = -1;
        *pid = getpid();
    }
}

void gwthread_wakeup(long thread)
{
    unsigned char c = 0;
    struct threadinfo *threadinfo;
    int fd;

    gw_assert(thread >= 0);

    lock();

    threadinfo = THREAD(thread);
    if (threadinfo == NULL || threadinfo->number != thread) {
        unlock();
        return;
    }

    fd = threadinfo->wakefd_send;
    unlock();

    write(fd, &c, 1);
}

int gwthread_pollfd(int fd, int events, double timeout)
{
    struct pollfd pollfd[2];
    struct threadinfo *threadinfo;
    int milliseconds;
    int ret;

    threadinfo = getthreadinfo();

    pollfd[0].fd = threadinfo->wakefd_recv;
    pollfd[0].events = POLLIN;
    pollfd[0].revents = 0;

    pollfd[1].fd = fd;
    pollfd[1].events = events;
    pollfd[1].revents = 0;

    milliseconds = timeout * 1000;
    if (milliseconds < 0)
        milliseconds = POLL_NOTIMEOUT;

    ret = poll(pollfd, 2, milliseconds);
    if (ret < 0) {
        if (errno != EINTR)
            error(errno, "gwthread_pollfd: error in poll");
        return -1;
    }

    if (pollfd[0].revents)
        flushpipe(pollfd[0].fd);

    return pollfd[1].revents;
}

int gwthread_poll(struct pollfd *fds, long numfds, double timeout)
{
    struct pollfd *pollfds;
    struct threadinfo *threadinfo;
    int milliseconds;
    int ret;

    threadinfo = getthreadinfo();

    /* Create a new pollfd array with an extra element for the
     * thread wakeup fd. */

    pollfds = gw_malloc((numfds + 1) * sizeof(*pollfds));
    pollfds[0].fd = threadinfo->wakefd_recv;
    pollfds[0].events = POLLIN;
    pollfds[0].revents = 0;
    memcpy(pollfds + 1, fds, numfds * sizeof(*pollfds));

    milliseconds = timeout * 1000;
    if (milliseconds < 0)
        milliseconds = POLL_NOTIMEOUT;

    ret = poll(pollfds, numfds + 1, milliseconds);
    if (ret < 0) {
        if (errno != EINTR)
            error(errno, "gwthread_poll: error in poll");
        gw_free(pollfds);
        return -1;
    }
    if (pollfds[0].revents)
        flushpipe(pollfds[0].fd);

    /* Copy the results back to the caller */
    memcpy(fds, pollfds + 1, numfds * sizeof(*pollfds));
    gw_free(pollfds);

    return ret;
}


void gwthread_sleep(double seconds)
{
    struct pollfd pollfd;
    struct threadinfo *threadinfo;
    int milliseconds;
    int ret;

    threadinfo = getthreadinfo();

    pollfd.fd = threadinfo->wakefd_recv;
    pollfd.events = POLLIN;

    milliseconds = seconds * 1000;
    if (milliseconds < 0)
        milliseconds = POLL_NOTIMEOUT;

    ret = poll(&pollfd, 1, milliseconds);
    if (ret < 0) {
        if (errno != EINTR && errno != EAGAIN) {
            warning(errno, "gwthread_sleep: error in poll");
        }
    }
    if (ret == 1) {
        flushpipe(pollfd.fd);
    }
}


void gwthread_sleep_micro(double dseconds)
{
    fd_set fd_set_recv;
    struct threadinfo *threadinfo;
    int fd;
    int ret;

    threadinfo = getthreadinfo();
    fd = threadinfo->wakefd_recv;

    FD_ZERO(&fd_set_recv);
    FD_SET(fd, &fd_set_recv);

    if (dseconds < 0) {
        ret = select(fd + 1, &fd_set_recv, NULL, NULL, NULL);
    } else {
        struct timeval timeout;
        timeout.tv_sec = dseconds;
        timeout.tv_usec = (dseconds - timeout.tv_sec) * 1000000;

        ret = select(fd + 1, &fd_set_recv, NULL, NULL, &timeout);
    }

    if (ret < 0) {
        if (errno != EINTR && errno != EAGAIN) {
            warning(errno, "gwthread_sleep_micro: error in select()");
        }
    }

    if (FD_ISSET(fd, &fd_set_recv)) {
        flushpipe(fd);
    }
}


int gwthread_cancel(long thread)
{
    struct threadinfo *threadinfo;
    
    gw_assert(thread >= 0);
    
    threadinfo = THREAD(thread);
    if (threadinfo == NULL || threadinfo->number != thread) {
        return -1;
    } else {
        return pthread_cancel(threadinfo->self);
    }
}


#ifndef BROKEN_PTHREADS

/* Working pthreads */
int gwthread_shouldhandlesignal(int signal){
    return 1;
}
#else

/* Somewhat broken pthreads */ 
int gwthread_shouldhandlesignal(int signal){
    return (gwthread_self() == MAIN_THREAD_ID);
}
#endif

int gwthread_dumpsigmask(void) {
    sigset_t signal_set;
    int signum;

    /* Grab the signal set data from our thread */
    if (pthread_sigmask(SIG_BLOCK, NULL, &signal_set) != 0) {
	    warning(0, "gwthread_dumpsigmask: Couldn't get signal mask.");
	    return -1;
    }
    
    /* For each signal normally defined (there are usually only 32),
     * print a message if we don't block it. */
    for (signum = 1; signum <= 32; signum++) {
	     if (!sigismember(&signal_set, signum)) {
	         debug("gwlib", 0,
		     "gwthread_dumpsigmask: Signal Number %d will be caught.", 
		     signum);
	     }
    }
    return 0;
}

/* DARWIN alias MacOS X doesnt have pthread_sigmask in its pthreads implementation */

#if defined(DARWIN_OLD)
static int pthread_sigmask()
{
    return 0;
}
#endif
