FROM ubuntu:26.10

WORKDIR /root

RUN apt update && apt install -y \
    adduser \
    acl \
    build-essential \
    cmake \
    elvis-tiny \
    git \
    locales \
    libboost-dev \
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

WORKDIR /tmp/docker

COPY entrypoint.sh .
RUN chmod a+x ./entrypoint.sh

COPY .lcovrc /tmp/docker

ARG DOCKER_USER=usera
ARG OTHER_USER=userb
# Mirror the user/group graph of vm/alma-10/Vagrantfile, which bats/test_setuid
# and bats/test_cap assume. In Docker the test runner is $DOCKER_USER, so it
# needs the memberships the VM's runner (vagrant) has.
#   userb:          primary userb, member of vagrant + usera
#   mean-user-name: primary userb, member of vagrant + usera
#   vagrant:        primary vagrant, member of usera + userb
#   usera:          member of vagrant (tests run ws_allocate -G vagrant as the
#                   current user; on the VM that user is vagrant itself)
RUN addgroup  $DOCKER_USER && adduser --home /home/$DOCKER_USER --system --group  $DOCKER_USER  && \
    addgroup  $OTHER_USER && adduser --home /home/$OTHER_USER   --system --group  $OTHER_USER   && adduser $OTHER_USER $DOCKER_USER && \
    adduser --home /home/vagrant  --system --group vagrant && \
    adduser vagrant $DOCKER_USER && \
    adduser vagrant $OTHER_USER && \
    adduser --home /home/mean-user-name   --system --ingroup $OTHER_USER mean-user-name && \
    adduser mean-user-name vagrant && \
    adduser mean-user-name $DOCKER_USER && \
    adduser $OTHER_USER vagrant && \
    adduser $DOCKER_USER vagrant && \
    adduser $DOCKER_USER sudo && \
    echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers && \
    chown -R usera /tmp/docker && \
    sed -i 's/^inet_interfaces = localhost$/inet_interfaces = loopback-only/' /etc/postfix/main.cf  && \
    echo localhost > /etc/mailname && \
    locale-gen en_US.UTF-8 && \
    update-locale LANG=en_US.UTF-8


USER $DOCKER_USER

COPY .lcovrc /home/usera

ENTRYPOINT ["/tmp/docker/entrypoint.sh"]
