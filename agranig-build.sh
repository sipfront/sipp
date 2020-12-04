#!/bin/sh

PROFILE="granig-admin"
AWS_ID="138885365874"

set -e

aws ecr get-login-password --region eu-central-1 --profile ${PROFILE} | \
    docker login \
    --username AWS \
    --password-stdin \
    ${AWS_ID}.dkr.ecr.eu-central-1.amazonaws.com

docker build -f docker/Dockerfile.sipfront -t sipfront-sipp .

docker tag sipfront-sipp:latest \
    ${AWS_ID}.dkr.ecr.eu-central-1.amazonaws.com/sipfront-sipp:latest

docker push \
    ${AWS_ID}.dkr.ecr.eu-central-1.amazonaws.com/sipfront-sipp:latest
