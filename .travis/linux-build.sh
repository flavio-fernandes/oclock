#!/bin/bash

set -o errexit

if [ "$1" = "verbose" ]; then
    V=1 make
else
    make
fi

exit 0
