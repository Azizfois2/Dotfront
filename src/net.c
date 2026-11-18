#define _POSIX_C_SOURCE 200809L
#include "net.h"
#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET net_sock;
#define NET_INVALID INVALID_SOCKET
#define net_close_sock closesocket
#define net_would_block() (WSAGetLastError() == WSAEWOULDBLOCK)
static int net_last_error(void) { return WSAGetLastError(); }
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <signal.h>
typedef int net_sock;
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#define NET_INVALID (-1)
#define net_close_sock close
#define net_would_block() (errno == EAGAIN || errno == EWOULDBLOCK)
static int net_last_error(void) { return errno; }
#endif

typedef struct {
    uint32_t magic;
    uint16_t type;
    uint16_t version;
    uint32_t len;
    uint32_t seq;
} NetHeader;

typedef struct {
    uint8_t slot;
    uint8_t players;
    uint8_t started;
    uint8_t reserved;
} NetWelcome;

#define NET_MAGIC 0x574F444Eu /* WODN */
#define NET_HDR_SIZE ((uint32_t)sizeof(NetHeader))

static void net_error(Net *n, const char *what) {
    snprintf(n->error, sizeof(n->error), "%s (errno %d)", what, net_last_error());
}

static int net_init_sockets(void) {
#ifdef _WIN32
    static int started = 0;
    if (started) return 1;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
    started = 1;
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    return 1;
}

static int net_set_nonblock(net_sock s) {
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
#else
    int fl = fcntl(s, F_GETFL, 0);
    return fl >= 0 && fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0;
#endif
}

static void net_set_nodelay(net_sock s) {
    int on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
}

static int net_send_all(net_sock s, const void *buf, size_t len) {
    const unsigned char *p = (const unsigned char *)buf;
    size_t off = 0;
    while (off < len) {
#ifdef _WIN32
        int k = send(s, (const char *)p + off, (int)(len - off), 0);
#else
        ssize_t k = send(s, p + off, len - off, MSG_NOSIGNAL);
#endif
        if (k <= 0) return 0;
        off += (size_t)k;
    }
    return 1;
}

static void client_free_buffers(NetClient *c) {
    free(c->tx);
    free(c->rx);
    c->tx = c->rx = NULL;
    c->txlen = c->txoff = c->rxlen = c->rxcap = 0;
}

static void net_drop_client(Net *n, int idx, Game *g) {
    NetClient *c = &n->clients[idx];
    int slot = c->slot;
    if (c->fd != NET_INVALID_FD) net_close_sock(c->fd);
    client_free_buffers(c);
    c->fd = NET_INVALID_FD;
    c->slot = -1;
    n->nclients--;
    if (g && slot >= 0 && slot < g->players) {
        g->ai_enabled[slot] = 1;
        if (n->started) game_notice(g, "CLIENT LEFT - AI TAKES OVER");
    }
}

static int net_slot_used(const Net *n, int slot) {
    for (int i = 0; i < NET_MAX_CLIENTS; i++)
        if (n->clients[i].fd != NET_INVALID_FD && n->clients[i].slot == slot)
            return 1;
    return 0;
}

static int net_free_slot(Net *n, const Game *g) {
    for (int p = 1; p < g->players; p++)
        if (!net_slot_used(n, p)) return p;
    return -1;
}

static void net_update_status(Net *n, const Game *g) {
    if (n->is_host) {
        if (!n->started)
            snprintf(n->status, sizeof(n->status),
                     "LAN HOST PORT %d - %d/%d CLIENTS - SPACE STARTS",
                     n->port, n->nclients, g->players - 1);
        else
            snprintf(n->status, sizeof(n->status),
                     "LAN HOST PORT %d - %d CLIENT%s",
                     n->port, n->nclients, n->nclients == 1 ? "" : "S");
    } else if (n->is_client) {
        if (!n->connected)
            snprintf(n->status, sizeof(n->status), "LAN DISCONNECTED");
        else if (n->local_slot >= 0)
            snprintf(n->status, sizeof(n->status), "LAN %s - %s",
                     owner_name(n->local_slot),
                     n->started ? "IN MATCH" : "WAITING FOR HOST");
        else
            snprintf(n->status, sizeof(n->status), "LAN CONNECTED - JOINING");
    }
}

int net_host(Net *n, int port) {
    memset(n, 0, sizeof(*n));
    n->is_host = 1;
    n->local_slot = 0;
    n->listen_fd = NET_INVALID_FD;
    n->server_fd = NET_INVALID_FD;
    for (int i = 0; i < NET_MAX_CLIENTS; i++) {
        n->clients[i].fd = NET_INVALID;
        n->clients[i].slot = -1;
    }
    if (!net_init_sockets()) {
        snprintf(n->error, sizeof(n->error), "socket startup failed");
        return 0;
    }
    net_sock s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == NET_INVALID) { net_error(n, "socket"); return 0; }
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((unsigned short)port);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0) {
        net_error(n, "bind");
        net_close_sock(s);
        return 0;
    }
    if (listen(s, NET_MAX_CLIENTS) != 0) {
        net_error(n, "listen");
        net_close_sock(s);
        return 0;
    }
    if (!net_set_nonblock(s)) {
        net_error(n, "nonblocking listen");
        net_close_sock(s);
        return 0;
    }
    n->listen_fd = (intptr_t)s;
    n->port = port;
    n->outcap = 65536;
    n->out = (unsigned char *)malloc(n->outcap);
    snprintf(n->status, sizeof(n->status), "LAN HOST PORT %d", port);
    return 1;
}

static void host_accept_clients(Net *n, Game *g) {
    for (;;) {
        net_sock s = accept((net_sock)n->listen_fd, NULL, NULL);
        if (s == NET_INVALID) break;
        int slot = n->started ? -1 : net_free_slot(n, g);
        if (slot < 0) {
            net_close_sock(s);
            continue;
        }
        int idx = -1;
        for (int i = 0; i < NET_MAX_CLIENTS; i++)
            if (n->clients[i].fd == NET_INVALID_FD) { idx = i; break; }
        if (idx < 0) { net_close_sock(s); continue; }

        net_set_nodelay(s);
        NetHeader h;
        NetWelcome w;
        memset(&w, 0, sizeof(w));
        w.slot = (uint8_t)slot;
        w.players = (uint8_t)g->players;
        w.started = (uint8_t)n->started;
        h.magic = NET_MAGIC;
        h.type = NET_MSG_WELCOME;
        h.version = NET_PROTO_VERSION;
        h.len = NET_HDR_SIZE + (uint32_t)sizeof(w);
        h.seq = 0;
        if (!net_send_all(s, &h, sizeof(h)) ||
            !net_send_all(s, &w, sizeof(w))) {
            net_close_sock(s);
            continue;
        }
        net_set_nonblock(s);
        NetClient *c = &n->clients[idx];
        memset(c, 0, sizeof(*c));
        c->fd = (intptr_t)s;
        c->slot = slot;
        c->tx = (unsigned char *)malloc(NET_HDR_SIZE + sizeof(Game));
        c->rx = (unsigned char *)malloc(8192);
        if (!c->tx || !c->rx) {
            net_close_sock(s);
            c->fd = NET_INVALID_FD;
            client_free_buffers(c);
            continue;
        }
        c->txlen = c->txoff = c->rxlen = 0;
        c->rxcap = 8192;
        n->nclients++;
        g->ai_enabled[slot] = 0;
        if (n->started) game_notice(g, "CLIENT JOINED");
        printf("LAN: client joined as %s\n", owner_name(slot));
    }
}

static void host_read_client(Net *n, int idx, Game *g) {
    NetClient *c = &n->clients[idx];
    unsigned char buf[4096];
    for (;;) {
#ifdef _WIN32
        int k = recv((net_sock)c->fd, (char *)buf, sizeof(buf), 0);
#else
        ssize_t k = recv(c->fd, buf, sizeof(buf), 0);
#endif
        if (k == 0) { net_drop_client(n, idx, g); return; }
        if (k < 0) {
            if (net_would_block()) return;
            net_drop_client(n, idx, g);
            return;
        }
        if (c->rxlen + (size_t)k > (size_t)c->rxcap) {
            if (c->rxcap > 65536) { net_drop_client(n, idx, g); return; }
            size_t cap = c->rxcap * 2;
            unsigned char *p = (unsigned char *)realloc(c->rx, cap);
            if (!p) { net_drop_client(n, idx, g); return; }
            c->rx = p;
            c->rxcap = cap;
        }
        memcpy(c->rx + c->rxlen, buf, (size_t)k);
        c->rxlen += (size_t)k;
    }
}

static void host_process_client_commands(Net *n, int idx, Game *g) {
    NetClient *c = &n->clients[idx];
    while (c->rxlen >= (size_t)NET_HDR_SIZE) {
        NetHeader h;
        memcpy(&h, c->rx, sizeof(h));
        if (h.magic != NET_MAGIC || h.version != NET_PROTO_VERSION ||
            h.len < NET_HDR_SIZE || h.len > 65536) {
            net_drop_client(n, idx, g);
            return;
        }
        if (c->rxlen < (size_t)h.len) return;
        if (h.type == NET_MSG_COMMAND &&
            h.len == NET_HDR_SIZE + sizeof(NetCmd)) {
            NetCmd cmd;
            memcpy(&cmd, c->rx + NET_HDR_SIZE, sizeof(cmd));
            cmd.owner = (uint8_t)c->slot;
            if (cmd.nids <= MAX_UNITS && cmd.npts <= NET_CMD_POINTS) {
                switch (cmd.type) {
                case NET_CMD_MOVE:
                    game_remote_order(g, (int)cmd.owner, cmd.ids, cmd.nids,
                                      cmd.pts, cmd.npts);
                    break;
                case NET_CMD_ATTACK_MOVE:
                    game_remote_attack_move(g, (int)cmd.owner, cmd.ids,
                                            cmd.nids, cmd.pts[0]);
                    break;
                case NET_CMD_HOLD:
                    game_remote_hold(g, (int)cmd.owner, cmd.ids, cmd.nids);
                    break;
                case NET_CMD_STOP:
                    game_remote_stop(g, (int)cmd.owner, cmd.ids, cmd.nids);
                    break;
                case NET_CMD_RALLY:
                    game_remote_city(g, (int)cmd.owner, cmd.city,
                                     GAME_CITY_RALLY, 0, cmd.pts[0]);
                    break;
                case NET_CMD_PRODUCE:
                    game_remote_city(g, (int)cmd.owner, cmd.city,
                                     cmd.queue ? GAME_CITY_QUEUE
                                               : GAME_CITY_PRODUCE,
                                     (int)cmd.unit_type, v2(0.0f, 0.0f));
                    break;
                case NET_CMD_FORTIFY:
                    game_remote_city(g, (int)cmd.owner, cmd.city,
                                     GAME_CITY_FORTIFY, 0, v2(0.0f, 0.0f));
                    break;
                default:
                    break;
                }
            }
        }
        size_t used = h.len;
        memmove(c->rx, c->rx + used, c->rxlen - used);
        c->rxlen -= used;
    }
}

static void host_send_pending(Net *n, Game *g) {
    for (int i = 0; i < NET_MAX_CLIENTS; i++) {
        NetClient *c = &n->clients[i];
        if (c->fd == NET_INVALID_FD) continue;
        while (c->txoff < c->txlen) {
#ifdef _WIN32
            int k = send((net_sock)c->fd, (const char *)c->tx + c->txoff,
                         (int)(c->txlen - c->txoff), 0);
#else
            ssize_t k = send(c->fd, c->tx + c->txoff, c->txlen - c->txoff,
                             MSG_NOSIGNAL);
#endif
            if (k <= 0) {
                if (k < 0 && net_would_block()) break;
                net_drop_client(n, i, g);
                break;
            }
            c->txoff += (size_t)k;
        }
    }
}

static void host_queue_snapshots(Net *n, const Game *g) {
    n->seq++;
    for (int i = 0; i < NET_MAX_CLIENTS; i++) {
        NetClient *c = &n->clients[i];
        if (c->fd == NET_INVALID_FD || !c->tx) continue;
        if (c->txoff < c->txlen) continue; /* still sending the previous state */
        NetHeader h;
        h.magic = NET_MAGIC;
        h.type = NET_MSG_SNAPSHOT;
        h.version = NET_PROTO_VERSION;
        h.len = NET_HDR_SIZE + (uint32_t)sizeof(Game);
        h.seq = n->seq;
        memcpy(c->tx, &h, sizeof(h));
        memcpy(c->tx + NET_HDR_SIZE, g, sizeof(Game));
        c->txoff = 0;
        c->txlen = h.len;
    }
}

void net_host_update(Net *n, Game *g, double now) {
    if (!n->is_host) return;
    host_accept_clients(n, g);
    for (int i = 0; i < NET_MAX_CLIENTS; i++) {
        if (n->clients[i].fd == NET_INVALID_FD) continue;
        host_read_client(n, i, g);
        if (n->clients[i].fd == NET_INVALID_FD) continue;
        host_process_client_commands(n, i, g);
    }
    host_send_pending(n, g);
    double interval = n->started ? 0.10 : 0.25;
    if (now - n->last_snapshot >= interval) {
        n->last_snapshot = now;
        host_queue_snapshots(n, g);
        host_send_pending(n, g);
    }
    net_update_status(n, g);
}

static void client_flush_commands(Net *n) {
    if (!n->out || n->server_fd == NET_INVALID_FD) return;
    while (n->outoff < n->outlen) {
#ifdef _WIN32
        int k = send((net_sock)n->server_fd,
                     (const char *)n->out + n->outoff,
                     (int)(n->outlen - n->outoff), 0);
#else
        ssize_t k = send(n->server_fd, n->out + n->outoff,
                         n->outlen - n->outoff, MSG_NOSIGNAL);
#endif
        if (k <= 0) {
            if (k < 0 && net_would_block()) return;
            n->connected = 0;
            snprintf(n->error, sizeof(n->error), "connection lost");
            fprintf(stderr, "LAN client: %s\n", n->error);
            return;
        }
        n->outoff += (size_t)k;
    }
    n->outoff = n->outlen = 0;
}

void net_send_command(Net *n, const NetCmd *cmd) {
    if (!n->is_client || !n->connected || n->server_fd == NET_INVALID_FD) return;
    if (!n->out) {
        n->outcap = 65536;
        n->out = (unsigned char *)malloc(n->outcap);
        if (!n->out) return;
    }
    if (n->outoff > 0) {
        if (n->outoff >= n->outlen) {
            n->outoff = n->outlen = 0;
        } else {
            memmove(n->out, n->out + n->outoff, n->outlen - n->outoff);
            n->outlen -= n->outoff;
            n->outoff = 0;
        }
    }
    size_t need = NET_HDR_SIZE + sizeof(NetCmd);
    if (n->outlen + need > n->outcap) return; /* drop rather than desync */
    NetHeader h;
    h.magic = NET_MAGIC;
    h.type = NET_MSG_COMMAND;
    h.version = NET_PROTO_VERSION;
    h.len = (uint32_t)need;
    h.seq = 0;
    memcpy(n->out + n->outlen, &h, sizeof(h));
    memcpy(n->out + n->outlen + NET_HDR_SIZE, cmd, sizeof(*cmd));
    n->outlen += need;
    client_flush_commands(n);
}

void net_host_autofill(Net *n, Game *g) {
    int occupied[MAX_PLAYERS] = { 0 };
    occupied[0] = 1;
    for (int i = 0; i < NET_MAX_CLIENTS; i++)
        if (n->clients[i].fd != NET_INVALID_FD && n->clients[i].slot >= 0)
            occupied[n->clients[i].slot] = 1;
    for (int p = 0; p < g->players; p++)
        g->ai_enabled[p] = occupied[p] ? 0 : 1;
}

int net_host_has_clients(const Net *n) {
    return n->nclients > 0;
}

static void client_parse(Net *n, Game *g);

static void client_apply_snapshot(Net *n, Game *g) {
    uint8_t sel[MAX_UNITS];
    int sel_city = g->sel_city;
    int sound_on = g->sound_on;
    char notice[sizeof(g->notice)];
    float notice_t = g->notice_t;
    memcpy(notice, g->notice, sizeof(notice));
    memcpy(sel, g->selected, sizeof(sel));

    *g = n->snapshot;
    if (g->menu && notice_t > 0.0f) {
        memcpy(g->notice, notice, sizeof(notice));
        g->notice_t = notice_t;
    }
    g->local = n->local_slot >= 0 ? n->local_slot : 0;
    g->sfx = 0;
    g->sound_on = sound_on;
    g->save_available = 0;
    if (g->local >= 0 && g->local < g->players)
        g->ai_enabled[g->local] = 0;

    memset(g->selected, 0, sizeof(g->selected));
    g->nsel = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!sel[i] || !g->units[i].alive) continue;
        if (g->units[i].owner != g->local) continue;
        g->selected[i] = 1;
        g->nsel++;
    }
    g->sel_city = (sel_city >= 0 && sel_city < g->ncity) ? sel_city : -1;

    if (n->spectating && !g->menu && g->winner < 0) {
        g->spectator = 1;
        g->eliminated = 0;
    } else {
        g->spectator = 0;
        if (!g->menu && g->winner < 0 && g->local >= 0)
            g->eliminated = (g->cities_owned[g->local] == 0 &&
                             g->units_owned[g->local] == 0) ? 1 : 0;
        else
            g->eliminated = 0;
    }
    n->started = g->menu ? 0 : 1;
}

static void client_read(Net *n, Game *g) {
    unsigned char buf[8192];
    for (;;) {
#ifdef _WIN32
        int k = recv((net_sock)n->server_fd, (char *)buf, sizeof(buf), 0);
#else
        ssize_t k = recv(n->server_fd, buf, sizeof(buf), 0);
#endif
        if (k == 0) {
            n->connected = 0;
            snprintf(n->error, sizeof(n->error), "host closed the connection");
            fprintf(stderr, "LAN client: %s\n", n->error);
            return;
        }
        if (k < 0) {
            if (net_would_block()) return;
            n->connected = 0;
            net_error(n, "recv");
            fprintf(stderr, "LAN client: %s\n", n->error);
            return;
        }
        size_t need = n->rxlen + (size_t)k;
        if (need > 1048576) {
            n->rxlen = 0;
            snprintf(n->error, sizeof(n->error), "oversized packet");
            n->connected = 0;
            fprintf(stderr, "LAN client: oversized packet (%zu)\n", need);
            return;
        }
        if (need > (size_t)n->rxcap) {
            size_t cap = n->rxcap ? n->rxcap : 65536;
            while (cap < need) cap *= 2;
            unsigned char *p = (unsigned char *)realloc(n->rx, cap);
            if (!p) { n->connected = 0; fprintf(stderr, "LAN client: rx alloc failed\n"); return; }
            n->rx = p;
            n->rxcap = cap;
        }
        memcpy(n->rx + n->rxlen, buf, (size_t)k);
        n->rxlen = need;
        client_parse(n, g);
        if (!n->connected) return;
    }
}

static void client_parse(Net *n, Game *g) {
    while (n->rxlen >= (size_t)NET_HDR_SIZE) {
        NetHeader h;
        memcpy(&h, n->rx, sizeof(h));
        if (h.magic != NET_MAGIC || h.version != NET_PROTO_VERSION ||
            h.len < NET_HDR_SIZE || h.len > 1048576) {
            snprintf(n->error, sizeof(n->error), "protocol mismatch");
            n->connected = 0;
            fprintf(stderr, "LAN client: %s (magic=%08x type=%u ver=%u len=%u)\n",
                    n->error, h.magic, h.type, h.version, h.len);
            return;
        }
        if (n->rxlen < (size_t)h.len) return;
        if (h.type == NET_MSG_WELCOME &&
            h.len == NET_HDR_SIZE + sizeof(NetWelcome)) {
            NetWelcome w;
            memcpy(&w, n->rx + NET_HDR_SIZE, sizeof(w));
            if (w.slot < MAX_PLAYERS && w.players >= 2 &&
                w.players <= MAX_PLAYERS) {
                n->local_slot = w.slot;
                n->started = w.started;
            }
        } else if (h.type == NET_MSG_SNAPSHOT &&
                   h.len == NET_HDR_SIZE + sizeof(Game)) {
            memcpy(&n->snapshot, n->rx + NET_HDR_SIZE, sizeof(Game));
            n->have_snapshot = 1;
            client_apply_snapshot(n, g);
        }
        size_t used = h.len;
        memmove(n->rx, n->rx + used, n->rxlen - used);
        n->rxlen -= used;
    }
}

void net_client_update(Net *n, Game *g) {
    if (!n->is_client || !n->connected) {
        net_update_status(n, g);
        return;
    }
    client_flush_commands(n);
    client_read(n, g);
    if (n->connected) client_parse(n, g);
    net_update_status(n, g);
}

int net_join(Net *n, const char *addr, int port) {
    memset(n, 0, sizeof(*n));
    n->is_client = 1;
    n->local_slot = -1;
    n->server_fd = NET_INVALID_FD;
    n->listen_fd = NET_INVALID_FD;
    for (int i = 0; i < NET_MAX_CLIENTS; i++) {
        n->clients[i].fd = NET_INVALID;
        n->clients[i].slot = -1;
    }
    if (!net_init_sockets()) {
        snprintf(n->error, sizeof(n->error), "socket startup failed");
        return 0;
    }
    char host[160];
    snprintf(host, sizeof(host), "%s", addr ? addr : "127.0.0.1");
    char *colon = strrchr(host, ':');
    if (colon && strchr(host, ':') == colon) {
        *colon = '\0';
        int p = atoi(colon + 1);
        if (p > 0 && p < 65536) port = p;
    }
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        snprintf(n->error, sizeof(n->error), "cannot resolve %.70s", host);
        return 0;
    }
    net_sock s = NET_INVALID;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == NET_INVALID) continue;
        if (!net_set_nonblock(s)) { net_close_sock(s); s = NET_INVALID; continue; }
        int r = connect(s, ai->ai_addr, (int)ai->ai_addrlen);
        if (r != 0) {
            int err = net_last_error();
#ifdef _WIN32
            int pending = (err == WSAEWOULDBLOCK);
#else
            int pending = (err == EINPROGRESS);
#endif
            if (pending) {
                fd_set wr;
                FD_ZERO(&wr);
                FD_SET(s, &wr);
                struct timeval tv = { 4, 0 };
                int sel = select((int)s + 1, NULL, &wr, NULL, &tv);
                if (sel > 0) {
                    int soerr = 0;
                    socklen_t sl = sizeof(soerr);
                    getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&soerr, &sl);
                    if (soerr == 0) r = 0;
                }
            }
        }
        if (r == 0) break;
        net_close_sock(s);
        s = NET_INVALID;
    }
    freeaddrinfo(res);
    if (s == NET_INVALID) {
        snprintf(n->error, sizeof(n->error), "cannot connect to %.70s:%d", host, port);
        return 0;
    }
    net_set_nodelay(s);
    n->server_fd = (intptr_t)s;
    n->connected = 1;
    n->port = port;
    n->outcap = 65536;
    n->out = (unsigned char *)malloc(n->outcap);
    snprintf(n->status, sizeof(n->status), "LAN CONNECTED TO %.70s:%d", host, port);
    return 1;
}

void net_close(Net *n) {
    if (n->listen_fd != NET_INVALID_FD) net_close_sock((net_sock)n->listen_fd);
    if (n->server_fd != NET_INVALID_FD) net_close_sock((net_sock)n->server_fd);
    for (int i = 0; i < NET_MAX_CLIENTS; i++) {
        if (n->clients[i].fd != NET_INVALID_FD) net_close_sock((net_sock)n->clients[i].fd);
        client_free_buffers(&n->clients[i]);
    }
    free(n->rx);
    n->rx = NULL;
    free(n->out);
    n->out = NULL;
    n->listen_fd = n->server_fd = NET_INVALID_FD;
    n->connected = 0;
}
