FROM ubuntu:24.04

WORKDIR /root

RUN apt-get update && apt-get install -y \
    git \
    build-essential \
    cmake \
    libboost-system-dev \
    libboost-program-options-dev \
    libncurses-dev \
    libcap-dev \
    bats \
    bats-assert \
    bats-file \
    lcov


WORKDIR /ws

COPY .lcovrc .
COPY entrypoint.sh .
RUN chmod +x ./entrypoint.sh

ENTRYPOINT ["./entrypoint.sh"]
