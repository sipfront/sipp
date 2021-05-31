#!/bin/sh

S="/home/admin/sipp/opensipit/stirshaken/300-orig-dest-tn-missing.xml"

T=$1
if [ -z "$T" ]; then
    echo "Usage: $0 <target-uri>"
    exit 1
fi
SIPP="/home/admin/sipp/sipp"
RUNNER="/home/admin/helpers/run-invite.sh"

"$RUNNER" "$SIPP" "$S" "$T" "/home/admin/helpers/create-jwt.pl --keypath=/home/admin/helpers/certs/sp/priv.pem --autoiat --autoorigid --x5u=http://sipp.opensipit.sipfront.org/cert.pem --attest=B"
