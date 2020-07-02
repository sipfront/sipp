#!/bin/bash

source mqtt-auth.conf
#UUID=$(uuidgen)
UUID='ecabb78c-5900-4c38-a63b-4e54ecadf642'

./sipp \
    -nd -default_behaviors abortunexp \
    -trace_stat -fd 1 \
    -trace_rtt -rtt_freq 1 \
    -mqtt_stats 1 -mqtt_stats_topic "/sipp/stats/$UUID/call" -mqtt_rttstats_topic "/sipp/stats/$UUID/rtt" \
    -mqtt_ctrl 1 -mqtt_ctrl_topic '/sipp/ctrl' \
    $MQTT_AUTH \
    -trace_err \
    -r 1 \
    -sf ~/sipfront-scenarios/uac-register.xml \
    -inf ~/sipfront-scenarios/caller-random.csv \
    35.158.106.202
