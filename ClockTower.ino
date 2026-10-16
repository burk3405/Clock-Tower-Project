
#include <TFT_eSPI.h>
#include <Wire.h>
#include <RTClib.h>
#include <math.h>

// --------------------------------------------------
// MINI CLOCK TOWER
// ESP32 + 3 GC9A01 round displays + DS3231 RTC
//
// All three displays share the same SPI and control
// pins, including CS. They receive identical pixels.
// --------------------------------------------------

TFT_eSPI tft = TFT_eSPI();
RTC_DS3231 rtc;

// ESP32 I2C pins
const int RTC_SDA = 21;
const int RTC_SCL = 22;

// Display geometry
const int SCREEN_SIZE = 240;
const int CENTER_X = 120;
const int CENTER_Y = 120;

const int FACE_RADIUS = 111;

// Colors (RGB565)
const uint16_t FACE_COLOR = 0xEF5D;
const uint16_t INK_COLOR  = 0x0861;
const uint16_t HUB_COLOR  = TFT_BLACK;

int lastMinute = -1;

// Convert degrees to radians.
float toRadians(float degrees) {
  return degrees * PI / 180.0f;
}

// Draw a thick hand as a filled quadrilateral.
// angleDegrees: 0 = right, 90 = down.
// length: distance from center to the tip.
// tail: distance behind the center.
// width: approximate half-width of the hand.
void drawHand(float angleDegrees,
              float length,
              float tail,
              float width,
              uint16_t color) {

  float a = toRadians(angleDegrees);

  float dx = cosf(a);
  float dy = sinf(a);

  // Perpendicular direction controls hand thickness.
  float px = -dy * width;
  float py =  dx * width;

  float xTip  = CENTER_X + dx * length;
  float yTip  = CENTER_Y + dy * length;
  float xTail = CENTER_X - dx * tail;
  float yTail = CENTER_Y - dy * tail;

  int x1 = (int)roundf(xTip + px);
  int y1 = (int)roundf(yTip + py);

  int x2 = (int)roundf(xTip - px);
  int y2 = (int)roundf(yTip - py);

  int x3 = (int)roundf(xTail - px);
  int y3 = (int)roundf(yTail - py);

  int x4 = (int)roundf(xTail + px);
  int y4 = (int)roundf(yTail + py);

  // Fill the quadrilateral with two triangles.
  tft.fillTriangle(x1, y1, x2, y2, x3, y3, color);
  tft.fillTriangle(x1, y1, x3, y3, x4, y4, color);
}

// Draw the hour markers.
// The four cardinal positions have double markers,
// as in the reference photograph.
void drawHourMarkers() {

  for (int hour = 0; hour < 12; hour++) {

    // 12 o'clock is -90 degrees.
    float angle = toRadians(hour * 30.0f - 90.0f);

    bool isCardinal =
      (hour == 0 || hour == 3 ||
       hour == 6 || hour == 9);

    int outerRadius = 96;
    int innerRadius = isCardinal ? 78 : 82;

    int xOuter = CENTER_X +
      (int)roundf(cosf(angle) * outerRadius);

    int yOuter = CENTER_Y +
      (int)roundf(sinf(angle) * outerRadius);

    int xInner = CENTER_X +
      (int)roundf(cosf(angle) * innerRadius);

    int yInner = CENTER_Y +
      (int)roundf(sinf(angle) * innerRadius);

    // Marker thickness.
    int thickness = 3;

    // A small perpendicular offset makes the markers
    // thicker while keeping them aligned radially.
    float px = -sinf(angle);
    float py =  cosf(angle);

    for (int offset = 0;
         offset < (isCardinal ? 2 : 1);
         offset++) {

      float shift = isCardinal
        ? (offset == 0 ? -3.0f : 3.0f)
        : 0.0f;

      int ox = (int)roundf(px * shift);
      int oy = (int)roundf(py * shift);

      tft.drawLine(
        xInner + ox, yInner + oy,
        xOuter + ox, yOuter + oy,
        INK_COLOR
      );

      // Reinforce the marker's width.
      for (int w = 1; w < thickness; w++) {
        tft.drawLine(
          xInner + ox + (int)roundf(px * w),
          yInner + oy + (int)roundf(py * w),
          xOuter + ox + (int)roundf(px * w),
          yOuter + oy + (int)roundf(py * w),
          INK_COLOR
        );
      }
    }
  }
}

// Draw the ivory dial and dark bezel.
void drawClockFace() {

  tft.fillScreen(TFT_BLACK);

  // Outer dark circular bezel.
  tft.fillCircle(
    CENTER_X, CENTER_Y,
    FACE_RADIUS, INK_COLOR
  );

  // Ivory dial inside the bezel.
  tft.fillCircle(
    CENTER_X, CENTER_Y,
    FACE_RADIUS - 4, FACE_COLOR
  );

  drawHourMarkers();
}

// Render the current RTC time.
void drawClock(DateTime now) {

  // Redraw the face to erase the previous hands.
  drawClockFace();

  int hour = now.hour() % 12;
  int minute = now.minute();

  // Minute hand advances 6 degrees per minute.
  float minuteAngle = minute * 6.0f - 90.0f;

  // Hour hand advances continuously with the minute.
  float hourAngle =
    (hour * 30.0f) + (minute * 0.5f) - 90.0f;

  // Draw the shorter hour hand first.
  drawHand(
    hourAngle,
    53.0f,  // Length beyond center
    12.0f,  // Tail behind center
    7.0f,   // Half-width
    HUB_COLOR
  );

  // Draw the longer minute hand on top.
  drawHand(
    minuteAngle,
    91.0f,
    15.0f,
    4.5f,
    HUB_COLOR
  );

  // Center pivot.
  tft.fillCircle(
    CENTER_X, CENTER_Y,
    4, HUB_COLOR
  );
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------

void setup() {

  Serial.begin(115200);

  // Initialize the shared SPI displays.
  // TFT_eSPI pins are configured in User_Setup.h.
  tft.init();
  tft.setRotation(0);

  // All three panels are connected to the same
  // CS, DC, RST, SCLK and MOSI signals.
  drawClockFace();

  // Initialize the DS3231.
  Wire.begin(RTC_SDA, RTC_SCL);

  if (!rtc.begin()) {
    Serial.println("ERROR: DS3231 not detected.");

    // Show an error face rather than using a
    // fabricated time.
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("RTC ERROR", CENTER_X, CENTER_Y, 2);

    while (true) {
      delay(1000);
    }
  }

  // Set the RTC to the sketch compilation time
  // only if the RTC reports that it lost power.
  if (rtc.lostPower()) {
    Serial.println("RTC lost power; setting compile time.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  DateTime now = rtc.now();

  drawClock(now);
  lastMinute = now.minute();

  Serial.println("Mini clock tower started.");
}

// --------------------------------------------------
// MAIN LOOP
// --------------------------------------------------

void loop() {

  DateTime now = rtc.now();

  // The face has no second hand, so update when
  // the minute changes.
  if (now.minute() != lastMinute) {

    drawClock(now);
    lastMinute = now.minute();

    Serial.printf(
      "Time: %02d:%02d:%02d\n",
      now.hour(),
      now.minute(),
      now.second()
    );
  }

  delay(200);
}
