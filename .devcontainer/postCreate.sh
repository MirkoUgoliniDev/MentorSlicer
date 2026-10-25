#!/bin/bash
# Update and upgrade all system packages
apt update
apt upgrade -y          

echo "-----------------------------------------"	
echo "Running BuildLinux.sh with update flag..."
echo "-----------------------------------------"	
apt-get install -y software-properties-common
wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | apt-key add -
apt-add-repository -y 'deb https://apt.kitware.com/ubuntu/ bionic main'
./BuildLinux.sh -u
echo "------------------------------"
echo "Installing missing packages..."
echo "------------------------------"
apt install -y libgl1-mesa-dev m4 autoconf libtool ccache