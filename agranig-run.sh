#!/bin/bash

source mqtt-auth.conf
#UUID=$(uuidgen)
UUID='ecabb78c-5900-4c38-a63b-4e54ecadf642'

./sipp \
    -trace_stat -fd 3 \
    -trace_rtt -rtt_freq 100 \
    -mqtt_stats 1 -mqtt_stats_topic "/sipp/stats/$UUID/call" -mqtt_rttstats_topic "/sipp/stats/$UUID/rtt" \
    -mqtt_ctrl 1 -mqtt_ctrl_topic '/sipp/ctrl' \
    $MQTT_AUTH \
    -sn uac -r 0 \
    -trace_err \
    127.0.0.1
