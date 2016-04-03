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

#define IMG_ART_LIST \
    X(imgArtSmiley),    \
    X(imgArtWink),      \
    X(imgArtBigHeart),  \
    X(imgArtCat),       \
    X(imgArtOwls),      \
    X(imgArtMail),      \
    X(imgArtMusic),	\
    X(imgArtMusicNote), \
    X(imgArtHeart),	\
    X(imgArtSpeakerA),	\
    X(imgArtSpeakerB),	\
    X(imgArt8x8),	\
    X(imgArtLast)

#define X(x) x
typedef enum {
    IMG_ART_LIST
} ImgArt;
#undef X

static const int imgArtStrUniqueOffset = 6;  // imgArtABC
extern const char* imgArtStr[];

#define MESSAGE_MAX_SIZE 4096
#define BACKGROUND_IMG_COUNT 100
#define BACKGROUND_MESSAGE_MSG_SIZE 120
#define BACKGROUND_MESSAGE_COUNT 100

#endif // _DISPLAY_TYPES_H
