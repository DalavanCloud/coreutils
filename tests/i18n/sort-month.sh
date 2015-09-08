#!/bin/sh
# Verify sort -M multi-byte support.

. "${srcdir=.}/tests/init.sh"; path_prepend_ ./src
print_ver_ sort
require_valgrind_

# Skip this test if some deallocations are
# avoided at process end.
grep '^#define lint 1' $CONFIG_HEADER > /dev/null ||
  skip_ 'Allocation checks only work reliably in "lint" mode'

export LC_ALL=en_US.UTF-8
locale -k LC_CTYPE | grep -q "charmap.*UTF-8" \
  || skip_ "No UTF-8 locale available"

cat <<EOF > exp
.
a
EOF


# check large mem leak with --month-sort
# https://bugzilla.redhat.com/show_bug.cgi?id=1259942
valgrind --leak-check=full \
         --error-exitcode=1 --errors-for-leak-kinds=definite \
         sort -M < exp > out || fail=1
compare exp out || { fail=1; cat out; }


Exit $fail
