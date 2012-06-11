#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include "procstat.h"

int
procstat(pid_t pid, struct procstat *pstb)
{
    FILE *fp;
    char path[PATH_MAX];
    char *buf;

    char *fields[128] = { NULL };
    int ii, ret = 0;

    int nbuf, nfields, bufmax;

    bufmax = sizeof(pstb->buf);
    buf = pstb->buf;

    pstb->sys_error = 0;
    pstb->lib_error = 0;


    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    fp = fopen(path, "r");
    if (!fp) {
        ret = -1;
        pstb->sys_error = errno;
        goto GT_RET;
    }

    if ( (nbuf = fread(buf, 1, bufmax, fp)) == 0) {
        pstb->lib_error = PROCSTAT_EEMPTY;
        ret = -1;
        goto GT_RET;
    }

    buf[nbuf-1] = '\0';

    if (!feof(fp)) {
        pstb->lib_error = PROCSTAT_ETOOBIG;
        ret = -1;
        goto GT_RET;
    }

    nfields = 0;
    for (ii = nbuf; ii >= 0; ii--) {
        if (buf[ii] == ' ') {
            buf[ii] = '\0';
            fields[nfields++] = buf + ii + 1;
        } else if (buf[ii] == ')') {
            break;
        }
    }

    /**
     * Now, let's parse in the last fields, but doing it from the beginning.
     */

    fields[nfields + 1] = buf;
    for (ii = 0; ii < nbuf; ii++) {
        if (buf[ii] == ' ') {
            buf[ii] = '\0';
            fields[nfields] = buf + ii + 1;
            break;
        }
    }
    nfields++;

    #define X(idx, cnst, fld, fmt, t) \
    if (fields[nfields-idx]) { \
        if (*fmt == 's') { \
            *(char**)&(pstb->pst_##fld) = fields[nfields-idx]; \
        } else { \
            sscanf(fields[nfields-idx], "%"fmt, &(pstb->pst_##fld)); \
        } \
    }

    PROCSTAT_XFLD(X)
    #undef X

    GT_RET:
    if (fp) {
        fclose(fp);
    }
    return ret;
}

#ifdef PROCSTAT_MAIN_PROG

int main(void) {
    struct procstat pstb;

    procstat(getpid(), &pstb);

    #define X(idx, cnst, fld, fmt, t) \
    printf("Found field %s at index %d: %"fmt"\n", \
           #fld, idx, pstb.pst_##fld);
    PROCSTAT_XFLD(X)
    #undef X

    return 0;
}
#endif /* PROCSTAT_MAIN_PROG */
