#!/bin/bash

set -o errexit

git clone git://github.com/flavio-fernandes/WiringPi.git wiringPi.git
cd wiringPi.git && ./build

