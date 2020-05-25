#!/bin/sh

./sipp -trace_stat -fd 3 -trace_rtt -rtt_freq 100 -mqtt_stats 1 -mqtt_stats_topic '/sipp/stats/call' -mqtt_rttstats_topic '/sipp/stats/rtt' -mqtt_ctrl 1 -mqtt_ctrl_topic '/sipp/ctrl' -sn uac -r 0 -trace_err 127.0.0.1
