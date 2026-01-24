// For Wemos Lolin32, OLED with 18650
// For TX only uart, check LTM telemetry with o-oscope (or add it here)

// NOTE - test leads are too long for i2c at 400 Khz. Must be set to 200 Khz!

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MSP.h>
#include <SPI.h>

#define I2C_DEV_ADDR_FC  0x0d

#define I2C_SCL_FC 22
#define I2C_SDA_FC 21

#define I2C_SCL_OLED 4
#define I2C_SDA_OLED 5

#if !defined(CONFIG_IDF_TARGET_ESP32)
#define VSPI FSPI
#endif

#include "bee.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define UART_RX 16
#define UART_TX 17
#define IO_PROBE 19

SPIClass *vspi = NULL;
int spi_wait = 0;
bool i2c_detected = false;

// FreeRTOS synchronization
SemaphoreHandle_t uart_complete = NULL;
SemaphoreHandle_t buzz_complete = NULL;

// One Dshot 150 frame = 107us, + 140us pause between frames.
// 2 frames = 494 us
// 500 us at 20 Mhz = 10000 clocks
// 10000 samples at 8 bits per byte = 1250 bytes
byte spi_out[1250];

enum State {
  IDLE,
  DSHOT_PENDING,
  DSHOT_SPI,
  SERVO_PENDING,
};
State state = IDLE;

// 100ms of 5-400 hz PWM
#define SAMPLE_COUNT (3 * 42)

int32_t debug_code = 0;
char msg[32];
uint32_t timings[SAMPLE_COUNT];
int timing_bit = 0;

typedef struct { int idle; int min_bits; int max_high; int min_high; int min_low; int max_low; } timing_conf;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, -1);
MSP msp;

void clear_display() {
  display.clearDisplay();
  display.display();
}

void write_oled(const char * msg) {
  Serial.println(msg);
  display.clearDisplay();
  display.setCursor(0, 20);
  display.write(msg);
  display.display();
  delay(2000);
}

void onReceive(int len) {
  uint8_t rData[2];

  if (len > 1) {
    rData[0] = Wire.read();
    rData[1] = Wire.read();
    Serial.printf("i2c recieved: %02x %02x\n", rData[0], rData[1]);
    if (
      ( (rData[0] == 0x0a) && (rData[1] == 0x80) ) ||
      ( (rData[0] == 0x0d) && (rData[1] == 0x0a) )
      )
    {
      i2c_detected = true;
    }
  }
  while (Wire.available()) {
    Wire.read();
  }
}

void test_sbus() {
  Serial2.begin(115200, SERIAL_8N1, 16, 17, true, 200);
  delay(50);

  Serial2.println("#");
  delay(70);
  Serial2.println("R");

  msp.begin(Serial2, 20);
  msp.command(68, NULL, 0);
  Serial2.end();
}

void test_uart_rx_only() {
  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  delay(50);

  Serial2.println("#");
  delay(70);
  Serial2.println("R");

  msp.begin(Serial2, 20);
  msp.command(68, NULL, 0);
  Serial2.end();
}

void test_uart() {
  msp_api_version_t apiv;

  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  msp.begin(Serial2, 100);

  if (msp.request(MSP_API_VERSION, &apiv, sizeof(apiv))) {
    Serial.printf("prot version: %u, API major: %u\n", (unsigned int) apiv.protocolVersion, (unsigned int) apiv.APIMajor);
    if ( (apiv.protocolVersion == 0) && apiv.APIMajor) {
      write_oled("uart ok");
    }
  } else {
    clear_display();
  }

  msp.reset();
  Serial2.end();
}

static inline int32_t cpu_ticks() {
  int32_t r;
  asm volatile ("rsr %0, ccount" : "=r"(r));
  return r;
}

uint32_t timing_to_bits(timing_conf conf) {
  uint32_t iobits = 0;
  int frame_start = 0;
  int i;

  for (int i=1; i < SAMPLE_COUNT / 2; i++) {
    uint32_t duration = ( (uint32_t)(timings[i] - timings[i - 1]) );
    if (duration > conf.idle)  {
       frame_start = i + 1;
       break;
    }
  }

  if (frame_start == 0) {
    Serial.println("frame start not found in first half of bits");
    return(0);
  }

  for (i=frame_start; i < frame_start + (conf.min_bits * 2); i = i + 2) {
    uint32_t duration = ( (uint32_t)(timings[i] - timings[i - 1]) );
    if ( (duration <= conf.max_high) && (duration >= conf.min_high) ) {
      iobits =  ( iobits << 1 ) | 1;
    } else if ( (duration <= conf.max_low) && (duration >= conf.min_low) ) {
      iobits =  ( iobits << 1 );
    } else {
      break;
    }
  }

  if ( (i - frame_start) >= conf.min_bits) {
    Serial.printf("iobits: %" PRIu32 "\n", iobits);
    return iobits;
  } else {
    return 0;
  }
}

uint32_t dshot_timing_to_bits(timing_conf conf) {
  uint32_t iobits = 0;
  int frame_start[4] = {-1,-1,-1,-1};
  int i = 0;
  int j = 0;

  for (int i=0; i < SAMPLE_COUNT - conf.min_bits; i++) {
    uint32_t duration = timings[i];

    if (duration > conf.idle)  {
       frame_start[j++] = i + 1;
       if (j > 3) {
        break;
       }
    }
  }

  if (frame_start[0] == -1) {
    return(0);
  }

  for (j=0; j < 4; j++) {
    if (frame_start[j] > -1) {
      for (i=frame_start[j]; i < frame_start[j] + (conf.min_bits); i++) {
        uint32_t duration = timings[i];
        if ( (duration <= conf.max_high) && (duration >= conf.min_high) ) {
          iobits =  ( iobits << 1 ) | 1;
        } else if ( (duration <= conf.max_low) && (duration >= conf.min_low) ) {
          iobits =  ( iobits << 1 );
        } else {
          Serial.println("");
          Serial.print("invalid bit length ");
          Serial.println(duration);
          break;
        }
      }

      if ( (i - frame_start[j]) >= conf.min_bits) {
        Serial.printf("iobits: %" PRIu32 "\n", iobits);
        return iobits;
      } else {
        Serial.printf("dshot_timing_to_bits Not enough bits found. Found %d bits after frame start %d\n", i - frame_start[j], frame_start[j]);
      }
    }
  }
  return 0;
}

void IRAM_ATTR probe_change() {
  if (timing_bit < SAMPLE_COUNT) {
    timings[timing_bit++] = cpu_ticks();
  } else {
    detachInterrupt(IO_PROBE);
  }
}

void IRAM_ATTR probe_high() {
  attachInterrupt(digitalPinToInterrupt(IO_PROBE), probe_change, CHANGE);
}

void start_timings() {
  timing_bit = 0;

  for (int i=0; i < SAMPLE_COUNT; i++) {
    timings[i] = 0;
  }
  attachInterrupt(digitalPinToInterrupt(IO_PROBE), probe_high, HIGH);
}

void stop_timings() {
  detachInterrupt(IO_PROBE);
}

void get_timings(int milliseconds) {
  start_timings();
  delay(milliseconds);
  stop_timings();
}

int dshot_crc(uint16_t iobits) {
  uint8_t crc = (iobits ^ (iobits >> 4) ^ (iobits >> 8)) & 0x0F;
  return( ( (iobits >> 12) & 0x0F) == crc);
}

void print_dshot_bits(uint16_t iobits) {
  for (int i=15; i > -1; i--) {
    if ((i % 4) == 0) {
      Serial.print(" ");
    }
    if ( bitRead(iobits,i) ) {
      Serial.print ("1");
    } else {
      Serial.print ("0");
    }
  }
  Serial.println("");
}

void test_dshot() {
  uint16_t iobits = 0;
  timing_conf bit_timing;

  bit_timing.idle = 150;
  bit_timing.min_bits = 16;
  bit_timing.max_high = 70;
  bit_timing.min_high = 40;
  bit_timing.max_low = 35;
  bit_timing.min_low = 15;

  display.clearDisplay();

  iobits = dshot_timing_to_bits(bit_timing);
  if (iobits > 0) {
    print_dshot_bits(iobits);
    if ( ! dshot_crc(iobits) ) {
      Serial.println("crc fail");
      return;
    }

    int throttle = (iobits >> 5) & 0x07ff;

    Serial.printf("Dshot throttle: %d, %d%%\n", throttle, (throttle - 48) / 20 );
    Serial.println(throttle);

    display.clearDisplay();
    display.setCursor(0, 10);
    display.write("Dshot OK");
    display.setCursor(0, 26);
    sprintf(msg, "%d", throttle);
    display.write(msg);
    display.setCursor(0, 48);
    sprintf( msg, "%d%%", (throttle - 48) / 20 + 1);
    display.write(msg);
    display.display();
    delay(2000);
    clear_display();
  }
}

uint32_t timing_to_pwm(timing_conf conf) {
  uint32_t iobits = 0;
  int frame_start = 0;
  uint32_t duration_idle;
  uint32_t duration_high;
  int  cpu_mhz = ESP.getCpuFreqMHz();
  uint32_t freq = 0;

  for (int i=1; i < SAMPLE_COUNT - 1; i++) {
    uint32_t duration = ( (uint32_t) (timings[i] - timings[i - 1]) ) / cpu_mhz;

    if (duration > conf.idle)  {
      duration_idle = duration;
      frame_start = i + 1;
      duration_high = ( (uint32_t)(timings[i + 1] - timings[i]) ) / cpu_mhz;
      break;
    }
  }

  if (frame_start == 0) {
    return(0);
  }

  if ( (duration_high >= conf.min_high) && (duration_high <= conf.max_high) ) {
    freq = 10e5 / (duration_high + duration_idle - 499); // 499 for rounding to the nearest integer instead of just truncating 49.9
  }

  iobits = freq | ( duration_high << 16);
  return iobits;
}

void test_servo() {
  uint32_t iobits = 0;
  uint16_t freq;
  uint16_t value;
  timing_conf bit_timing;

  bit_timing.idle = 17000 / 8;
  bit_timing.min_bits = 16;
  bit_timing.max_high = 2250;
  bit_timing.min_high =  750;

  display.clearDisplay();

  Serial.print("running servo test\n");
  iobits = timing_to_pwm(bit_timing);
  freq = iobits & 0xffff;
  value = iobits >> 16;

  if (value > 200 && freq > 10) {
    Serial.printf("pwm: %d @ %d Hz\n", value, freq);

    display.clearDisplay();
    display.setCursor(0, 10);
    sprintf(msg, "PWM %d", value);
    display.write(msg);
    display.setCursor(0, 26);
    sprintf(msg, "%d %%", (value - 1000) / 10);
    display.write(msg);
    display.setCursor(0, 42);
    sprintf(msg, "%d Hz", freq);
    display.write(msg);

    display.display();
    delay(2000);
    clear_display();
  }
}

void bzzz() {
#define BMP_HEIGHT 64
#define BMP_WIDTH  128
  display.setCursor(0, 0);
  display.drawBitmap(0, 0,  bee_bmp, BMP_WIDTH, BMP_HEIGHT, 1);
  display.display();
}

void test_buzz() {
  pinMode(IO_PROBE, INPUT_PULLUP);
  display.clearDisplay();

  int oldtime = millis();
  int oldval = digitalRead(IO_PROBE);
  int count_pulses = 0;

  Serial.printf("running buzzer test\n");

  for (int i = 0; i < 80; i++) {
    int newval = digitalRead(IO_PROBE);
    if (newval != oldval) {
      int newtime = millis();
      int duration = newtime - oldtime;
      Serial.printf("buzzer duration %d ms\n", duration);
      if ( (duration > 40 ) && (duration < 350) ) {
        count_pulses++;
      } else {
        count_pulses = 0;
      }
      oldval = newval;
      oldtime = newtime;
    }
    delay(5);
  }
  pinMode(IO_PROBE, INPUT);

  if (count_pulses > 2) {
    Serial.printf("Found %d buzzer pulses in 400 ms\n", count_pulses);
    bzzz();
    delay(1500);
    display.invertDisplay(false);
    clear_display();
  }
}

void get_timings_spi() {
  static const int spiClk = 2000000; // 2 Mhz
  vspi->begin();
  vspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE1));
  vspi->transferBytes(NULL, spi_out, sizeof(spi_out));
  vspi->endTransaction();
  vspi->end();
}

void setup_spi() {
  vspi = new SPIClass(VSPI);
  vspi->begin();
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  delay(600);
  Serial.println("Booted");
  setup_spi();

  Wire1.begin(5, 4);

  Wire.onReceive(onReceive);
  Wire.begin((uint8_t)I2C_DEV_ADDR_FC, I2C_SDA_FC, I2C_SCL_FC, 200000L);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  delay(100);

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.cp437(true);
  display.setRotation(0);

  // Create semaphores for task synchronization
  uart_complete = xSemaphoreCreateBinary();
  buzz_complete = xSemaphoreCreateBinary();

  Serial.println("setup done");
}

void IRAM_ATTR probe_trigger_spi() {
  detachInterrupt(IO_PROBE);
  state = DSHOT_SPI;
}

void spi_to_dshot_timing() {
  bool lastbit = spi_out[0] | 1;
  bool thisbit;
  int hightime = 0;
  int lowtime = 0;
  int highstarted = 0;
  int timeindex = 0;
  int multiplier = 5;
  int outindex = 0;

  memset(timings, 0, sizeof(timings));
  if ( (spi_out[0] ^ spi_out[1] ^ spi_out[2] ^ spi_out[3]) != 255 ) {
    for (int i = 0; i < sizeof(spi_out); i++) {
      for (int bit = 7; bit > -1; bit--) {
        if (timeindex >= SAMPLE_COUNT) {
          break;
        }

        thisbit = (spi_out[i] >> bit) & 1;

        if (thisbit && lastbit && (hightime < 1000)) {
          hightime++;
        } else if ( (thisbit) && (!lastbit) ) {
          highstarted = 1;
          if ( (lowtime * multiplier) > 1000) {
            timings[outindex++] = lowtime * multiplier;
          }
          lowtime = 1;
        } else if ( (!thisbit) && (lastbit)) {
          if ( highstarted) {
            timings[outindex++] = hightime * multiplier;
          }
          hightime = 1;
        } else if ( (!thisbit) && (!lastbit) && (lowtime < 10000) ) {
          if ( highstarted) {
            lowtime++;
          }
        }
        lastbit = thisbit;
      }
    }
    memset(spi_out, 0, sizeof(spi_out));
  }

  state = IDLE;
}

void accept_command() {
  if (Serial.available() > 0) {
    int incoming = Serial.read();
    if (incoming == 'r') {
      for (int i = 0; i < 100; i++) {
        test_uart_rx_only();
        Serial.println("running test_uart_rx_only on rx jack");
      }
    }

    if (incoming == 's') {
      for (int i = 0; i < 100; i++) {
        test_sbus();
        Serial.println("running sbus test on tx jack");
      }
    }
  }
  Serial.println("Commands: s(bus), r(xonly might be on rx probe)");
}

// FreeRTOS task wrappers for concurrent execution
void task_test_uart(void *parameter) {
  test_uart();
  xSemaphoreGive(uart_complete);
  vTaskDelete(NULL);
}

void task_test_buzz(void *parameter) {
  test_buzz();
  xSemaphoreGive(buzz_complete);
  vTaskDelete(NULL);
}

void loop() {
  if (i2c_detected) {
    write_oled("i2c ok");
    i2c_detected = false;
  }

  if (state == IDLE) {
    state = DSHOT_PENDING;
    spi_wait = 0;
    attachInterrupt(digitalPinToInterrupt(IO_PROBE), probe_trigger_spi, HIGH);
  }

  if ((state == DSHOT_PENDING) && (spi_wait++ > 100) ) {
    spi_wait = 0;
    state = SERVO_PENDING;
  }

  if (state == DSHOT_SPI) {
    get_timings_spi();
    delay(3);
    spi_to_dshot_timing();
    test_dshot();
    state = SERVO_PENDING;
  }

  if (state == SERVO_PENDING) {
    accept_command();

    // Run test_uart and test_buzz concurrently to save ~100ms
    xTaskCreate(task_test_uart, "uart", 4096, NULL, 1, NULL);
    xTaskCreate(task_test_buzz, "buzz", 4096, NULL, 1, NULL);

    // Wait for both tasks to complete (max 500ms timeout each)
    xSemaphoreTake(uart_complete, pdMS_TO_TICKS(500));
    xSemaphoreTake(buzz_complete, pdMS_TO_TICKS(500));

    get_timings(80);
    test_servo();
    state = IDLE;
  }

  delay(20);
}
