#!/bin/sh

set -e

docker build -f docker/Dockerfile.sipfront -t sipfront-sipp:latest .

rm -rf sipfront-scenarios
