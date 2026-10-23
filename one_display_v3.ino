#include <TFT_eSPI.h>
#include <math.h>

// --------------------------------------------------
// MINI CLOCK TOWER - SINGLE DISPLAY TEST
// ESP32 DevKit V1 + one GC9A01 240x240 TFT
// No DS3231 RTC required. Uses millis() for timekeeping.
// --------------------------------------------------

TFT_eSPI tft = TFT_eSPI();

const int CENTER_X = 120;
const int CENTER_Y = 120;
const int FACE_RADIUS = 119;

// 24-hour time: 3:45 PM = 15 and 45.
const int START_HOUR = 12;
const int START_MINUTE = 0;

// RGB565 palette. Daylight remains natural ivory, not blue-white.
const uint16_t DAY_FACE   = 0xFFDF;
const uint16_t DUSK_FACE  = 0xFEDA;
const uint16_t NIGHT_FACE = 0xFE15;
const uint16_t INK_COLOR  = 0x18E3;
const uint16_t RIM_COLOR  = 0x4208;
const uint16_t HUB_COLOR  = 0x39C7;

int currentHour = START_HOUR;
int currentMinute = START_MINUTE;
unsigned long lastMinuteTick = 0;

float toRadians(float degrees) {
  return degrees * PI / 180.0f;
}

uint16_t blend565(uint16_t from, uint16_t to, float amount) {
  if (amount < 0.0f) amount = 0.0f;
  if (amount > 1.0f) amount = 1.0f;

  int r1 = (from >> 11) & 0x1F, g1 = (from >> 5) & 0x3F, b1 = from & 0x1F;
  int r2 = (to >> 11) & 0x1F,   g2 = (to >> 5) & 0x3F,   b2 = to & 0x1F;

  int r = roundf(r1 + (r2 - r1) * amount);
  int g = roundf(g1 + (g2 - g1) * amount);
  int b = roundf(b1 + (b2 - b1) * amount);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// Approximation based on displayed time, not actual sunrise/sunset.
// 07:00-16:59: daytime ivory; 17:00-21:59: gradual warming;
// 22:00-06:59: warm night ivory. It never becomes cooler/bluer than day.
uint16_t getFaceColor() {
  int minutesOfDay = currentHour * 60 + currentMinute;

  if (minutesOfDay >= 7 * 60 && minutesOfDay < 17 * 60) {
    return DAY_FACE;
  }
  if (minutesOfDay >= 17 * 60 && minutesOfDay < 22 * 60) {
    float progress = (minutesOfDay - 17 * 60) / (5.0f * 60.0f);
    return blend565(DUSK_FACE, NIGHT_FACE, progress);
  }
  return NIGHT_FACE;
}

// Tapered triangular hand: wide at the base, sharp at the tip.
void drawHand(float angleDegrees, float length, float tailLength,
              float baseHalfWidth, uint16_t color) {
  float angle = toRadians(angleDegrees);
  float dx = cosf(angle), dy = sinf(angle);
  float px = -dy, py = dx;

  int tipX = roundf(CENTER_X + dx * length);
  int tipY = roundf(CENTER_Y + dy * length);

  int baseX = roundf(CENTER_X + dx * 5.0f);
  int baseY = roundf(CENTER_Y + dy * 5.0f);
  int leftX = roundf(baseX + px * baseHalfWidth);
  int leftY = roundf(baseY + py * baseHalfWidth);
  int rightX = roundf(baseX - px * baseHalfWidth);
  int rightY = roundf(baseY - py * baseHalfWidth);

  tft.fillTriangle(tipX, tipY, leftX, leftY, rightX, rightY, color);

  // Small counterweight behind the pivot.
  int tailX = roundf(CENTER_X - dx * tailLength);
  int tailY = roundf(CENTER_Y - dy * tailLength);
  int tailLeftX = roundf(CENTER_X + px * (baseHalfWidth * 0.45f));
  int tailLeftY = roundf(CENTER_Y + py * (baseHalfWidth * 0.45f));
  int tailRightX = roundf(CENTER_X - px * (baseHalfWidth * 0.45f));
  int tailRightY = roundf(CENTER_Y - py * (baseHalfWidth * 0.45f));
  tft.fillTriangle(tailX, tailY, tailLeftX, tailLeftY,
                   tailRightX, tailRightY, color);
}

// Large Arabic hour numerals arranged around the perimeter.
void drawHourNumbers(uint16_t faceColor) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextSize(1);
  tft.setTextColor(INK_COLOR, faceColor);

  const int numberRadius = 99;
  for (int hour = 1; hour <= 12; hour++) {
    float angle = toRadians(hour * 30.0f - 90.0f);
    int x = CENTER_X + roundf(cosf(angle) * numberRadius);
    int y = CENTER_Y + roundf(sinf(angle) * numberRadius);
    tft.drawNumber(hour, x, y);
  }
}

// Fine minute marks, with stronger marks at every five minutes.
void drawMinuteMarkers() {
  for (int minute = 0; minute < 60; minute++) {
    float angle = toRadians(minute * 6.0f - 90.0f);
    bool fiveMinute = (minute % 5 == 0);
    bool quarterHour = (minute % 15 == 0);

    int outerRadius = 116;
    int innerRadius = quarterHour ? 108 : (fiveMinute ? 110 : 113);
    int x1 = CENTER_X + roundf(cosf(angle) * innerRadius);
    int y1 = CENTER_Y + roundf(sinf(angle) * innerRadius);
    int x2 = CENTER_X + roundf(cosf(angle) * outerRadius);
    int y2 = CENTER_Y + roundf(sinf(angle) * outerRadius);

    if (fiveMinute) {
      tft.drawLine(x1, y1, x2, y2, INK_COLOR);
      if (quarterHour) {
        tft.drawLine(x1 + 1, y1, x2 + 1, y2, INK_COLOR);
      }
    } else {
      tft.drawPixel(x2, y2, INK_COLOR);
    }
  }
}

void drawClockFace() {
  uint16_t faceColor = getFaceColor();
  tft.fillScreen(TFT_BLACK);

  // A one-pixel dark outline, not the previous thick black bezel.
  tft.fillCircle(CENTER_X, CENTER_Y, FACE_RADIUS, RIM_COLOR);
  tft.fillCircle(CENTER_X, CENTER_Y, FACE_RADIUS - 1, faceColor);

  drawMinuteMarkers();
  drawHourNumbers(faceColor);
}

void drawClock() {
  drawClockFace();

  float hourAngle =
    (currentHour % 12) * 30.0f + currentMinute * 0.5f - 90.0f;
  float minuteAngle = currentMinute * 6.0f - 90.0f;

  // Short hour hand first; longer, thinner minute hand on top.
  drawHand(hourAngle, 55.0f, 8.0f, 3.0f, INK_COLOR);
  drawHand(minuteAngle, 91.0f, 11.0f, 2.0f, INK_COLOR);

  // Slightly lighter charcoal pivot.
  tft.fillCircle(CENTER_X, CENTER_Y, 5, HUB_COLOR);
  tft.drawCircle(CENTER_X, CENTER_Y, 5, INK_COLOR);
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

  Serial.println("Single-display clock started (no RTC).");
  Serial.println("Time begins at START_HOUR / START_MINUTE.");
}

void loop() {
  unsigned long now = millis();

  while ((unsigned long)(now - lastMinuteTick) >= 60000UL) {
    lastMinuteTick += 60000UL;
    advanceMinute();
    drawClock();
    Serial.printf("Clock time: %02d:%02d\n", currentHour, currentMinute);
  }

  delay(20);
}
