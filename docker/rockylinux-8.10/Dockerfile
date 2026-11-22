FROM rockylinux/rockylinux:9.9

WORKDIR /root

RUN dnf install -y --allowerasing \
    git \
    cmake \
    g++ \
    glibc-langpack-en \
    libasan \
    libubsan \
    boost-devel \
    ncurses-devel \
    libcap-devel \
    libcap \
    libcurl-devel \
    libcurl \
    wget \
    lld \
    which \
    acl \
    sudo \
    postfix \
    s-nail && \
    dnf --enablerepo=crb install -y ninja-build

# Installing libs for static build;
# Note: nothing there for libcap ... :/
#RUN dfn --enablerepo=crb \
#    glibc-static \
#    libstdc++-static \
#    boost-static


# no rpms for the following packages in the repo:
#    bats
#    bats-assert
#    bats-file
#    lcov

# Installing bats and libraries based on documentation and their Dockerfile
RUN git clone https://github.com/bats-core/bats-core.git
WORKDIR bats-core
RUN ./install.sh /opt/bats 
RUN ln -s /opt/bats/bin/bats /usr/local/bin/bats 
RUN ./docker/install_libs.sh support 0.3.0 
RUN ./docker/install_libs.sh assert 2.1.0 
RUN ./docker/install_libs.sh file 0.4.0


WORKDIR /ws

COPY entrypoint.sh .
RUN chmod +x ./entrypoint.sh

# Mirror the user/group graph of vm/alma-10/Vagrantfile (and of docker/ubuntu-24.04),
# which bats/test_setuid and bats/test_cap assume. In Docker the test runner is
# $DOCKER_USER, so it needs the memberships the VM's runner (vagrant) has.
#   userb:          primary userb, member of vagrant + usera
#   mean-user-name: primary userb, member of vagrant + usera
#   vagrant:        primary vagrant, member of usera + userb
#   usera:          member of vagrant (tests run ws_allocate -G vagrant as the
#                   current user; on the VM that user is vagrant itself)
ARG DOCKER_USER=usera
ARG OTHER_USER=userb
RUN useradd --home-dir /home/$DOCKER_USER -m --system --user-group $DOCKER_USER && \
    useradd --home-dir /home/$OTHER_USER -m --system --user-group $OTHER_USER && usermod -aG $DOCKER_USER $OTHER_USER && \
    useradd --home-dir /home/vagrant -m --system --user-group vagrant && \
    usermod -aG $DOCKER_USER vagrant && \
    usermod -aG $OTHER_USER vagrant && \
    useradd --home-dir /home/mean-user-name -m --system -g $OTHER_USER mean-user-name && \
    usermod -aG vagrant mean-user-name && \
    usermod -aG $DOCKER_USER mean-user-name && \
    usermod -aG vagrant $OTHER_USER && \
    usermod -aG vagrant $DOCKER_USER && \
    groupadd -f sudo && usermod -aG sudo $DOCKER_USER && \
    echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers && \
    sed -i 's/^inet_interfaces = localhost$/inet_interfaces = loopback-only/' /etc/postfix/main.cf && \
    echo localhost > /etc/mailname && \
    chown $DOCKER_USER /ws

USER $DOCKER_USER

ENTRYPOINT ["./entrypoint.sh"]
