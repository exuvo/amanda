# vim:ft=perl
# Copyright (c) 2009-2012 Zmanda, Inc.  All Rights Reserved.
# Copyright (c) 2013-2016 Carbonite, Inc.  All Rights Reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
#
# Contact information: Carbonite Inc., 756 N Pastoria Ave
# Sunnyvale, CA 94086, USA, or: http://www.zmanda.com

package Installcheck::Mock;

=head1 NAME

Installcheck::Mock - utilities for the installcheck mocks

=head1 SYNOPSIS

  use Installcheck::Mock;

  my $statefile = Installcheck::Mock::setup_mock_mtx(
	 num_slots => 5,
	 num_ie => 1,
	 barcodes => 1,
	 track_orig => 1,
	 num_drives => 2,
	 loaded_slots => {
	     1 => '023984'
         },
	 first_slot => 1,
	 first_drive => 0,
	 first_ie => 6,
       );

=head1 USAGE

=head2 mtx

C<setup_mock_mtx> sets up a state file for C<mock/mtx> with the given config
hash, and returns the filename of the state file.  This function must be run
with the current dirctory pointing to 'installcheck/'.

The C<cleanup> method will clean up any data files, and should be called when a
test suite finishes.  The C<reset> method resets the changer to its initial
state, without restarting ndmjob.

=cut

use Installcheck;
use Cwd qw(abs_path);
use File::Path qw( mkpath rmtree );
use Data::Dumper;
use POSIX;
use IPC::Open3;
use Amanda::Paths;

require Exporter;
@ISA = qw(Exporter);
@EXPORT_OK = qw( setup_mock_mtx $mock_mtx_path );

sub setup_mock_mtx {
    my %config = @_;
    my $state_filename = "$Installcheck::TMP/mtx_state";
    open (my $fh, ">", $state_filename) or die $!;
    print $fh (Data::Dumper->Dump([
	    { config => \%config }
	], ["STATE"]));
    close ($fh);

    return $state_filename;
}

our $mock_mtx_path = abs_path("mock") . "/mtx"; # abs_path only does dirs in perl-5.6

package Installcheck::Mock::NdmpServer;

use File::Path qw( mkpath rmtree );
use Amanda::Paths;

sub new {
    my $class = shift;
    my %params = @_;

    my $self = bless {}, $class;

    no warnings; # don't complain that OUTFH is used only once

    # note that we put this under /vtapes/ so that Dumpcache will pick it up
    $self->{'simdir'} = "$Installcheck::TMP/vtapes/ndmjob";
    $self->reset() unless ($params{'no_reset'});

    # launch ndmjob
    my $port = Installcheck::get_unused_port();
    my @extra_args;
    if ($params{'tape_limit'}) {
	push @extra_args, "-o", "tape-limit=".$params{'tape_limit'};
    }
    my $ndmjob_pid = IPC::Open3::open3("INFH", "OUTFH", "ERRFH",
	"$amlibexecdir/ndmjob", "-d8", "-o", "test-daemon",
		"-p", $port, @extra_args);

    # wait for it to start up
    unless (<OUTFH> =~ /READY/) {
	for (<ERRFH>) {
	    main::diag($_);
	}
	die "ndmjob did not start up";
    }

    # and fill in the various attributes
    $self->{'port'} = $port;
    $self->{'changer'} = $self->{'simdir'};
    $self->{'drive0'} = $self->{'simdir'} . '/drive0';
    $self->{'drive1'} = $self->{'simdir'} . '/drive1';
    $self->{'drive'} = $self->{'simdir'} . '/solo-drive';
    $self->{'pid'} = $ndmjob_pid;

    return $self;
}

sub config {
    my $self = shift;
    my ($testconf) = @_;

    my $port = $self->{'port'};
    my $chg = $self->{'changer'};
    my $drive0 = $self->{'drive0'};
    my $drive1 = $self->{'drive1'};

    $testconf->remove_param('tapedev');
    $testconf->remove_param('tpchanger');
    $testconf->remove_param('changerfile');
}

sub edit_config {
    my $self = shift;

    # this is a bit sneaky, but is useful for dumpcache'd ndmp runs
    my $amanda_conf_filename = "$CONFIG_DIR/TESTCONF/amanda.conf";
    
    # slurp the whole file
    open(my $fh, "<", $amanda_conf_filename) or die("open $amanda_conf_filename: $!");
    my $amanda_conf = do { local $/; <$fh> };
    close($fh);

    # replace all existing ndmp changers and devices with the new port
    # note that we assume that the remaining paths are correct
    my $port = $self->{'port'};
    $amanda_conf =~ s/ndmp:127.0.0.1:\d+/ndmp:127.0.0.1:$port/g;

    open($fh, ">", $amanda_conf_filename) or die("open $amanda_conf_filename for writing: $!");
    print $fh $amanda_conf;
    close($fh);
}

sub reset {
    my $self = shift;

    $self->cleanup();
    mkpath($self->{'simdir'})
	or die("cannot create " . $self->{'simdir'});

    # non-changer drive must exist
    my $drive = $self->{'simdir'} . '/solo-drive';
    open(my $fd, ">", $drive)
	or die("Could not open '$drive': $!");
    close($fd);
}

sub cleanup {
    my $self = shift;

    rmtree($self->{'simdir'}) if -d $self->{'simdir'};
}

1;
