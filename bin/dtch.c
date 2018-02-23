/* Copyright (c) 2017, Curtis McEnroe <programble@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sysexits.h>
#include <termios.h>
#include <unistd.h>

#if defined __FreeBSD__
#include <libutil.h>
#elif defined __linux__
#include <pty.h>
#else
#include <util.h>
#endif

static struct passwd *getUser(void) {
    uid_t uid = getuid();
    struct passwd *user = getpwuid(uid);
    if (!user) err(EX_OSFILE, "/etc/passwd");
    return user;
}

static struct sockaddr_un sockAddr(const char *home, const char *name) {
    struct sockaddr_un addr = { .sun_family = AF_LOCAL };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/.dtch/%s", home, name);
    return addr;
}

static char z;
static struct iovec iov = { .iov_base = &z, .iov_len = 1 };

static ssize_t sendFd(int sock, int fd) {
    size_t size = CMSG_LEN(sizeof(int));
    char buf[size];
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = buf,
        .msg_controllen = size,
    };

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_len = size;
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    *(int *)CMSG_DATA(cmsg) = fd;

    return sendmsg(sock, &msg, 0);
}

static int recvFd(int sock) {
    size_t size = CMSG_LEN(sizeof(int));
    char buf[size];
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = buf,
        .msg_controllen = size,
    };

    ssize_t n = recvmsg(sock, &msg, 0);
    if (n < 0) return -1;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS) {
        errno = ENOMSG;
        return -1;
    }

    return *(int *)CMSG_DATA(cmsg);
}

static struct sockaddr_un addr;
static void unlinkAddr(void) {
    unlink(addr.sun_path);
}

static int dtch(int argc, char *argv[]) {
    int error;

    const struct passwd *user = getUser();

    const char *name = user->pw_name;
    if (argc > 1) {
        name = argv[1];
        argv++;
        argc--;
    }
    if (argc > 1) {
        argv++;
    } else {
        argv[0] = user->pw_shell;
    }

    int home = open(user->pw_dir, 0);
    if (home < 0) err(EX_CANTCREAT, "%s", user->pw_dir);

    error = mkdirat(home, ".dtch", 0700);
    if (error && errno != EEXIST) err(EX_CANTCREAT, "%s/.dtch", user->pw_dir);

    close(home);

    int server = socket(PF_LOCAL, SOCK_STREAM, 0);
    if (server < 0) err(EX_OSERR, "socket");

    addr = sockAddr(user->pw_dir, name);
    error = bind(server, (struct sockaddr *)&addr, sizeof(addr));
    if (error) err(EX_CANTCREAT, "%s", addr.sun_path);
    atexit(unlinkAddr);

    error = chmod(addr.sun_path, 0600);
    if (error) err(EX_IOERR, "%s", addr.sun_path);

    error = fcntl(server, F_SETFD, FD_CLOEXEC);
    if (error) err(EX_IOERR, "fcntl(%d)", server);

    int pty;
    pid_t pid = forkpty(&pty, NULL, NULL, NULL);
    if (pid < 0) err(EX_OSERR, "forkpty");

    if (!pid) {
        execvp(*argv, argv);
        err(EX_NOINPUT, "%s", *argv);
    }

    error = listen(server, 0);
    if (error) err(EX_OSERR, "listen");

    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) err(EX_IOERR, "accept(%d)", server);

        ssize_t size = sendFd(client, pty);
        if (size < 0) warn("sendmsg(%d)", client);

        size = recv(client, &z, sizeof(z), 0);
        if (size < 0) warn("recv(%d)", client);

        close(client);

        int status;
        pid_t dead = waitpid(pid, &status, WNOHANG);
        if (dead < 0) err(EX_OSERR, "waitpid(%d)", pid);
        if (dead) return WIFEXITED(status) ? WEXITSTATUS(status) : EX_SOFTWARE;
    }
}

static struct termios saveTerm;
static void restoreTerm(void) {
    tcsetattr(STDERR_FILENO, TCSADRAIN, &saveTerm);
    fprintf(
        stderr,
        "\x1b[?1049l" // rmcup
        "\x1b\x63\x1b[!p\x1b[?3;4l\x1b[4l\x1b>" // reset
    );
}

static int atch(int argc, char *argv[]) {
    int error;

    const struct passwd *user = getUser();
    const char *name = (argc > 1) ? argv[1] : user->pw_name;

    int client = socket(PF_LOCAL, SOCK_STREAM, 0);
    if (client < 0) err(EX_OSERR, "socket");

    struct sockaddr_un addr = sockAddr(user->pw_dir, name);
    error = connect(client, (struct sockaddr *)&addr, sizeof(addr));
    if (error) err(EX_NOINPUT, "%s", addr.sun_path);

    int pty = recvFd(client);
    if (pty < 0) err(EX_IOERR, "recvmsg(%d)", client);

    struct winsize window;
    error = ioctl(STDERR_FILENO, TIOCGWINSZ, &window);
    if (error) err(EX_IOERR, "ioctl(%d, TIOCGWINSZ)", STDERR_FILENO);

    error = ioctl(pty, TIOCSWINSZ, &window);
    if (error) err(EX_IOERR, "ioctl(%d, TIOCSWINSZ)", pty);

    error = tcgetattr(STDERR_FILENO, &saveTerm);
    if (error) err(EX_IOERR, "tcgetattr(%d)", STDERR_FILENO);
    atexit(restoreTerm);

    struct termios raw;
    cfmakeraw(&raw);
    error = tcsetattr(STDERR_FILENO, TCSADRAIN, &raw);
    if (error) err(EX_IOERR, "tcsetattr(%d)", STDERR_FILENO);

    char c = CTRL('L');
    ssize_t size = write(pty, &c, 1);
    if (size < 0) err(EX_IOERR, "write(%d)", pty);

    char buf[4096];
    struct pollfd fds[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = pty, .events = POLLIN },
    };
    while (0 < poll(fds, 2, -1)) {
        if (fds[0].revents == POLLIN) {
            ssize_t readSize = read(STDIN_FILENO, buf, sizeof(buf));
            if (readSize < 0) err(EX_IOERR, "read(%d)", STDIN_FILENO);

            if (readSize == 1 && buf[0] == CTRL('Q')) return EX_OK;

            ssize_t writeSize = write(pty, buf, readSize);
            if (writeSize < 0) err(EX_IOERR, "write(%d)", pty);
            if (writeSize < readSize) errx(EX_IOERR, "short write(%d)", pty);
        }

        if (fds[1].revents == POLLIN) {
            ssize_t readSize = read(pty, buf, sizeof(buf));
            if (readSize < 0) err(EX_IOERR, "read(%d)", pty);

            ssize_t writeSize = write(STDOUT_FILENO, buf, readSize);
            if (writeSize < 0) err(EX_IOERR, "write(%d)", STDOUT_FILENO);
            if (writeSize < readSize) {
                errx(EX_IOERR, "short write(%d)", STDOUT_FILENO);
            }
        }
    }
    err(EX_IOERR, "poll");
}

int main(int argc, char *argv[]) {
    switch (argv[0][0]) {
        case 'd': return dtch(argc, argv);
        case 'a': return atch(argc, argv);
        default:  return EX_USAGE;
    }
}
