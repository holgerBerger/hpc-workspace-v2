#!/bin/bash

echo "== creating users for tests and prepare sudo"  

export DOCKER_USER=vagrant
OTHER_USER=userb
sudo groupadd  $DOCKER_USER 
sudo adduser --home-dir /home/$DOCKER_USER -m -G sudo,$DOCKER_USER --system $DOCKER_USER  
sudo groupadd  $OTHER_USER 
sudo groupadd  usera
sudo adduser --home-dir /home/$OTHER_USER -m -g userb -G $DOCKER_USER,usera --system $OTHER_USER   
sudo adduser --home-dir /home/mean-user-name -m -g userb -G $DOCKER_USER,usera --system mean-user-name
sudo usermod -G usera vagrant
echo '%sudo ALL=(ALL) NOPASSWD:ALL' | sudo tee -a /etc/sudoers
