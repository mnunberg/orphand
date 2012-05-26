#!/usr/bin/perl
use strict;
use warnings;
use Getopt::Long;
use IO::Socket::UNIX;
use Socket;
use POSIX qw(:sys_wait_h);

GetOptions(
    'i|implicit' => \my $Implicit
);
my $sock;
unless ($Implicit) {
$sock = IO::Socket::UNIX->new(
    Type => SOCK_DGRAM,
    Peer => "/tmp/orphand.sock",
    Listen => 0
);
die $! unless $sock;
}

sub send_msg {
    my ($pid,$action) = @_;
    if (!$sock) {
        return;
    }
    my $buf = pack("LLL", $$, $pid, $action);
    $sock->send($buf);
}

my $ORIGPID = $$;
END {
    if ($$ != $ORIGPID) {
        print "Terminating ($$)\n";
    }
}

my @pids = ();

for (0..10) {
    my $pid = fork();
    if ($pid) {
        send_msg($pid, 1);
        push @pids, $pid;
    } else {
        alarm(10);
        sleep(100);
    }
}

foreach my $pid (@pids[0..4]) {
    kill(9, $pid);
    waitpid($pid, 0);
    send_msg($pid, 2);
}
