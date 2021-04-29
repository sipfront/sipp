#!/bin/sh

INSTANCE=$(uuidgen)

echo $INSTANCE > /tmp/sipp-instance.txt

./sipp -nd -default_behaviors none,abortunexp \
    -aa -base_cseq 1 -trace_stat -fd 1 \
    -mqtt_stats 1 -mqtt_ctrl 1 \
    -p 5062 -timeout '60s' -l '50' \
    -m '100000' -r '5' -d '10000' \
    -cid_str 'sipfront-fc6b56aa-a8b5-11eb-a711-925a8cf8733b-%u-%p@%s' \
    -mqtt_stats_topic "com/sipfront/agentpool/3d58eae5-3802-4a24-b905-a2a010f914e4/stats/fc6b56aa-a8b5-11eb-a711-925a8cf8733b/call/caller/$INSTANCE" \
    -mqtt_rttstats_topic "com/sipfront/agentpool/3d58eae5-3802-4a24-b905-a2a010f914e4/stats/fc6b56aa-a8b5-11eb-a711-925a8cf8733b/rtt/caller/$INSTANCE" \
    -mqtt_countstats_topic "com/sipfront/agentpool/3d58eae5-3802-4a24-b905-a2a010f914e4/stats/fc6b56aa-a8b5-11eb-a711-925a8cf8733b/count/caller/$INSTANCE" \
    -mqtt_codestats_topic "com/sipfront/agentpool/3d58eae5-3802-4a24-b905-a2a010f914e4/stats/fc6b56aa-a8b5-11eb-a711-925a8cf8733b/code/caller/$INSTANCE" \
    -trace_err -mqtt_error_topic "com/sipfront/agentpool/3d58eae5-3802-4a24-b905-a2a010f914e4/stats/fc6b56aa-a8b5-11eb-a711-925a8cf8733b/errors/caller/$INSTANCE" \
    -trace_msg -mqtt_message_topic "com/sipfront/agentpool/3d58eae5-3802-4a24-b905-a2a010f914e4/stats/fc6b56aa-a8b5-11eb-a711-925a8cf8733b/messages/caller/$INSTANCE" \
    -mqtt_ctrl_topic 'com/sipfront/agentpool/3d58eae5-3802-4a24-b905-a2a010f914e4/local/fc6b56aa-a8b5-11eb-a711-925a8cf8733b/ctrl/#' \
    -mqtt_host 'b-82001cda-7aaf-4f5e-8da3-a5b043dcb0ff-1.mq.eu-central-1.amazonaws.com' -mqtt_port '8883' \
    -mqtt_user '3d58eae5-3802-4a24-b905-a2a010f914e4' -mqtt_pass 'EBiiB52kyX0kAwVBPWZL3cRS' \
    -mqtt_ca_file '/usr/share/ca-certificates/mozilla/Amazon_Root_CA_1.crt' \
    -sf 'playground/uac.xml' -inf 'playground/caller.csv' -inf 'playground/callee.csv' \
    -key target_uri 'sip:c5.dev.sipfront.com:5060;transport=udp' \
    -t u1 c5.dev.sipfront.com:5060

echo "Instance was $INSTANCE"
