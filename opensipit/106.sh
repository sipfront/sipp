#!/bin/sh

S="/home/admin/sipp/opensipit/stirshaken/106-iat-negative.xml"

T=$1
if [ -z "$T" ]; then
    echo "Usage: $0 <target-uri>"
    exit 1
fi
SIPP="/home/admin/sipp/sipp"
RUNNER="/home/admin/helpers/run-invite.sh"

iat="-$(date +%s)"

"$RUNNER" "$SIPP" "$S" "$T" "/home/admin/helpers/create-jwt.pl --keypath=/home/admin/helpers/certs/sp/priv.pem --iat=$iat --autoorigid --x5u=http://sipp.opensipit.sipfront.org/cert.pem --origtn=439991001 --desttn=439991002 --attest=B"
