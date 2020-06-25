FROM debian:stable

RUN apt-get update && apt-get -y install \
    build-essential cmake \
    libncurses-dev libpcap-dev \
    libgsl-dev libmosquitto-dev libssl-dev

ADD . /src

CMD cd /src && \
    rm -f CMakeCache.txt && \
    cmake . -DUSE_PCAP=1 -DUSE_GSL=1 -DUSE_MQTT=1 -DUSE_SSL=1 && \
    make
