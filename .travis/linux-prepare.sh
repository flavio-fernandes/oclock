#!/bin/bash

set -o errexit

pushd $(dirname $0)
for pkg in $(cat ../bindep.txt); do
    sudo apt-get install -y $pkg
done
sudo apt-get install -y git
popd

git clone git://github.com/flavio-fernandes/WiringPi.git wiringPi.git
cd wiringPi.git && ./build
