#ifndef _DISPLAY_TYPES_H

#define _DISPLAY_TYPES_H

#include "stdTypes.h"

typedef enum {
  displayColorGreen = 0,
  displayColorRed,
  displayColorYellow,    // red+green
  displayColorAlternate, // take turns on red/green/yellow
} DisplayColor;

typedef enum {
  font5x4,  // 0  default (expected to be first)
  font8x4,  // 1
  font7x5,  // 2
  font8x6,  // 3 
  font16x8, // 4  aka bigFont

  fontLast
} Font;

typedef enum {
  animationStepNone = 0,
  animationStepFast,
  animationStep250ms,
  animationStep500ms,
  animationStep1sec,
  animationStep5sec,
  animationStep10sec,
} AnimationStep;

typedef enum {
  imgArtSmiley, // 0 izaArt.h
  imgArtWink,   // 1
  imgArtBigHeart,  // 2
  imgArtCat,   // 3
  imgArtOwls,  // 4
  imgArtMail,  // 5 images.h
  imgArtMusic,      // 6
  imgArtMusicNote,  // 7
  imgArtHeart,      // 8
  imgArtSpeakerA,   // 9
  imgArtSpeakerB,   // 10

  imgArtLast
} ImgArt;

#define MESSAGE_MAX_SIZE 4096
#define BACKGROUND_MESSAGE_MSG_SIZE 25
#define BACKGROUND_MESSAGE_COUNT 16
#define BACKGROUND_IMG_COUNT 100
#define BACKGROUND_MESSAGE_MSG_SIZE 25
#define BACKGROUND_MESSAGE_COUNT 16

#endif // _DISPLAY_TYPES_H
