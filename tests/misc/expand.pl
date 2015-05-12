#!/usr/bin/perl
# Exercise expand.

# Copyright (C) 2004-2015 Free Software Foundation, Inc.

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

# Turn off localization of executable's output.
@ENV{qw(LANGUAGE LANG LC_ALL)} = ('C') x 3;

#comment out next line to disable multibyte tests
my $mb_locale = $ENV{LOCALE_FR_UTF8};
! defined $mb_locale || $mb_locale eq 'none'
 and $mb_locale = 'C';

my $prog = 'expand';
my $try = "Try \`$prog --help' for more information.\n";
my $inval = "$prog: invalid byte, character or field list\n$try";

my @Tests =
  (
   ['t1', '--tabs=3',     {IN=>"a\tb"}, {OUT=>"a  b"}],
   ['t2', '--tabs=3,6,9', {IN=>"a\tb\tc\td\te"}, {OUT=>"a  b  c  d e"}],
   ['i1', '--tabs=3 -i', {IN=>"\ta\tb"}, {OUT=>"   a\tb"}],
   ['i2', '--tabs=3 -i', {IN=>" \ta\tb"}, {OUT=>"   a\tb"}],
  );

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

        # Depending on whether expand is multi-byte-patched,
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

my $save_temps = $ENV{DEBUG};
my $verbose = $ENV{VERBOSE};

my $prog = 'expand';
my $fail = run_tests ($program_name, $prog, \@Tests, $save_temps, $verbose);
exit $fail;
