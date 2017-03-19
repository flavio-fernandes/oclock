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
  animationStep100ms,
  animationStep250ms,
  animationStep500ms,
  animationStep1sec,
  animationStep5sec,
  animationStep10sec,
} AnimationStep;

#define IMG_ART_LIST \
    X(imgArtSmiley),     /* 0*/  \
    X(imgArtWink),       /* 1*/  \
    X(imgArtBigHeart),   /* 2*/  \
    X(imgArtCat),        /* 3*/  \
    X(imgArtOwls),       /* 4*/  \
    X(imgArtMail),       /* 5*/  \
    X(imgArtMusic),      /* 6*/  \
    X(imgArtMusicNote),  /* 7*/  \
    X(imgArtHeart),      /* 8*/  \
    X(imgArtSpeakerA),   /* 9*/  \
    X(imgArtSpeakerB),   /*10*/  \
    X(imgArt8x8),        /*11*/  \
    X(imgArtStickMan1),  /*12*/  \
    X(imgArtStickMan2),  /*13*/  \
    X(imgArtStickMan3),  /*14*/  \
    X(imgArtStickMan4),  /*15*/  \
    X(imgArtWave1),      /*16*/  \
    X(imgArtWave2),      /*17*/  \
    X(imgArtWave3),      /*18*/  \
    X(imgArtWave4),      /*19*/  \
    X(imgArtJump1),      /*20*/  \
    X(imgArtJump2),      /*21*/  \
    X(imgArtJump3),      /*22*/  \
    X(imgArtJump4),      /*23*/  \
    X(imgArtUp),         /*24*/  \
    X(imgArtDown),       /*25*/  \
    X(imgArtUp1),        /*26*/  \
    X(imgArtUp2),        /*27*/  \
    X(imgArtUp3),        /*28*/  \
    X(imgArtUp4),        /*29*/  \
    X(imgArtDown1),      /*30*/  \
    X(imgArtDown2),      /*31*/  \
    X(imgArtDown3),      /*32*/  \
    X(imgArtDown4),      /*33*/  \
    X(imgArtUp21),       /*34*/  \
    X(imgArtUp22),       /*35*/  \
    X(imgArtUp23),       /*36*/  \
    X(imgArtUp24),       /*37*/  \
    X(imgArtDown21),     /*38*/  \
    X(imgArtDown22),     /*39*/  \
    X(imgArtDown23),     /*40*/  \
    X(imgArtDown24),     /*41*/  \
    X(imgArtCar1),       /*42*/	 \
    X(imgArtCar2),       /*43*/	 \
    X(imgArtBlank),      /*44*/	 \
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
#define BACKGROUND_MESSAGE_COUNT 100
#define BACKGROUND_MESSAGE_MSG_SIZE 120

#endif // _DISPLAY_TYPES_H
