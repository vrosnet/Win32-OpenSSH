#include "w32posix.h"
#include "w32fd.h"
#include <stdarg.h>
#include <errno.h>
#include <time.h>

struct w32fd_table {
    w32_fd_set occupied;
    struct w32_io* w32_ios[MAX_FDS];
};

struct w32fd_table fd_table;
struct w32_io w32_io_stdin, w32_io_stdout, w32_io_stderr;
void fd_table_set(struct w32_io* pio, int index);

int fd_table_initialize() {
    memset(&fd_table, 0, sizeof(fd_table));
    memset(&w32_io_stdin, 0, sizeof(w32_io_stdin));
    w32_io_stdin.handle = (HANDLE)STD_INPUT_HANDLE;
    fd_table_set(&w32_io_stdin, STDIN_FILENO);
    memset(&w32_io_stdout, 0, sizeof(w32_io_stdout));
    w32_io_stdout.handle = (HANDLE)STD_OUTPUT_HANDLE;
    fd_table_set(&w32_io_stdout, STDOUT_FILENO);
    memset(&w32_io_stderr, 0, sizeof(w32_io_stderr));
    w32_io_stderr.handle = (HANDLE)STD_ERROR_HANDLE;
    fd_table_set(&w32_io_stderr, STDERR_FILENO);
    return 0;
}

int fd_table_get_min_index() {
    int min_index = 0;
    unsigned char* bitmap = fd_table.occupied.bitmap;
    unsigned char tmp;

    while (*bitmap == 0xff)
    {
        bitmap++;
        min_index += 8;
        if (min_index >= MAX_FDS) {
            errno = EMFILE;
            debug("ERROR: MAX_FDS limit reached");
            return -1;
        }
    }

    tmp = *bitmap;

    while (tmp & 0x80)
    {
        tmp <<= 1;
        min_index++;
    }

    return min_index;
}

void fd_table_set(struct w32_io* pio, int index) {
    fd_table.w32_ios[index] = pio;
    pio->table_index = index;
    FD_SET(index, &(fd_table.occupied));
}

void fd_table_clear(int index)
{
    fd_table.w32_ios[index]->table_index = -1;
    fd_table.w32_ios[index] = NULL;
    FD_CLR(index, &(fd_table.occupied));
}

void w32posix_initialize() {
    debug_initialize();
    fd_table_initialize();
    socketio_initialize();
}

void w32posix_done() {
    debug_done();
    socketio_done();
}

BOOL w32_io_is_blocking(struct w32_io* pio)
{
    return (pio->fd_status_flags & O_NONBLOCK) ? FALSE : TRUE;
}

BOOL w32_io_is_io_available(struct w32_io* pio, BOOL rd) {
    if ((pio->type == LISTEN_FD) || (pio->type == SOCK_FD)) {
        return socketio_is_io_available(pio, rd);
    }
    else {
        return fileio_is_io_available(pio, rd);
    }

}

int w32_io_on_select(struct w32_io* pio, BOOL rd)
{
    if ((pio->type == LISTEN_FD) || (pio->type == SOCK_FD)) {
        return socketio_on_select(pio, rd);
    }
    else {
        return fileio_on_select(pio, rd);
    }

}

int w32_socket(int domain, int type, int protocol) {
    int min_index = fd_table_get_min_index();
    struct w32_io* pio = NULL;

    if (min_index == -1)
        return -1;

    pio = socketio_socket(domain, type, protocol);
    if (!pio) {
        return -1;
    }

    fd_table_set(pio, min_index);
    debug("socket:%p, io:%p, fd:%d ", pio->sock, pio, min_index);
    return min_index;
}

int w32_accept(int fd, struct sockaddr* addr, int* addrlen)
{
    int min_index = fd_table_get_min_index();
    struct w32_io* pio = NULL;

    if (min_index == -1)
        return -1;

    pio = socketio_accept(fd_table.w32_ios[fd], addr, addrlen);
    if (!pio) {
        return -1;
    }

    fd_table_set(pio, min_index);
    debug("socket:%p, io:%p, fd:%d ", pio->sock, pio, min_index);
    return min_index;
}

#define CHECK_FD(fd) \
do { \
    if ((fd > MAX_FDS - 1) || fd_table.w32_ios[fd] == NULL) { \
         errno = EBADF; \
         debug("bad fd: %d", fd); \
         return -1; \
    } \
} while (0)

int w32_setsockopt(int fd, int level, int optname, const char* optval, int optlen) {
    CHECK_FD(fd);
    return socketio_setsockopt(fd_table.w32_ios[fd], level, optname, optval, optlen);
}

int w32_getsockopt(int fd, int level, int optname, char* optval, int* optlen) {
    CHECK_FD(fd);
    return socketio_getsockopt(fd_table.w32_ios[fd], level, optname, optval, optlen);
}

int w32_getsockname(int fd, struct sockaddr* name, int* namelen) {
    CHECK_FD(fd);
    return socketio_getsockname(fd_table.w32_ios[fd], name, namelen);
}

int w32_getpeername(int fd, struct sockaddr* name, int* namelen) {
    CHECK_FD(fd);
    return socketio_getpeername(fd_table.w32_ios[fd], name, namelen);
}

int w32_listen(int fd, int backlog) {
    CHECK_FD(fd);
    return socketio_listen(fd_table.w32_ios[fd], backlog);
}

int w32_bind(int fd, const struct sockaddr *name, int namelen) {
    CHECK_FD(fd);
    return socketio_bind(fd_table.w32_ios[fd], name, namelen);
}

int w32_connect(int fd, const struct sockaddr* name, int namelen) {
    CHECK_FD(fd);
    return socketio_connect(fd_table.w32_ios[fd], name, namelen);
}

int w32_recv(int fd, void *buf, size_t len, int flags) {
    CHECK_FD(fd);
    return socketio_recv(fd_table.w32_ios[fd], buf, len, flags);
}

int w32_send(int fd, const void *buf, size_t len, int flags) {
    CHECK_FD(fd);
    return socketio_send(fd_table.w32_ios[fd], buf, len, flags);
}


int w32_shutdown(int fd, int how) {
    CHECK_FD(fd);
    return socketio_shutdown(fd_table.w32_ios[fd], how);
}


//file io
int w32_pipe(int *pfds){
    int read_index, write_index;
    struct w32_io* pio[2];

    read_index = fd_table_get_min_index();
    if (read_index == -1)
        return -1;

    //temporarily set occupied bit
    FD_SET(read_index, &fd_table.occupied);
    write_index = fd_table_get_min_index();
    FD_CLR(read_index, &fd_table.occupied);
    if (write_index == -1)
        return -1;

    if (-1 == fileio_pipe(pio))
        return -1;

    fd_table_set(pio[0], read_index);
    fd_table_set(pio[1], write_index);
    pfds[0] = read_index;
    pfds[1] = write_index;
    debug("read end: handle:%p, io:%p, fd:%d", pio[0]->handle, pio[0], read_index);
    debug("write end: handle:%p, io:%p, fd:%d", pio[1]->handle, pio[1], write_index);
    return 0;
}

int w32_open(const char *pathname, int flags, ...) {
    int min_index = fd_table_get_min_index();
    struct w32_io* pio;
    
    if (min_index == -1)
        return -1;
    
    pio = fileio_open(pathname, flags, 0);
    if (pio == NULL)
        return -1;

    fd_table_set(pio, min_index);
    debug("handle:%p, io:%p, fd:%d", pio->handle, pio, min_index);
    return min_index;
}

int w32_read(int fd, void *dst, unsigned int max) {
    CHECK_FD(fd);
    return fileio_read(fd_table.w32_ios[fd], dst, max);
}

int w32_write(int fd, const void *buf, unsigned int max) {
    CHECK_FD(fd);
    return fileio_write(fd_table.w32_ios[fd], buf, max);
}

int w32_fstat(int fd, struct stat *buf) {
    CHECK_FD(fd);
    return fileio_fstat(fd_table.w32_ios[fd], buf);
}

int w32_isatty(int fd) {
    CHECK_FD(fd);
    return fileio_isatty(fd_table.w32_ios[fd]);
}

FILE* w32_fdopen(int fd, const char *mode) {
    if ((fd > MAX_FDS - 1) || fd_table.w32_ios[fd] == NULL) {
        errno = EBADF;
        debug("bad fd: %d", fd);
        return NULL;
    }

    return fileio_fdopen(fd_table.w32_ios[fd], mode);
}

//Common IO
int w32_close(int fd) {

    struct w32_io* pio;

    CHECK_FD(fd);
    pio = fd_table.w32_ios[fd];
    
    debug("io:%p, type:%d, fd:%d, table_index:%d", pio, pio->type, fd, pio->table_index);
    fd_table_clear(pio->table_index);
    if ((pio->type == LISTEN_FD) || (pio->type == SOCK_FD)) {
        return socketio_close(pio);
    }
    else
        return fileio_close(pio);
}

int w32_fcntl(int fd, int cmd, ... /* arg */) {
    va_list valist;
    va_start(valist, cmd);

    CHECK_FD(fd);

    switch (cmd){
    case F_GETFL:
        return fd_table.w32_ios[fd]->fd_status_flags;
    case F_SETFL:
        fd_table.w32_ios[fd]->fd_status_flags = va_arg(valist, int);
        return 0;
    case F_GETFD:
        return fd_table.w32_ios[fd]->fd_flags;
        return 0;
    case F_SETFD:
        fd_table.w32_ios[fd]->fd_flags = va_arg(valist, int);
        return 0;
    default:
        errno = EINVAL;
        debug("ERROR: cmd:%d", cmd);
        return -1;
    }
}

int w32_select(int fds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const struct timeval *timeout) {
    int in_ready_fds = 0, out_ready_fds = 0, i;
    fd_set read_ready_fds, write_ready_fds;
    HANDLE events[32];
    int num_events = 0;
    unsigned int time_milliseconds = timeout->tv_sec * 100 + timeout->tv_usec / 1000;
    ULONGLONG ticks_start = GetTickCount64(), ticks_now;

    memset(&read_ready_fds, 0, sizeof(fd_set));
    memset(&write_ready_fds, 0, sizeof(fd_set));

    if (fds > MAX_FDS ) {
        errno = EINVAL;
        debug("ERROR: fds: %d", fds);
        return -1;
    }

    if (!readfds && !writefds) {
        errno = EINVAL;
        debug("ERROR: null fd_sets");
        return -1;
    }

    if (exceptfds) {
        errno = EOPNOTSUPP;
        debug("ERROR: exceptfds not supported");
        return -1;
    }

    if (readfds) {
        for (i = 0; i < fds; i++) 
            if (FD_ISSET(i, readfds))
                CHECK_FD(i);
    }

    if (writefds) {
        for (i = 0; i < fds; i++) 
            if (FD_ISSET(i, writefds))
                CHECK_FD(i);
    }


    //see if any io is ready
    for (i = 0; i < fds; i++) {

        if (readfds && FD_ISSET(i, readfds)) {
            in_ready_fds++;
            if (w32_io_is_io_available(fd_table.w32_ios[i], TRUE)) {
                FD_SET(i, &read_ready_fds);
                out_ready_fds++;
            }
        }

        if (writefds && FD_ISSET(i, writefds)) {
            in_ready_fds++;
            if (w32_io_is_io_available(fd_table.w32_ios[i], FALSE)) {
                FD_SET(i, &write_ready_fds);
                out_ready_fds++;
            }
        }
    }

    //if none of input fds are set return error
    if (in_ready_fds == 0) {
        errno = EINVAL;
        debug("ERROR: empty fd_sets");
        return -1;
    }

    //if io on some fds is already ready, return
    if (out_ready_fds)
    {
        if (readfds)
            *readfds = read_ready_fds;
        if (writefds)
            *writefds = write_ready_fds;
        debug2("IO ready:%d, no wait", out_ready_fds);
        return out_ready_fds;
    }

    //start async io on selected fds
    for (int i = 0; i < fds; i++) {

        if (readfds && FD_ISSET(i, readfds)) {
            if (w32_io_on_select(fd_table.w32_ios[i], TRUE) == -1)
                return -1;
            if (fd_table.w32_ios[i]->type == LISTEN_FD) {
                events[num_events++] = fd_table.w32_ios[i]->read_overlapped.hEvent;
            }
        }

        if (writefds && FD_ISSET(i, writefds)) {
            if (w32_io_on_select(fd_table.w32_ios[i], FALSE) == -1)
                return -1;
        }
    }

    
    do {
        ticks_now = GetTickCount64();
        if (time_milliseconds < (ticks_now - ticks_start)) {
            errno = ETIMEDOUT;
            debug("select timing out");
            return -1;
        }
            
        if (0 != wait_for_any_event(events, num_events, time_milliseconds - (ticks_now - ticks_start))) {
            return -1;
        }

        //check on fd status
        out_ready_fds = 0;
        for (int i = 0; i < fds; i++) {

            if (readfds && FD_ISSET(i, readfds)) {
                in_ready_fds++;
                if (w32_io_is_io_available(fd_table.w32_ios[i], TRUE)) {
                    FD_SET(i, &read_ready_fds);
                    out_ready_fds++;
                }
            }

            if (writefds && FD_ISSET(i, writefds)) {
                in_ready_fds++;
                if (w32_io_is_io_available(fd_table.w32_ios[i], FALSE)) {
                    FD_SET(i, &write_ready_fds);
                    out_ready_fds++;
                }
            }
        }

        if (out_ready_fds)
            break;

        debug2("wait ended without any IO completion, looping again");

    } while (1);

    if (readfds)
        *readfds = read_ready_fds;
    if (writefds)
        *writefds = write_ready_fds;
    
    return out_ready_fds;
 
}


int w32_dup(int oldfd) {
    errno = EOPNOTSUPP;
    debug("ERROR: dup is not implemented yet");
    return -1;
}

int w32_dup2(int oldfd, int newfd) {
    errno = EOPNOTSUPP;
    debug("ERROR: dup2 is not implemented yet");
    return -1;
}