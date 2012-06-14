#include "orphand_priv.h"
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

int
orphand_io_init(orphand_server *srv,
                const char *path)
{
    int status,
        sock = socket(AF_UNIX, SOCK_STREAM, 0);

    struct sockaddr_un uaddr;

    if (sock == -1) {
        perror("socket");
        return -1;
    }

    memset(&uaddr, 0, sizeof(uaddr));
    uaddr.sun_family = AF_UNIX;
    strcpy(uaddr.sun_path, path);

    unlink(path);
    status = bind(sock, (struct sockaddr*)&uaddr, sizeof(uaddr));
    if (status == -1) {
        perror("bind");
        close(sock);
        return -1;
    }

    status = listen(sock, 128);
    if (status == -1) {
        perror("listen");
        close(sock);
        return -1;
    }
    srv->sock = sock;

    FD_ZERO(&srv->fds_rd);
    FD_ZERO(&srv->fds_wr);
    FD_SET(srv->sock, &srv->fds_rd);
    srv->nsock = 1;
    srv->maxfd = -1;

    srv->clients = embht_make(1023, 0);

    return sock;
}

static int
do_sockio(orphand_server *srv,
          orphand_client *cli,
          int events)
{
    int ret = SOCKEV_RD;
    DEBUG("Got events 0x%x", events);

    if (events & SOCKEV_WR) {
        size_t wtotal;
        ssize_t nw;
        struct orphand_buffer *ob = &cli->sndbuf;
        wtotal = 0;

        while (wtotal < ob->used) {
            nw = send(cli->sockfd,
                      ob->buf + wtotal,
                      ob->used - wtotal,
                      MSG_DONTWAIT);

            if (nw > 0) {
                wtotal += nw;
                continue;
            }

            if (nw == -1) {
                if (errno == EWOULDBLOCK) {
                    goto GT_MOVE;
                } else if (errno == EINTR) {
                    continue;
                } else {
                    ERROR("fd=%d send: %s",
                          cli->sockfd,
                          strerror(errno));
                    ret |= SOCKEV_ER;
                }
            } else {
                ret |= SOCKEV_ER;
                INFO("Socket %d closed the connection",
                     cli->sockfd);
            }
            break;
        }

        GT_MOVE:
        if (wtotal) {
            ob->used -= wtotal;
            memmove(ob->buf, ob->buf + wtotal, ob->used);
        }
    }

    if (events & SOCKEV_RD) {
        struct orphand_buffer *ob = &cli->rcvbuf;
        size_t left = ob->total - ob->used;
        ssize_t nr;
        uint32_t *bufp = (uint32_t*)ob->buf;

        while (left) {

            nr = recv(cli->sockfd,
                      ob->buf + ob->used,
                      left,
                      MSG_DONTWAIT);

            if (nr > 0) {
                left -= nr;
                ob->used += nr;
                continue;
            }

            if (nr == -1) {
                if (errno == EWOULDBLOCK) {
                    break; /* meh */
                } else if (errno == EINTR) {
                    continue;
                } else {
                    ERROR("fd=%d recv: %s",
                          cli->sockfd,
                          strerror(errno));
                    ret |= SOCKEV_ER;
                }
            } else {
                ret |= SOCKEV_ER;
                DEBUG("Socket %d closed the connection",
                     cli->sockfd);
            }
            break;
        }

        nr = 0;

        while (ob->used >= 12) {
            orphand_message msg;
            msg.parent = bufp[0];
            msg.child = bufp[1];
            msg.action = bufp[2];

            bufp += 3;
            ob->used -= 12;
            nr += 12;

            orphand_process_message(srv, cli, &msg);
        }

        if (nr && ob->used) {
            memmove(ob->buf, ob->buf + nr, ob->used);
        }
    }

    if (cli->sndbuf.used) {
        DEBUG("Socket %d still has %d bytes of data to be written..",
              cli->sockfd,
              cli->sndbuf.used);
        ret |= SOCKEV_WR;
    }
    return ret;
}


#define MAX(a,b) ((a > b) ? a : b)

void
orphand_io_iteronce(orphand_server *srv)
{
    embht_iterator iter;
    fd_set fout_rd, fout_wr;
    int nevents;

    fout_rd = srv->fds_rd;
    fout_wr = srv->fds_wr;

    if (srv->maxfd == -1) {
        srv->maxfd = srv->sock;
        embht_iterinit(srv->clients, &iter);
        while (embht_iternext(&iter)) {
            struct orphand_client *cli = embht_iterval(&iter).ptr;
            assert(cli);
            srv->maxfd = MAX(srv->maxfd, cli->sockfd);
        }
    }

    GT_SELECT:
    nevents = select(srv->maxfd+1,
                     &fout_rd,
                     &fout_wr,
                     NULL,
                     &srv->tmo);

    if (nevents < 1) {
        if (nevents == -1) {
            if (errno == EINTR) {
                goto GT_SELECT;
            } else {
                ERROR("select: %s", strerror(errno));
            }
        }
        return;
    }

    embht_iterinit(srv->clients, &iter);
    while (embht_iternext(&iter) && nevents) {

        struct orphand_client *cli = embht_iterval(&iter).ptr;
        int cbevents = 0;
        assert(cli);
        DEBUG("Checking fd %d for events", cli->sockfd);

        if (FD_ISSET(cli->sockfd, &fout_rd)) {
            nevents--;
            cbevents |= SOCKEV_RD;
        }
        if (FD_ISSET(cli->sockfd, &fout_wr)) {
            nevents--;
            cbevents |= SOCKEV_WR;
        }

        if (!cbevents) {
            continue;
        }

        cbevents = do_sockio(srv, cli, cbevents);

        if (cbevents & SOCKEV_ER) {

            FD_CLR(cli->sockfd, &srv->fds_wr);
            FD_CLR(cli->sockfd, &srv->fds_rd);

            if (cli->sockfd == srv->maxfd) {
                srv->maxfd = -1;
            }

            srv->nsock--;

            close(cli->sockfd);
            embht_iterdel(&iter);
            free(cli);

            continue;

        }

        if (cbevents & SOCKEV_WR) {
            FD_SET(cli->sockfd, &srv->fds_wr);
        } else {
            FD_CLR(cli->sockfd, &srv->fds_wr);
        }

        assert(cbevents & SOCKEV_RD);
    }

    if (nevents) {

        int newsock;
        struct orphand_client *newcli;
        embht_entry *newent;

        assert(nevents == 1);
        assert(FD_ISSET(srv->sock, &fout_rd));

        newsock = accept(srv->sock, NULL, 0);
        if (newsock == -1) {
            ERROR("accept: %s", strerror(errno));
            return;
        }

        newcli = calloc(1, sizeof(*newcli));
        newcli->rcvbuf.total = sizeof(newcli->rcvbuf.buf);
        newcli->sndbuf.total = sizeof(newcli->sndbuf.buf);

        newent = embht_fetchi(srv->clients, newsock, 1);
        assert(newent);
        newcli->sockfd = newsock;
        newent->u_value.ptr = newcli;
        FD_SET(newsock, &srv->fds_rd);
        srv->maxfd = -1;
        srv->nsock++;
        nevents--;
    }
    assert(nevents == 0);
}
