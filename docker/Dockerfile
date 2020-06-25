FROM debian:stable AS build-stage
ADD . /src
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get -y install \
    build-essential cmake \
    libncurses-dev libpcap-dev \
    libgsl-dev libmosquitto-dev libssl-dev
RUN cd /src && \
    rm -f CMakeCache.txt && \
    cmake . -DUSE_PCAP=1 -DUSE_GSL=1 -DUSE_MQTT=1 -DUSE_SSL=1 && \
    make
RUN mv /src/sipp /bin/sipp

FROM debian:stable AS run-stage
RUN apt-get update && apt-get -y install \
    libncurses6 libpcap0.8 libgsl23 libmosquitto1 libssl1.1
COPY --from=build-stage /bin/sipp /bin/sipp
