#!/bin/sh

./sipp -nd -default_behaviors none,abortunexp \
    -aa -base_cseq 1 -fd 1 \
    -timeout '60s' -l '50' \
    -m '100000' -r '1' -d '1' \
    -trace_err \
    -trace_msg \
    -trace_logs \
    -sf 'playground/uac_curl.xml' -inf 'playground/caller.csv' -inf 'playground/callee.csv' \
    -key target_uri 'sip:c5.dev.sipfront.com:5060;transport=udp' \
    -t u1 c5.dev.sipfront.com:5060
