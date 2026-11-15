#include <TFT_eSPI.h>
#include <Wire.h>
#include <RTClib.h>
#include <FS.h>
#include <LittleFS.h>
#include <driver/i2s.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// --------------------------------------------------
// MINI CLOCK TOWER - THREE SYNCHRONIZED DISPLAYS
// ESP32 DevKit V1 + 3 GC9A01 displays + DS3231 + MAX98357A
//
// The three GC9A01 modules share SCLK, MOSI, CS, DC, and RST.
// TFT_eSPI therefore sends every initialization and drawing command to
// all three panels at once. Do not add a per-display rendering loop.
//
// Required libraries:
//   TFT_eSPI by Bodmer (configured as described in README.md)
//   RTClib by Adafruit
// Bell playback uses the ESP32 Arduino core's I2S driver and LittleFS.
// --------------------------------------------------

TFT_eSPI tft = TFT_eSPI();
RTC_DS3231 rtc;

// DS3231 I2C pins.
const int RTC_SDA = 21;
const int RTC_SCL = 22;

// MAX98357A I2S pins.
const int I2S_BCLK = 25;
const int I2S_LRC = 26;
const int I2S_DOUT = 27;
const uint8_t AUDIO_ATTENUATION_SHIFT = 3;  // 3 = one eighth PCM amplitude.
const char *ALL_BELLS_FILE = "/all16.wav";
const char *QUARTER_FILES[3] = {"/quarter1.wav", "/quarter2.wav", "/quarter3.wav"};
const char *LONG_STRIKE_FILE = "/longstrike.wav";

// Set true only for the first upload after deliberately replacing a
// depleted RTC battery. It sets the DS3231 from the compile timestamp,
// which may differ from local time. Set it back to false after uploading.
const bool SET_RTC_FROM_COMPILE_TIME = false;

// Scheduled chimes: four short bells at :15, :30, and :45.
// At :00, play all 16 short bells followed by 1-12 long strikes.

const int SCREEN_SIZE = 240;
const int CENTER_X = SCREEN_SIZE / 2;
const int CENTER_Y = SCREEN_SIZE / 2;
const int FACE_RADIUS = 119;
// These 40-pixel radial bars are deliberate: they are the approved refined
// face geometry, not the shorter markers from the earlier test sketch.
const float MARKER_INNER_RADIUS = 71.0f;
const float MARKER_OUTER_RADIUS = 111.0f;
const float MARKER_HALF_WIDTH = 1.5f;
const float DOUBLE_MARKER_OFFSET = 3.5f;

// Daylight stays at the approved neutral-warm ivory. Night only shifts
// warmer; it never becomes blue, dark, orange, or brown.
const uint8_t DAY_RED = 239;
const uint8_t DAY_GREEN = 235;
const uint8_t DAY_BLUE = 232;
const uint8_t NIGHT_RED = 248;
const uint8_t NIGHT_GREEN = 229;
const uint8_t NIGHT_BLUE = 202;

const uint16_t INK_COLOR = 0x18E3;  // near-black neutral charcoal
const uint16_t HUB_COLOR = 0x31A6;  // understated lighter graphite

bool rtcReady = false;
bool audioReady = false;
bool bellPlaying = false;
fs::File bellFile;
uint32_t bellDataRemaining = 0;
size_t pendingAudioOffset = 0;
size_t pendingAudioBytes = 0;
int lastRenderedHour = -1;
int lastRenderedMinute = -1;
int lastBellDay = -1;
int lastBellHour = -1;
int lastBellMinute = -1;
const size_t MAX_AUDIO_SEQUENCE = 13;
const char *audioSequence[MAX_AUDIO_SEQUENCE];
size_t audioSequenceLength = 0;
size_t audioSequenceIndex = 0;
unsigned long lastRtcRetry = 0;

const size_t AUDIO_FRAMES_PER_CHUNK = 128;
int16_t monoAudio[AUDIO_FRAMES_PER_CHUNK];
int16_t stereoAudio[AUDIO_FRAMES_PER_CHUNK * 2];

const size_t SERIAL_COMMAND_LENGTH = 40;
char serialCommand[SERIAL_COMMAND_LENGTH];
size_t serialCommandLength = 0;

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

// Return 0.0 in daylight and 1.0 in the dark part of night, with three
// hour transitions after sunset and before sunrise.
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

uint16_t faceColorForTime(const DateTime &now) {
  float warmth = nightWarmth(now.hour(), now.minute());
  return rgb565(
    blendChannel(DAY_RED, NIGHT_RED, warmth),
    blendChannel(DAY_GREEN, NIGHT_GREEN, warmth),
    blendChannel(DAY_BLUE, NIGHT_BLUE, warmth)
  );
}

// Draw a tapered hand with a pointed outer end. Its base extends under
// the hub so no gap can appear around the pivot.
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
  float tipHalfWidth = baseHalfWidth * 0.20f;

  int tipLeftX = (int)roundf(CENTER_X + dx * length + px * tipHalfWidth);
  int tipLeftY = (int)roundf(CENTER_Y + dy * length + py * tipHalfWidth);
  int tipRightX = (int)roundf(CENTER_X + dx * length - px * tipHalfWidth);
  int tipRightY = (int)roundf(CENTER_Y + dy * length - py * tipHalfWidth);
  int tailLeftX = (int)roundf(CENTER_X - dx * tail + px * baseHalfWidth);
  int tailLeftY = (int)roundf(CENTER_Y - dy * tail + py * baseHalfWidth);
  int tailRightX = (int)roundf(CENTER_X - dx * tail - px * baseHalfWidth);
  int tailRightY = (int)roundf(CENTER_Y - dy * tail - py * baseHalfWidth);

  tft.fillTriangle(tipLeftX, tipLeftY, tailLeftX, tailLeftY,
                   tailRightX, tailRightY, color);
  tft.fillTriangle(tipLeftX, tipLeftY, tailRightX, tailRightY,
                   tipRightX, tipRightY, color);
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
  tft.fillScreen(TFT_BLACK);
  tft.fillCircle(CENTER_X, CENTER_Y, FACE_RADIUS - 1, faceColor);
  tft.drawCircle(CENTER_X, CENTER_Y, FACE_RADIUS, INK_COLOR);
  drawHourMarkers();
}

// One call broadcasts the full drawing sequence to all three displays,
// because their SPI and control lines (including CS) are wired together.
void updateAllDisplays(const DateTime &now) {
  drawClockFace(faceColorForTime(now));

  float hourAngle =
    (now.hour() % 12) * 30.0f + now.minute() * 0.5f - 90.0f;
  float minuteAngle = now.minute() * 6.0f - 90.0f;

  drawTaperedHand(hourAngle, 57.0f, 18.0f, 8.0f, INK_COLOR);
  drawTaperedHand(minuteAngle, 103.0f, 20.0f, 5.4f, INK_COLOR);
  tft.fillCircle(CENTER_X, CENTER_Y, 5, HUB_COLOR);
}

void initializeDisplays() {
  // TFT_eSPI must be configured for GC9A01 and pins 18, 23, 5, 2, and 4.
  // With shared CS, this single initialization reaches every module.
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
}

bool initializeRTC() {
  if (!rtc.begin(&Wire)) {
    Serial.println("ERROR: DS3231 not detected; waiting to retry.");
    return false;
  }

  if (rtc.lostPower()) {
    if (SET_RTC_FROM_COMPILE_TIME) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      Serial.println("DS3231 was reset; set from compile time.");
    } else {
      Serial.println("ERROR: DS3231 lost power. Set its correct local time, then restart.");
      return false;
    }
  }

  Serial.println("DS3231 time source ready.");
  return true;
}

uint16_t readLittleEndian16(fs::File &file) {
  uint8_t bytes[2];
  if (file.read(bytes, sizeof(bytes)) != sizeof(bytes)) {
    return 0;
  }
  return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

uint32_t readLittleEndian32(fs::File &file) {
  uint8_t bytes[4];
  if (file.read(bytes, sizeof(bytes)) != sizeof(bytes)) {
    return 0;
  }
  return (uint32_t)bytes[0] |
         ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) |
         ((uint32_t)bytes[3] << 24);
}

bool findPcmWaveData(fs::File &file, uint32_t &sampleRate, uint32_t &dataSize) {
  char riff[4];
  char wave[4];
  if (file.read((uint8_t *)riff, sizeof(riff)) != sizeof(riff) ||
      memcmp(riff, "RIFF", sizeof(riff)) != 0) {
    return false;
  }
  readLittleEndian32(file);  // RIFF container size is not needed.
  if (file.read((uint8_t *)wave, sizeof(wave)) != sizeof(wave) ||
      memcmp(wave, "WAVE", sizeof(wave)) != 0) {
    return false;
  }

  bool hasFormat = false;
  while (file.available() >= 8) {
    char chunkId[4];
    file.read((uint8_t *)chunkId, sizeof(chunkId));
    uint32_t chunkSize = readLittleEndian32(file);
    uint32_t nextChunk = file.position() + chunkSize + (chunkSize & 1U);

    if (memcmp(chunkId, "fmt ", sizeof(chunkId)) == 0) {
      if (chunkSize < 16) {
        return false;
      }
      uint16_t format = readLittleEndian16(file);
      uint16_t channels = readLittleEndian16(file);
      sampleRate = readLittleEndian32(file);
      readLittleEndian32(file);  // byte rate
      readLittleEndian16(file);  // block alignment
      uint16_t bitsPerSample = readLittleEndian16(file);
      if (format != 1 || channels != 1 || bitsPerSample != 16 ||
          sampleRate < 16000 || sampleRate > 48000) {
        return false;
      }
      hasFormat = true;
      if (!file.seek(nextChunk)) {
        return false;
      }
    } else if (memcmp(chunkId, "data", sizeof(chunkId)) == 0) {
      if (!hasFormat || chunkSize == 0) {
        return false;
      }
      dataSize = chunkSize;
      return true;  // File cursor is now at the first audio sample.
    }

    if (!file.seek(nextChunk)) {
      return false;
    }
  }
  return false;
}

void initializeAudio() {
  if (!LittleFS.begin(false)) {
    Serial.println("ERROR: LittleFS could not mount; bell playback disabled.");
    return;
  }

  i2s_config_t i2sConfig = {};
  i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2sConfig.sample_rate = 22050;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2sConfig.dma_buf_count = 8;
  i2sConfig.dma_buf_len = 128;
  i2sConfig.tx_desc_auto_clear = true;

  i2s_pin_config_t pinConfig = {};
  pinConfig.mck_io_num = I2S_PIN_NO_CHANGE;
  pinConfig.bck_io_num = I2S_BCLK;
  pinConfig.ws_io_num = I2S_LRC;
  pinConfig.data_out_num = I2S_DOUT;
  pinConfig.data_in_num = I2S_PIN_NO_CHANGE;

  if (i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, NULL) != ESP_OK) {
    Serial.println("ERROR: I2S driver installation failed; bell playback disabled.");
    return;
  }
  if (i2s_set_pin(I2S_NUM_0, &pinConfig) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    Serial.println("ERROR: I2S setup failed; bell playback disabled.");
    return;
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  audioReady = true;
  Serial.println("I2S audio ready. Send b in Serial Monitor to test /all16.wav.");
}

void stopBell() {
  if (bellFile) bellFile.close();
  bellDataRemaining = 0;
  pendingAudioOffset = 0;
  pendingAudioBytes = 0;
  bellPlaying = false;
  audioSequenceLength = 0;
  audioSequenceIndex = 0;
  i2s_zero_dma_buffer(I2S_NUM_0);
}

bool openAudioFile(const char *path) {
  if (!audioReady || !LittleFS.exists(path)) {
    Serial.printf("Audio file unavailable: %s\n", path);
    return false;
  }
  if (bellFile) bellFile.close();
  bellFile = LittleFS.open(path, "r");
  uint32_t sampleRate = 0;
  if (!bellFile || !findPcmWaveData(bellFile, sampleRate, bellDataRemaining)) {
    if (bellFile) bellFile.close();
    Serial.printf("Invalid WAV file: %s\n", path);
    return false;
  }
  i2s_set_clk(I2S_NUM_0, sampleRate, I2S_BITS_PER_SAMPLE_16BIT,
              I2S_CHANNEL_STEREO);
  pendingAudioOffset = 0;
  pendingAudioBytes = 0;
  bellPlaying = true;
  Serial.printf("Playing %s\n", path);
  return true;
}

bool startSequence(const char *path) {
  stopBell();
  audioSequence[0] = path;
  audioSequenceLength = 1;
  audioSequenceIndex = 0;
  return openAudioFile(path);
}

bool playQuarter(int quarterIndex) {
  stopBell();
  audioSequence[0] = QUARTER_FILES[quarterIndex];
  audioSequenceLength = 1;
  audioSequenceIndex = 0;
  return openAudioFile(audioSequence[0]);
}

bool playHourSequence(int hour12) {
  stopBell();
  audioSequence[0] = ALL_BELLS_FILE;
  for (int i = 0; i < hour12; i++) audioSequence[1 + i] = LONG_STRIKE_FILE;
  audioSequenceLength = 1 + hour12;
  audioSequenceIndex = 0;
  return openAudioFile(audioSequence[0]);
}

void pumpBellAudio() {
  if (!bellPlaying) {
    if (audioSequenceIndex < audioSequenceLength) {
      if (!openAudioFile(audioSequence[audioSequenceIndex])) {
        audioSequenceLength = 0;
        return;
      }
    } else {
      return;
    }
  }

  if (pendingAudioBytes == 0) {
    if (bellDataRemaining == 0) {
      if (bellFile) bellFile.close();
      audioSequenceIndex++;
      bellPlaying = false;
      return;
    }

    size_t bytesToRead = min(bellDataRemaining, (uint32_t)sizeof(monoAudio));
    size_t bytesRead = bellFile.read((uint8_t *)monoAudio, bytesToRead);
    bytesRead &= ~1U;
    bellDataRemaining -= bytesRead;
    if (bytesRead == 0) {
      if (bellFile) bellFile.close();
      audioSequenceIndex++;
      bellPlaying = false;
      return;
    }

    size_t frames = bytesRead / sizeof(int16_t);
    for (size_t frame = 0; frame < frames; frame++) {
      int16_t sample = monoAudio[frame] >> AUDIO_ATTENUATION_SHIFT;
      stereoAudio[frame * 2] = sample;
      stereoAudio[frame * 2 + 1] = sample;
    }
    pendingAudioOffset = 0;
    pendingAudioBytes = frames * sizeof(int16_t) * 2;
  }

  size_t bytesWritten = 0;
  esp_err_t result = i2s_write(I2S_NUM_0,
      (const uint8_t *)stereoAudio + pendingAudioOffset,
      pendingAudioBytes, &bytesWritten, 2);
  if (result != ESP_OK) {
    Serial.println("ERROR: I2S write failed; sequence stopped.");
    stopBell();
    return;
  }
  pendingAudioOffset += bytesWritten;
  pendingAudioBytes -= bytesWritten;

  if (bellDataRemaining == 0 && pendingAudioBytes == 0) {
    if (bellFile) bellFile.close();
    audioSequenceIndex++;
    bellPlaying = false;
  }
}

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month) {
  static const uint8_t days[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  return days[month - 1];
}

void setRtcFromSerialCommand(const char *command) {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;

  if (sscanf(command + 1, "%d-%d-%d %d:%d:%d",
             &year, &month, &day, &hour, &minute, &second) != 6 ||
      year < 2000 || year > 2099 || month < 1 || month > 12 ||
      day < 1 || day > daysInMonth(year, month) || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 || second < 0 || second > 59) {
    Serial.println("Invalid time. Use: s YYYY-MM-DD HH:MM:SS");
    return;
  }

  if (!rtc.begin(&Wire)) {
    rtcReady = false;
    Serial.println("ERROR: DS3231 not detected; time was not set.");
    return;
  }

  rtc.adjust(DateTime(year, month, day, hour, minute, second));
  rtcReady = true;
  lastRenderedHour = -1;
  lastRenderedMinute = -1;
  Serial.printf("DS3231 set to %04d-%02d-%02d %02d:%02d:%02d\n",
                year, month, day, hour, minute, second);
}

void processSerialCommand(const char *command) {
  if (strcmp(command, "b") == 0 || strcmp(command, "B") == 0) {
    startSequence(ALL_BELLS_FILE);
  } else if (command[0] == 's' || command[0] == 'S') {
    setRtcFromSerialCommand(command);
  } else if (strcmp(command, "h") == 0 || strcmp(command, "H") == 0) {
    Serial.println("Commands: b = play all 16 bells; s YYYY-MM-DD HH:MM:SS = set DS3231.");
  } else if (command[0] != '\0') {
    Serial.println("Unknown command. Send h for help.");
  }
}

void readSerialCommands() {
  while (Serial.available() > 0) {
    char character = (char)Serial.read();
    if (character == '\r') {
      continue;
    }
    if (character == '\n') {
      serialCommand[serialCommandLength] = '\0';
      processSerialCommand(serialCommand);
      serialCommandLength = 0;
      continue;
    }
    if (serialCommandLength < SERIAL_COMMAND_LENGTH - 1) {
      serialCommand[serialCommandLength++] = character;
    } else {
      serialCommandLength = 0;
      Serial.println("Command too long. Send h for help.");
    }
  }
}

void ringScheduledBells(const DateTime &now) {
  if (now.second() > 2) return;
  int minute = now.minute();
  if (minute != 15 && minute != 30 && minute != 45 && minute != 0) return;

  int dayKey = now.year() * 10000 + now.month() * 100 + now.day();
  if (dayKey == lastBellDay && now.hour() == lastBellHour &&
      minute == lastBellMinute) return;
  if (bellPlaying || audioSequenceIndex < audioSequenceLength) return;

  bool started = false;
  if (minute == 15) started = playQuarter(0);
  else if (minute == 30) started = playQuarter(1);
  else if (minute == 45) started = playQuarter(2);
  else {
    int hour12 = now.hour() % 12;
    if (hour12 == 0) hour12 = 12;
    started = playHourSequence(hour12);
  }

  if (started) {
    lastBellDay = dayKey;
    lastBellHour = now.hour();
    lastBellMinute = minute;
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(RTC_SDA, RTC_SCL);

  initializeDisplays();
  rtcReady = initializeRTC();
  initializeAudio();

  if (rtcReady) {
    DateTime now = rtc.now();
    updateAllDisplays(now);
    lastRenderedHour = now.hour();
    lastRenderedMinute = now.minute();
    Serial.printf("Clock time: %02d:%02d\n", now.hour(), now.minute());
  }

  Serial.println("Mini clock tower started.");
}

void loop() {
  // Send only a small PCM block per pass so RTC/display work continues
  // while a recorded bell is playing.
  pumpBellAudio();
  readSerialCommands();

  if (!rtcReady) {
    if (millis() - lastRtcRetry >= 5000UL) {
      lastRtcRetry = millis();
      rtcReady = initializeRTC();
      if (rtcReady) {
        lastRenderedHour = -1;
        lastRenderedMinute = -1;
      }
    }
    delay(5);
    return;
  }

  DateTime now = rtc.now();
  if (now.hour() != lastRenderedHour || now.minute() != lastRenderedMinute) {
    updateAllDisplays(now);
    lastRenderedHour = now.hour();
    lastRenderedMinute = now.minute();
    Serial.printf("Clock time: %02d:%02d\n", now.hour(), now.minute());
  }

  ringScheduledBells(now);
  delay(5);
}
