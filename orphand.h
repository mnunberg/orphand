#ifndef ORPHAND_H_
#define ORPHAND_H_

#include <stdint.h>

typedef struct {
    uint32_t parent;
    uint32_t child;
    uint16_t action;
} orphand_message;

enum {
    ORPHAND_ACTION_REGISTER = 1,
    ORPHAND_ACTION_UNREGISTER = 2
};

#endif /* ORPHAND_H_ */
