#!/usr/bin/perl
use strict;
use warnings;
use IO::Socket::UNIX;
use Socket;
my $sock = IO::Socket::UNIX->new(
    Type => SOCK_DGRAM,
    Peer => "/tmp/orphand.sock",
    Listen => 0
);

die $! unless $sock;

my $ORIGPID = $$;
END {
    if ($$ != $ORIGPID) {
        print "Terminating ($$)\n";
    }
}

for (0..10) {
    my $pid = fork();
    if ($pid) {
        my $buf = pack("LLL", $$, $pid, 1);
        $sock->send($buf);
    } else {
        alarm(10);
        sleep(100);
    }
}
