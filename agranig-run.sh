#!/bin/sh

./sipp -trace_stat -fd 3 -trace_rtt -rtt_freq 10 -mqtt_stats 1 -mqtt_stats_topic '/sipp/stats' -mqtt_ctrl 1 -mqtt_ctrl_topic '/sipp/ctrl' -sn uac -r 1 -trace_err 127.0.0.1
