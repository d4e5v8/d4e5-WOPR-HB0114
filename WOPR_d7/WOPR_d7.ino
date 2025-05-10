// Loosely based on original version
// Random effect for WOPR LED Matrix in HackerBox 0114
// https://hackerboxes.com/products/hackerbox-0114-wopr
//
// by d7
//
// The Matrix is 96 x 8, col 0 row 0 is bottom right.
// The matrix is filled with blocks, functions draw the blocks, the blocks all have 8 rows,
// and 4, 8, 12, 13 or 16 columns. Each block is separated by one unlit column.
// Blocks arragement and location, format Location[block size]
// 0[4] 5[16] 22[16] 39[4] 44[16] 61[4] 66[13] 80[12] 93[3]
//
// The system boots with a long sequence
// the Reboot is faster (happens when memory block is - drawMemoryBlock16x8() full)
//
//
// Credits: most of the code was written by Cursor using prompts
//

#include <MD_MAX72xx.h>
#include <SPI.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES	12
#define CLK_PIN   4
#define DATA_PIN  6
#define CS_PIN    7

// Bunch of global variables, see in-fucntion

uint8_t intensity = 1;
uint8_t state = 0;
float fillPercent = 0.05; // 25% initially lit, adjust as needed
bool memoryBlock[8][16];  // [row][col], true if lit
bool memoryBlockInitialized = false;
bool shiftBlock[8][12];  // [row][col], true if lit
bool shiftBlockInitialized = false;

// WOPR block
float fillUL = 0.5;  // Upper left 4x4
float fillUR = 0.75; // Upper right 4x4
float fillBM = 0.25; // Bottom middle 5x3
bool block13x8_UL[4][4]; // Upper left 4x4
bool block13x8_UR[4][4]; // Upper right 4x4
bool block13x8_BM[3][5]; // Bottom middle 5x3
bool block13x8_initialized = false;

// Scrolling big 4-bits prime text vars
const char* scrollNumText = "627739294058898082130101526509219"; // a big 32 digit prime number
uint8_t scrollNumIndex = 0; // Current digit index in the string
uint8_t scrollNumBuffer[8]; // Holds the 8 most recent digits to display

// 4x8 block made og two 4x4 blocks
bool block4x8_upper[4][4];
bool block4x8_lower[4][4];
bool block4x8_initialized = false;

// Dialer counter 
uint8_t counter3x3 = 0; // 0..8, which LED in the 3x3 block is lit

MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

//
// 4x8 block
// Upper block: random, 6 unlit leds, updated state 0 & 2
// Lower Block, 8 leds lit, updated all states
//
void drawBlock4x8_special(uint8_t location, uint8_t state, uint8_t mode) {
  // Initialize or update upper block
  if (!block4x8_initialized || state == 0 || state == 2) {
    // Upper 4x4: 6 unlit, 10 lit
    uint8_t litCount = 0;
    // First, set all to false
    for (uint8_t y = 0; y < 4; y++)
      for (uint8_t x = 0; x < 4; x++)
        block4x8_upper[y][x] = false;
    // Randomly light 10 LEDs
    while (litCount < 10) {
      uint8_t rx = random(0, 4);
      uint8_t ry = random(0, 4);
      if (!block4x8_upper[ry][rx]) {
        block4x8_upper[ry][rx] = true;
        litCount++;
      }
    }
  }

  // Always update lower block
  // Lower 4x4: 8 lit, 8 unlit
  uint8_t litCount = 0;
  for (uint8_t y = 0; y < 4; y++)
    for (uint8_t x = 0; x < 4; x++)
      block4x8_lower[y][x] = false;
  while (litCount < 8) {
    uint8_t rx = random(0, 4);
    uint8_t ry = random(0, 4);
    if (!block4x8_lower[ry][rx]) {
      block4x8_lower[ry][rx] = true;
      litCount++;
    }
  }

  block4x8_initialized = true;

  // Draw the block
  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = 0; x < 4; x++) {
      bool val = false;
      if (y >= 4) { // Upper 4x4
        val = block4x8_upper[y - 4][x];
      } else { // Lower 4x4
        val = block4x8_lower[y][x];
      }
      mx.setPoint(7 - y, location + x, val); // 7-y: y=0 is bottom
    }
  }
}

//
// Scroll big prime in 4-bits
//
void drawScrollPrime4x8(uint8_t location, uint8_t state, uint8_t mode) {
  // Shift buffer up
  for (uint8_t i = 0; i < 7; i++) {
    scrollNumBuffer[i] = scrollNumBuffer[i + 1];
  }

  // Get next digit from string
  char nextChar = scrollNumText[scrollNumIndex];
  scrollNumIndex++;
  if (scrollNumText[scrollNumIndex] == '\0') scrollNumIndex = 0; // Loop

  // Store in buffer
  if (nextChar >= '0' && nextChar <= '9') {
    scrollNumBuffer[7] = nextChar - '0'; // 0-9
  } else {
    scrollNumBuffer[7] = 0; // Default to 0 for non-digits
  }

  // Draw the 8 rows
  for (uint8_t y = 0; y < 8; y++) {
    uint8_t code = scrollNumBuffer[y];
    for (uint8_t x = 0; x < 4; x++) {
      bool val = (code >> (3 - x)) & 0x01; // MSB left
      mx.setPoint(7 - y, location + x, val); // 7-y: y=0 is bottom
    }
  }
}


//
// "Memory" block
// Initial fill (fiiPercent constant), then add dots until matix is full, 'reboot' when full
//
uint8_t drawMemoryBlock16x8(uint8_t location, uint8_t state, uint8_t mode) {
  // Only operate if state is not 0
  if ((state == 1) || (state ==2) || (state == 3)) return 0;

  // Initialize on first call or if mode == 0
  if (!memoryBlockInitialized || mode == 0) {
    // Clear the block
    for (uint8_t y = 0; y < 8; y++) {
      for (uint8_t x = 0; x < 16; x++) {
        memoryBlock[y][x] = false;
      }
    }
    // Randomly light up fillPercent of the dots
    uint8_t toLight = (uint8_t)(128 * fillPercent);
    uint8_t lit = 0;
    while (lit < toLight) {
      uint8_t rx = random(0, 16);
      uint8_t ry = random(0, 8);
      if (!memoryBlock[ry][rx]) {
        memoryBlock[ry][rx] = true;
        lit++;
      }
    }
    memoryBlockInitialized = true;
  }

  // If mode == 1, randomly light up one more dot (if not already all lit)
  if (mode == 1) {
    // Count currently lit dots
    uint8_t lit = 0;
    for (uint8_t y = 0; y < 8; y++)
      for (uint8_t x = 0; x < 16; x++)
        if (memoryBlock[y][x]) lit++;
    if (lit < 128) {
      while (true) {
        uint8_t rx = random(0, 16);
        uint8_t ry = random(0, 8);
        if (!memoryBlock[ry][rx]) {
          memoryBlock[ry][rx] = true;
          break;
        }
      }
    }
  }

  // Draw the block
  uint8_t allLit = 1;
  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = 0; x < 16; x++) {
      mx.setPoint(7 - y, location + x, memoryBlock[y][x]);
      if (!memoryBlock[y][x]) allLit = 0;
    }
  }

  return allLit;
}

//
// Random 16x8 block - ignores state, always display, mode not used
//
uint8_t drawRandomBlock16x8(uint8_t location, uint8_t state, uint8_t mode) {
 
  for (uint8_t col = location; col < location + 16; col++) {
    uint8_t pattern = 0;
    if (mode == 0) {
      pattern = random(0, 256); // Initial random pattern for this column
    } else if (mode == 1) {
      pattern = random(0, 256); // Update logic (random for now)
    }
    for (uint8_t y = 0; y < 8; y++) {
      // y=0 is bottom, so row = 7 - y
      mx.setPoint(7 - y, col, (pattern >> y) & 0x01);
    }
  }
  return 0;
}

//
// "Processor" Block
// Three first columns are updated 1/4 of the time (state == 0 only)
// Col 3 is unlit
// cols 4-15 randomly updated every cycle (ignores state)
//
uint8_t drawProcessorBlock16x8_v2(uint8_t location, uint8_t state, uint8_t mode) {
  for (uint8_t relCol = 0; relCol < 16; relCol++) {
    uint8_t absCol = location + relCol;

    // Column 4 is never lit
    if (relCol == 3) {
      for (uint8_t y = 0; y < 8; y++) {
        mx.setPoint(7 - y, absCol, false);
      }
      continue;
    }

    // Columns 0, 1, 2: only update if state is 0 or 2
    if ((relCol == 0 || relCol == 1 || relCol == 2) && !(state == 0)) {
      continue;
    }

    // All other columns: always update
    uint8_t pattern = random(0, 256);
    for (uint8_t y = 0; y < 8; y++) {
      mx.setPoint(7 - y, absCol, (pattern >> y) & 0x01);
    }
  }
  return 0;
}


// Boot & reboot function
void reboot() {

// On line 1 (Well 6)(y=6, so row=1), light up first 16 columns one per second
  for (uint8_t col = 0; col < 16; col++) {
    mx.setPoint(6, col, true); // 7-6 = 1
    mx.update();
    delay(250); // 1/4 second per LED
  }
}

void boot() {
  //
  // fills in the three top lines and 77 columns, pausing a bit for each line
  for (uint8_t y = 0; y < 3; y++) { // y=0 is bottom
    for (uint8_t col = 0; col < 77; col++) {
      mx.setPoint(7 - y, col, true); // 7-y flips so 0 is bottom
    }
    mx.update();
    delay(150); // Fast, adjust as needed
  }
  delay(300);
  // Fills in next line, 36 leds, pause at each dot
  for (uint8_t col = 0; col < 36; col++) { // Half of 96 columns
    mx.setPoint(4, col, true);
    delay(200);
    mx.update();
  }
  delay(300);

  // On line 6 (y=6, so row=1), light up first 16 columns one per second
  for (uint8_t col = 0; col < 16; col++) {
    mx.setPoint(7 - 6, col, true); // 7-6 = 1
    mx.update();
    delay(500); // 1/2 second per LED
  }

  // Clear the display
  mx.clear();
  mx.update();
}

// Draws a 4x8 block at the specified location with the pattern described - ignores state, always display, mode not used
// Upper 4x4 block all lit except 5 random dots
// line 5 unlit
// lines 6,7 & 8 display 1057 in binary vertically, fixed - Defcon Lost (1057) Reference
//
void drawBlock4_1057(uint8_t location, uint8_t state, uint8_t mode) {

  // Top 4x4 block: all ON except 4 randomly OFF
  // y = 7 (top) to y = 4
  bool topBlock[4][4];
  for (uint8_t y = 0; y < 4; y++) {
    for (uint8_t x = 0; x < 4; x++) {
      topBlock[y][x] = true;
    }
  }
  // Randomly turn off 4 LEDs in the top 4x4 block
  uint8_t offCount = 0;
  while (offCount < 4) {
    uint8_t rx = random(0, 4);
    uint8_t ry = random(0, 4);
    if (topBlock[ry][rx]) {
      topBlock[ry][rx] = false;
      offCount++;
    }
  }
  // Set the top 4x4 block
  for (uint8_t y = 0; y < 4; y++) {
    for (uint8_t x = 0; x < 4; x++) {
      mx.setPoint(7 - y, location + x, topBlock[y][x]);
    }
  }

  // 5th line from top (y=3): all OFF
  for (uint8_t x = 0; x < 4; x++) {
    mx.setPoint(7 - 4, location + x, false); // y=4 is 5th from top
  }
  // Bottom 3 lines (y=0,1,2)
  // y=2 (6th from top): (0,0,1,1)
  mx.setPoint(7 - 5, location + 0, false);
  mx.setPoint(7 - 5, location + 1, false);
  mx.setPoint(7 - 5, location + 2, true);
  mx.setPoint(7 - 5, location + 3, true);

  // y=1 (7th from top): (0,0,0,1)
  mx.setPoint(7 - 6, location + 0, false);
  mx.setPoint(7 - 6, location + 1, false);
  mx.setPoint(7 - 6, location + 2, false);
  mx.setPoint(7 - 6, location + 3, true);

  // y=0 (8th from top): (1,0,1,1)
  mx.setPoint(7 - 7, location + 0, true);
  mx.setPoint(7 - 7, location + 1, false);
  mx.setPoint(7 - 7, location + 2, true);
  mx.setPoint(7 - 7, location + 3, true);
}

//
// 12x8 block - Registers shifts 
// Randomly initializes, then shifts the 12 bits (randomly by state)
//
void drawShiftBlock12x8(uint8_t location, uint8_t state, uint8_t mode) {
  // Only operate if state is not 0
  if (state == 0) return;

  // Initialize on first call or if mode == 0
  if (!shiftBlockInitialized || mode == 0) {
    for (uint8_t y = 0; y < 8; y++) {
      for (uint8_t x = 0; x < 12; x++) {
        shiftBlock[y][x] = random(0, 2); // 0 or 1
      }
    }
    shiftBlockInitialized = true;
  }

  // If mode == 1, for each row, maybe shift
  if (mode == 1) {
    for (uint8_t y = 0; y < 8; y++) {
      uint8_t r = random(0, 4); // 0,1,2,3
      if (r == state) {
        // Circular shift right by 1
        bool last = shiftBlock[y][11];
        for (int8_t x = 11; x > 0; x--) {
          shiftBlock[y][x] = shiftBlock[y][x - 1];
        }
        shiftBlock[y][0] = last;
      }
    }
  }

  // Draw the block
  for (uint8_t y = 0; y < 8; y++) {
    for (uint8_t x = 0; x < 12; x++) {
      mx.setPoint(7 - y, location + x, shiftBlock[y][x]);
    }
  }
}

//
// WOPR Block (top of console)
// 13x8 block, from the top row (0 to 7:
// - line 5 is always off
// - upper left 4x4 block, random lit at 50% (adjustable) lit, update on state 0 only
// - upper right 4x4 block,  randon lit at 75% (adjustable) , update on state 1 & 3 only
// - bottom middle, 5x3, random lit at 25% (adjustable), all states 
//
void drawWOPRBlock13x8(uint8_t location, uint8_t state, uint8_t mode) {
  // y=0 is bottom, y=7 is top

  // Initialization or mode 0: fill all blocks
  if (!block13x8_initialized || mode == 0) {
    // Upper left 4x4: x=0..3, y=4..7 (top 4 rows, left 4 columns)
    for (uint8_t y = 0; y < 4; y++)
      for (uint8_t x = 0; x < 4; x++)
        block13x8_UL[y][x] = (random(0, 100) < (fillUL * 100));

    // Upper right 4x4: x=9..12, y=4..7 (top 4 rows, right 4 columns)
    for (uint8_t y = 0; y < 4; y++)
      for (uint8_t x = 0; x < 4; x++)
        block13x8_UR[y][x] = (random(0, 100) < (fillUR * 100));

    // Bottom middle 5x3: x=4..8, y=0..2 (bottom 3 rows, middle 5 columns)
    for (uint8_t y = 0; y < 3; y++)
      for (uint8_t x = 0; x < 5; x++)
        block13x8_BM[y][x] = (random(0, 100) < (fillBM * 100));

    block13x8_initialized = true;
  } else {
    // Update logic
    // Upper left 4x4: update only if state == 0
    if (state == 0) {
      for (uint8_t y = 0; y < 4; y++)
        for (uint8_t x = 0; x < 4; x++)
          block13x8_UL[y][x] = (random(0, 100) < (fillUL * 100));
    }
    // Upper right 4x4: update only if state == 1 or 3
    if (state == 1 || state == 3) {
      for (uint8_t y = 0; y < 4; y++)
        for (uint8_t x = 0; x < 4; x++)
          block13x8_UR[y][x] = (random(0, 100) < (fillUR * 100));
    }
    // Bottom middle 5x3: always update
    for (uint8_t y = 0; y < 3; y++)
      for (uint8_t x = 0; x < 5; x++)
        block13x8_BM[y][x] = (random(0, 100) < (fillBM * 100));
  }

  // Draw the block
  for (uint8_t y = 0; y < 8; y++) { // y=0 is bottom, y=7 is top
    for (uint8_t x = 0; x < 13; x++) {
      bool val = false;
      // Line 5 from top (y=2 from top, so y=5 from bottom): always OFF
      if (y == 3) {
        val = false;
      }
      // Upper left 4x4: x=0..3, y=4..7 (top 4 rows, left 4 columns)
      else if (x < 4 && y >= 4) {
        val = block13x8_UL[y][x];
      }
      // Upper right 4x4: x=9..12, y=4..7 (top 4 rows, right 4 columns)
      else if (x >= 9 && y >= 4) {
        val = block13x8_UR[y][x - 9];
      }
      // Bottom middle 5x3: x=4..8, y=0..2 (bottom 3 rows, middle 5 columns)
      else if (x >= 4 && x <= 8 && y <= 2) {
        val = block13x8_BM[7-y][x - 4];
      }
      // All other positions: OFF
      else {
        val = false;
      }
      mx.setPoint(y, location + x, val); // 7-y flips y so 0 is bottom
    }
  }
}


//
// Setup
//
void setup() {
  mx.begin();
  mx.clear();
  randomSeed(analogRead(0));
  mx.control(MD_MAX72XX::INTENSITY, intensity);
  boot();
  state = 0;
  // Scroll Prime setup
  scrollNumIndex = 0;
  for (uint8_t i = 0; i < 8; i++) {
    char c = scrollNumText[scrollNumIndex++];
    if (c >= '0' && c <= '9') scrollNumBuffer[i] = c - '0';
    else scrollNumBuffer[i] = 0;
    if (scrollNumText[scrollNumIndex] == '\0') scrollNumIndex = 0;
  }
}

//
// 311 555 2368 Dialer No counter 
//
void drawDigitInColumn(uint8_t digit, uint8_t col) {
  for (uint8_t y = 0; y < 8; y++) {
    bool val = (digit >> (7 - y)) & 0x01; // Only bottom 4 bits will be used for 0-9
    mx.setPoint(7 - y, col, val); // 7-y: y=0 is bottom
  }
}
void drawDialBlock3x8(uint8_t location, uint8_t state, uint8_t mode) {
  uint8_t digits[3] = {0, 0, 0};
  bool blank[3] = {false, false, false};

  switch (state) {
    case 0: // 311
      digits[0] = 3;
      digits[1] = 1;
      digits[2] = 1;
      break;
    case 1: // 555
      digits[0] = 5;
      digits[1] = 5;
      digits[2] = 5;
      break;
    case 2: // 236
      digits[0] = 2;
      digits[1] = 3;
      digits[2] = 6;
      break;
    case 3: // 8, two columns blank
      digits[0] = 8;
      blank[1] = true;
      blank[2] = true;
      break;
    default:
      break;
  }

  // Draw each column
  for (uint8_t i = 0; i < 3; i++) {
    if (blank[i]) {
      // Blank column
      for (uint8_t y = 0; y < 8; y++) {
        mx.setPoint(7 - y, location + i, false);
      }
    } else {
      // Draw digit in binary (bottom 4 bits)
      for (uint8_t y = 0; y < 8; y++) {
        bool val = (digits[i] >> (7 - y)) & 0x01; // Only bottom 4 bits will be used for 0-9
        mx.setPoint(7 - y, location + i, val);
      }
    }
  }
}

// Dialer with counter
void drawDialBlock3x8_withCounter(uint8_t location, uint8_t state, uint8_t mode) {
  uint8_t digits[3] = {0, 0, 0};
  bool blank[3] = {false, false, false};

  switch (state) {
    case 0: // 311
      digits[0] = 3;
      digits[1] = 1;
      digits[2] = 1;
      break;
    case 1: // 555
      digits[0] = 5;
      digits[1] = 5;
      digits[2] = 5;
      break;
    case 2: // 236
      digits[0] = 2;
      digits[1] = 3;
      digits[2] = 6;
      break;
    case 3: // 8, two columns blank
      digits[0] = 8;
      blank[1] = true;
      blank[2] = true;
      break;
    default:
      break;
  }

  // Draw the 3x3 counter block (rows 7, 6, 5)
  for (uint8_t y = 0; y < 3; y++) {         // y: 0=top, 2=bottom of 3x3
    for (uint8_t x = 0; x < 3; x++) {       // x: 0=left, 2=right
      uint8_t ledIndex = y * 3 + x;         // 0..8
      bool val = (ledIndex <= counter3x3);
      mx.setPoint(7 - y, location + x, val);
    }
  }

  // Draw the digits in the bottom 4 rows (rows 0–3)
  for (uint8_t i = 0; i < 3; i++) {
    if (blank[i]) {
      // Blank column
      for (uint8_t y = 0; y < 4; y++) {
        mx.setPoint(3 - y, location + i, false); // rows 3,2,1,0
      }
    } else {
      // Draw digit in binary (bottom 4 bits)
      for (uint8_t y = 0; y < 4; y++) {
        bool val = (digits[i] >> y) & 0x01; // LSB at bottom
        mx.setPoint(y, location + i, val);  // y=0 is bottom
      }
    }
  }

  // Update the counter for next call
  counter3x3++;
  if (counter3x3 > 8) counter3x3 = 0;
}

//
// Main Loop
//
void loop() {
  // 1057 Block
  drawBlock4_1057(0, state, 1);
  mx.update();
  
  // full random block 
  drawRandomBlock16x8(5, state, 1);
  mx.update();
  
  // processor simulation (3 cols random ops code, 1 col blank, 8 bits random data)
  drawProcessorBlock16x8_v2(22, state, 1);

  // 4x8 Special, two 4x4 random blocks with various feel (see function)
  drawBlock4x8_special(39, 2, 0); // Example location, state, mode
  mx.update();

  // memory block (see function)
  uint8_t filled = drawMemoryBlock16x8(44, state, 1); // location 10, state 1, mode 1
  mx.update();
  if (filled) {
    // Memory bank is full, do something!
    delay(2000);
    mx.clear();
    delay(2000);
    reboot();
    // Optionally reset for demo:
    memoryBlockInitialized = false;
  }

  // Prime Scroll block 
  drawScrollPrime4x8(61, 1, 0); // Example location, state, mode
  mx.update();  

  // WOPR Block (see function)  
  drawWOPRBlock13x8(66, state, 1);
  mx.update();

  // Shift registers block 
  drawShiftBlock12x8(80, state, 1);
  mx.update();
  
  // 3x8 block 311 555 2368 dialier
  drawDialBlock3x8_withCounter(93, state, 0); // Example location, state, mode
  mx.update();

  delay(250);
  //
  // update state
  //
  state++;
  if (state == 4) {
    state = 0;
  }
}