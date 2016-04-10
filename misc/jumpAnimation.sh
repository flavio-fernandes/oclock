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
    curl --request POST  ${urlMsgMode} --data 'timeout=0&confetti=0'

    y=0
    imgBase=20         ; # enumerated in /src/displayTypes.h
    color=1            ; # also enumerated
    animationSpeed=3   ; # also enumerated; 1=fastest
    animationFrames=7
    animationInfo="animationStep=${animationSpeed}&animationPhase=${animationFrames}"
    x=0
    while : ; do
	idx=25
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

	  curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgBase+0))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	  idx=$((idx + 1))

          x=$((x + 3)) ; x=$((x % 122));  # update x for spot where next jumper will be
	  ## color=$((color + 1)) ; color=$((color % 3)) ; # make next dancer with different color

	## sleep 1

    done
}

# invoke main function
main
