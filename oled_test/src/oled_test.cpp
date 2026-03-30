/*
 * oled_test — standalone SSD1306 hello world
 *
 * No USB host, no MIDI, no LittleFS.
 * Just the OLED on GP4(SDA)/GP5(SCL) I2C addr 0x3C.
 *
 * Expected result:
 *   - Display shows "Hello!" on line 1
 *   - A counter increments every second on line 2
 *   - Built-in LED blinks every 500ms
 *   - Serial prints the same counter (if terminal connected)
 *
 * platformio.ini for this test:
 *   [env:oled_test]
 *   platform  = https://github.com/maxgerhardt/platform-raspberrypi.git
 *   board     = rpipico2
 *   framework = arduino
 *   board_build.core = earlephilhower
 *   lib_deps =
 *       adafruit/Adafruit SSD1306 @ ^2.5.9
 *       adafruit/Adafruit GFX Library @ ^1.11.9
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_W    64
#define OLED_H    32
#define OLED_ADDR 0x3C
#define SDA_PIN   4
#define SCL_PIN   5

Adafruit_SSD1306 disp(OLED_W, OLED_H, &Wire, -1);

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);

    Wire.setSDA(SDA_PIN);
    Wire.setSCL(SCL_PIN);
    Wire.begin();

    Serial.println("Starting OLED test...");

    if (!disp.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("ERROR: SSD1306 not found at 0x3C on GP4/GP5");
        Serial.println("Check wiring: SDA=GP4, SCL=GP5, VCC=3.3V, GND=GND");
        // Blink fast to indicate error
        while (true) {
            digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
            delay(100);
        }
    }

    Serial.println("SSD1306 OK");
    disp.clearDisplay();
    disp.setTextSize(1);
    disp.setTextColor(SSD1306_WHITE);
    disp.setCursor(0, 0);
    disp.println("Hello!");
    disp.println("USB2MIDI");
    disp.display();
}

void loop() {
    static uint32_t last = 0;
    static uint32_t count = 0;

    if (millis() - last < 1000) return;
    last = millis();
    count++;

    // Blink LED
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

    // Update display
    disp.clearDisplay();
    disp.setTextSize(1);
    disp.setTextColor(SSD1306_WHITE);
    disp.setCursor(0, 0);
    disp.println("Hello!");
    disp.setCursor(0, 12);
    disp.printf("t=%lu", count);
    disp.display();

    // Serial
    if (Serial) {
        Serial.printf("tick %lu\r\n", count);
        Serial.flush();
    }
}