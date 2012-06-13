#!/usr/bin/perl
use strict;
use warnings;
use Getopt::Long;
use IO::Socket::UNIX;
use Socket::MsgHdr qw(sendmsg recvmsg);
use Socket;
use POSIX qw(:sys_wait_h);

GetOptions(
    'i|implicit' => \my $Implicit,
    'p|ping' => \my $DoPing,
    'n|num-children=i' => \my $ChildCount,
);

$ChildCount ||= 10;

my $sock;
unless ($Implicit) {
$sock = IO::Socket::UNIX->new(
    Type => SOCK_STREAM,
    Peer => "/tmp/orphand.sock",
);
die $! unless $sock;
}

$SIG{ALRM} = sub {
    warn("$$ Dying from SIGALRM");
    exit(1);
};

sub send_msg {
    my ($pid,$action) = @_;
    if (!$sock) {
        return;
    }
    my $buf = pack("LLL", $$, $pid, $action);
    $sock->send($buf);
}

socketpair( my $sw, my $sr, AF_UNIX, SOCK_DGRAM, 0 )
    or die "socketpair: $!";

sub do_ping {
    $sock->send(pack("LLL", $$, -1, 0x3), MSG_WAITALL);
    $sock->recv(my $buf = "", 12, MSG_WAITALL);
    length($buf) == 12 or die "Didn't get good buffer!";
}

if ($DoPing) {
    do_ping() foreach (0..5);
    exit(0);
}

my $ORIGPID = $$;
END {
    if ($$ != $ORIGPID) {
        print "Terminating ($$)\n";
    }
}

my @pids = ();
print "Will fork $ChildCount children\n";

for (0..$ChildCount) {
    my $pid = fork();
    die "Couldn't fork! ($!)\n" unless defined $pid;

    if ($pid) {
        print "Forked $pid\n";
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
