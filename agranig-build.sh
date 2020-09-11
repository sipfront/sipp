#!/bin/sh

set -e

pushd ../sipfront-scenarios
git u
popd

test -d sipfront-scenarios && rm -rf sipfront-scenarios
cp -r ../sipfront-scenarios .

aws ecr get-login-password --region eu-central-1 --profile Administrator | \
    docker login \
    --username AWS \
    --password-stdin \
    138885365874.dkr.ecr.eu-central-1.amazonaws.com

docker build -f docker/Dockerfile.sipfront -t sipfront-sipp .

rm -rf sipfront-scenarios

docker tag sipfront-sipp:latest \
    138885365874.dkr.ecr.eu-central-1.amazonaws.com/sipfront-sipp:latest

docker push \
    138885365874.dkr.ecr.eu-central-1.amazonaws.com/sipfront-sipp:latest
