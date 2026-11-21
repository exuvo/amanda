# Copyright (c) 2026 Exuvo.  All Rights Reserved.
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

# A server-compressed dump can't use DAR, so amidxtaped streams the whole
# dump.  amrecover/amgtar must stop once the requested files are restored,
# and the tape must be usable for the next restore.

use Test::More tests => 9;
use strict;
use warnings;
use File::Path;
use IO::File;

use lib '@amperldir@';
use Installcheck;
use Installcheck::Config;
use Installcheck::Run qw(run $diskname amdump_diag);
use Amanda::Paths;
use Amanda::Util;

my $src = "$Installcheck::TMP/early_stop_src";
my $rest = "$Installcheck::TMP/early_stop_rest";
rmtree($src);
mkpath($src);
my $fh = IO::File->new("$src/a_small", '>') or die "$!";
print $fh "small\n";
close $fh;
# random data, so the dump is as large compressed as it is raw
system("head -c 64M /dev/urandom > '$src/z_big'") == 0 or die "head failed";

my $compress = "$Installcheck::TMP/early_stop_compress";
open(my $w, '>', $compress) or die "$!";
print $w "#!/bin/sh\nif [ \"\$1\" = -d ]; then exec gzip -dc; else exec gzip -1c; fi\n";
close $w;
chmod 0755, $compress;

my $testconf = Installcheck::Run::setup();
$testconf->add_tapetype('TEST-TAPE', [ length => '500 mbytes', filemark => '4 kbytes' ]);
$testconf->add_param('autolabel', '"TESTCONF%%" empty volume_error');
$testconf->add_dle(<<EODLE);
localhost early $src {
    installcheck-test
    program "APPLICATION"
    application {
	plugin "amgtar"
	property "ATIME-PRESERVE" "NO"
    }
    compress server custom
    server_custom_compress "$compress"
}
EODLE
$testconf->write();

ok(run('amdump', 'TESTCONF'), "amdump of a server-compressed dump")
    or amdump_diag();

sub amrecover {
    my ($file) = @_;
    rmtree($rest);
    mkpath($rest);
    chdir($rest) or die "$!";
    my $out = "$Installcheck::TMP/early_stop_amrecover.out";
    open(my $amr, "|-", "'$sbindir/amrecover' TESTCONF -s localhost -t localhost"
			. " -o auth=local > '$out' 2>&1") or die "$!";
    print $amr "sethost localhost\nsetdisk early\nadd $file\nextract\nY\nY\nY\nquit\n";
    close($amr);
    my $status = $? >> 8;
    chdir($Installcheck::TMP);
    return ($status, Amanda::Util::slurp($out));
}

my ($status, $out) = amrecover('a_small');
is($status, 0, "amrecover of the first file exits 0") or diag($out);
is(Amanda::Util::slurp("$rest/a_small"), "small\n", "a_small restored");
ok(!-e "$rest/z_big", "z_big not restored");
unlike($out, qr/Failed to write|Broken pipe|Extractor child exited/,
       "no error reported") or diag($out);
# progress is printed in kb; the whole dump is 64 MB
my ($kb) = $out =~ /(\d+) kb\s*\n\s*\n?\.\/a_small/;
ok(defined $kb && $kb < 16*1024, "stopped reading early (" . ($kb // '?') . " kb)")
    or diag($out);

($status, $out) = amrecover('z_big');
is($status, 0, "amrecover of the last file exits 0") or diag($out);
ok(-s "$rest/z_big" == 64*1024*1024, "z_big restored completely");
is(system("cmp -s '$src/z_big' '$rest/z_big'"), 0, "z_big content matches");

rmtree($src);
rmtree($rest);
unlink($compress);
Installcheck::Run::cleanup();
