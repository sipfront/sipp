#!/bin/bash

set -e

PATH="/bin:/usr/bin"

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

INSTANCE_UUID=$(uuidgen);

# from debian package "ca-certificates"
AWS_CA_FILE="/usr/share/ca-certificates/mozilla/Amazon_Root_CA_1.crt"

if ! [ -e "$AWS_CA_FILE" ]; then
    echo "Missing AWS CA file '$AWS_CA_FILE', aborting"
    exit 1
fi
MQTT_CA_FILE="-mqtt_ca_file $AWS_CA_FILE"


if [ -z "$SFC_CREDENTIALS_API" ]; then
    echo "Missing env SFC_CREDENTIALS_API, aborting"
    exit 1
fi
CREDENTIALS_API="$SFC_CREDENTIALS_API"

if [ -z "$SFC_SESSION_UUID" ]; then
    echo "Missing env SFC_SESSION_UUID, aborting"
    exit 1
fi
SESSION_UUID="$SFC_SESSION_UUID"

if [ -z "$SFC_TARGET" ]; then
    echo "Missing env SFC_TARGET, aborting"
    exit 1
fi
TARGET="$SFC_TARGET"

if [ -z "$SM_MQTT_HOST" ]; then
    echo "Missing env SM_MQTT_HOST, aborting"
    exit 1
fi
MQTT_HOST="-mqtt_host $SM_MQTT_HOST"

if [ -z "$SM_MQTT_PORT" ]; then
    echo "Missing env SM_MQTT_PORT, aborting"
    exit 1
fi
MQTT_PORT="-mqtt_port $SM_MQTT_PORT"

if [ -z "$SM_MQTT_USER" ]; then
    echo "Missing env SM_MQTT_USER, aborting"
    exit 1
fi
MQTT_USER="-mqtt_user $SM_MQTT_USER"

if [ -z "$SM_MQTT_PASS" ]; then
    echo "Missing env SM_MQTT_PASS, aborting"
    exit 1
fi
MQTT_PASS="-mqtt_pass $SM_MQTT_PASS"

if [ -z "$SM_MQTT_TOPICBASE" ]; then
    echo "Missing env SM_MQTT_TOPICBASE, aborting"
    exit 1
fi

if [ -z "$SFC_SCENARIO" ]; then
    echo "Missing env SFC_SCENARIO, aborting"
    exit 1
fi
SCENARIO="/etc/sipfront-scenarios/$SFC_SCENARIO"

CREDENTIALS_CALLER="$SFC_CREDENTIALS_CALLER"
CREDENTIALS_CALLER_SEQ="$SFC_CREDENTIALS_CALLER_SEQ"

CREDENTIALS_CALLEE="$SFC_CREDENTIALS_CALLEE"
CREDENTIALS_CALLEE_SEQ="$SFC_CREDENTIALS_CALLEE_SEQ"

CREDENTIALS_CALLER_FILE="/etc/sipfront-credentials/caller.csv"
CREDENTIALS_CALLEE_FILE="/etc/sipfront-credentials/callee.csv"

if [ "$CREDENTIALS_CALLER" = "1" ]; then
    PARAMS=""
    if [ -n "$CREDENTIALS_CALLER_SEQ" ]; then
        PARAMS="force_seq=$CREDENTIALS_CALLER_SEQ"
    fi

    URL="${CREDENTIALS_API}/internal/sessions/${SESSION_UUID}/credentials?${PARAMS}"
    echo "Fetching caller credentials from '$URL' to '$CREDENTIALS_CALLER_FILE'"
    curl -H 'Accept: text/csv' "$URL" -o "$CREDENTIALS_CALLER_FILE"
fi

if [ "$CREDENTIALS_CALLEE" = "1" ]; then
    PARAMS=""
    if [ -n "$CREDENTIALS_CALLEE_SEQ" ]; then
        PARAMS="force_seq=$CREDENTIALS_CALLEE_SEQ"
    fi
    URL="${CREDENTIALS_API}/internal/sessions/${SESSION_UUID}/credentials?${PARAMS}"

    echo "Fetching callee credentials from '$URL' to '$CREDENTIALS_CALLEE_FILE'"
    curl -H 'Accept: text/csv' "$URL" -o "$CREDENTIALS_CALLEE_FILE"
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

BEHAVIOR="-nd"

echo "Starting sipp"
sipp \
    $BEHAVIOR \
    -aa \
    -cid_str 'sipfront-%u-%p@%s' \
    -base_cseq 1 \
    -trace_stat -fd 1 \
    -trace_rtt -rtt_freq 1 \
    -mqtt_stats 1 \
    -mqtt_stats_topic "${SM_MQTT_TOPICBASE}/${SESSION_UUID}/call/${INSTANCE_UUID}" \
    -mqtt_rttstats_topic "${SM_MQTT_TOPICBASE}/${SESSION_UUID}/rtt/${INSTANCE_UUID}" \
    -mqtt_ctrl 1 -mqtt_ctrl_topic "/sipp/ctrl/$SESSION_UUID" \
    $MQTT_HOST $MQTT_PORT $MQTT_USER $MQTT_PASS $MQTT_CA_FILE \
    -trace_err \
    -r 1 \
    -sf $SCENARIO $CREDENTIAL_PARAMS \
    $TARGET

