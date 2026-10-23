#include <TFT_eSPI.h>
#include <math.h>

// --------------------------------------------------
// MINI CLOCK TOWER - SINGLE DISPLAY TEST
// ESP32 DevKit V1 + one GC9A01 240x240 TFT
// No DS3231 or other RTC required.
//
// This version uses the ESP32's millis() timer.
// Set START_HOUR and START_MINUTE below before
// uploading. Time resets when the ESP32 loses power.
// --------------------------------------------------

TFT_eSPI tft = TFT_eSPI();

const int CENTER_X = 120;
const int CENTER_Y = 120;
const int FACE_RADIUS = 111;

// Change these before uploading to set the initial time.
// Uses 24-hour values: e.g. 3:45 PM = 15 and 45.
const int START_HOUR = 12;
const int START_MINUTE = 0;

const uint16_t FACE_COLOR = 0xEF5D; // warm ivory
const uint16_t INK_COLOR  = 0x0861; // dark blue-black
const uint16_t HUB_COLOR  = TFT_BLACK;

int currentHour = START_HOUR;
int currentMinute = START_MINUTE;
unsigned long lastMinuteTick = 0;

float toRadians(float degrees) {
  return degrees * PI / 180.0f;
}

void drawHand(float angleDegrees,
              float length,
              float tail,
              float width,
              uint16_t color) {
  float a = toRadians(angleDegrees);
  float dx = cosf(a);
  float dy = sinf(a);
  float px = -dy * width;
  float py =  dx * width;

  int x1 = (int)roundf(CENTER_X + dx * length + px);
  int y1 = (int)roundf(CENTER_Y + dy * length + py);
  int x2 = (int)roundf(CENTER_X + dx * length - px);
  int y2 = (int)roundf(CENTER_Y + dy * length - py);
  int x3 = (int)roundf(CENTER_X - dx * tail - px);
  int y3 = (int)roundf(CENTER_Y - dy * tail - py);
  int x4 = (int)roundf(CENTER_X - dx * tail + px);
  int y4 = (int)roundf(CENTER_Y - dy * tail + py);

  tft.fillTriangle(x1, y1, x2, y2, x3, y3, color);
  tft.fillTriangle(x1, y1, x3, y3, x4, y4, color);
}

void drawHourMarkers() {
  for (int hour = 0; hour < 12; hour++) {
    float angle = toRadians(hour * 30.0f - 90.0f);
    bool cardinal = (hour == 0 || hour == 3 ||
                     hour == 6 || hour == 9);

    int outerRadius = 96;
    int innerRadius = cardinal ? 78 : 82;

    float px = -sinf(angle);
    float py =  cosf(angle);

    int xOuter = CENTER_X + (int)roundf(cosf(angle) * outerRadius);
    int yOuter = CENTER_Y + (int)roundf(sinf(angle) * outerRadius);
    int xInner = CENTER_X + (int)roundf(cosf(angle) * innerRadius);
    int yInner = CENTER_Y + (int)roundf(sinf(angle) * innerRadius);

    int count = cardinal ? 2 : 1;
    for (int marker = 0; marker < count; marker++) {
      float shift = cardinal ? (marker == 0 ? -3.0f : 3.0f) : 0.0f;
      int ox = (int)roundf(px * shift);
      int oy = (int)roundf(py * shift);

      for (int w = 0; w < 3; w++) {
        int wx = (int)roundf(px * w);
        int wy = (int)roundf(py * w);
        tft.drawLine(xInner + ox + wx, yInner + oy + wy,
                     xOuter + ox + wx, yOuter + oy + wy,
                     INK_COLOR);
      }
    }
  }
}

void drawClockFace() {
  tft.fillScreen(TFT_BLACK);
  tft.fillCircle(CENTER_X, CENTER_Y, FACE_RADIUS, INK_COLOR);
  tft.fillCircle(CENTER_X, CENTER_Y, FACE_RADIUS - 4, FACE_COLOR);
  drawHourMarkers();
}

void drawClock() {
  drawClockFace();

  float hourAngle =
    (currentHour % 12) * 30.0f + currentMinute * 0.5f - 90.0f;
  float minuteAngle = currentMinute * 6.0f - 90.0f;

  drawHand(hourAngle, 53.0f, 12.0f, 7.0f, HUB_COLOR);
  drawHand(minuteAngle, 91.0f, 15.0f, 4.5f, HUB_COLOR);
  tft.fillCircle(CENTER_X, CENTER_Y, 4, HUB_COLOR);
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

  // Start counting elapsed time from this point.
  lastMinuteTick = millis();

  drawClock();
  Serial.println("Single-display clock started (no RTC).");
  Serial.println("Time begins at the START_HOUR / START_MINUTE values.");
}

void loop() {
  unsigned long now = millis();

  // Unsigned subtraction handles millis() rollover.
  while ((unsigned long)(now - lastMinuteTick) >= 60000UL) {
    lastMinuteTick += 60000UL;
    advanceMinute();
    drawClock();

    Serial.printf("Clock time: %02d:%02d\n", currentHour, currentMinute);
  }

  delay(20);
}
