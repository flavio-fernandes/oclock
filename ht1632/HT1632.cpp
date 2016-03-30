#include "HT1632.h"

#include <stdlib.h>
#include <math.h>       /* ceil */
#include <string.h>

#ifdef FAKE_WIRING
#include "fakeWiringPi.h"
#else
#include <wiringPi.h>
#endif // ifdef FAKE_WIRING

#pragma GCC diagnostic ignored "-Wchar-subscripts"

HT1632Class::HT1632Class(std::recursive_mutex* gpioLockMutexP) :
  gpioLockMutex(*gpioLockMutexP), brightness(16), _tgtBuffer(-1) {
}

HT1632Class::~HT1632Class() {
  // De-allocate memory for mem (including secondary)
  for (int i=0; i < MAX_BOARDS; ++i) {
    if (mem[i] != 0) {
      free(mem[i]);
      mem[i] = 0;
    }
  }
}

/*
 * HIGH LEVEL FUNCTIONS
 * Functions that perform advanced tasks using lower-level
 * functions go here:
 */

void HT1632Class::drawText(const char text [], int x, int y, const char font [], const char font_width [], char font_height, int font_glyph_step, char gutter_space) {
  int curr_x = x;
  char i = 0;
  char currchar;
  
  // Check if string is within y-bounds
  if (y + font_height < 0 || y >= COM_SIZE)
    return;
  
  while(true){  
    if (text[i] == '\0')
      return;
    
    currchar = text[i] - 32;
    if (currchar >= 65 && currchar <= 90) // If character is lower-case, automatically make it upper-case
      currchar -= 32; // Make this character uppercase.
    if (currchar < 0 || currchar >= 64) { // If out of bounds, skip
      ++i;
      continue; // Skip this character.
    }
    // Check to see if character is not too far right.
    if (curr_x >= OUT_SIZE)
      break; // Stop rendering - all other characters are no longer within the screen 
    
    // Check to see if character is not too far left.
    if (curr_x + font_width[currchar] + gutter_space >= 0){
      drawImage(font, font_width[currchar], font_height, curr_x, y,  currchar*font_glyph_step);
      
      // Draw the gutter space
      for (char j = 0; j < gutter_space; ++j)
        drawImage(font, 1, font_height, curr_x + font_width[currchar] + j, y, 0);
      
    }
    
    curr_x += font_width[currchar] + gutter_space;
    ++i;
  }
}

// Gives you the width, in columns, of a particular string.
int HT1632Class::getTextWidth(const char text [], const char font_width [], char font_height, char gutter_space) {
  int wd = 0;
  char i = 0;
  char currchar;
  
  while(true){  
    if (text[i] == '\0')
      return wd - gutter_space;
      
    currchar = text[i] - 32;
    if (currchar >= 65 && currchar <= 90) // If character is lower-case, automatically make it upper-case
      currchar -= 32; // Make this character uppercase.
    if (currchar < 0 || currchar >= 64) { // If out of bounds, skip
      ++i;
      continue; // Skip this character.
    }
    wd += font_width[currchar] + gutter_space;
    ++i;
  }
}

/*
 * MID LEVEL FUNCTIONS
 * Functions that handle internal memory, initialize the hardware
 * and perform the rendering go here:
 */

void HT1632Class::begin(int pinCS, int pinWR, int pinDATA, int pinCLK) {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  _pinForCS = pinCS;
  _pinWR = pinWR;
  _pinDATA = pinDATA;
  _pinCLK = pinCLK;

  int i=0;
  
  // Allocate new memory for mem (including secondary)
  for (i=0; i < MAX_BOARDS; ++i) mem[i] = (char *) malloc(ADDR_SPACE_SIZE);

  pinMode(_pinForCS, OUTPUT);
  pinMode(_pinWR, OUTPUT);
  pinMode(_pinDATA, OUTPUT);
  pinMode(_pinCLK, OUTPUT);

  initialize(_pinWR, _pinDATA);

  for (i=0; i < MAX_BOARDS; ++i) {
    drawTarget(i);
    clear();
    render(); // Perform the initial rendering
  }

  // Set drawTarget to default board.
  drawTarget(0);
}

void HT1632Class::reinit() {
  const char saveTarget = _tgtBuffer;

  initialize(_pinWR, _pinDATA);
  for (int i=0; i < MAX_BOARDS; ++i) {
    drawTarget(i);
    render(); // Redo render from undisturbed memory
  }

  drawTarget(saveTarget);
}

void HT1632Class::blank() {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  select(0);
  // Send Master commands
  for (int i=1; i <= NUM_ACTIVE_CHIPS; ++i) {
    select(i);  // 1 based
    writeData(HT1632_ID_CMD, HT1632_ID_LEN);    // Command mode
    writeCommand(HT1632_CMD_LEDOFF); // Turn off LED duty cycle generator
    select(0);
  }
}

void HT1632Class::initialize(int pinWR, int pinDATA) {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);


  // Each 8-bit mem array element stores data in the 4 least significant bits,
  //   and meta-data in the 4 most significant bits. Use bitmasking to read/write
  //   the meta-data.

  // Send configuration to chip:
  // This configuration is from the HT1632 datasheet, with one modification:
  //   The RC_MASTER_MODE command is not sent to the master. Since acting as
  //   the RC Master is the default behaviour, this is not needed. Sending
  //   this command causes problems in HT1632C (note the C at the end) chips. 

  select(0);

  // Send Master commands
  for (int i=1; i <= NUM_ACTIVE_CHIPS; ++i) {
    select(i);  // 1 based
    writeData(HT1632_ID_CMD, HT1632_ID_LEN);    // Command mode
  
    writeCommand(HT1632_CMD_SYSDIS); // Turn off system oscillator
    writeCommand(HT1632_CMD_COMS00); // 16*32, PMOS drivers
    //    writeCommand(HT1632_CMD_MSTMD);  // Master Mode (!!!!)
    writeCommand(HT1632_CMD_RCCLK);  // Master Mode, external clock
    writeCommand(HT1632_CMD_SYSEN); // Turn on system
    writeCommand(HT1632_CMD_LEDON); // Turn on LED duty cycle generator
    writeCommand(HT1632_CMD_PWM(brightness)); // PWM duty
    writeCommand(HT1632_CMD_BLOFF); // Blink off
    select(0);
  }

  for (int i=0; i < MAX_BOARDS; ++i) _globalNeedsRewriting[i] = true;
}

void HT1632Class::setPixel(int loc_x, int loc_y, bool datum) {
  if (_tgtBuffer > BUFFER_SECONDARY || _tgtBuffer < 0) return;

  if (datum) {
    mem[_tgtBuffer][GET_ADDR_FROM_X_Y(loc_x,loc_y)] = (mem[_tgtBuffer][GET_ADDR_FROM_X_Y(loc_x,loc_y)] | (1 << (loc_y % 4))) | MASK_NEEDS_REWRITING;
  }
  else {
    mem[_tgtBuffer][GET_ADDR_FROM_X_Y(loc_x,loc_y)] = (mem[_tgtBuffer][GET_ADDR_FROM_X_Y(loc_x,loc_y)] & ~(1 << (loc_y % 4))) | MASK_NEEDS_REWRITING;
  }
}

void HT1632Class::drawTarget(char targetBuffer) {
  if (targetBuffer >= 0 && targetBuffer < MAX_BOARDS) _tgtBuffer = targetBuffer;
}

void HT1632Class::drawImage(const char * img, char width, char height, int x, int y, int offset){
  char mask;

  if (_tgtBuffer > BUFFER_SECONDARY || _tgtBuffer < 0) return;

  // Sanity checks
  if (y + height < 0 || x + width < 0 || y > COM_SIZE || x > OUT_SIZE)
    return;
  // After looking at the rest of this function, you may need one.
  
  // Copying Engine.
  // You are only expected to understand this if it does not work right. ;)
  for (char i=0; i<width; ++i) {
    char carryover_y = 0; // Simply a copy of the last 4-bit word of img.
    char carryover_num = (y - (y & ~ 3)); // Number of digits carried over
    bool carryover_valid = false; // If true, there is data to be carried over.
    
    char loc_x = i + x;
    if (loc_x < 0 || loc_x >= OUT_SIZE) // Skip this column if it is out of range.
      continue;
    for (char j=0; j < (carryover_valid ? (height+4):height) ; j+=4) {
      const char loc_y = j + y;
      if (loc_y <= -4 || loc_y >= COM_SIZE) // Skip this row if it is out of range.
        continue;
      // Direct copying possible when render is on boundaries.
      // The bit manipulation here is designed to copy from img only the relevant sections.
      
      // This mask is only not used when emptying the cache (for copying to non-4-bit aligned spaces)
     
      //if (j<height)
      //  mask = (height-loc_y >= 4)?0b00001111:(0b00001111 >> (4-(height-j))) & 0b00001111; // Mask bottom
        
      if (loc_y % 4 == 0) {
	  const int shiftBottom = 4-(height-j);
	  mask = (height-loc_y >= 4 || shiftBottom <= 0) ? 0b00001111 : (0b00001111 >> shiftBottom) & 0b00001111; // Mask bottom
          mem[_tgtBuffer][GET_ADDR_FROM_X_Y(loc_x,loc_y)] = (mem[_tgtBuffer][GET_ADDR_FROM_X_Y(loc_x,loc_y)] & (~mask) & 0b00001111) | (img[(int)ceil((float)height/4.0f)*i + j/4 + offset] & mask) | MASK_NEEDS_REWRITING;
      } else {
        // If carryover_valid is NOT true, then this is the first set to be copied.
        //   If loc_y > 0, preserve the contents of the pixels above, copy to mem, and then copy remaining
        //   data to the carry over buffer. If y loc_y < 0, just copy data to carry over buffer. 
        //   It is expected that this section is only reached when j == 0.
        // COPY START
        if (!carryover_valid) { 
          if (loc_y > 0) {
	    const int shiftBottom = 4-(height-j);
	    mask = (height-loc_y >= 4 || shiftBottom <= 0) ? 0b00001111 : (0b00001111 >> shiftBottom) & 0b00001111; // Mask bottom
            mask = (0b00001111 << carryover_num) & mask; // Mask top
            mem[_tgtBuffer][GET_ADDR_FROM_X_Y(loc_x,loc_y)] = (mem[_tgtBuffer][GET_ADDR_FROM_X_Y(loc_x,loc_y)] & (~mask) & 0b00001111) | ((img[(int)ceil((float)height/4.0f)*i + j/4 + offset] << carryover_num) & mask) | MASK_NEEDS_REWRITING;
          }
          carryover_valid = true;
        } else {
          // COPY END
          if (j >= height) {
            // Its writing one line past the end.
            // Use this line to get rid of the final carry-over.
            mask = (0b00001111 >> (4 - carryover_num)) & 0b00001111; // Mask bottom
            mem[_tgtBuffer][GET_ADDR_FROM_X_Y(loc_x,loc_y)] = (mem[_tgtBuffer][GET_ADDR_FROM_X_Y(loc_x,loc_y)] & (~mask) & 0b00001111) | (carryover_y >> (4 - carryover_num) & mask) | MASK_NEEDS_REWRITING;
          // COPY MIDDLE  
          } else {
            // There is data in the carry-over buffer. Copy that data and the values from the current cell into mem.
            // The inclusion of a carryover_num term is to account for the presence of the carryover data  when calculating the bottom clipping.
	    const int shiftBottom = 4-(height+carryover_num-j);
	    mask = (height-loc_y >= 4 || shiftBottom <= 0) ? 0b00001111 : (0b00001111 >> shiftBottom) & 0b00001111; // Mask bottom
            mem[_tgtBuffer][GET_ADDR_FROM_X_Y(loc_x,loc_y)] = (mem[_tgtBuffer][GET_ADDR_FROM_X_Y(loc_x,loc_y)] & (~mask) & 0b00001111) | ((img[(int)ceil((float)height/4.0f)*i + j/4 + offset] << carryover_num) & mask) | (carryover_y >> (4 - carryover_num) & mask) | MASK_NEEDS_REWRITING;
          }
        }
        carryover_y = img[(int)ceil((float)height/4.0f)*i + j/4 + offset];
      }
    }
  }
}

void HT1632Class::clear() {
  if (_tgtBuffer > BUFFER_SECONDARY || _tgtBuffer < 0) return;

  memset(mem[_tgtBuffer], 0, ADDR_SPACE_SIZE);
  _globalNeedsRewriting[_tgtBuffer] = true;
}

void HT1632Class::clearAll() {
  const char saveTarget = _tgtBuffer;

  for (int i=0; i < MAX_BOARDS; ++i) {
    drawTarget(i);
    clear();
  }

  drawTarget(saveTarget);
}
  
void HT1632Class::renderAll() {
  const char saveTarget = _tgtBuffer;

  for (int i=0; i < MAX_BOARDS; ++i) {
    drawTarget(i);
    render();
  }

  drawTarget(saveTarget);
}

// Draw the contents of map to screen, for memory addresses that have the needsRedrawing flag
void HT1632Class::render() {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  if (_tgtBuffer >= BUFFER_SECONDARY || _tgtBuffer < 0) return;
  
  char nChip;
  char nChipOpen = -1;                   // Automatically compact sequential writes.
  char chipBasedAddress;
  const int colorOffset = _tgtBuffer * 32;      // Color (aka board) memory offset in chip 
  
  select(0);
  for (int i=0; i < ADDR_SPACE_SIZE; ++i) {
    if (_globalNeedsRewriting[_tgtBuffer] ||
	(mem[_tgtBuffer][i] & MASK_NEEDS_REWRITING)) {  // Does this memory chunk need to be written to?
      nChip = (i / 32) + 1;  // calculate nChip we will need to talk to (1 based!)
      if ( nChipOpen != nChip ) {                      // If necessary, open the writing session by:
	chipBasedAddress = i % 32;
        select(nChip);       //   Selecting the chip
        writeData(HT1632_ID_WR, HT1632_ID_LEN);
        writeData(chipBasedAddress + colorOffset, HT1632_ADDR_LEN);   //   Selecting the memory address
        nChipOpen = nChip;
      }
      writeDataRev(mem[_tgtBuffer][i], HT1632_WORD_LEN); // Write the data in reverse.
    } else {                               // If a previous sequential write session is open, close it.
      if (nChipOpen != -1) { select(0); nChipOpen = -1; }
    }
  }
  if (nChipOpen != -1) { // Close the stream at the end
    select(0);
    // nChipOpen = -1;
  }

  _globalNeedsRewriting[_tgtBuffer] = false;
}

// Set the brightness to an integer level between 1 and 16 (inclusive).
// Uses the PWM feature to set the brightness.
void HT1632Class::setBrightness(char brightnessParam) {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  if (brightnessParam < 1 || brightnessParam > 16) return;

  brightness = brightnessParam; // save/store

  for (int i=1; i <= NUM_ACTIVE_CHIPS; ++i) {
    select(i);  // 1 based!
    writeData(HT1632_ID_CMD, HT1632_ID_LEN);    // Command mode
    writeCommand(HT1632_CMD_PWM(brightness));   // Set brightness
  }
  select(0);
}

void HT1632Class::transition(char mode, int time){
  if (_tgtBuffer >= BUFFER_SECONDARY || _tgtBuffer < 0) return;
  
  switch(mode) {
    case TRANSITION_BUFFER_SWAP:
      {
        char* tmp = mem[_tgtBuffer];
        mem[_tgtBuffer] = mem[BUFFER_SECONDARY];
        mem[BUFFER_SECONDARY] = tmp;
        _globalNeedsRewriting[_tgtBuffer] = true;
      }
      break;
    case TRANSITION_SAVE:
      memcpy(mem[BUFFER_SECONDARY], mem[_tgtBuffer], ADDR_SPACE_SIZE);
      break;
    case TRANSITION_SAVE_XOR:
      for (int i=0; i < ADDR_SPACE_SIZE; ++i) {
        mem[BUFFER_SECONDARY][i] ^= mem[_tgtBuffer][i];
      }
      break;
    case TRANSITION_REMOVE_PIXELS:
      for (int i=0; i < ADDR_SPACE_SIZE; ++i) {
	char pixels = mem[_tgtBuffer][i];
        mem[BUFFER_SECONDARY][i] &= ~pixels;
      }
      break;
    case TRANSITION_RESTORE:
      memcpy(mem[_tgtBuffer], mem[BUFFER_SECONDARY], ADDR_SPACE_SIZE);
      _globalNeedsRewriting[_tgtBuffer] = true;
      break;
    case TRANSITION_RESTORE_OR:
      for (int i=0; i < ADDR_SPACE_SIZE; ++i) {
        mem[_tgtBuffer][i] |= mem[BUFFER_SECONDARY][i]; // Needs to be redrawn 
      }
      _globalNeedsRewriting[_tgtBuffer] = true;
      break;
    case TRANSITION_FADE:
      {
	const char brightnessSave = brightness;
	time /= 32;
	for (int i = 15; i > 0; --i) {
	  setBrightness(i);
	  delay(time);
	}
	clear();
	render();
	delay(time);
	transition(TRANSITION_BUFFER_SWAP);
	render();
	delay(time);
	for (int i = 2; i <= 16; ++i) {
	  setBrightness(i);
	  delay(time);
	}
	setBrightness(brightnessSave); // restore
      }
      break;
    default:
      break;
  }
}


/*
 * LOWER LEVEL FUNCTIONS
 * Functions that directly talk to hardware go here:
 */
 
void HT1632Class::writeCommand(char data) {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  writeData(data, HT1632_CMD_LEN);
  writeSingleBit();
} 
// Integer write to display. Used to write commands/addresses.
// PRECONDITION: WR is LOW
void HT1632Class::writeData(char data, char len) {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  for (int j=len-1, t = 1 << (len - 1); j>=0; --j, t >>= 1){
    // Set the DATA pin to the correct state
    digitalWrite(_pinDATA, ((data & t) == 0)?LOW:HIGH);
    NOP(); // Delay 
    // Raise the WR momentarily to allow the device to capture the data
    digitalWrite(_pinWR, HIGH);
    NOP(); // Delay
    // Lower it again, in preparation for the next cycle.
    digitalWrite(_pinWR, LOW);
  }
}
// REVERSED Integer write to display. Used to write cell values.
// PRECONDITION: WR is LOW
void HT1632Class::writeDataRev(char data, char len) {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  for (int j=0; j<len; ++j){
    // Set the DATA pin to the correct state
    digitalWrite(_pinDATA, data & 1);
    NOP(); // Delay
    // Raise the WR momentarily to allow the device to capture the data
    digitalWrite(_pinWR, HIGH);
    NOP(); // Delay
    // Lower it again, in preparation for the next cycle.
    digitalWrite(_pinWR, LOW);
    data >>= 1;
  }
}
// Write single bit to display, used as padding between commands.
// PRECONDITION: WR is LOW
void HT1632Class::writeSingleBit() {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  // Set the DATA pin to the correct state
  digitalWrite(_pinDATA, LOW);
  NOP(); // Delay
  // Raise the WR momentarily to allow the device to capture the data
  digitalWrite(_pinWR, HIGH);
  NOP(); // Delay
  // Lower it again, in preparation for the next cycle.
  digitalWrite(_pinWR, LOW);
}

//Output a clock pulse
static inline void outputCLK_Pulse(char _pinCLK) { digitalWrite(_pinCLK, HIGH); digitalWrite(_pinCLK, LOW); }

// Choose a chip. This function sets the correct CS line to LOW, and the rest to HIGH
// Call the function with no arguments to deselect all chips.
void HT1632Class::select(char mask) {
  std::lock_guard<std::recursive_mutex> guard(gpioLockMutex);

  char tmp = 0;

  if (mask < 0) { // Enable all HT1632C
    digitalWrite(_pinForCS, LOW);
    for (tmp = 0; tmp < NUM_ACTIVE_CHIPS; tmp++) outputCLK_Pulse(_pinCLK);
  } else if (mask == 0) { // Disable all HT1632Cs
    digitalWrite(_pinForCS, HIGH);
    for (tmp = 0; tmp < NUM_ACTIVE_CHIPS; tmp++) outputCLK_Pulse(_pinCLK);
  } else {
    digitalWrite(_pinForCS, HIGH);
    for (tmp = 0; tmp < NUM_ACTIVE_CHIPS; tmp++) outputCLK_Pulse(_pinCLK);
    digitalWrite(_pinForCS, LOW);
    outputCLK_Pulse(_pinCLK);
    digitalWrite(_pinForCS, HIGH);
    for (tmp = 1 ; tmp < mask; tmp++) outputCLK_Pulse(_pinCLK);
  }
}
