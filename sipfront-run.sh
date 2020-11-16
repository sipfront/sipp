#!/bin/bash

PATH="/bin:/usr/bin"

########################################################################
# global configuration
########################################################################

INSTANCE_UUID=$(uuidgen);
CREDENTIALS_CALLER_FILE="/etc/sipfront-credentials/caller.csv"
CREDENTIALS_CALLEE_FILE="/etc/sipfront-credentials/callee.csv"
STATS_ROLE="caller" # will be overridden by action

function send_trigger() {
    token="$1"
    status="$2"
    task_arn="$3"
    state_index="$4"

    if [ -n "$token" ]; then
        aws stepfunctions send-task-success --task-token "$token" \
            --task-output "{\"status\":{\"gate\":\"$status\"}, \"taskarn\":\"$task_arn\", \"state_index\":\"$state_index\"}" --region eu-central-1
    fi
}


########################################################################
# fetch credentials from AWS
########################################################################

CONTAINER_METADATA=$(curl -f ${ECS_CONTAINER_METADATA_URI_V4}/task)
echo $CONTAINER_METADATA
if [ "$?" -ne "0" ]; then
    echo "Failed to fetch container metadata, aborting"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
TASK_ARN=$(echo $CONTAINER_METADATA | jq -r '.TaskARN')

secret=$(aws secretsmanager get-secret-value --secret-id "mqtt-sipp-stats-credentials" --region eu-central-1 | jq -r '.SecretString')
if [ "$secret" = "null" ]; then
    echo "Failed to fetch MQTT credentials from AWS SecretsManager, missing SecretString attribute, aborting"
    exit 1
fi

SM_MQTT_USER=$(echo $secret | jq -r '.username')
SM_MQTT_PASS=$(echo $secret | jq -r '.password')
SM_MQTT_HOST=$(echo $secret | jq -r '.host')
SM_MQTT_PORT=$(echo $secret | jq -r '.port')
SM_MQTT_TOPICBASE=$(echo $secret | jq -r '.topicbase')

########################################################################
# system specific checks
########################################################################

# from debian package "ca-certificates"
AWS_CA_FILE="/usr/share/ca-certificates/mozilla/Amazon_Root_CA_1.crt"

if ! [ -e "$AWS_CA_FILE" ]; then
    echo "Missing AWS CA file '$AWS_CA_FILE', aborting"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
MQTT_CA_FILE="-mqtt_ca_file $AWS_CA_FILE"

########################################################################
# mqtt specific checks, so we can publish state
########################################################################

if [ -z "$SFC_STATE_INDEX" ]; then
    echo "Missing env SFC_STATE_INDEX, aborting"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
STATE_INDEX="$SFC_STATE_INDEX"

if [ -z "$SFC_SESSION_UUID" ]; then
    echo "Missing env SFC_SESSION_UUID, aborting"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
SESSION_UUID="$SFC_SESSION_UUID"

if [ -z "$SM_MQTT_HOST" ]; then
    echo "Missing env SM_MQTT_HOST, aborting"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
MQTT_HOST="-mqtt_host $SM_MQTT_HOST"

if [ -z "$SM_MQTT_PORT" ]; then
    echo "Missing env SM_MQTT_PORT, aborting"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
MQTT_PORT="-mqtt_port $SM_MQTT_PORT"

if [ -z "$SM_MQTT_USER" ]; then
    echo "Missing env SM_MQTT_USER, aborting"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
MQTT_USER="-mqtt_user $SM_MQTT_USER"

if [ -z "$SM_MQTT_PASS" ]; then
    echo "Missing env SM_MQTT_PASS, aborting"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
MQTT_PASS="-mqtt_pass $SM_MQTT_PASS"

if [ -z "$SM_MQTT_TOPICBASE" ]; then
    echo "Missing env SM_MQTT_TOPICBASE, aborting"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi

function publish_mqtt() {
    topic="$1"
    role="$2"
    message="$3"

    mosquitto_pub \
        -i "sipp_${SESSION_UUID}_${INSTANCE_UUID}" \
        -q 1 \
        -t "$SM_MQTT_TOPICBASE/${SESSION_UUID}/$topic/$role/${INSTANCE_UUID}" \
        -h "$SM_MQTT_HOST" -p "$SM_MQTT_PORT" \
        -m "$message" \
        --cafile "$AWS_CA_FILE" \
        -u "$SM_MQTT_USER" -P "$SM_MQTT_PASS";
}

publish_mqtt "status" "$STATS_ROLE" "infra_container_started"

########################################################################
# scenario specific checks
########################################################################

if [ -z "$SFC_TARGET_HOST" ]; then
    echo "Missing env SFC_TARGET_HOST, aborting"
    publish_mqtt "status" "$STATS_ROLE" "infra_container_failed_env"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
TARGET_HOST="$SFC_TARGET_HOST"

if [ -z "$SFC_TARGET_PORT" ]; then
    echo "Missing env SFC_TARGET_PORT, aborting"
    publish_mqtt "status" "$STATS_ROLE" "infra_container_failed_env"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
TARGET_PORT="$SFC_TARGET_PORT"

if [ -z "$SFC_TARGET_PROTO" ]; then
    echo "Missing env SFC_TARGET_PROTO, aborting"
    publish_mqtt "status" "$STATS_ROLE" "infra_container_failed_env"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
TARGET_PROTO=$(echo "$SFC_TARGET_PROTO" | tr '[:upper:]' '[:lower:]')


if [ -z "$SFC_SIPFRONT_API" ]; then
    echo "Missing env SFC_SIPFRONT_API, aborting"
    publish_mqtt "status" "$STATS_ROLE" "infra_container_failed_env"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
SIPFRONT_API="$SFC_SIPFRONT_API"

if [ -z "$SFC_SIPFRONT_API_TOKEN" ]; then
    echo "Missing env SFC_SIPFRONT_API_TOKEN, aborting"
    publish_mqtt "status" "$STATS_ROLE" "infra_container_failed_env"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
SIPFRONT_API_TOKEN="$SFC_SIPFRONT_API_TOKEN"

var="S_${STATE_INDEX}_SFC_ACTIONS"
SFC_ACTIONS=${!var}
if [ -z "$SFC_ACTIONS" ]; then
    echo "Missing env $var, aborting"
    publish_mqtt "status" "$STATS_ROLE" "infra_container_failed_env"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
if [ "$SFC_ACTIONS" -lt "1" ]; then
    echo "Invalid env $var, must be >= 1"
    publish_mqtt "status" "$STATS_ROLE" "infra_container_failed_env"
    send_trigger "$AWS_TASK_TOKEN" "failed"
    exit 1
fi
ACTIONS="$SFC_ACTIONS"

OUTBOUND_PROXY=""
if [ -n "$SFC_OUTBOUND_HOST" ]; then
    if [ -z "$SFC_OUTBOUND_PORT" ]; then
        OUTBOUND_PORT="5060"
    else
        OUTBOUND_PORT="$SFC_OUTBOUND_PORT"
    fi
    OUTBOUND_PROXY="-rsa ${SFC_OUTBOUND_HOST}:${$SFC_OUTBOUND_PORT}"
fi

TRANSPORT_PROTO="$TARGET_PROTO"
if [ -n "$OUTBOUND_PROXY" ] && [ -n "$SFC_OUTBOUND_PROTO" ]; then
    TRANSPORT_PROTO=$(echo "$SFC_OUTBOUND_PROTO" | tr '[:upper:]' '[:lower:]')
fi

LOCAL_PORT="5060"
TRANSPORT_MODE=""
case "$TRANSPORT_PROTO" in
    "udp")
        TRANSPORT_MODE="-t u1"
        ;;
    "tcp")
        #TRANSPORT_MODE="-t tn -max_socket 1024"
        TRANSPORT_MODE="-t t1 -max_socket 1024"
        ;;
    "tls")
        ;;
    *)
        ;;
esac

########################################################################
# action specific checks
########################################################################

# env vars are in format A_${action_idx}_SFC_VARNAME, e.g. A_0_CALL_RATE

for i in $( seq 0 $((ACTIONS-1)) ); do

    for v in SFC_TRIGGER_STEP SFC_SCENARIO SFC_STATS_ROLE \
             SFC_CREDENTIALS_CALLER SFC_CREDENTIALS_CALLEE \
             SFC_PERF_TEST_DURATION SFC_PERF_MAX_TOTAL_CALLS \
             SFC_PERF_CALL_DURATION SFC_PERF_CAPS SFC_PERF_CC \
             SFC_TRIGGER_READY SFC_TRIGGER_QUIT \
             SFC_PERF_REGEXPIRE SFC_STATS_ROLE; do

        var="S_${STATE_INDEX}_A_${i}_${v}";
        declare "${v}"="${!var}";
        echo "$var = ${v} = ${!var}"
    done

    if [ -z "$SFC_SCENARIO" ]; then
        echo "Missing env A_${i}_SFC_SCENARIO, aborting"
        publish_mqtt "status" "$STATS_ROLE" "infra_container_failed_env"
        send_trigger "$AWS_TASK_TOKEN" "failed"
        exit 1
    fi
    SCENARIO="$SFC_SCENARIO"
    SCENARIO_FILE="/etc/sipfront-scenarios/${SCENARIO}.xml"

    CALL_RATE=""
    if ! [ -z "$SFC_PERF_CAPS" ]; then
        CALL_RATE="-r $SFC_PERF_CAPS"
    fi

    CONCURRENT_CALLS=1000000
    if ! [ -z "$SFC_PERF_CC" ]; then
        CONCURRENT_CALLS="$SFC_PERF_CC"
    fi

    TEST_DURATION=43200
    if ! [ -z "$SFC_PERF_TEST_DURATION" ]; then
        TEST_DURATION="$SFC_PERF_TEST_DURATION"
    fi

    MAX_TOTAL_CALLS=10000000
    if ! [ -z "$SFC_PERF_MAX_TOTAL_CALLS" ]; then
        MAX_TOTAL_CALLS="$SFC_PERF_MAX_TOTAL_CALLS"
    fi

    CALL_DURATION=""
    if ! [ -z "$SFC_PERF_CALL_DURATION" ]; then
        CALL_DURATION="-d ${SFC_PERF_CALL_DURATION}000"
    fi

    REGISTRATION_EXPIRE=0
    if ! [ -z "$SFC_PERF_REGEXPIRE" ]; then
        REGISTRATION_EXPIRE="$SFC_PERF_REGEXPIRE"
    fi

    STATS_ROLE="caller"
    if ! [ -z "$SFC_STATS_ROLE" ]; then
        STATS_ROLE="$SFC_STATS_ROLE"
    fi

    CREDENTIALS_CALLER="$SFC_CREDENTIALS_CALLER"
    CREDENTIALS_CALLEE="$SFC_CREDENTIALS_CALLEE"

    URL="${SIPFRONT_API}/scenarios/?name=${SCENARIO}"
    echo "Fetching scenario '$URL' to '$SCENARIO_FILE'"
    curl -f -H 'Accept: application/xml' -H "Authorization: Bearer $SIPFRONT_API_TOKEN" "$URL" -o "$SCENARIO_FILE"

    if [ "$CREDENTIALS_CALLER" = "1" ] && ! [ -f "$CREDENTIALS_CALLER_FILE" ]; then
        URL="${SIPFRONT_API}/internal/sessions/${SESSION_UUID}/credentials/caller"
        echo "Fetching caller credentials from '$URL' to '$CREDENTIALS_CALLER_FILE'"
        curl -f -H 'Accept: text/csv' -H "Authorization: Bearer $SIPFRONT_API_TOKEN" "$URL" -o "$CREDENTIALS_CALLER_FILE"
        if [ $? -ne 0 ]; then
            echo "Failed to fetch caller credentials from api, aborting..."
            publish_mqtt "status" "$STATS_ROLE" "infra_container_failed_creds"
            send_trigger "$AWS_TASK_TOKEN" "failed"
            exit 1
        fi
    fi

    if [ "$CREDENTIALS_CALLEE" = "1" ] && ! [ -f "$CREDENTIALS_CALLEE_FILE" ]; then
        URL="${SIPFRONT_API}/internal/sessions/${SESSION_UUID}/credentials/callee"

        echo "Fetching callee credentials from '$URL' to '$CREDENTIALS_CALLEE_FILE'"
        curl -f -H 'Accept: text/csv' -H "Authorization: Bearer $SIPFRONT_API_TOKEN" "$URL" -o "$CREDENTIALS_CALLEE_FILE"
        if [ $? -ne 0 ]; then
            echo "Failed to fetch callee credentials from api, aborting..."
            publish_mqtt "status" "$STATS_ROLE" "infra_container_failed_creds"
            send_trigger "$AWS_TASK_TOKEN" "failed"
            exit 1
        fi
    fi

    CREDENTIAL_PARAMS=""
    if [ -e "$CREDENTIALS_CALLER_FILE" ]; then
        CREDENTIAL_PARAMS="$CREDENTIAL_PARAMS -inf $CREDENTIALS_CALLER_FILE"
    fi
    if [ -e "$CREDENTIALS_CALLEE_FILE" ]; then
        CREDENTIAL_PARAMS="$CREDENTIAL_PARAMS -inf $CREDENTIALS_CALLEE_FILE"
    fi


    # -nd -default_behaviors: no defaults, but abort on unexpected message
    # -aa: auto-answer 200 for INFO, NOTIFY, OPTIONS, UPDATE \
    # -l: max concurrent calls
    # -rtt_freq: send rtt every $x calls, so set to call rate to get per sec

    # -lost: number of packets to lose per default

    # -key: set "keyword" to value
    # -m: exit after -m calls are processed
    # -users: start with -users concurrent calls and keep it constant

    # -t: transport mode

    BEHAVIOR="-nd"

    echo "Starting sipp"
    ulimit -c unlimited

    publish_mqtt "status" "$STATS_ROLE" "infra_container_start_sipp"

    echo "Checkin for ready-trigger"
    if [ "$SFC_TRIGGER_READY" -eq "1" ]; then
        echo "Triggering ready state so consumers can start drawing"
        send_trigger "$AWS_TASK_TOKEN" "$trigger_state" "$TASK_ARN" "$((STATE_INDEX+1))"
        publish_mqtt "status" "$STATS_ROLE" "infra_action_ready"
    fi

    echo timeout -s SIGUSR1 -k 300 "${TEST_DURATION}s" sipp \
        $BEHAVIOR -l "$CONCURRENT_CALLS" \
        -m "$MAX_TOTAL_CALLS" \
        -aa $CALL_DURATION $TRANSPORT_MODE \
        -cid_str "sipfront-${SESSION_UUID}-%u-%p@%s" \
        -base_cseq 1 \
        -trace_stat -fd 1 \
        -mqtt_stats 1 \
        -mqtt_stats_topic "${SM_MQTT_TOPICBASE}/${SESSION_UUID}/call/${STATS_ROLE}/${INSTANCE_UUID}" \
        -mqtt_rttstats_topic "${SM_MQTT_TOPICBASE}/${SESSION_UUID}/rtt/${STATS_ROLE}/${INSTANCE_UUID}" \
        -mqtt_countstats_topic "${SM_MQTT_TOPICBASE}/${SESSION_UUID}/count/${STATS_ROLE}/${INSTANCE_UUID}" \
        -mqtt_codestats_topic "${SM_MQTT_TOPICBASE}/${SESSION_UUID}/code/${STATS_ROLE}/${INSTANCE_UUID}" \
        -mqtt_ctrl 1 -mqtt_ctrl_topic "${SM_MQTT_TOPICBASE}/${SESSION_UUID}/ctrl/#" \
        $MQTT_HOST $MQTT_PORT $MQTT_USER $MQTT_PASS $MQTT_CA_FILE \
        -trace_err $CALL_RATE \
        -sf $SCENARIO_FILE $CREDENTIAL_PARAMS \
        -p $LOCAL_PORT \
        "$TARGET_HOST:$TARGET_PORT"

    timeout -s SIGUSR1 -k 300 "${TEST_DURATION}s" sipp \
        $BEHAVIOR -l "$CONCURRENT_CALLS" \
        -m "$MAX_TOTAL_CALLS" \
        -aa $CALL_DURATION $TRANSPORT_MODE \
        -cid_str "sipfront-${SESSION_UUID}-%u-%p@%s" \
        -base_cseq 1 \
        -trace_stat -fd 1 \
        -mqtt_stats 1 \
        -mqtt_stats_topic "${SM_MQTT_TOPICBASE}/${SESSION_UUID}/call/${STATS_ROLE}/${INSTANCE_UUID}" \
        -mqtt_rttstats_topic "${SM_MQTT_TOPICBASE}/${SESSION_UUID}/rtt/${STATS_ROLE}/${INSTANCE_UUID}" \
        -mqtt_countstats_topic "${SM_MQTT_TOPICBASE}/${SESSION_UUID}/count/${STATS_ROLE}/${INSTANCE_UUID}" \
        -mqtt_codestats_topic "${SM_MQTT_TOPICBASE}/${SESSION_UUID}/code/${STATS_ROLE}/${INSTANCE_UUID}" \
        -mqtt_ctrl 1 -mqtt_ctrl_topic "${SM_MQTT_TOPICBASE}/${SESSION_UUID}/ctrl/#" \
        $MQTT_HOST $MQTT_PORT $MQTT_USER $MQTT_PASS $MQTT_CA_FILE \
        -trace_err $CALL_RATE \
        -sf $SCENARIO_FILE $CREDENTIAL_PARAMS \
        -p $LOCAL_PORT \
        "$TARGET_HOST:$TARGET_PORT"

    sipp_ret="$?"

    # if we get killed by "timeout -k" after unsuccessful shutdown, we have no exit code
    if [ -z "$sipp_ret" ]; then
        sipp_ret=1
    fi

    echo "Sipp finished, exit code is '$sipp_ret'"

    cat /*errors.log
    if ls /core.* 1>/dev/null 2>/dev/null; then
        CF="/gdb.txt"
        echo "bt" > $CF
        echo "quit" >> $CF
        gdb -x $CF /bin/sipp /core.*
    fi

    if [ $sipp_ret -eq 0 ]; then
        publish_mqtt "status" "$STATS_ROLE" "infra_action_normal_done"
        trigger_state="passed"
    elif [ $sipp_ret -eq 1 ]; then
        publish_mqtt "status" "$STATS_ROLE" "infra_action_hardtimeout_done"
        trigger_state="passed"
    elif [ $sipp_ret -eq 124 ]; then
        publish_mqtt "status" "$STATS_ROLE" "infra_action_timeout_done"
        trigger_state="passed"
    else
        publish_mqtt "status" "$STATS_ROLE" "infra_action_failed_done"
        trigger_state="failed"
    fi
    rm -f /*errors.log /core*

    echo "Checkin for AWS task token"
    if [ "$SFC_TRIGGER_STEP" -eq "1" ]; then
        echo "Triggering AWS step function task change"
        send_trigger "$AWS_TASK_TOKEN" "$trigger_state" "$TASK_ARN" "$((STATE_INDEX+1))"
    fi

    echo "Checkin for quit-trigger"
    if [ "$SFC_TRIGGER_QUIT" -eq "1" ]; then
        echo "Triggering quit state to shutdown all other sipp callee instances"
        
        publish_mqtt "ctrl" "callee" "q"
    fi

done

# publish last exit status
if [ $sipp_ret -eq 0 ]; then
    publish_mqtt "status" "$STATS_ROLE" "infra_container_normal_done"
elif [ $sipp_ret -eq 124 ]; then
    publish_mqtt "status" "$STATS_ROLE" "infra_container_timeout_done"
else
    publish_mqtt "status" "$STATS_ROLE" "infra_container_failed_done"
fi

echo "Task finished"
