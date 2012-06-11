#!/usr/bin/perl
use strict;
use warnings;

my @Fields = (
    ["PID",     "d"],
    ["COMM",    "s"],
    ["STATE",   "c"],
    ["PPID",    "d"],
    ["PGRP",    "d"],
    ["SID",     "d"],
    ["TTYNR",   "d"],
    ["TGPID",   "d"],
    ["FLAGS",   "lu"],
    ["MINFLT",  "lu"],
    ["CMINFLT", "lu"],
    ["MAJFLT",  "lu"],
    ["CMAJFLT", "lu"],
    ["UTIME",   "lu"],
    ["STIME",   "lu"],
    ["CUTIME",  "ld"],
    ["CSTIME",  "ld"],
    ["PRIORITY", "ld"],
    ["NICE",    "ld"],
    ["NTHREADS", "ld"],
    ["ITREAL",  "ld"],
    ["STARTTIME", "llu"],
    ["VSIZE", "lu"],
    ["RSS", "ld"],
    ["RSSLIM", "lu"],
    ["STARTCODE", "lu"],
    ["ENDCODE", "lu"],
    ["STARTSTACK", "lu"],
    ["ESP", "lu"],
    ["EIP", "lu"],
    ["SIGPENDING", "lu"],
    ["SIGBLOCKED", "lu"],
    ["SIGCATCH", "lu"],
    ["WCHAN", "lu"],
    ["NSWAP", "lu"],
    ["CNSWAP", "lu"],
    ["EXITSIGNAL", "lu"],
    ["PROCESSOR", "d"],
    ["RTPRIORITY", "u"],
    ["POLICY", "u"],
    ["BLKIOTICKS", "llu", [2,6,18]],
    ["GUESTTIME", "lu", [2,6,24]],
    ["CGUESTTIME", "ld", [2,6,24]]
);

my $ctype_map = {
    'd' => 'int',
    'ld' => 'long int',
    'lu' => 'long unsigned int',
    'u' => 'unsigned int',
    'c' => 'char',
    's' => 'char*',
    'llu' => 'long long unsigned int',
    'lld' => 'long long int',
};

for (my $i = 0; $i < $#Fields; $i++) {
    my ($name,$fmt,$version) = @{$Fields[$i]};
    my $ctype = $ctype_map->{$fmt} or die "Unrecognized fmd $fmt";
    my $indent = '    ';

    printf("X(%d, %s, %s,", $i, $name, lc($name));
    printf("\\\n$indent\"%s\", %s)",
        $fmt, $ctype);
    printf("\\\n");
}
