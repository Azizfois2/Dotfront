#ifndef NET_H
#define NET_H

#include "game.h"
#include <stddef.h>
#include <stdint.h>
#define NET_INVALID_FD ((intptr_t)-1)

#define NET_DEFAULT_PORT 47777
#define NET_MAX_CLIENTS  (MAX_PLAYERS - 1)
#define NET_CMD_POINTS   64
#define NET_PROTO_VERSION 1u

enum {
    NET_MSG_WELCOME = 1,
    NET_MSG_SNAPSHOT = 2,
    NET_MSG_COMMAND = 3
};

enum {
    NET_CMD_MOVE = 1,
    NET_CMD_ATTACK_MOVE,
    NET_CMD_HOLD,
    NET_CMD_STOP,
    NET_CMD_RALLY,
    NET_CMD_PRODUCE,
    NET_CMD_FORTIFY
};

typedef struct {
    uint8_t  type;
    uint8_t  owner;       /* filled in by the host, never trusted from a client */
    uint8_t  city;
    uint8_t  unit_type;
    uint8_t  queue;
    uint8_t  pad[3];
    uint16_t nids;
    uint16_t npts;
    uint8_t  ids[MAX_UNITS];
    Vec2     pts[NET_CMD_POINTS];
} NetCmd;

typedef struct {
    intptr_t fd;
    int slot;
    unsigned char *tx;
    size_t txlen, txoff;
    unsigned char *rx;
    size_t rxlen, rxcap;
    unsigned char *out;
    size_t outlen, outoff, outcap;
} NetClient;

typedef struct {
    int is_host;
    int is_client;
    int connected;
    int started;
    int spectating;
    intptr_t listen_fd;
    intptr_t server_fd;
    int port;
    int local_slot;
    int nclients;
    uint32_t seq;
    double last_snapshot;
    NetClient clients[NET_MAX_CLIENTS];
    Game snapshot;
    int have_snapshot;
    unsigned char *rx;
    size_t rxlen, rxcap;
    unsigned char *out;
    size_t outlen, outoff, outcap;
    char status[96];
    char error[96];
} Net;

/* Returns 1 on success, 0 on failure (see net->error). */
int  net_host(Net *n, int port);
int  net_join(Net *n, const char *addr, int port);
void net_close(Net *n);

/* Call every frame. Host accepts/reads commands and broadcasts snapshots.
 * Client applies received snapshots to g. */
void net_host_update(Net *n, Game *g, double now);
void net_client_update(Net *n, Game *g);

/* Send one command from a client to the host. */
void net_send_command(Net *n, const NetCmd *cmd);

/* Mark every slot not occupied by the host or a client as AI-controlled. */
void net_host_autofill(Net *n, Game *g);
int  net_host_has_clients(const Net *n);

#endif /* NET_H */
