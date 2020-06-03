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
//#define AREF_V_SCALE 0.003154296875 // 3.23V
const float V_REF           = 3.23;    // External reference voltage.
#else
//#define AREF_V_SCALE 0.0044921875 // 4.60V
const float V_REF           = 4.6;    // Internal default reference voltage.
#endif
const float AD_RESOLUTION   = 1023.0;   // AD resolution - 1 = 2^10 - 1.
const float AD_SCALE        = V_REF / AD_RESOLUTION;

#if LCD_DRIVER_SSD1306
#include <Adafruit_SSD1306.h>
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#elif LCD_DRIVER_SH1106
#include "Adafruit_SH1106.h"
Adafruit_SH1106 display(OLED_RESET);
#else
#error "No valid OLED driver selected!"
#endif

// OLED update
uint16_t const oledRefreshTime = 100; // ms
uint32_t oledRefreshPrevious = 0;

// sample update
uint16_t const samplingRate = 10; // ms
uint32_t samplingRatePrevious = 0;

// Samples over time
uint16_t samples[SCREEN_WIDTH] = {0};
uint16_t next_sample_pos = 0;

// Info Values over current samples
uint16_t minSample = 65535;
uint16_t maxSample = 0;
uint32_t meanSample = 0;
uint8_t  maxSampledBm = 0;


uint16_t TEMP_VAL = 0;

#define DBM_MAX 0.0
#define DBM_MIN -50.0

// Calibration data, mV @ 0dBm
//  * measured to match DBM_MIN
struct calib_data
{
    uint16_t freq;
    float ref_0V;
    float slope;
    float offset_db;
};
// TODO: calibrations are not valid yet!
//    values are based on the -10dBm value from the datasheet
struct calib_data calibrations[] = {
    { 900, 21.837, 24.5, 55.},
    {1900, 19.918, 24.4, 55.},
    {2200, 19.918, 24.4, 55.},
    {2400, 19.918, 24.4, 55.},
    {3600, 19.506, 24.3, 55.},
    {5800, 25.391, 24.3, 55.},
    {8000, 36.087, 23.0, 55.},
};
struct calib_data & calibration = calibrations[0];

uint16_t sampleAdc(void)
{
#define OVERSAMPLING 16
    uint16_t adc_sum = 0;
    uint16_t i;
    for (i = 0; i < OVERSAMPLING; i++)
    {
        adc_sum += analogRead(AD8318_PIN);
    }
    return (adc_sum / OVERSAMPLING);
}

float sample_to_dBm(uint16_t sample)
{
#if 1
    /*
      Calibrated against an IMRC Tramp V2.0
      dBm = -0.1656*ADC + 56.73
      R² = 0.9913
    */
    // Convert raw sample to dBm
    float dBm = sample;
#if ADC_REF_V_EXT
    dBm *= -0.1161672; // = 0 @ ~670
#else
    dBm *= -0.1656; // = 0 @ ~470
#endif
    dBm += CALIBRATION;
    return dBm;

#else
    // ADC value to voltage
    float voltage = sample;
    voltage *= AD_SCALE;
    // Voltage to dBm
    voltage = (calibration.ref_0V - (voltage / calibration.slope)) /* + calibration.offset_db*/;
    // Check limits
    if (0 < voltage)
        voltage = 0;
    else if (-50. > voltage)
        voltage = -50;
    return voltage + 50; // scale to 0...50dBm
#endif
}

uint16_t sample_to_mW(uint16_t sample)
{
    float dBm = sample_to_dBm(sample);
    return pow(10, (dBm / 10.0)); // dBm to mW
}

void AD8318_sample_next(void)
{
    uint16_t sample = sampleAdc();

    TEMP_VAL = ((uint16_t)(AD_SCALE * 1000 * sample) + 50) / 100; // debug

    samples[next_sample_pos++] = sample_to_mW(sample);
    next_sample_pos %= SCREEN_WIDTH;
}

void AD8318_collect_range(void)
{
    int i;
    uint16_t sample;

    /* Search min, max and mean of the samples */
    minSample = 65535;
    maxSample = 0;
    meanSample = 0;

    for (i = 0; i < SCREEN_WIDTH; i++)
    {
        sample = samples[i];
        if (sample > maxSample)
        {
            maxSample = sample;
        }
        if (sample < minSample)
        {
            minSample = sample;
        }

        meanSample += sample;
    }

    meanSample /= SCREEN_WIDTH;
}

void OLED_drawPlot(void)
{
    int iter = next_sample_pos;
    uint16_t sample_curr, sample_prev;

    uint8_t meanSampledBm = 10.0 * log10(meanSample);
    uint8_t maxSampledBm = 10.0 * log10(maxSample);

    display.clearDisplay();

    sample_prev = map(samples[iter++], minSample, maxSample, SCREEN_HEIGHT - 1 - 18, 0);
    iter %= SCREEN_WIDTH;

    for (int i = 1; i < SCREEN_WIDTH - 1; i++)
    {
        sample_curr = map(samples[iter++], minSample, maxSample, SCREEN_HEIGHT - 1 - 18, 0);
        iter %= SCREEN_WIDTH;

        //display.drawLine(i, samples[i] + 18, i + 1, sample[i+1] + 18, WHITE);
        display.drawLine(i, sample_prev + 18, i + 1, sample_curr + 18, WHITE);

        sample_prev = sample;
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
    display.println(TEMP_VAL);

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

    if (samplingRate <= (now - samplingRatePrevious))
    {
        /* Get next value */
        AD8318_sample_next();
        samplingRatePrevious = now;
    }

    if (oledRefreshTime <= (now - oledRefreshPrevious))
    {
        AD8318_collect_range();
        OLED_drawPlot();

        oledRefreshPrevious = now;
    }
}
