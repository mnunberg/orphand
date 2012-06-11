#ifndef PROCSTAT_H_
#define PROCSTAT_H_

#ifdef __cplusplus
extern "C {"
#endif

enum {
    #define PROCSTAT_XFLD(X) \
    X(0, PID, pid,\
        "d", int)\
    X(1, COMM, comm,\
        "s", char*)\
    X(2, STATE, state,\
        "c", char)\
    X(3, PPID, ppid,\
        "d", int)\
    X(4, PGRP, pgrp,\
        "d", int)\
    X(5, SID, sid,\
        "d", int)\
    X(6, TTYNR, ttynr,\
        "d", int)\
    X(7, TGPID, tgpid,\
        "d", int)\
    X(8, FLAGS, flags,\
        "lu", long unsigned int)\
    X(9, MINFLT, minflt,\
        "lu", long unsigned int)\
    X(10, CMINFLT, cminflt,\
        "lu", long unsigned int)\
    X(11, MAJFLT, majflt,\
        "lu", long unsigned int)\
    X(12, CMAJFLT, cmajflt,\
        "lu", long unsigned int)\
    X(13, UTIME, utime,\
        "lu", long unsigned int)\
    X(14, STIME, stime,\
        "lu", long unsigned int)\
    X(15, CUTIME, cutime,\
        "ld", long int)\
    X(16, CSTIME, cstime,\
        "ld", long int)\
    X(17, PRIORITY, priority,\
        "ld", long int)\
    X(18, NICE, nice,\
        "ld", long int)\
    X(19, NTHREADS, nthreads,\
        "ld", long int)\
    X(20, ITREAL, itreal,\
        "ld", long int)\
    X(21, STARTTIME, starttime,\
        "llu", long long unsigned int)\
    X(22, VSIZE, vsize,\
        "lu", long unsigned int)\
    X(23, RSS, rss,\
        "ld", long int)\
    X(24, RSSLIM, rsslim,\
        "lu", long unsigned int)\
    X(25, STARTCODE, startcode,\
        "lu", long unsigned int)\
    X(26, ENDCODE, endcode,\
        "lu", long unsigned int)\
    X(27, STARTSTACK, startstack,\
        "lu", long unsigned int)\
    X(28, ESP, esp,\
        "lu", long unsigned int)\
    X(29, EIP, eip,\
        "lu", long unsigned int)\
    X(30, SIGPENDING, sigpending,\
        "lu", long unsigned int)\
    X(31, SIGBLOCKED, sigblocked,\
        "lu", long unsigned int)\
    X(32, SIGCATCH, sigcatch,\
        "lu", long unsigned int)\
    X(33, WCHAN, wchan,\
        "lu", long unsigned int)\
    X(34, NSWAP, nswap,\
        "lu", long unsigned int)\
    X(35, CNSWAP, cnswap,\
        "lu", long unsigned int)\
    X(36, EXITSIGNAL, exitsignal,\
        "lu", long unsigned int)\
    X(37, PROCESSOR, processor,\
        "d", int)\
    X(38, RTPRIORITY, rtpriority,\
        "u", unsigned int)\
    X(39, POLICY, policy,\
        "u", unsigned int)\
    X(40, BLKIOTICKS, blkioticks,\
        "llu", long long unsigned int)\
    X(41, GUESTTIME, guesttime,\
        "lu", long unsigned int)

    #define X(idx, constname, fld, fmt, ctp) \
    PROCSTAT_##constname = idx,
    PROCSTAT_XFLD(X)
    #undef X

    PROCSTAT_FLD_MAX
};

/**
 * Errors which may be thrown by the library
 */
enum {
    PROCSTAT_EEMPTY = 1,
    PROCSTAT_ETOOBIG
};

struct procstat {
    /** Buffer holding the contents of /proc/[pid]/stat */
    char buf[512];

    /** errno on syscalls */
    int sys_error;
    /** procstat error */
    int lib_error;

    /**
     * Fields representing the various information in /proc/[pid]/stat.
     * These fields are named by the third argument in the x-macro defined
     * above, and prefixed with the identifier 'pst_'
     */

    #define X(idx, constname, fld_name, fmt, ctp) \
    ctp pst_##fld_name;
    PROCSTAT_XFLD(X)
    #undef X
};

/**
 * Populates pstb with information about the process pid.
 * Returns zero on success, and non-zero on error. The error received may
 * be inspected by viewing the pstb->sys_error or pstb->lib_error (depending
 * on the error received).
 */
int
procstat(pid_t pid, struct procstat *pstb);

#ifdef __cplusplus
}
#endif

#endif /* PROCSTAT_H_ */
