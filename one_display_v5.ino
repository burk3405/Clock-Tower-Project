#include <TFT_eSPI.h>
#include <math.h>

// --------------------------------------------------
// MINI CLOCK TOWER - SINGLE DISPLAY, REFINED DIAL
// ESP32 DevKit V1 + one GC9A01 240 x 240 round TFT
// No RTC required: time is based on millis().
//
// This implementation follows the Mini Clock Tower Watch-Face Brand
// Guide: an edge-to-edge warm-ivory dial, twelve radial graphic markers
// (single except for paired cardinal markers), tapered charcoal hands,
// and a small graphite pivot. There are intentionally no numerals,
// words, ticks, or decorative rings.
// --------------------------------------------------

TFT_eSPI tft = TFT_eSPI();

const int SCREEN_SIZE = 240;
const int CENTER_X = SCREEN_SIZE / 2;
const int CENTER_Y = SCREEN_SIZE / 2;
const int FACE_RADIUS = 119;
// Keep the marker ends near the perimeter and extend them inward to
// double their original 20-pixel radial length.
const float MARKER_INNER_RADIUS = 71.0f;
const float MARKER_OUTER_RADIUS = 111.0f;
const float MARKER_HALF_WIDTH = 1.5f;
const float DOUBLE_MARKER_OFFSET = 3.5f;

// Change these before uploading. Uses 24-hour time.
const int START_HOUR = 12;
const int START_MINUTE = 0;

// Daylight starts at the existing neutral ivory. At night it blends to
// a warmer cream, never to a bluer/cooler white. Adjust these if the
// installed display's colour calibration needs refinement.
const uint8_t DAY_RED = 239;
const uint8_t DAY_GREEN = 235;
const uint8_t DAY_BLUE = 232;
const uint8_t NIGHT_RED = 248;
const uint8_t NIGHT_GREEN = 229;
const uint8_t NIGHT_BLUE = 202;

const uint16_t INK_COLOR = 0x18E3;       // near-black neutral charcoal
const uint16_t HUB_COLOR = 0x31A6;       // understated lighter graphite

int currentHour = START_HOUR;
int currentMinute = START_MINUTE;
unsigned long lastMinuteTick = 0;

float toRadians(float degrees) {
  return degrees * PI / 180.0f;
}

uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return ((uint16_t)(red & 0xF8) << 8) |
         ((uint16_t)(green & 0xFC) << 3) |
         (blue >> 3);
}

uint8_t blendChannel(uint8_t day, uint8_t night, float amount) {
  return (uint8_t)roundf(day + (night - day) * amount);
}

// Return 0.0 in daylight, 1.0 during the dark part of the night, with
// a three-hour gradual transition after sunset and before sunrise.
float nightWarmth(int hour, int minute) {
  float time = hour + minute / 60.0f;

  if (time >= 7.0f && time < 18.0f) {
    return 0.0f;
  }
  if (time >= 18.0f && time < 21.0f) {
    return (time - 18.0f) / 3.0f;
  }
  if (time >= 4.0f && time < 7.0f) {
    return (7.0f - time) / 3.0f;
  }
  return 1.0f;
}

uint16_t faceColorForTime() {
  float warmth = nightWarmth(currentHour, currentMinute);
  return rgb565(
    blendChannel(DAY_RED, NIGHT_RED, warmth),
    blendChannel(DAY_GREEN, NIGHT_GREEN, warmth),
    blendChannel(DAY_BLUE, NIGHT_BLUE, warmth)
  );
}

// Draw a clean triangular hand: broad at the pivot and sharply pointed
// at its outer end. The base extends beneath the pivot cap, leaving no gap.
void drawTaperedHand(float angleDegrees,
                     float length,
                     float tail,
                     float baseHalfWidth,
                     uint16_t color) {
  float angle = toRadians(angleDegrees);
  float dx = cosf(angle);
  float dy = sinf(angle);
  float px = -dy;
  float py = dx;

  int tipX = (int)roundf(CENTER_X + dx * length);
  int tipY = (int)roundf(CENTER_Y + dy * length);
  int tailLeftX = (int)roundf(CENTER_X - dx * tail + px * baseHalfWidth);
  int tailLeftY = (int)roundf(CENTER_Y - dy * tail + py * baseHalfWidth);
  int tailRightX = (int)roundf(CENTER_X - dx * tail - px * baseHalfWidth);
  int tailRightY = (int)roundf(CENTER_Y - dy * tail - py * baseHalfWidth);

  tft.fillTriangle(tipX, tipY, tailLeftX, tailLeftY,
                   tailRightX, tailRightY, color);
}

// Draw one narrow rectangular radial marker, optionally shifted along
// its tangent. Filled triangles keep the stroke consistent at all angles.
void drawRadialMarker(float angle, float tangentOffset) {
  float dx = cosf(angle);
  float dy = sinf(angle);
  float px = -dy;
  float py = dx;

  float outerX = CENTER_X + dx * MARKER_OUTER_RADIUS + px * tangentOffset;
  float outerY = CENTER_Y + dy * MARKER_OUTER_RADIUS + py * tangentOffset;
  float innerX = CENTER_X + dx * MARKER_INNER_RADIUS + px * tangentOffset;
  float innerY = CENTER_Y + dy * MARKER_INNER_RADIUS + py * tangentOffset;

  int outerLeftX = (int)roundf(outerX + px * MARKER_HALF_WIDTH);
  int outerLeftY = (int)roundf(outerY + py * MARKER_HALF_WIDTH);
  int outerRightX = (int)roundf(outerX - px * MARKER_HALF_WIDTH);
  int outerRightY = (int)roundf(outerY - py * MARKER_HALF_WIDTH);
  int innerLeftX = (int)roundf(innerX + px * MARKER_HALF_WIDTH);
  int innerLeftY = (int)roundf(innerY + py * MARKER_HALF_WIDTH);
  int innerRightX = (int)roundf(innerX - px * MARKER_HALF_WIDTH);
  int innerRightY = (int)roundf(innerY - py * MARKER_HALF_WIDTH);

  tft.fillTriangle(outerLeftX, outerLeftY, outerRightX, outerRightY,
                   innerLeftX, innerLeftY, INK_COLOR);
  tft.fillTriangle(outerRightX, outerRightY, innerRightX, innerRightY,
                   innerLeftX, innerLeftY, INK_COLOR);
}

void drawHourMarkers() {
  for (int hour = 0; hour < 12; hour++) {
    float angle = toRadians(hour * 30.0f - 90.0f);
    bool isCardinal = (hour % 3 == 0);

    if (isCardinal) {
      drawRadialMarker(angle, -DOUBLE_MARKER_OFFSET);
      drawRadialMarker(angle, DOUBLE_MARKER_OFFSET);
    } else {
      drawRadialMarker(angle, 0.0f);
    }
  }
}

void drawClockFace(uint16_t faceColor) {
  // A single-pixel dark boundary defines the dial without consuming
  // usable area as a thick bezel would.
  tft.fillScreen(TFT_BLACK);
  tft.fillCircle(CENTER_X, CENTER_Y, FACE_RADIUS - 1, faceColor);
  tft.drawCircle(CENTER_X, CENTER_Y, FACE_RADIUS, INK_COLOR);
  drawHourMarkers();
}

void drawClock() {
  uint16_t faceColor = faceColorForTime();
  drawClockFace(faceColor);

  float hourAngle =
    (currentHour % 12) * 30.0f + currentMinute * 0.5f - 90.0f;
  float minuteAngle = currentMinute * 6.0f - 90.0f;

  // The short, broader hour hand extends visibly behind the pivot.
  drawTaperedHand(hourAngle, 57.0f, 18.0f, 8.0f, INK_COLOR);

  // The longer minute hand is drawn last, above the hour hand, with a
  // matching longer tail beyond the central pivot.
  drawTaperedHand(minuteAngle, 103.0f, 20.0f, 5.4f, INK_COLOR);

  // Small, unadorned graphite pivot cap drawn after both hands.
  tft.fillCircle(CENTER_X, CENTER_Y, 5, HUB_COLOR);
}

void advanceMinute() {
  currentMinute++;
  if (currentMinute >= 60) {
    currentMinute = 0;
    currentHour = (currentHour + 1) % 24;
  }
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(0);

  lastMinuteTick = millis();
  drawClock();

  Serial.println("Single-display refined clock started (no RTC).");
  Serial.println("The face warms automatically from sunset to sunrise.");
}

void loop() {
  unsigned long now = millis();

  // Unsigned subtraction remains correct across millis() rollover.
  while ((unsigned long)(now - lastMinuteTick) >= 60000UL) {
    lastMinuteTick += 60000UL;
    advanceMinute();
    drawClock();

    Serial.printf("Clock time: %02d:%02d\n", currentHour, currentMinute);
  }

  delay(20);
}
