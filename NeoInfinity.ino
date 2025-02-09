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

// Temporal dithering error diffusion
#define RED 0b11100000
#define RED_OFFSET 5
#define GREEN 0b00011100
#define GREEN_OFFSET 2
#define BLUE 0b00000011
#define BLUE_OFFSET 0

struct IColor {
  uint16_t r;
  uint16_t g;
  uint16_t b;
};

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
uint8_t ditherError[PIXEL_COUNT];
uint8_t currentSpeed;
uint8_t currentGradient;
uint8_t currentDirection;
uint8_t brightness;
bool buttonInterrupt = false;
uint16_t movingOffset;
uint8_t movingSpeed;


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

  // Bitshift to to get 10-11 bit int (with 2-3 bit offset) from 16 bit colors
  red = red >> (8 - 3); // 2⁸*2³ = 2¹¹
  green = green >> (8 - 3); // 2⁸*2³ = 2¹¹
  blue = blue >> (8 - 2); // 2⁸*2² = 2¹⁰

#if DO_ERROR_DIFFUSION
  // Get last error and add
  uint8_t redError = (ditherError[pixel] & RED) >> RED_OFFSET;
  uint8_t greenError = (ditherError[pixel] & GREEN) >> GREEN_OFFSET;
  uint8_t blueError = (ditherError[pixel] & BLUE) >> BLUE_OFFSET;

  // Add error
  red += redError;
  green += greenError;
  blue += blueError;


  // Save new error
  redError = red & 0b111;
  greenError = green & 0b111;
  blueError = blue & 0b11;

  ditherError[pixel] = (uint8_t)((redError << RED_OFFSET) | (greenError << GREEN_OFFSET) | (blueError << BLUE_OFFSET));
#endif

  // shift to regular 8 bit values
  red = red >> 3;
  green = green >> 3;
  blue = blue >> 2;

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
