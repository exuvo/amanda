# Copyright (c) 2008-2012 Zmanda, Inc.  All Rights Reserved.
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

use Test::More;
use File::Path qw( mkpath rmtree );
use Sys::Hostname;
use Carp;
use strict;
use warnings;

use lib '@amperldir@';
use Installcheck;
use Installcheck::Mock;
use Installcheck::Config;
use Amanda::Debug;
use Amanda::Device qw( :constants );
use Amanda::Config qw( :getconf :init );
use Amanda::Xfer qw( :constants );
use Amanda::Header qw( :constants );
use Amanda::Paths;
use Amanda::Constants;
use Amanda::Util;
use Amanda::MainLoop;
use IO::Socket;

my $dev;
my $dev_name;
my ($vtape1, $vtape2);
my ($input_filename, $output_filename) =
    ( "$Installcheck::TMP/input.tmp", "$Installcheck::TMP/output.tmp" );
my $taperoot = "$Installcheck::TMP/Amanda_Device_test_tapes";
my $diskflatroot = "$Installcheck::TMP/Amanda_Device_test_diskflat";
my $testconf;

# we'll need some vtapes..
sub mkvtape {
    my ($num) = @_;

    my $mytape = "$taperoot/$num";
    if (-d $mytape) { rmtree($mytape); }
    mkpath("$mytape/data");
    return $mytape;
}

# we'll need some diskflat..
sub mkdiskflat {
    my ($num) = @_;

    my $mytape = "$diskflatroot/TESTCONF$num";
    if (-e $mytape) { unlink($mytape); }
    mkpath("$diskflatroot");
    return $mytape;
}


# make up a fake dumpfile_t to write with
my $dumpfile = Amanda::Header->new();
$dumpfile->{type} = $Amanda::Header::F_DUMPFILE;
$dumpfile->{datestamp} = "20070102030405";
$dumpfile->{dumplevel} = 0;
$dumpfile->{compressed} = 1;
$dumpfile->{name} = "localhost";
$dumpfile->{disk} = "/home";
$dumpfile->{program} = "INSTALLCHECK";

my $write_file_count = 5;
sub write_file {
    my ($seed, $length, $filenum) = @_;

    $dumpfile->{'datestamp'} = "2000010101010$filenum";

    ok($dev->start_file($dumpfile),
	"start file $filenum")
	or diag($dev->error_or_status());

    is($dev->file(), $filenum,
	"Device has correct filenum");

    croak ("selected file size $length is *way* too big")
	unless ($length < 1024*1024*10);
    ok(Amanda::Device::write_random_to_device($seed, $length, $dev),
	"write random data");

    if(ok($dev->in_file(),
	"still in_file")) {
	ok($dev->finish_file(),
	    "finish_file")
	    or diag($dev->error_or_status());
    } else {
	pass("not in file, so not calling finish_file");
    }
}

my $verify_file_count = 4;
sub verify_file {
    my ($seed, $length, $filenum) = @_;

    ok(my $read_dumpfile = $dev->seek_file($filenum),
	"seek to file $filenum")
	or diag($dev->error_or_status());
    is($dev->file(), $filenum,
	"device is really at file $filenum");
    ok(header_for($read_dumpfile, $filenum),
	"header is correct")
	or diag($dev->error_or_status());
    ok(Amanda::Device::verify_random_from_device($seed, $length, $dev),
	"verified file contents");
}

sub header_for {
    my ($hdr, $filenum) = @_;
    return ($hdr and $hdr->{'datestamp'} eq "2000010101010$filenum");
}

# properties test

my @common_properties = (
    'appendable',
    'block_size',
    'canonical_name',
    'concurrency',
    'max_block_size',
    'medium_access_type',
    'min_block_size',
    'partial_deletion',
    'full_deletion',
    'streaming',
);

sub properties_include {
    my ($got, $should_include, $msg) = @_;
    my %got = map { $_->{'name'}, 1 } @$got;
    my @missing = grep { !defined($got{$_}) } @$should_include;
    if (@missing) {
	fail($msg);
	diag(" Expected properties: " . join(", ", @$should_include));
	diag("      Got properties: " . join(", ", @$got));
	diag("  Missing properties: " . join(", ", @missing));
    } else {
	pass($msg);
    }
}

####
## get stuff set up

$testconf = Installcheck::Config->new();
$testconf->write();
config_init($CONFIG_INIT_EXPLICIT_NAME, 'TESTCONF') == $CFGERR_OK
    or die("Could not load configuration");

# put the debug messages somewhere
Amanda::Debug::dbopen("installcheck");
Installcheck::log_test_output();

####
## Test errors a little bit

$dev = Amanda::Device->new("foobar:");
isnt($dev->status(), $DEVICE_STATUS_SUCCESS,
    "creation of a bogus 'foobar:' device fails");

$dev = Amanda::Device->new("rait:{{");
isnt($dev->status(), $DEVICE_STATUS_SUCCESS,
    "creation of a bogus 'rait:{{' device fails");

$dev = Amanda::Device->new("rait:{a,b");
isnt($dev->status(), $DEVICE_STATUS_SUCCESS,
    "creation of a bogus 'rait:{a,b' device fails");

####
## first, test out the 'null' device.

$dev_name = "null:";

$dev = Amanda::Device->new($dev_name);
is($dev->status(), $DEVICE_STATUS_SUCCESS,
    "create null device")
    or diag $dev->error_or_status();
ok($dev->start($ACCESS_WRITE, "NULL1", "19780615010203"),
    "start null device in write mode")
    or diag $dev->error_or_status();

# try properties
properties_include([ $dev->property_list() ], [ @common_properties ],
    "necessary properties listed on null device");
is($dev->property_get("canonical_name"), "null:",
    "property_get(canonical_name) on null device");
is($dev->property_get("caNONical-name"), "null:",
    "property_get(caNONical-name) on null device (case, dash-insensitivity)");
is_deeply([ $dev->property_get("canonical_name") ],
    [ "null:", $PROPERTY_SURETY_GOOD, $PROPERTY_SOURCE_DEFAULT ],
    "extended property_get returns correct surety/source");
for my $prop ($dev->property_list()) {
    next unless $prop->{'name'} eq 'canonical_name';
    is($prop->{'description'},
	"The most reliable device name to use to refer to this device.",
	"property info for canonical name is correct");
}
ok(!$dev->property_get("full_deletion"),
    "property_get(full_deletion) on null device");
is($dev->property_get("comment"), undef,
    "no comment by default");
ok(!$dev->property_set("comment", "well, that was silly"),
    "set comment property");
is($dev->property_get("comment"), "well, that was silly",
    "comment correctly stored");

# and write a file to it
write_file(0xabcde, 1024*256, 1);

# (don't finish the device, testing the finalize method's cleanup)

####
## Now some full device tests

## VFS device

$vtape1 = mkvtape(1);
$dev_name = "file:$vtape1";

$dev = Amanda::Device->new($dev_name);
is($dev->status(), $DEVICE_STATUS_SUCCESS,
    "$dev_name: create successful")
    or diag($dev->error_or_status());

properties_include([ $dev->property_list() ],
    [ @common_properties, 'max_volume_usage' ],
    "necessary properties listed on vfs device");

# play with properties a little bit
is($dev->property_set("comment", 16), undef,
    "set an string property to an integer");

is($dev->property_set("comment", 16.0), undef,
    "set an string property to a float");

is($dev->property_set("comment", "hi mom"), undef,
    "set an string property to a string");

is($dev->property_set("comment", "32768"), undef,
    "set an integer property to a simple string");

is($dev->property_set("comment", "32k"), undef,
    "set an integer property to a string with a unit");

is($dev->property_set("block_size", 32768), undef,
    "set an integer property to an integer");

is($dev->property_set("invalid-property-name", 32768), "No such device-property",
    "set an invalid-property-name");

is($dev->property_set("block-size", "toto"), "The value is no allowed",
    "set an illegal value");

is($dev->property_set("block-size", "128km"), "The value is no allowed",
    "set an illegal value");

is($dev->property_set("streaming", "toto"), "Not allowed to set property",
    "set streaming");

$dev->read_label();
ok($dev->status() & $DEVICE_STATUS_VOLUME_UNLABELED,
    "initially unlabeled")
    or diag($dev->error_or_status());

ok($dev->start($ACCESS_WRITE, "TESTCONF13", undef),
    "start in write mode")
    or diag($dev->error_or_status());

ok(!($dev->status() & $DEVICE_STATUS_VOLUME_UNLABELED),
    "not unlabeled anymore")
    or diag($dev->error_or_status());

for (my $i = 1; $i <= 3; $i++) {
    write_file(0x2FACE, $dev->block_size()*10+17, $i);
}

ok($dev->finish(),
    "finish device after write")
    or diag($dev->error_or_status());

$dev->read_label();
ok(!($dev->status()),
    "no error, at all, from read_label")
    or diag($dev->error_or_status());

# append one more copy, to test ACCESS_APPEND

ok($dev->start($ACCESS_APPEND, undef, undef),
    "start in append mode")
    or diag($dev->error_or_status());

write_file(0xD0ED0E, $dev->block_size()*4, 4);

ok($dev->finish(),
    "finish device after append")
    or diag($dev->error_or_status());

# try reading the third file back, creating a new device
# object first, and skipping the read-label step.

$dev = undef;
$dev = Amanda::Device->new($dev_name);
is($dev->status(), $DEVICE_STATUS_SUCCESS,
    "$dev_name: re-create successful")
    or diag($dev->error_or_status());

ok($dev->start($ACCESS_READ, undef, undef),
    "start in read mode")
    or diag($dev->error_or_status());

verify_file(0x2FACE, $dev->block_size()*10+17, 3);

{
    # try two seek_file's in a row
    my $hdr = $dev->seek_file(3);
    is($hdr? $hdr->{'type'} : -1, $Amanda::Header::F_DUMPFILE, "seek_file the first time");
    $hdr = $dev->seek_file(3);
    is($hdr? $hdr->{'type'} : -1, $Amanda::Header::F_DUMPFILE, "seek_file the second time");
}

ok($dev->finish(),
    "finish device after read")
    or diag($dev->error_or_status());

# test erase
ok($dev->erase(),
   "erase device")
    or diag($dev->error_or_status());

ok($dev->erase(),
   "erase device (again)")
    or diag($dev->error_or_status());

ok($dev->finish(),
   "finish device after erase")
    or diag($dev->error_or_status());

# test monitor_free_space property (testing the monitoring would require a
# dedicated partition for the tests - it's not worth it)

ok($dev->property_get("monitor_free_space"),
    "monitor_free_space property is set by default");

is($dev->property_set("monitor_free_space", 0), undef,
    "monitor_free_space property can be set to false");

ok(!$dev->property_get("monitor_free_space"),
    "monitor_free_space property value 'sticks'");

# test the LEOM functionality

$dev = undef;
$dev = Amanda::Device->new($dev_name);
is($dev->status(), $DEVICE_STATUS_SUCCESS,
    "$dev_name: re-create successful")
    or diag($dev->error_or_status());
is($dev->property_set("MAX_VOLUME_USAGE", "512k"), undef,
    "set MAX_VOLUME_USAGE to test LEOM");
is($dev->property_set("LEOM", 1), undef,
    "set LEOM");
is($dev->property_set("ENFORCE_MAX_VOLUME_USAGE", 0), undef,
    "set ENFORCE_MAX_VOLUME_USAGE");

ok($dev->start($ACCESS_WRITE, 'TESTCONF23', undef),
    "start in write mode")
    or diag($dev->error_or_status());

ok($dev->start_file($dumpfile),
    "start file 1")
    or diag($dev->error_or_status());

ok(Amanda::Device::write_random_to_device(0xCAFE, 440*1024, $dev),
    "write random data into the early-warning zone");

ok(!$dev->is_eom,
    "device does not indicates LEOM after writing when ENFORCE_MAX_VOLUME_USAGE is FALSE");

ok($dev->finish_file(),
    "..but a finish_file is allowed to complete")
    or diag($dev->error_or_status());

ok($dev->finish(),
   "finish device after LEOM test")
    or diag($dev->error_or_status());

$dev = undef;
$dev = Amanda::Device->new($dev_name);
is($dev->status(), $DEVICE_STATUS_SUCCESS,
    "$dev_name: re-create successful")
    or diag($dev->error_or_status());
is($dev->property_set("MAX_VOLUME_USAGE", "512k"), undef,
    "set MAX_VOLUME_USAGE to test LEOM");
is($dev->property_set("LEOM", 1), undef,
    "set LEOM");
is($dev->property_set("ENFORCE_MAX_VOLUME_USAGE", 1), undef,
    "set ENFORCE_MAX_VOLUME_USAGE");

ok($dev->start($ACCESS_WRITE, 'TESTCONF23', undef),
    "start in write mode")
    or diag($dev->error_or_status());

ok($dev->start_file($dumpfile),
    "start file 1")
    or diag($dev->error_or_status());

ok(!$dev->is_eom,
    "device does not indicate LEOM before writing");

ok(Amanda::Device::write_random_to_device(0xCAFE, 440*1024, $dev),
    "write random data into the early-warning zone");

ok($dev->is_eom,
    "device indicates LEOM after writing");

ok($dev->finish_file(),
    "..but a finish_file is allowed to complete")
    or diag($dev->error_or_status());

ok($dev->finish(),
   "finish device after LEOM test")
    or diag($dev->error_or_status());

$dev = undef;
$dev = Amanda::Device->new($dev_name);
is($dev->status(), $DEVICE_STATUS_SUCCESS,
    "$dev_name: re-create successful")
    or diag($dev->error_or_status());
is($dev->property_set("MAX_VOLUME_USAGE", "512k"), undef,
    "set MAX_VOLUME_USAGE to test LEOM");
is($dev->property_set("LEOM", 1), undef,
    "set LEOM");

ok($dev->start($ACCESS_WRITE, 'TESTCONF23', undef),
    "start in write mode")
    or diag($dev->error_or_status());

ok($dev->start_file($dumpfile),
    "start file 1")
    or diag($dev->error_or_status());

ok(!$dev->is_eom,
    "device does not indicate LEOM before writing");

ok(Amanda::Device::write_random_to_device(0xCAFE, 440*1024, $dev),
    "write random data into the early-warning zone");

ok($dev->is_eom,
    "device indicates LEOM after writing as default value of ENFORCE_MAX_VOLUME_USAGE is true for vfs device");

ok($dev->finish_file(),
    "..but a finish_file is allowed to complete")
    or diag($dev->error_or_status());

ok($dev->finish(),
   "finish device after LEOM test")
    or diag($dev->error_or_status());

$dev = undef;
$dev = Amanda::Device->new($dev_name);
is($dev->status(), $DEVICE_STATUS_SUCCESS,
    "$dev_name: re-create successful")
    or diag($dev->error_or_status());
is($dev->property_set("MAX_VOLUME_USAGE", "160k"), undef,
    "set MAX_VOLUME_USAGE to test LEOM while writing the first header");
is($dev->property_set("LEOM", 1), undef,
    "set LEOM");

ok($dev->start($ACCESS_WRITE, 'TESTCONF23', undef),
    "start in write mode")
    or diag($dev->error_or_status());

ok($dev->start_file($dumpfile),
    "start file 1")
    or diag($dev->error_or_status());

ok($dev->is_eom,
    "device indicates LEOM after writing first header");

ok($dev->finish_file(),
    "..but a finish_file is allowed to complete")
    or diag($dev->error_or_status());

ok($dev->finish(),
   "finish device after LEOM test")
    or diag($dev->error_or_status());

####
## Now some full device tests

## DISKFLAT device

my $diskflat1 = mkdiskflat(13);
$dev_name = "diskflat:$diskflat1";

$dev = Amanda::Device->new($dev_name);
is($dev->status(), $DEVICE_STATUS_SUCCESS,
    "$dev_name: create successful")
    or diag($dev->error_or_status());

properties_include([ $dev->property_list() ],
    [ @common_properties, 'max_volume_usage' ],
    "necessary properties listed on diskflat device");

# play with properties a little bit
is($dev->property_set("comment", 16), undef,
    "set an string property to an integer");

is($dev->property_set("comment", 16.0), undef,
    "set an string property to a float");

is($dev->property_set("comment", "hi mom"), undef,
    "set an string property to a string");

is($dev->property_set("comment", "32768"), undef,
    "set an integer property to a simple string");

is($dev->property_set("comment", "32k"), undef,
    "set an integer property to a string with a unit");

is($dev->property_set("block_size", 32768), undef,
    "set an integer property to an integer");

is($dev->property_set("invalid-property-name", 32768), "No such device-property",
    "set an invalid-property-name");

$dev->read_label();
ok($dev->status() & $DEVICE_STATUS_VOLUME_UNLABELED,
    "initially unlabeled")
    or diag($dev->error_or_status());

ok($dev->start($ACCESS_WRITE, "TESTCONF13", undef),
    "start in write mode")
    or diag($dev->error_or_status());

ok(!($dev->status() & $DEVICE_STATUS_VOLUME_UNLABELED),
    "not unlabeled anymore")
    or diag($dev->error_or_status());

write_file(0x2FACE, $dev->block_size()*10+17, 1);

# try to write a second file

ok(!$dev->start_file($dumpfile),
    "start_file of seconf file failed")
    or diag($dev->error_or_status());

ok($dev->error_or_status() eq "Can't write more than one file to the diskflat device",
    "error message is correct")
    or diag($dev->error_or_status());

ok($dev->finish(),
    "finish device after write")
    or diag($dev->error_or_status());

$dev->read_label();
ok(!($dev->status()),
    "no error, at all, from read_label")
    or diag($dev->error_or_status());

# try reading the first file back, creating a new device
# object first, and skipping the read-label step.

$dev = undef;
$dev = Amanda::Device->new($dev_name);
is($dev->status(), $DEVICE_STATUS_SUCCESS,
    "$dev_name: re-create successful")
    or diag($dev->error_or_status());

ok($dev->start($ACCESS_READ, undef, undef),
    "start in read mode")
    or diag($dev->error_or_status());

verify_file(0x2FACE, $dev->block_size()*10+17, 1);

ok($dev->finish(),
    "finish device after read")
    or diag($dev->error_or_status());

# test erase
ok($dev->erase(),
   "erase device")
    or diag($dev->error_or_status());

ok($dev->erase(),
   "erase device (again)")
    or diag($dev->error_or_status());

ok($dev->finish(),
   "finish device after erase")
    or diag($dev->error_or_status());

# test monitor_free_space property (testing the monitoring would require a
# dedicated partition for the tests - it's not worth it)

ok($dev->property_get("monitor_free_space"),
    "monitor_free_space property is set by default");

is($dev->property_set("monitor_free_space", 0), undef,
    "monitor_free_space property can be set to false");

ok(!$dev->property_get("monitor_free_space"),
    "monitor_free_space property value 'sticks'");

# test the LEOM functionality

$dev = undef;
$dev = Amanda::Device->new($dev_name);
is($dev->status(), $DEVICE_STATUS_SUCCESS,
    "$dev_name: re-create successful")
    or diag($dev->error_or_status());
is($dev->property_set("MAX_VOLUME_USAGE", "512k"), undef,
    "set MAX_VOLUME_USAGE to test LEOM");
is($dev->property_set("LEOM", 1), undef,
    "set LEOM");
is($dev->property_set("ENFORCE_MAX_VOLUME_USAGE", 0), undef,
    "set ENFORCE_MAX_VOLUME_USAGE");

ok($dev->start($ACCESS_WRITE, 'TESTCONF23', undef),
    "start in write mode")
    or diag($dev->error_or_status());

ok($dev->start_file($dumpfile),
    "start file 1")
    or diag($dev->error_or_status());

ok(Amanda::Device::write_random_to_device(0xCAFE, 440*1024, $dev),
    "write random data into the early-warning zone");

ok(!$dev->is_eom,
    "device does not indicates LEOM after writing when ENFORCE_MAX_VOLUME_USAGE is FALSE");

ok($dev->finish_file(),
    "..but a finish_file is allowed to complete")
    or diag($dev->error_or_status());

ok($dev->finish(),
   "finish device after LEOM test")
    or diag($dev->error_or_status());

$dev = undef;
$dev = Amanda::Device->new($dev_name);
is($dev->status(), $DEVICE_STATUS_SUCCESS,
    "$dev_name: re-create successful")
    or diag($dev->error_or_status());
is($dev->property_set("MAX_VOLUME_USAGE", "512k"), undef,
    "set MAX_VOLUME_USAGE to test LEOM");
is($dev->property_set("LEOM", 1), undef,
    "set LEOM");
is($dev->property_set("ENFORCE_MAX_VOLUME_USAGE", 1), undef,
    "set ENFORCE_MAX_VOLUME_USAGE");

ok($dev->start($ACCESS_WRITE, 'TESTCONF23', undef),
    "start in write mode")
    or diag($dev->error_or_status());

ok($dev->start_file($dumpfile),
    "start file 1")
    or diag($dev->error_or_status());

ok(!$dev->is_eom,
    "device does not indicate LEOM before writing");

ok(Amanda::Device::write_random_to_device(0xCAFE, 440*1024, $dev),
    "write random data into the early-warning zone");

ok($dev->is_eom,
    "device indicates LEOM after writing");

ok($dev->finish_file(),
    "..but a finish_file is allowed to complete")
    or diag($dev->error_or_status());

ok($dev->finish(),
   "finish device after LEOM test")
    or diag($dev->error_or_status());

$dev = undef;
$dev = Amanda::Device->new($dev_name);
is($dev->status(), $DEVICE_STATUS_SUCCESS,
    "$dev_name: re-create successful")
    or diag($dev->error_or_status());
is($dev->property_set("MAX_VOLUME_USAGE", "512k"), undef,
    "set MAX_VOLUME_USAGE to test LEOM");
is($dev->property_set("LEOM", 1), undef,
    "set LEOM");

ok($dev->start($ACCESS_WRITE, 'TESTCONF23', undef),
    "start in write mode")
    or diag($dev->error_or_status());

ok($dev->start_file($dumpfile),
    "start file 1")
    or diag($dev->error_or_status());

ok(!$dev->is_eom,
    "device does not indicate LEOM before writing");

ok(Amanda::Device::write_random_to_device(0xCAFE, 440*1024, $dev),
    "write random data into the early-warning zone");

ok($dev->is_eom,
    "device indicates LEOM after writing as default value of ENFORCE_MAX_VOLUME_USAGE is true for vfs device");

ok($dev->finish_file(),
    "..but a finish_file is allowed to complete")
    or diag($dev->error_or_status());

ok($dev->finish(),
   "finish device after LEOM test")
    or diag($dev->error_or_status());

$dev = undef;
$dev = Amanda::Device->new($dev_name);
is($dev->status(), $DEVICE_STATUS_SUCCESS,
    "$dev_name: re-create successful")
    or diag($dev->error_or_status());
is($dev->property_set("MAX_VOLUME_USAGE", "160k"), undef,
    "set MAX_VOLUME_USAGE to test LEOM while writing the first header");
is($dev->property_set("LEOM", 1), undef,
    "set LEOM");

ok($dev->start($ACCESS_WRITE, 'TESTCONF23', undef),
    "start in write mode")
    or diag($dev->error_or_status());

ok($dev->start_file($dumpfile),
    "start file 1")
    or diag($dev->error_or_status());

ok($dev->is_eom,
    "device indicates LEOM after writing first header");

ok($dev->finish_file(),
    "..but a finish_file is allowed to complete")
    or diag($dev->error_or_status());

ok($dev->finish(),
   "finish device after LEOM test")
    or diag($dev->error_or_status());

# Test a tape device if the proper environment variables are set
my $TAPE_DEVICE = $ENV{'INSTALLCHECK_TAPE_DEVICE'};
my $run_tape_tests = defined $TAPE_DEVICE;
SKIP: {
    skip "define \$INSTALLCHECK_TAPE_DEVICE to run tape tests",
	    30 +
	    7 * $verify_file_count +
	    5 * $write_file_count
	unless $run_tape_tests;

    $dev_name = "tape:$TAPE_DEVICE";
    $dev = Amanda::Device->new($dev_name);
    is($dev->status(), $DEVICE_STATUS_SUCCESS,
	"$dev_name: create successful")
	or diag($dev->error_or_status());

    my $status = $dev->read_label();
    ok(($status == $DEVICE_STATUS_SUCCESS) || (($status & $DEVICE_STATUS_VOLUME_UNLABELED) != 0),
       "status is either OK or possibly unlabeled")
        or diag($dev->error_or_status());

    ok($dev->start($ACCESS_WRITE, "TESTCONF13", undef),
	"start in write mode")
	or diag($dev->error_or_status());

    ok(!($dev->status() & $DEVICE_STATUS_VOLUME_UNLABELED),
	"not unlabeled anymore")
	or diag($dev->error_or_status());

    for (my $i = 1; $i <= 4; $i++) {
	write_file(0x2FACE+$i, $dev->block_size()*10+17, $i);
    }

    ok($dev->finish(),
	"finish device after write")
	or diag($dev->error_or_status());

    $dev->read_label();
    ok(!($dev->status()),
	"no error, at all, from read_label")
	or diag($dev->error_or_status());

    is($dev->volume_label(), "TESTCONF13",
	"read_label reads the correct label")
	or diag($dev->error_or_status());

    # append one more copy, to test ACCESS_APPEND

    # if final_filemarks is 1, then the tape device will use F_NOOP,
    # inserting an extra file, and we'll be appending at file number 6.
    my $append_fileno = ($dev->property_get("FINAL_FILEMARKS") == 2)? 5:6;

    SKIP: {
        skip "APPEND not supported", $write_file_count + 2
            unless $dev->property_get("APPENDABLE");

        ok($dev->start($ACCESS_APPEND, undef, undef),
            "start in append mode")
            or diag($dev->error_or_status());

        write_file(0xD0ED0E, $dev->block_size()*4, $append_fileno);

        ok($dev->finish(),
            "finish device after append")
            or diag($dev->error_or_status());
    }

    # try reading the second and third files back, creating a new
    # device object first, and skipping the read-label step.

    $dev = undef;
    $dev = Amanda::Device->new($dev_name);
    is($dev->status(), $DEVICE_STATUS_SUCCESS,
	"$dev_name: re-create successful")
	or diag($dev->error_or_status());

    # use a big read_block_size, checking that it's also settable
    # via read_buffer_size
    is($dev->property_set("read_buffer_size", 256*1024), undef,
	"can set read_buffer_size");
    is($dev->property_get("read_block_size"), 256*1024,
	"and its value is reflected in read_block_size");
    is($dev->property_set("read_block_size", 32*1024), undef,
	"can set read_block_size");

    ok($dev->start($ACCESS_READ, undef, undef),
	"start in read mode")
	or diag($dev->error_or_status());

    # now verify those files in a particular order to trigger all of the
    # seeking edge cases

    verify_file(0x2FACE+1, $dev->block_size()*10+17, 1);
    verify_file(0x2FACE+2, $dev->block_size()*10+17, 2);
    verify_file(0x2FACE+4, $dev->block_size()*10+17, 4);
    verify_file(0x2FACE+3, $dev->block_size()*10+17, 3);
    verify_file(0x2FACE+1, $dev->block_size()*10+17, 1);

    # try re-seeking to the same file
    ok(header_for($dev->seek_file(2), 2), "seek to file 2 the first time");
    verify_file(0x2FACE+2, $dev->block_size()*10+17, 2);
    ok(header_for($dev->seek_file(2), 2), "seek to file 2 the third time");

    # and seek through the same pattern *without* reading to EOF
    ok(header_for($dev->seek_file(1), 1), "seek to file 1");
    ok(header_for($dev->seek_file(2), 2), "seek to file 2");
    ok(header_for($dev->seek_file(4), 4), "seek to file 4");
    ok(header_for($dev->seek_file(3), 3), "seek to file 3");
    ok(header_for($dev->seek_file(1), 1), "seek to file 1");

    SKIP: {
        skip "APPEND not supported", $verify_file_count
            unless $dev->property_get("APPENDABLE");
	verify_file(0xD0ED0E, $dev->block_size()*4, $append_fileno);
    }

    ok($dev->finish(),
	"finish device after read")
	or diag($dev->error_or_status());

    # tickle a regression in improperly closing fd's
    ok($dev->finish(),
	"finish device again after read")
	or diag($dev->error_or_status());

    ok($dev->read_label() == $DEVICE_STATUS_SUCCESS,
	"read_label after second finish (used to fail)")
	or diag($dev->error_or_status());

    # finally, run the device with FSF and BSF set to "no", to test the
    # fallback schemes for this condition

    $dev = undef;
    $dev = Amanda::Device->new($dev_name);
    is($dev->status(), $DEVICE_STATUS_SUCCESS,
	"$dev_name: re-create successful")
	or diag($dev->error_or_status());
    $dev->property_set("fsf", "no");
    $dev->property_set("bsf", "no");

    ok($dev->start($ACCESS_READ, undef, undef),
	"start in read mode")
	or diag($dev->error_or_status());

    ok(header_for($dev->seek_file(1), 1), "seek to file 1");
    ok(header_for($dev->seek_file(4), 4), "seek to file 4");
    ok(header_for($dev->seek_file(2), 2), "seek to file 2");

    ok($dev->finish(),
	"finish device after read")
	or diag($dev->error_or_status());
}

unlink($input_filename);
unlink($output_filename);
rmtree($taperoot);

done_testing();
