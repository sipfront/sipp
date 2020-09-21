#!/bin/sh

set -e

aws ecr get-login-password --region eu-central-1 --profile Administrator | \
    docker login \
    --username AWS \
    --password-stdin \
    138885365874.dkr.ecr.eu-central-1.amazonaws.com

docker build -f docker/Dockerfile.sipfront -t sipfront-sipp .

docker tag sipfront-sipp:latest \
    138885365874.dkr.ecr.eu-central-1.amazonaws.com/sipfront-sipp:latest

docker push \
    138885365874.dkr.ecr.eu-central-1.amazonaws.com/sipfront-sipp:latest
