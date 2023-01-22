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

# "time": 1674300068, "summary": "Mostly Cloudy", "icon": "partly-cloudy-night", "nearestStormDistance": 21, "nearestStormDistance": 309, "precipIntensity": 0, "precipProbability": 0, "temperature": 27.62, "apparentTemperature": 27.91, "dewPoint": 25.67, "humidity": 0.92, "pressure": 1016.6, "windSpeed": 2.69, "windGust": 6.14, "windBearing": 352, "cloudCover": 0.82, "uvIndex": 0, "visibility": 10, "ozone": 381.8, "precipProbabilityPercent": "0+%25"

function weather_details()
{
    windSpeed='555'
    precipProbabilityPercent='909+%25'
    nearestStormDistance='321'
    dewPoint='66.6'
    summary='chance of meatballs'
    cloudCover='1.01'
    uvIndex='42'
    dawn='9:99'
    dusk='23:99'
    tempMin='1.1'
    tempMax='99.9'

    # override the fudged values above by reading in /tmp/weather.txt
    [ -e /tmp/weather.txt ] && source /tmp/weather.txt

    curl --request POST  ${urlMsgMode} --data "msg=${summary}&repeats=4&timeout=0&y=0&font=4&color=2"

    curl --request POST  ${urlMsgBg}  --data "msg=Wind&x=0&y=1&index=0&color=1&font=0&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${windSpeed}&x=21&y=1&index=1&color=0&font=0&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=Precip&x=42&y=1&index=2&color=1&font=0&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${precipProbabilityPercent}&x=68&y=1&index=3&color=0&font=0&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=Dist&x=95&y=1&index=4&color=1&font=0&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${nearestStormDistance}&x=113&y=1&index=5&color=0&font=0&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=Dew&x=0&y=9&index=6&color=1&font=0&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${dewPoint}&x=15&y=9&index=7&color=0&font=0&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=Cloud&x=42&y=9&index=10&color=1&font=0&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${cloudCover}&x=66&y=9&index=11&color=0&font=0&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=UV&x=83&y=9&index=12&color=1&font=0&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${uvIndex}&x=95&y=9&index=13&color=0&font=0&enabled=yes"

    # dusk, dawn, tempMin, tempMax animation

    curl --request POST  ${urlMsgBg}  --data "msg=dawn&index=14&x=107&y=9&animationStep=5&animationPhase=8&animationPhaseValue=0&color=2&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${dawn}&index=15&x=107&y=9&animationStep=5&animationPhase=8&animationPhaseValue=1&color=2&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=dusk&index=16&x=107&y=9&animationStep=5&animationPhase=8&animationPhaseValue=2&color=1&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${dusk}&index=17&x=107&y=9&animationStep=5&animationPhase=8&animationPhaseValue=3&color=1&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=Min T&index=18&x=107&y=9&animationStep=5&animationPhase=8&animationPhaseValue=4&color=2&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${tempMin}&index=19&x=107&y=9&animationStep=5&animationPhase=8&animationPhaseValue=5&color=2&enabled=yes"

    curl --request POST  ${urlMsgBg}  --data "msg=Max T&index=20&x=107&y=9&animationStep=5&animationPhase=8&animationPhaseValue=6&color=1&enabled=yes"
    curl --request POST  ${urlMsgBg}  --data "msg=${tempMax}&index=21&x=107&y=9&animationStep=5&animationPhase=8&animationPhaseValue=7&color=1&enabled=yes"
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
    exec 100>/tmp/weatherLock.lock || { >&2 echo "unable to open lock file" ; exit 1; }
    flock -w 10 100 || { >&2 echo "failed to lock lock file"; exit 2; }

    [ -e /tmp/weather.txt ] && cat /tmp/weather.txt > /dev/shm/weather.txt.tmp || touch /dev/shm/weather.txt.tmp
    while (( "$#" )); do
        key="$1"
        value="$2"
        shift 2

        grep -v "export ${key}=" /dev/shm/weather.txt.tmp > /dev/shm/weather.txt.tmp.2
        echo "export ${key}='${value}'" >> /dev/shm/weather.txt.tmp.2
        cat /dev/shm/weather.txt.tmp.2 > /dev/shm/weather.txt.tmp
    done
    rm -f /dev/shm/weather.txt.tmp.2
    mv /dev/shm/weather.txt.tmp /tmp/weather.txt

    exit 0
fi

# invoke display function
display
