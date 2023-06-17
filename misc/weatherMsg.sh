#!/bin/bash

set -x

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

function weather_details()
{
    windSpeed='555'
    windDeg='360'
    windGust='666'
    dewPoint='66.66'
    cloudCover='100'
    pressure='1001'
    summary='chance of meatballs'
    dawn='9:99'
    dusk='23:99'
    tempMin='1.1'
    tempMax='99.9'

    # override the fudged values above by reading in /dev/shm/weather.txt
    [ -e /dev/shm/weather.txt ] && source /dev/shm/weather.txt

    LONE=1
    LTWO=9

    # dusk, dawn, tempMin, tempMax animation

    curl --request POST  ${urlMsgBg}  --data "msg=dawn&index=0&x=107&y=${LTWO}&animationStep=5&animationPhase=8&animationPhaseValue=0&color=2&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${dawn}&index=1&x=107&y=${LTWO}&animationStep=5&animationPhase=8&animationPhaseValue=1&color=2&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=dusk&index=2&x=107&y=${LTWO}&animationStep=5&animationPhase=8&animationPhaseValue=2&color=1&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${dusk}&index=3&x=107&y=${LTWO}&animationStep=5&animationPhase=8&animationPhaseValue=3&color=1&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=Min T&index=4&x=107&y=${LTWO}&animationStep=5&animationPhase=8&animationPhaseValue=4&color=2&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${tempMin}&index=5&x=107&y=${LTWO}&animationStep=5&animationPhase=8&animationPhaseValue=5&color=2&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=Max T&index=6&x=107&y=${LTWO}&animationStep=5&animationPhase=8&animationPhaseValue=6&color=1&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${tempMax}&index=7&x=107&y=${LTWO}&animationStep=5&animationPhase=8&animationPhaseValue=7&color=1&enabled=yes"

    # non animated info
 
    curl --request POST  ${urlMsgBg}  --data "msg=Wind&x=0&y=${LONE}&index=8&color=1&font=0&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${windSpeed}&x=21&y=${LONE}&index=9&color=0&font=0&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=Gust&x=0&y=${LTWO}&index=10&color=1&font=0&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${windGust}&x=21&y=${LTWO}&index=11&color=0&font=0&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=Deg&x=36&y=${LONE}&index=12&color=1&font=0&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${windDeg}&x=50&y=${LONE}&index=13&color=0&font=0&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=Dew&x=36&y=${LTWO}&index=14&color=1&font=0&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${dewPoint}&x=51&y=${LTWO}&index=15&color=0&font=0&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=Pressure&x=69&y=${LONE}&index=16&color=1&font=0&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${pressure}&x=107&y=${LONE}&index=17&color=0&font=0&enabled=yes"
    
    curl --request POST  ${urlMsgBg}  --data "msg=Clud&x=74&y=${LTWO}&index=18&color=1&font=0&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${cloudCover}&x=93&y=${LTWO}&index=19&color=0&font=0&enabled=yes"

    # at last, start message mode now that all wanted msg bg is populated
    curl --request POST  ${urlMsgMode} --data "msg=${summary}&repeats=4&timeout=0&y=0&font=4&color=2"
}

function display()
{
    # commands to execute on the way out
    add_on_exit curl --request POST  ${urlMsgMode} --data 'timeout=1'
    add_on_exit curl --request POST  ${urlMsgBg}  --data 'clearAll=1'
    # add_on_exit curl --request POST  ${urlImgBg}  --data 'clearAll=1'

    # curl --request POST  ${urlImgBg}  --data 'clearAll=1'
    curl --request POST  ${urlMsgBg}  --data 'clearAll=1'

    weather_details
	sleep 25
}

# if invoked with parameters, add them to data file
if [ $# -ne 0 ]; then \
    exec 100>/dev/shm/weatherLock.lock || { >&2 echo "unable to open lock file" ; exit 1; }
    flock -w 10 100 || { >&2 echo "failed to lock lock file"; exit 2; }

    [ -e /dev/shm/weather.txt ] && cat /dev/shm/weather.txt > /dev/shm/weather.txt.tmp || touch /dev/shm/weather.txt.tmp
    while (( "$#" )); do
        key="$1"
        value="$2"
        shift 2

        grep -v "export ${key}=" /dev/shm/weather.txt.tmp > /dev/shm/weather.txt.tmp.2
        echo "export ${key}='${value}'" >> /dev/shm/weather.txt.tmp.2
        cat /dev/shm/weather.txt.tmp.2 > /dev/shm/weather.txt.tmp
    done
    rm -f /dev/shm/weather.txt.tmp.2
    mv /dev/shm/weather.txt.tmp /dev/shm/weather.txt

    exit 0
fi

# invoke display function
display
