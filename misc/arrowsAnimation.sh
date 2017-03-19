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
    curl --request POST  ${urlMsgMode} --data 'timeout=0&confetti=0'

    color=0            ; # also enumerated
    animationSpeed=3   ; # also enumerated; 1=fastest
    animationFrames=4
    animationInfo="animationStep=${animationSpeed}&animationPhase=${animationFrames}"
    x=0
    y=0

    idx=0

    ## up arrow animation 1
    imgBase=26         ; # enumerated in /src/displayTypes.h
    for imgStep in $(seq 0 3); do
	curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgBase+imgStep))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	idx=$((idx + 1))
    done

    x=$((x + 8))
    color=$((color + 1)) ; color=$((color % 3)) ; # make next arrow with different color

    ## down arrow animation 1
    imgBase=30         ; # enumerated in /src/displayTypes.h
    for imgStep in $(seq 0 3); do
	curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgBase+imgStep))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	idx=$((idx + 1))
    done

    x=$((x + 8))
    color=$((color + 1)) ; color=$((color % 3)) ; # make next arrow with different color
    
    ## up arrow 2 animation 2
    imgBase=34         ; # enumerated in /src/displayTypes.h
    for imgStep in $(seq 0 3); do
	curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgBase+imgStep))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	idx=$((idx + 1))
    done

    x=$((x + 6))
    color=$((color + 1)) ; color=$((color % 3)) ; # make next arrow with different color
    
    ## down arrow 2 animation 2
    imgBase=38         ; # enumerated in /src/displayTypes.h
    for imgStep in $(seq 0 3); do
	curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgBase+imgStep))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	idx=$((idx + 1))
    done

    x=$((x + 6))
    color=$((color + 1)) ; color=$((color % 3)) ; # make next arrow with different color

    # up
    animationSpeed=4   ; # also enumerated; 1=fastest
    animationFrames=14
    animationInfo="animationStep=${animationSpeed}&animationPhase=${animationFrames}"
    imgArrow=24           ; # enumerated in /src/displayTypes.h
    for animationStep in $(seq 0 13); do
	y=$((13 - animationStep))
	curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgArrow))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	idx=$((idx + 1))
    done
    
    x=$((x + 6))
    color=$((color + 1)) ; color=$((color % 3)) ; # make next arrow with different color

    # down
    animationSpeed=4   ; # also enumerated; 1=fastest
    animationFrames=14
    animationInfo="animationStep=${animationSpeed}&animationPhase=${animationFrames}"
    imgArrow=25         ; # enumerated in /src/displayTypes.h
    for animationStep in $(seq 0 13); do
	y=$((animationStep))
	curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgArrow))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	idx=$((idx + 1))
    done
    
    x=$((x + 6))
    color=$((color + 1)) ; color=$((color % 3)) ; # make next arrow with different color
    
    # up and down
    animationSpeed=1   ; # also enumerated; 1=fastest
    animationFrames=28
    animationInfo="animationStep=${animationSpeed}&animationPhase=${animationFrames}"
    imgArrow=24           ; # enumerated in /src/displayTypes.h
    for animationStep in $(seq 0 13); do
	y=$((13 - animationStep))
	curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgArrow))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	idx=$((idx + 1))
    done
    color=$((color + 1)) ; color=$((color % 3)) ; # make next arrow with different color
    imgArrow=$((imgArrow + 1))
    for animationStep in $(seq 0 13); do
	y=$((animationStep))
	curl --request POST  ${urlImgBg}  --data \
	       "index=${idx}&imgArt=$((imgArrow))&enabled=1&color=${color}&x=${x}&y=${y}&${animationInfo}&animationPhaseValue=$((idx % animationFrames))"
	idx=$((idx + 1))
    done

    idx=0
    x=0
    y=8
    while : ; do
	curl --request POST  ${urlMsgBg}  --data \
	       "index=${idx}&msg=arrows&enabled=1&color=${color}&x=${x}&y=${y}"
        color=$((color + 1)) ; color=$((color % 3)) ; # make next arrow with different color
	sleep 10
    done
}

# invoke main function
main
