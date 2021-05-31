#!/bin/sh

S="/home/admin/sipp/opensipit/stirshaken/303-origtn-not-matching.xml"

T=$1
if [ -z "$T" ]; then
    echo "Usage: $0 <target-uri>"
    exit 1
fi
SIPP="/home/admin/sipp/sipp"
RUNNER="/home/admin/helpers/run-invite.sh"

"$RUNNER" "$SIPP" "$S" "$T"
