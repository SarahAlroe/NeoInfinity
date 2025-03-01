/*
   Some notes:
   - If possible, go for 20 mHz clock. Animation speeds are scaled, but somewhat flickery.
   - Disable millis() and use minimal printf() to reduce size. Expect compile to use upwards of 95% flash.
*/

#include <tinyNeoPixel_Static.h>
#include <EEPROM.h>

// Config
#define LIGHT_ATTENUATION_LEVEL 3       // 0-8 Limits brightness of pixels. Higher means darker.
#define DO_ERROR_DIFFUSION true         // Should the pixels use temporal dithering through error diffusion. 
#define GAMMA_CORRECTION_COMPLEXITY 1   // 0,1,2 Switches Gamma correction algorithm. Higher makes larger/slower, but more accurate to x^2.6
#define SINGLE_COLOR_SINGLE_SPEED true  // Skip speed 0 and 2 for the single color gradients

// Patterns to cycle through
#define GRADIENT_COUNT 5  // Five gradients: Pastel rainbow, red, yellow, green, HSV rainbow (Last one calculated seperately)
#define DIRECTION_COUNT 3 // Three directions: Infinity symbol, Left-to-right, randomly distributed
#define SPEED_COUNT 3     // Three animation speeds, see below

// Animation speeds
// Scale based on cpu frequency, to maintain same speed
#define ANIM_SPEED_0 8 * 20000000 / F_CPU
#define ANIM_SPEED_1 30 * 20000000 / F_CPU
#define ANIM_SPEED_2 80 * 20000000 / F_CPU

// Alt speed set for photosensitivity
// #define ANIM_SPEED_0 0
// #define ANIM_SPEED_1 8 * 20000000 / F_CPU
// #define ANIM_SPEED_2 30 * 20000000 / F_CPU

// Default, first boot values
#define DEFAULT_BRIGHTNESS 18
#define DEFAULT_SPEED 0
#define DEFAULT_GRADIENT 0
#define DEFAULT_DIRECTION 0

// Button input specifics
#define SWITCH_DEBOUNCE 10                            // ms Debounce time to sure actual press.
#define SWITCH_SECONDARY_DELAY 200                    // ms Time where a press means cycle patterns, after cycle brightness.
#define SWITCH_BRIGHTNESS_DELAY 30 * 20000000 / F_CPU // Cycles of showing pixels while setting brightness, normalized to cpu speed.
#define SWITCH_BRIGHTNESS_ACCEL 5                     // 0-8 How much faster should it be to skip through brighter brightness.

// Hardware
#define BUTTON_PIN PIN_PA2
#define PIXEL_PIN PIN_PA1
#define PIXEL_COUNT 27

// EEPROM
#define ADDRESS_NEW 0
#define ADDRESS_BRIGHTNESS 1*4
#define ADDRESS_SPEED 2*4
#define ADDRESS_GRADIENT 3*4
#define ADDRESS_DIRECTION 4*4
#define EEPROM_SET 42

struct IColor {
  uint16_t r;
  uint16_t g;
  uint16_t b;
};

// Optimal 8-bit dither pattern. See http://www.cv.ulichney.com/papers/1998-1d-dither.pdf https://gist.github.com/SarahAlroe/706621091b395e601d7d15213bfbb540 
const uint8_t dither8 [256] = {0, 128, 64, 192, 32, 160, 96, 224, 16, 144, 80, 208, 48, 176, 112, 240, 8, 136, 72, 200, 40, 168, 104, 232, 24, 152, 88, 216, 56, 184, 120, 248, 4, 132, 68, 196, 36, 164, 100, 228, 20, 148, 84, 212, 52, 180, 116, 244, 12, 140, 76, 204, 44, 172, 108, 236, 28, 156, 92, 220, 60, 188, 124, 252, 2, 130, 66, 194, 34, 162, 98, 226, 18, 146, 82, 210, 50, 178, 114, 242, 10, 138, 74, 202, 42, 170, 106, 234, 26, 154, 90, 218, 58, 186, 122, 250, 6, 134, 70, 198, 38, 166, 102, 230, 22, 150, 86, 214, 54, 182, 118, 246, 14, 142, 78, 206, 46, 174, 110, 238, 30, 158, 94, 222, 62, 190, 126, 254, 1, 129, 65, 193, 33, 161, 97, 225, 17, 145, 81, 209, 49, 177, 113, 241, 9, 137, 73, 201, 41, 169, 105, 233, 25, 153, 89, 217, 57, 185, 121, 249, 5, 133, 69, 197, 37, 165, 101, 229, 21, 149, 85, 213, 53, 181, 117, 245, 13, 141, 77, 205, 45, 173, 109, 237, 29, 157, 93, 221, 61, 189, 125, 253, 3, 131, 67, 195, 35, 163, 99, 227, 19, 147, 83, 211, 51, 179, 115, 243, 11, 139, 75, 203, 43, 171, 107, 235, 27, 155, 91, 219, 59, 187, 123, 251, 7, 135, 71, 199, 39, 167, 103, 231, 23, 151, 87, 215, 55, 183, 119, 247, 15, 143, 79, 207, 47, 175, 111, 239, 31, 159, 95, 223, 63, 191, 127, 255};

// How far along a path from 0 to 2^16-1 is each pixel
const uint16_t PIXEL_POSITIONS [DIRECTION_COUNT][PIXEL_COUNT] = {
  { // On the infinity symbol
    2333,
    4450,
    6594,
    8952,
    11456,
    14012,
    16446,
    19019,
    21475,
    23966,
    26375,
    28617,
    30734,
    32840,
    34939,
    37121,
    39294,
    41695,
    44173,
    46723,
    49164,
    51614,
    54164,
    56541,
    58886,
    61095,
    63177
  }, { // From left to right
    37145,
    41608,
    46520,
    52555,
    59078,
    64031,
    65535,
    63589,
    58725,
    52268,
    46077,
    40948,
    36733,
    32723,
    28654,
    24144,
    19280,
    13089,
    6633,
    1591,
    0,
    1680,
    6721,
    12868,
    18926,
    24056,
    28301
  }, { // In a random equal distribution
    53398,
    24272,
    0,
    19417,
    55826,
    48544,
    63107,
    36408,
    9708,
    16990,
    33981,
    21845,
    46117,
    58253,
    43690,
    31553,
    50971,
    29126,
    14563,
    2427,
    41262,
    60680,
    38835,
    4854,
    12136,
    26699,
    7281
  }
};

// Color gradients consisting of four equally spaced colors. 8 Bits per color
const uint8_t quadGradientColors[GRADIENT_COUNT - 1][4][3] = {
  { // Pastels
    {0, 184, 239},
    {104, 227, 149},
    {250, 149, 71},
    {240, 107, 185},
  },
  { // Red
    {255, 0, 0},
    {255, 90, 0},
    {255, 0, 0},
    {190, 0, 0},
  },
  { // Yellow
    {255, 200, 0},
    {255, 160, 0},
    {255, 200, 0},
    {200, 180, 0},
  },
  { // Green
    {50, 255, 0},
    {200, 255, 0},
    {50, 255, 0},
    {50, 190, 0},
  }
};

// Three destinct moving speeds, see above
const uint8_t MOVING_SPEEDS[SPEED_COUNT] = {
  ANIM_SPEED_0,
  ANIM_SPEED_1,
  ANIM_SPEED_2
};

byte pixels[PIXEL_COUNT * 3];
tinyNeoPixel strip = tinyNeoPixel(PIXEL_COUNT, PIXEL_PIN, NEO_GRB, pixels);
//uint8_t ditherError[PIXEL_COUNT];
uint8_t currentSpeed;
uint8_t currentGradient;
uint8_t currentDirection;
uint8_t brightness;
bool buttonInterrupt = false;
uint16_t movingOffset;
uint8_t movingSpeed;
uint8_t ditherPos;


void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  PORTA.PIN2CTRL |= PORT_ISC_FALLING_gc; // Interrupt on falling edge of button
  pinMode(PIXEL_PIN, OUTPUT);
  loadEEPROM();
  movingSpeed = MOVING_SPEEDS[currentSpeed];
  strip.show();
}

void loop() {
  if (buttonInterrupt) {
    checkButton();
  }
  updateStrip();
}

ISR(PORTA_PORT_vect) {
  PORTA.INTFLAGS = 255; //clear flags
  buttonInterrupt = true;
}

void checkButton() {
  delay(SWITCH_DEBOUNCE); // Debounce
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(SWITCH_SECONDARY_DELAY);
    if (digitalRead(BUTTON_PIN) == LOW) { //If still held after a while, start changing brightness
      while (digitalRead(BUTTON_PIN) == LOW) {
        // Change brightness
        brightness++;
        uint16_t setColor = UINT16_MAX;
        for (uint8_t delayCounter = 0; delayCounter < SWITCH_BRIGHTNESS_DELAY; // Loop instead of delay to get accurate temporal dither.
             delayCounter += 1 + brightness / (1 << (8 - SWITCH_BRIGHTNESS_ACCEL))) { // Increase loop speed with brightness to keep perceptually similar pace
          for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
            setColorProcessed(i, &setColor, &setColor, &setColor);
          }
          strip.show();
        }
      }
    } else {
      // Update pattern
      currentSpeed ++;
      if (SINGLE_COLOR_SINGLE_SPEED && (currentGradient == 1 || currentGradient == 2 || currentGradient == 3)){
        currentSpeed ++; // If moving through the single-color gradients, only use speed 1
      }
      if (currentSpeed >= SPEED_COUNT) {
        currentSpeed = 0;
        currentGradient ++;
        if (SINGLE_COLOR_SINGLE_SPEED && (currentGradient == 1 || currentGradient == 2 || currentGradient == 3)){
          currentSpeed ++; // If moving through the single-color gradients, only use speed 1
        }
        if (currentGradient >= GRADIENT_COUNT) {
          currentGradient = 0;
          currentDirection ++;
          if (currentDirection >= DIRECTION_COUNT) {
            currentDirection = 0;
          }
        }
      }
      movingSpeed = MOVING_SPEEDS[currentSpeed];
    }
    saveEEPROM();
  }
  buttonInterrupt = false;
}

void updateStrip() {
  if (currentGradient < (GRADIENT_COUNT - 1)) {
    for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
      uint16_t pixelOffset = movingOffset + PIXEL_POSITIONS[currentDirection][i]; // Will overflow to 0 as necessary
      IColor color;
      gradientColor(currentGradient, pixelOffset, &color);
      setColorProcessed(i, &color);
    }
  } else {
    for (uint8_t i = 0; i < PIXEL_COUNT; i++) {
      uint16_t pixelOffset = movingOffset + PIXEL_POSITIONS[currentDirection][i]; // Will overflow to 0 as necessary
      IColor color;
      hslToIColor(pixelOffset, &color);
      setColorProcessed(i, &color);
    }
  }
  strip.show();
  ditherPos ++;
  movingOffset = movingOffset + movingSpeed;
}

void gradientColor(uint8_t gradient, uint32_t point, IColor *color) {
  if (point < 65535 / 4) {
    color->r = (((65535 / 4 - point) * ((uint32_t)quadGradientColors[gradient][0][0])) + (point * ((uint32_t)quadGradientColors[gradient][1][0]))) >> 6;
    color->g = (((65535 / 4 - point) * ((uint32_t)quadGradientColors[gradient][0][1])) + (point * ((uint32_t)quadGradientColors[gradient][1][1]))) >> 6;
    color->b = (((65535 / 4 - point) * ((uint32_t)quadGradientColors[gradient][0][2])) + (point * ((uint32_t)quadGradientColors[gradient][1][2]))) >> 6;
    return;
  }
  if (point < 65535 / 2) {
    point = (point - 65535 / 4);
    color->r = ((65535 / 4 - point) * ((uint32_t)quadGradientColors[gradient][1][0]) + (point * ((uint32_t)quadGradientColors[gradient][2][0]))) >> 6;
    color->g = ((65535 / 4 - point) * ((uint32_t)quadGradientColors[gradient][1][1]) + (point * ((uint32_t)quadGradientColors[gradient][2][1]))) >> 6;
    color->b = ((65535 / 4 - point) * ((uint32_t)quadGradientColors[gradient][1][2]) + (point * ((uint32_t)quadGradientColors[gradient][2][2]))) >> 6;
    return;
  }
  if (point < 65535 * 3 / 4) {
    point = (point - 65535 / 2);
    color->r = ((65535 / 4 - point) * ((uint32_t)quadGradientColors[gradient][2][0]) + (point * ((uint32_t)quadGradientColors[gradient][3][0]))) >> 6;
    color->g = ((65535 / 4 - point) * ((uint32_t)quadGradientColors[gradient][2][1]) + (point * ((uint32_t)quadGradientColors[gradient][3][1]))) >> 6;
    color->b = ((65535 / 4 - point) * ((uint32_t)quadGradientColors[gradient][2][2]) + (point * ((uint32_t)quadGradientColors[gradient][3][2]))) >> 6;
    return;
  }
  point = (point - 65535 * 3 / 4);
  color->r = ((65535 / 4 - point) * ((uint32_t)quadGradientColors[gradient][3][0]) + (point * ((uint32_t)quadGradientColors[gradient][0][0]))) >> 6;
  color->g = ((65535 / 4 - point) * ((uint32_t)quadGradientColors[gradient][3][1]) + (point * ((uint32_t)quadGradientColors[gradient][0][1]))) >> 6;
  color->b = ((65535 / 4 - point) * ((uint32_t)quadGradientColors[gradient][3][2]) + (point * ((uint32_t)quadGradientColors[gradient][0][2]))) >> 6;
  return;
}

void hslToIColor(uint16_t h, IColor *color) {
  color->r = hueToRgb(h + 65535 / 3);
  color->g = hueToRgb(h);
  color->b = hueToRgb(h + 65535 / 3 * 2);
  return;
}

uint16_t hueToRgb(uint16_t t) {
  if (t < 65535 / 6) return 6 * t;
  if (t < 65535 / 2) return 65535;
  if (t < 65535 / 3 * 2) return 65535 - (t * 6);
  return 0;
}

uint16_t gamma(uint32_t value) {
#if GAMMA_CORRECTION_COMPLEXITY == 0
  // Normalized square
  return (uint16_t)((value * value) >> 16);
#elif GAMMA_CORRECTION_COMPLEXITY == 1
  // Normalized cube
  return (uint16_t)((((value * value) >> 16) * value ) >> 16);
#else
  // Average of normalized square and square of square
  uint32_t firstIteration = (value * value) >> 16;
  uint32_t secondIteration = (firstIteration * firstIteration) >> 16;
  return (uint16_t)((firstIteration + secondIteration) / 2); // Get average of the two
#endif
}

void setColorProcessed(uint8_t pixel, IColor *color) {
  setColorProcessed(pixel, &color->r, &color->g, &color->b);
}

void setColorProcessed(uint8_t pixel, uint16_t *pRed, uint16_t *pGreen, uint16_t *pBlue) {
  uint32_t red = (uint32_t) * pRed;
  uint32_t green = (uint32_t) * pGreen;
  uint32_t blue = (uint32_t) * pBlue;

  // Calculate gamma
  red = gamma(red);
  green = gamma(green);
  blue = gamma(blue);

  // Calculate brightness
  red = (red * (uint32_t)brightness) / ( 1 << (8 + LIGHT_ATTENUATION_LEVEL));
  green = (green * (uint32_t)brightness) / (1 << (8 + LIGHT_ATTENUATION_LEVEL));
  blue = (blue * (uint32_t)brightness) / (1 << (8 + LIGHT_ATTENUATION_LEVEL));


#if DO_ERROR_DIFFUSION

  // Get error
  uint8_t redError = red & 0xFF;
  uint8_t greenError = green & 0xFF;
  uint8_t blueError = blue & 0xFF;

  // Shift to 8-bit
  red = red >> 8;
  green = green >> 8;
  blue = blue >> 8;
  
  // Add position to distribute any flickering
  uint8_t ditherPixelPos = ditherPos + pixel*8; 

  // Increment if error larger than pattern lookup
  red += redError >= dither8[ditherPixelPos];
  green += greenError >= dither8[ditherPixelPos + 1];
  blue += blueError >= dither8[ditherPixelPos + 2];
  
#else

  // shift to regular 8 bit values
  red = red >> 8;
  green = green >> 8;
  blue = blue >> 8;
  
#endif


#if LIGHT_ATTENUATION_LEVEL == 0
  // clamp on rare chance of dither overflow, and output
  // Will not happen with LIGHT_ATTENUATION_LEVEL > 0
  red = min(red, 0xFFu);
  green = min(green, 0xFFu);
  blue = min(blue, 0xFFu);
#endif

  strip.setPixelColor(pixel, red, green, blue);
}


void loadEEPROM() {
  // If the EEPROM has never been used before, save default values.
  if (EEPROM.read(ADDRESS_NEW) != EEPROM_SET) {
    EEPROM.update(ADDRESS_GRADIENT, DEFAULT_GRADIENT);
    EEPROM.update(ADDRESS_SPEED, DEFAULT_SPEED);
    EEPROM.update(ADDRESS_DIRECTION, DEFAULT_DIRECTION);
    EEPROM.update(ADDRESS_BRIGHTNESS, DEFAULT_BRIGHTNESS);
    EEPROM.update(ADDRESS_NEW, EEPROM_SET);
  }

  // Load values from EEPROM
  currentGradient = EEPROM.read(ADDRESS_GRADIENT);
  currentSpeed = EEPROM.read(ADDRESS_SPEED);
  currentDirection = EEPROM.read(ADDRESS_DIRECTION);
  brightness = EEPROM.read(ADDRESS_BRIGHTNESS);
}

void saveEEPROM() {
  // Update whatever thing changed this time.
  EEPROM.update(ADDRESS_GRADIENT, currentGradient);
  EEPROM.update(ADDRESS_SPEED, currentSpeed);
  EEPROM.update(ADDRESS_DIRECTION, currentDirection);
  EEPROM.update(ADDRESS_BRIGHTNESS, brightness);
}
