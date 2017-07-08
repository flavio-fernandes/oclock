#!/bin/bash

urlBase='http://localhost:80'
urlImgBg="${urlBase}/imgBackground"
urlMsgBg="${urlBase}/msgBackground"
urlMsgMode="${urlBase}/msgMode"

declare -a on_exit_items

function on_exit()
{
    for i in "${on_exit_items[@]}"
    do
	# echo "on_exit: $i"
	eval $i
    done
}

function add_on_exit()
{
    local n=${#on_exit_items[*]}
    on_exit_items[$n]="$*"
    if [[ $n -eq 0 ]]; then
	# echo "Setting trap fo on_exit"
	trap on_exit EXIT
    fi
}


function main()
{
    # commands to execute on the way out
    add_on_exit curl --request POST  ${urlImgBg}  --data 'clearAll=1'
    add_on_exit curl --request POST  ${urlMsgMode} --data 'timeout=1'

    curl --request POST  ${urlImgBg}  --data 'clearAll=1'
    curl --request POST  ${urlMsgBg}  --data 'clearAll=1'
    curl --request POST  ${urlMsgMode} --data 'timeout=0'

    color=1
    while : ; do
	for idx in $(seq 0 31); do
	    if [ "$idx" -gt 15 ]; then y=8; else y=0; fi
	    x=$((idx * 8))
	    x=$((x % 128))
	    ## echo "x is $x  idx is $idx"
	    #color=$((color + 1)) ; color=$((color % 3))
	    #color=$(python -S -c "import random; print random.randrange(0,5)")
	    color=$(((RANDOM + RANDOM) % 5))
	    if [ $color -eq 4 ]; then
		img=44 ; color=0
	    else
		img=11
	    fi
	    curl --request POST  ${urlImgBg}  --data \
		 "index=${idx}&imgArt=${img}&enabled=1&color=${color}&x=${x}&y=${y}"
	done
	echo -n '.'
    done
}

main

