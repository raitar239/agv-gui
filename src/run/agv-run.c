/*
 * agv-run: start a program outside the VPN.
 *
 *   agv-run [--app ID] [--] COMMAND [ARGS...]
 *
 * Asks agvd to move this process into an excluded cgroup, then execs COMMAND, which keeps the
 * pid and therefore the cgroup; its children inherit it. Without --app the builtin group is
 * used. If agvd refuses or is not running, the command is NOT started, so it never silently
 * goes through the VPN.
 *
 * Plain libc on purpose: it runs before every excluded app and must start instantly.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define DEFAULT_SOCKET "/run/agvd/run.sock"

static void usage(void)
{
    fprintf(stderr, "Usage: agv-run [--app ID] [--] COMMAND [ARGS...]\n"
                    "Start COMMAND with its traffic excluded from AdGuard VPN.\n");
}

static int request(const char *socket_path, const char *app, char *reply, size_t reply_size)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(address.sun_path)) {
        close(fd);
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(address.sun_path, socket_path);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    char line[128];
    int length = app ? snprintf(line, sizeof(line), "RUN %s\n", app) : snprintf(line, sizeof(line), "RUN\n");
    if (length < 0 || (size_t)length >= sizeof(line) || write(fd, line, (size_t)length) != length) {
        close(fd);
        errno = EINVAL;
        return -1;
    }

    size_t used = 0;
    while (used + 1 < reply_size) {
        ssize_t n = read(fd, reply + used, reply_size - used - 1);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        used += (size_t)n;
        if (memchr(reply, '\n', used))
            break;
    }
    reply[used] = '\0';
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    const char *app = NULL;
    int i = 1;
    for (; i < argc; ++i) {
        if (strcmp(argv[i], "--app") == 0 && i + 1 < argc) {
            app = argv[++i];
            if (!*app || strpbrk(app, " \t\r\n")) {
                fprintf(stderr, "agv-run: invalid app id\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "--") == 0) {
            ++i;
            break;
        } else {
            break;
        }
    }
    if (i >= argc) {
        usage();
        return 2;
    }

    const char *socket_path = getenv("AGV_RUN_SOCKET");
    if (!socket_path || !*socket_path)
        socket_path = DEFAULT_SOCKET;

    char reply[256];
    if (request(socket_path, app, reply, sizeof(reply)) < 0) {
        fprintf(stderr, "agv-run: cannot reach agvd at %s: %s\n", socket_path, strerror(errno));
        return 1;
    }
    if (strncmp(reply, "OK", 2) != 0) {
        const char *reason = strncmp(reply, "ERR ", 4) == 0 ? reply + 4 : "no answer from agvd\n";
        fprintf(stderr, "agv-run: %s", reason);
        if (reason[strlen(reason) - 1] != '\n')
            fputc('\n', stderr);
        return 1;
    }

    execvp(argv[i], argv + i);
    fprintf(stderr, "agv-run: %s: %s\n", argv[i], strerror(errno));
    return 127;
}
