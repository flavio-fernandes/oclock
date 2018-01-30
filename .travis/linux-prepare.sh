#!/bin/bash

set -o errexit

# Apt update. Retry in case other apt process is taking place
NEXT_WAIT_TIME=1
until sudo apt-get update || [ $NEXT_WAIT_TIME -eq 66 ]; do
    sleep $(( NEXT_WAIT_TIME++ ))
    echo "Will retry apt update.... ${NEXT_WAIT_TIME}"
done

pushd $(dirname $0)
for pkg in $(cat ../bindep.txt); do
    sudo apt-get install -y $pkg
done
sudo apt-get install -y git
popd

git clone git://github.com/flavio-fernandes/WiringPi.git wiringPi.git
cd wiringPi.git && ./build
