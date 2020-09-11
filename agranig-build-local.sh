#!/bin/sh

set -e

pushd ../sipfront-scenarios
git u
popd

test -d sipfront-scenarios && rm -rf sipfront-scenarios
cp -r ../sipfront-scenarios .

docker build -f docker/Dockerfile.sipfront -t sipfront-sipp:latest .

rm -rf sipfront-scenarios
