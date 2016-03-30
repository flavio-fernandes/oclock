/* 
 * SIMPLE IMAGES FOR RENDERING TO THE LED SCREEN.
 * Gaurav Manek, 2011
 */

#ifndef __IMAGES_H
#define __IMAGES_H

const char IMG_MAIL [] = {0b1111, 0b1111, 0b0011, 0b1000, 0b0101, 0b1000, 0b1001, 0b1000, 0b0001, 0b1001, 0b0001, 0b1010, 0b0001, 0b1010, 0b0001, 0b1001, 0b1001, 0b1000, 0b0101, 0b1000, 0b0011, 0b1000, 0b1111, 0b1111};
#define IMG_MAIL_WIDTH 	12
#define IMG_MAIL_HEIGHT 	 8

const char IMG_FB [] = {0b1100, 0b1111, 0b0010, 0b0000, 0b0001, 0b0000, 0b0001, 0b0000, 0b0001, 0b0010, 0b0001, 0b1111, 0b1001, 0b0010, 0b1001, 0b0000};
#define IMG_FB_WIDTH 	 8
#define IMG_FB_HEIGHT 	 8

const char IMG_PHONE [] = {0b0000, 0b0100, 0b0000, 0b1110, 0b1111, 0b0111, 0b0011, 0b0000, 0b0011, 0b0000, 0b0011, 0b0010, 0b0011, 0b0111, 0b1111, 0b0011};
#define IMG_PHONE_WIDTH 	 8
#define IMG_PHONE_HEIGHT 	 8

const char IMG_MUSIC [] = {0b0000, 0b0100, 0b0000, 0b1110, 0b1111, 0b0111, 0b0011, 0b0000, 0b0011, 0b0000, 0b0011, 0b0010, 0b0011, 0b0111, 0b1111, 0b0011};
#define IMG_MUSIC_WIDTH 	 8
#define IMG_MUSIC_HEIGHT 	 8

const char IMG_MUSICNOTE [] = {0b0000, 0b0010, 0b0000, 0b0111, 0b1111, 0b0011, 0b0010, 0b0000};
#define IMG_MUSICNOTE_WIDTH 	 4
#define IMG_MUSICNOTE_HEIGHT 	 7

const char IMG_HEART [] = {0b1110, 0b0000, 0b1111, 0b0001, 0b1111, 0b0011, 0b1111, 0b0111, 0b1110, 0b1111, 0b1111, 0b0111, 0b1111, 0b0011, 0b1111, 0b0001, 0b1110, 0b0000};
#define IMG_HEART_WIDTH 	 9
#define IMG_HEART_HEIGHT 	 8


const char IMG_SPEAKER_A [] = {0b1000, 0b0001, 0b1000, 0b0001, 0b1100, 0b0011, 0b0010, 0b0100, 0b0101, 0b1010, 0b1000, 0b0001};
const char IMG_SPEAKER_B [] = {0b1000, 0b0001, 0b1000, 0b0001, 0b1100, 0b0011, 0b0010, 0b0100, 0b1101, 0b1011, 0b0000, 0b0000};
#define IMG_SPEAKER_WIDTH 	 6
#define IMG_SPEAKER_HEIGHT 	 8

#define IMG_SPEAKER_A_WIDTH 	 6
#define IMG_SPEAKER_A_HEIGHT 	 8
#define IMG_SPEAKER_B_WIDTH 	 6
#define IMG_SPEAKER_B_HEIGHT 	 8

// smiley
const char IMG_SMILEY [] = {0b1100, 0b1111, 0b0000, 0b0010, 0b0000, 0b0001, 0b0001, 0b0100, 0b0010, 0b1101, 0b1001, 0b0010, 0b0001, 0b1000, 0b0010, 0b1101, 0b1001, 0b0010, 0b0001, 0b0100, 0b0010, 0b0010, 0b0000, 0b0001, 0b1100, 0b1111, 0b0000};
#define IMG_SMILEY_WIDTH 9
#define IMG_SMILEY_HEIGHT 10

// wink (smiley animation)
const char IMG_WINK [] = {0b1100, 0b1111, 0b0000, 0b0010, 0b0000, 0b0001, 0b0001, 0b0000, 0b0010, 0b1101, 0b0001, 0b0010, 0b0001, 0b1000, 0b0010, 0b0001, 0b0001, 0b0010, 0b0001, 0b0001, 0b0010, 0b0010, 0b0000, 0b0001, 0b1100, 0b1111, 0b0000};
#define IMG_WINK_WIDTH 9
#define IMG_WINK_HEIGHT 10

// big heart
const char IMG_BIG_HEART [] = {0b1100, 0b0001, 0b0000, 0b1110, 0b0011, 0b0000, 0b1111, 0b0111, 0b0000, 0b1111, 0b1111, 0b0000, 0b1110, 0b1111, 0b0001, 0b1111, 0b1111, 0b0000, 0b1111, 0b0111, 0b0000, 0b1110, 0b0011, 0b0000, 0b1100, 0b0001, 0b0000};
#define IMG_BIG_HEART_WIDTH 9
#define IMG_BIG_HEART_HEIGHT 10

// cat
const char IMG_CAT [] = {0b1111, 0b1111, 0b0000, 0b0110, 0b0000, 0b0001, 0b0010, 0b0001, 0b0010, 0b0010, 0b1000, 0b0010, 0b0010, 0b1100, 0b0010, 0b0010, 0b1000, 0b0010, 0b0010, 0b0001, 0b0010, 0b0110, 0b0000, 0b0001, 0b1111, 0b1111, 0b0000, 0b0000, 0b0000, 0b0000};
#define IMG_CAT_WIDTH 9
#define IMG_CAT_HEIGHT 10

// owls
const char IMG_OWLS [] = {0b0000, 0b1100, 0b0000, 0b1111, 0b1101, 0b0000, 0b0010, 0b1110, 0b0000, 0b1010, 0b1110, 0b0000, 0b0010, 0b1110, 0b0000, 0b1111, 0b1101, 0b0000, 0b0000, 0b1100, 0b0000, 0b1111, 0b1101, 0b0000, 0b0010, 0b1110, 0b0000, 0b1010, 0b1110, 0b0000, 0b0010, 0b1110, 0b0000, 0b1111, 0b1101, 0b0000, 0b0000, 0b1100, 0b0000, 0b1111, 0b1101, 0b0000, 0b0010, 0b1110, 0b0000, 0b1010, 0b1110, 0b0000, 0b0010, 0b1110, 0b0000, 0b1111, 0b1101, 0b0000, 0b0000, 0b1100, 0b0000};
#define IMG_OWLS_WIDTH 19
#define IMG_OWLS_HEIGHT 9

#endif // __IMAGES_H
