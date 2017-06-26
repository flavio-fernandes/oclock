#!/bin/bash

urlBase='http://localhost:80'
urlLedStrip="${urlBase}/ledStrip"

declare -a on_exit_items

function on_exit()
{
    for i in "${on_exit_items[@]}"
    do
	echo "on_exit: $i"
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
    if [ "$1" -eq "$1" ] 2>/dev/null; then
        SLP=$1
    else
        SLP=1
    fi    
    # commands to execute on the way out
    add_on_exit curl --request POST ${urlLedStrip} --data \'ledStripMode=2\&red=1\&green=0\&blue=0\&timeout=1\'

    # random fill strip with random colors, every SLP seconds
    bodyData='extraParam=randomColor&ledStripMode=2&red=0&green=0&blue=0'
    while : ; do
	curl --request POST ${urlLedStrip} --data "${bodyData}&timeout=$((SLP + 3))"
        sleep ${SLP}
    done
}

# invoke main function
main $1
