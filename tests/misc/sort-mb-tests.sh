#!/bin/sh
# Verify sort's multi-byte support.

. "${srcdir=.}/tests/init.sh"; path_prepend_ ./src
print_ver_ sort

export LC_ALL=en_US.UTF-8
locale -k LC_CTYPE | grep -q "charmap.*UTF-8" \
  || skip_ "No UTF-8 locale available"


cat <<EOF > exp
Banana＠5
Apple＠10
Citrus＠20
Cherry＠30
EOF

cat <<EOF | sort -t ＠ -k2 -n > out || fail=1
Apple＠10
Banana＠5
Citrus＠20
Cherry＠30
EOF

compare exp out || { fail=1; cat out; }


cat <<EOF > exp
Citrus＠ＡＡ20＠＠5
Cherry＠ＡＡ30＠＠10
Apple＠ＡＡ10＠＠20
Banana＠ＡＡ5＠＠30
EOF

cat <<EOF | sort -t ＠ -k4 -n > out || fail=1
Apple＠ＡＡ10＠＠20
Banana＠ＡＡ5＠＠30
Citrus＠ＡＡ20＠＠5
Cherry＠ＡＡ30＠＠10
EOF

compare exp out || { fail=1; cat out; }

Exit $fail
