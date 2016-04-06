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
    # curl --request POST  ${urlMsgBg}  --data 'clearAll=1'
    curl --request POST  ${urlMsgMode} --data 'timeout=0&confetti=2'

    y=0
    imgBase=12
    color=1
    animationSpeed=2   ; # 1=fastest
    animationFrames=6
    animationInfo="animationStep=${animationSpeed}&animationPhase=${animationFrames}"
    while : ; do
	x=-2
	idx=0
	for dancer in $(seq 11); do
	
	  curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgBase+0))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	  idx=$((idx + 1))

	  curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgBase+1))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	  idx=$((idx + 1))

	  curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgBase+2))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	  idx=$((idx + 1))

	  curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgBase+3))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	  idx=$((idx + 1))

	  curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgBase+2))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	  idx=$((idx + 1))

	  curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgBase+1))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	  idx=$((idx + 1))

	  idx=$((idx + 2))  ; # skip some idx to make dancers out of sync from each other
          x=$((x + 12))     ; # update x for spot where next dancer will be
	  color=$((color + 1)) ; color=$((color % 3)) ; # make next dancer with different color

	done

	sleep 13
    done
}

main

