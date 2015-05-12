#!/usr/bin/perl
# Exercise fold.

# Copyright (C) 2003-2015 Free Software Foundation, Inc.

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

(my $program_name = $0) =~ s|.*/||;

my $prog = 'fold';
my $try = "Try \`$prog --help' for more information.\n";
my $inval = "$prog: invalid byte, character or field list\n$try";

# Turn off localization of executable's output.
@ENV{qw(LANGUAGE LANG LC_ALL)} = ('C') x 3;

# uncommented to enable multibyte paths
my $mb_locale = $ENV{LOCALE_FR_UTF8};
! defined $mb_locale || $mb_locale eq 'none'
 and $mb_locale = 'C';

my @Tests =
  (
   ['s1', '-w2 -s', {IN=>"a\t"}, {OUT=>"a\n\t"}],
   ['s2', '-w4 -s', {IN=>"abcdef d\n"}, {OUT=>"abcd\nef d\n"}],
   ['s3', '-w4 -s', {IN=>"a cd fgh\n"}, {OUT=>"a \ncd \nfgh\n"}],
   ['s4', '-w4 -s', {IN=>"abc ef\n"}, {OUT=>"abc \nef\n"}],
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

        # Depending on whether fold is multi-byte-patched,
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
        push @new, ["$test_name-mb", @new_t, {ENV => "LC_ALL=$mb_locale"}];
      }
    push @Tests, @new;
  }

@Tests = triple_test \@Tests;

# Remember that triple_test creates from each test with exactly one "IN"
# file two more tests (.p and .r suffix on name) corresponding to reading
# input from a file and from a pipe.  The pipe-reading test would fail
# due to a race condition about 1 in 20 times.
# Remove the IN_PIPE version of the "output-is-input" test above.
# The others aren't susceptible because they have three inputs each.
@Tests = grep {$_->[0] ne 'output-is-input.p'} @Tests;

my $save_temps = $ENV{DEBUG};
my $verbose = $ENV{VERBOSE};

my $fail = run_tests ($program_name, $prog, \@Tests, $save_temps, $verbose);
exit $fail;
