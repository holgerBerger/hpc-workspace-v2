FROM ubuntu:24.04

WORKDIR /root

RUN apt-get update && apt-get install -y \
    acl \
    build-essential \
    cmake \
    elvis-tiny \
    git \
    locales \
    libboost-system-dev \
    libboost-program-options-dev \
    libncurses-dev \
    libcap-dev \
    libcap2-bin \
    libcap-dev \
    libcap2 \
    lld \
    ninja-build \
    wget \
    lcov \
    libcurl4t64 \
    libcurl4-openssl-dev \
    postfix \
    sudo \
    s-nail


# Installing bats and libraries based on documentation and their Dockerfile
RUN git clone https://github.com/bats-core/bats-core.git
WORKDIR bats-core
RUN ./install.sh /opt/bats && \
    ln -s /opt/bats/bin/bats /usr/local/bin/bats && \
    ./docker/install_libs.sh support 0.3.0 && \
    ./docker/install_libs.sh assert 2.1.0 && \
    ./docker/install_libs.sh file 0.4.0 

WORKDIR /tmp/ws

COPY entrypoint.sh .
RUN chmod a+x ./entrypoint.sh

COPY .lcovrc /tmp/ws

ARG DOCKER_USER=usera
ARG OTHER_USER=userb
RUN addgroup  $DOCKER_USER && adduser --home /home/$DOCKER_USER --system --group  $DOCKER_USER  && \
    addgroup  $OTHER_USER && adduser --home /home/$OTHER_USER   --system --group  $OTHER_USER   && adduser $OTHER_USER $DOCKER_USER && \
    adduser --home /home/mean-user-name   --system --ingroup userb mean-user-name && \
    adduser $DOCKER_USER sudo && \
    echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers && \
    chown -R usera /tmp/ws && \
    sed -i 's/^inet_interfaces = localhost$/inet_interfaces = loopback-only/' /etc/postfix/main.cf  && \
    locale-gen en_US.UTF-8 && \
    update-locale LANG=en_US.UTF-8


USER $DOCKER_USER

COPY .lcovrc /home/usera

ENTRYPOINT ["/tmp/ws/entrypoint.sh"]
