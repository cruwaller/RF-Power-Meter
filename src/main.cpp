#include <Wire.h>
#include <Adafruit_GFX.h>

#define CALIBRATION 56.73

#define LCD_DRIVER_SSD1306 0
#define LCD_DRIVER_SH1106 1

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define AD8318_PIN A7

//#define OLED_ADDR 0x79 // 0x3C
#define OLED_ADDR 0x3C

#define ADC_REF_V_EXT 1

#if ADC_REF_V_EXT
#define AREF_V_SCALE 0.003154296875 // 3.23V
#else
#define AREF_V_SCALE 0.0044921875 // 4.60V
#endif

#if LCD_DRIVER_SSD1306
#include <Adafruit_SSD1306.h>
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#elif LCD_DRIVER_SH1106
#include "Adafruit_SH1106.h"
Adafruit_SH1106 display(OLED_RESET);
#else
#error "No valid OLED driver selected!"
#endif

uint16_t oledRefreshTime = 500; // ms
uint32_t previousSampleTime = 0;
uint16_t samples[SCREEN_WIDTH] = {0};
uint16_t next_sample_pos = 0;
uint16_t minSample = 65535;
uint16_t maxSample = 0;
uint32_t meanSample = 0;
//uint8_t minSampledBm = 65535;
uint8_t maxSampledBm = 0;
uint8_t meanSampledBm = 0;

// Calibration data, mV @ 0dBm
struct calib_data
{
    int freq;
    float ref;
};
struct calib_data calibs[] = {
    {100, 1.67},
    {600, 1.69},
    {1150, 1.667},
    {1870, 1.86},
    {2300, 1.812},
    {2500, 1.705}};

uint16_t sampleAdc(void)
{
#define OVERSAMPLING 8
    uint32_t temp = 0;
    for (int i = 0; i < OVERSAMPLING; i++)
    {
        temp += analogRead(AD8318_PIN);
    }
    return temp / OVERSAMPLING;
}

void sampleAd8318()
{
    float voltage;
    float dBm;
    for (int i = 0; i < SCREEN_WIDTH; i++)
    {
        samples[i] = sampleAdc();
    }
    next_sample_pos = ((uint16_t)(AREF_V_SCALE * 1000 * samples[0]) + 50) / 100;
    voltage = AREF_V_SCALE * samples[0];

    //Serial.println(samples[0]);

    minSample = 65535;
    maxSample = 0;
    meanSample = 0;

    for (int i = 0; i < SCREEN_WIDTH; i++)
    {
        /*
        Calibrated against an IMRC Tramp V2.0
        dBm = -0.1656*ADC + 56.73
        R² = 0.9913
        */

        // Convert raw sample to dBm
#if ADC_REF_V_EXT
        dBm = -0.1161672 * samples[i]; // = 0 @ ~670
#else
        dBm = -0.1656 * samples[i]; // = 0 @ ~470
#endif
        dBm += CALIBRATION;
        samples[i] = pow(10, (dBm / 10.0)); // dBm to mW

        if (samples[i] > maxSample)
        {
            maxSample = samples[i];
        }
        if (samples[i] < minSample)
        {
            minSample = samples[i];
        }

        meanSample += samples[i];

        // Serial.print(samples[i]);
        // Serial.print(",");
    }
    // Serial.println("");

    meanSample /= SCREEN_WIDTH;

    meanSampledBm = 10.0 * log10(meanSample);
    maxSampledBm = 10.0 * log10(maxSample);

    for (int i = 0; i < SCREEN_WIDTH; i++)
    {
        // samples[i] = map(samples[i], minSample, maxSample, 0, SCREEN_HEIGHT - 1 - 18);
        samples[i] = map(samples[i], minSample, maxSample, SCREEN_HEIGHT - 1 - 18, 0);
    }
}

void drawPlot()
{
    display.clearDisplay();

    for (int i = 0; i < SCREEN_WIDTH - 1; i++)
    {
        display.drawLine(i, samples[i] + 18, i + 1, samples[i + 1] + 18, WHITE);
    }

    display.setTextSize(1);
    display.setTextColor(WHITE, BLACK);
    display.setCursor(0, 0);

    display.print("Max:  ");
    display.print(maxSample);
    display.print(" mW, ");
    display.print(maxSampledBm);
    display.println(" dBm");

    /*
    display.print("Mean: ");
    display.print(meanSample);
    display.print(" mW, ");
    display.print(meanSampledBm);
    display.println(" dBm");
    */
    display.println(next_sample_pos);

    display.display();
}

void setup()
{
#if ADC_REF_V_EXT
    analogReference(EXTERNAL);
#else
    analogReference(DEFAULT);
#endif

    Serial.begin(115200);

    pinMode(AD8318_PIN, INPUT);

#if LCD_DRIVER_SSD1306
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
    {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;)
            ; // Don't proceed, loop forever
    }
#elif LCD_DRIVER_SH1106
    display.begin(SH1106_SWITCHCAPVCC, OLED_ADDR);
#endif

    display.clearDisplay();
}

void loop()
{
    uint32_t now = millis();

    if (oledRefreshTime <= (now - previousSampleTime))
    {
        Serial.println("sampling");
        sampleAd8318();

        drawPlot();

        previousSampleTime = now;
    }
}
