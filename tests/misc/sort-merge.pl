#!/usr/bin/perl
# Test "sort -m".

# Copyright (C) 2002-2015 Free Software Foundation, Inc.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

use strict;

my $limits = getlimits ();

(my $program_name = $0) =~ s|.*/||;
my $prog = 'sort';

# Turn off localization of executable's output.
@ENV{qw(LANGUAGE LANG LC_ALL)} = ('C') x 3;

my $mb_locale;
# uncommented according to upstream commit enabling multibyte paths
$mb_locale = $ENV{LOCALE_FR_UTF8};
! defined $mb_locale || $mb_locale eq 'none'
 and $mb_locale = 'C';

my $try = "Try \`$prog --help' for more information.\n";
my $inval = "$prog: invalid byte, character or field list\n$try";

# three empty files and one that says 'foo'
my @inputs = (+(map{{IN=> {"empty$_"=> ''}}}1..3), {IN=> {foo=> "foo\n"}});

my $big_input = "aaa\n" x 1024;

# don't need to check for existence, since we're running in a temp dir
my $badtmp = 'does/not/exist';

my $bigint = $limits->{UINTMAX_OFLOW};

my @Tests =
    (
     ['m1', '-m', @inputs, {OUT=>"foo\n"}],

     # check validation of --batch-size option
     ['nmerge-0', "-m --batch-size=0", @inputs,
        {ERR=>"$prog: invalid --batch-size argument '0'\n".
              "$prog: minimum --batch-size argument is '2'\n"}, {EXIT=>2}],

     ['nmerge-1', "-m --batch-size=1", @inputs,
        {ERR=>"$prog: invalid --batch-size argument '1'\n".
              "$prog: minimum --batch-size argument is '2'\n"}, {EXIT=>2}],

     ['nmerge-neg', "-m --batch-size=-1", @inputs,
        {ERR=>"$prog: invalid --batch-size argument '-1'\n"}, {EXIT=>2}],

     ['nmerge-nan', "-m --batch-size=a", @inputs,
        {ERR=>"$prog: invalid --batch-size argument 'a'\n"}, {EXIT=>2}],

     ['nmerge-big', "-m --batch-size=$bigint", @inputs,
        {ERR_SUBST=>'s/(current rlimit is) \d+/$1/'},
        {ERR=>"$prog: --batch-size argument '$bigint' too large\n".
              "$prog: maximum --batch-size argument with current rlimit is\n"},
        {EXIT=>2}],

     # This should work since nmerge >= the number of input files
     ['nmerge-yes', "-m --batch-size=4 -T$badtmp", @inputs, {OUT=>"foo\n"}],

     # this should fail since nmerge < # of input files, so
     # temp files are needed
     ['nmerge-no', "-m --batch-size=2 -T$badtmp", @inputs,
        {ERR_SUBST=>"s|': .*|':|"},
        {ERR=>"$prog: cannot create temporary file in '$badtmp':\n"},
        {EXIT=>2}],

     # This used to fail because setting batch-size without also setting
     # buffer size would cause the buffer size to be set to the minimum.
     ['batch-size', "--batch-size=16 -T$badtmp", {IN=> {big=> $big_input}},
        {OUT=>$big_input}],
    );

# Add _POSIX2_VERSION=199209 to the environment of each test
# that uses an old-style option like +1.
if ($mb_locale ne 'C')
  {
    # Duplicate each test vector, appending "-mb" to the test name and
    # inserting {ENV => "LC_ALL=$mb_locale"} in the copy, so that we
    # provide coverage for the distro-added multi-byte code paths.
    my @new;
    foreach my $t (@Tests)
      {
        my @new_t = @$t;
        my $test_name = shift @new_t;

        # Depending on whether sort is multi-byte-patched,
        # it emits different diagnostics:
        #   non-MB: invalid byte or field list
        #   MB:     invalid byte, character or field list
        # Adjust the expected error output accordingly.
        if (grep {ref $_ eq 'HASH' && exists $_->{ERR} && $_->{ERR} eq $inval}
            (@new_t))
          {
            my $sub = {ERR_SUBST => 's/, character//'};
            push @new_t, $sub;
            push @$t, $sub;
          }
        next if ($test_name =~ "nmerge-.");
        push @new, ["$test_name-mb", @new_t, {ENV => "LC_ALL=$mb_locale"}];
      }
    push @Tests, @new;
  }

@Tests = triple_test \@Tests;

my $save_temps = $ENV{DEBUG};
my $verbose = $ENV{VERBOSE};

my $fail = run_tests ($program_name, $prog, \@Tests, $save_temps, $verbose);
exit $fail;
