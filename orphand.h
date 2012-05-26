#ifndef ORPHAND_H_
#define ORPHAND_H_

#include <stdint.h>

struct hashtable;

#define DEFAULT_SIGNAL SIGINT
#define DEFAULT_PATH "/tmp/orphand.sock"
#define DEFAULT_SWEEP_INTERVAL 2

typedef struct {
    int sock;
    int sweep_interval;
    int default_signum;
    struct hashtable *ht;
} orphand_server;

typedef struct {
    uint32_t parent;
    uint32_t child;
    uint16_t action;
} orphand_message;

enum {
    ORPHAND_ACTION_REGISTER     = 0x1,
    ORPHAND_ACTION_UNREGISTER   = 0x2,
    ORPHAND_ACTION_PING         = 0x3,
};

#endif /* ORPHAND_H_ */
