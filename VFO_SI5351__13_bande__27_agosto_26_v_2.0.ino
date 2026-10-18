/*
VFO Si5351 + Arduino Nano from original idea 
by PU2CLR https://www.qrz.com/db/PU2CLR
https://projecthub.arduino.cc/CesarSound/10khz-to-225mhz-vforf-generator-with-si5351-version-2-acdc25

I5SKK modifications
Version 2.0 27 aug 2026

PASSI:
25 Hz
100 Hz
1 kHz
10 kHz
500 kHz
1 MHz

BANDE:
160 m
80 m
60 m
40 m
30 m
20 m
17 m
15 m
12 m
10 m
40.65 MHz
6 m
4 m

A2 = RX / TX
A3 = MODO

A3 a GND:
RX = TX = frequenza impostata

A3 aperto:
TX = frequenza impostata
RX = frequenza - 20 kHz

EEPROM:
frequenza indipendente per ogni banda
ultima banda utilizzata
ultimo passo di sintonia
*/

#include <Wire.h>
#include <Rotary.h>
#include <si5351.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

// =================================================
// PIN
// =================================================

#define tunestep       A0
#define tunestep_down  5
#define band           A1
#define band_down      4
#define rx_tx          A2
#define adc             A3
#define LED             13

// =================================================
// PARAMETRI
// =================================================

#define BAND_INIT 3
#define XT_CAL_F 33000

const long rxOffset = 20000;

// =================================================
// EEPROM
// =================================================

#define EEPROM_FREQ_BASE 0
#define EEPROM_LAST_BAND 52
#define EEPROM_LAST_STEP 53

// =================================================
// OGGETTI
// =================================================

Rotary r = Rotary(2, 3);

Adafruit_SSD1306 display =
  Adafruit_SSD1306(128, 64, &Wire);

Si5351 si5351(0x60);

// =================================================
// VARIABILI
// =================================================

unsigned long freq = 0;
unsigned long fstep = 0;

long cal = XT_CAL_F;

byte encoder = 1;
byte stp = 2;
byte count = BAND_INIT;

bool sts = 0;
bool rf_on = true;

unsigned int period = 100;
unsigned long time_now = 0;

// =================================================
// DEBOUNCE
// =================================================

unsigned long lastTunestepTime = 0;
unsigned long lastTunestepDownTime = 0;
unsigned long lastBandTime = 0;
unsigned long lastBandDownTime = 0;

// =================================================
// PASSI
// =================================================

const unsigned long steps[] = {
  25,
  100,
  1000,
  10000,
  500000,
  1000000
};

// =================================================
// SETUP
// =================================================

void setup() {

  Wire.begin();

  display.begin(
    SSD1306_SWITCHCAPVCC,
    0x3C
  );

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.display();

  pinMode(2, INPUT_PULLUP);
  pinMode(3, INPUT_PULLUP);

  pinMode(tunestep, INPUT_PULLUP);
  pinMode(tunestep_down, INPUT_PULLUP);

  pinMode(band, INPUT_PULLUP);
  pinMode(band_down, INPUT_PULLUP);

  pinMode(rx_tx, INPUT_PULLUP);
  pinMode(adc, INPUT_PULLUP);

  pinMode(LED, OUTPUT);

  // =================================================
  // SI5351
  // =================================================

  si5351.init(
    SI5351_CRYSTAL_LOAD_8PF,
    0,
    0
  );

  si5351.set_correction(
    cal,
    SI5351_PLL_INPUT_XO
  );

  si5351.drive_strength(
    SI5351_CLK0,
    SI5351_DRIVE_8MA
  );

  si5351.output_enable(
    SI5351_CLK0,
    1
  );

  si5351.output_enable(
    SI5351_CLK1,
    0
  );

  si5351.output_enable(
    SI5351_CLK2,
    0
  );

  si5351.set_freq(
    0,
    SI5351_CLK1
  );

  si5351.set_freq(
    0,
    SI5351_CLK2
  );

  si5351.drive_strength(
    SI5351_CLK1,
    SI5351_DRIVE_2MA
  );

  si5351.drive_strength(
    SI5351_CLK2,
    SI5351_DRIVE_2MA
  );

  si5351.output_enable(
    SI5351_CLK1,
    0
  );

  si5351.output_enable(
    SI5351_CLK2,
    0
  );

  // =================================================
  // ULTIMA BANDA
  // =================================================

  EEPROM.get(
    EEPROM_LAST_BAND,
    count
  );

  if (count < 1 || count > 13)
    count = BAND_INIT;

  // =================================================
  // FREQUENZA BANDA
  // =================================================

  loadBandFrequency();

  // =================================================
  // CARICA ULTIMO PASSO
  // =================================================

  EEPROM.get(
    EEPROM_LAST_STEP,
    stp
  );

  if (stp > 5)
    stp = 2;

  fstep = steps[stp];

  // =================================================
  // RX / TX
  // =================================================

  sts =
    (digitalRead(rx_tx) == LOW) ?
    HIGH :
    LOW;

  tunegen();
  update_rf_output();
}

// =================================================
// LOOP
// =================================================

void loop() {

  // =================================================
  // ENCODER
  // =================================================

  char result = r.process();

  if (result == DIR_CW)
    set_frequency(1);

  else if (result == DIR_CCW)
    set_frequency(-1);

  unsigned long now = millis();

  // =================================================
  // STEP UP
  // =================================================

  if (
    digitalRead(tunestep) == LOW &&
    now - lastTunestepTime > 200
  ) {

    setstep();

    lastTunestepTime = now;
  }

  // =================================================
  // STEP DOWN
  // =================================================

  if (
    digitalRead(tunestep_down) == LOW &&
    now - lastTunestepDownTime > 200
  ) {

    setstep_down();

    lastTunestepDownTime = now;
  }

  // =================================================
  // BANDA UP
  // =================================================

  if (
    digitalRead(band) == LOW &&
    now - lastBandTime > 200
  ) {

    inc_preset();

    lastBandTime = now;
  }

  // =================================================
  // BANDA DOWN
  // =================================================

  if (
    digitalRead(band_down) == LOW &&
    now - lastBandDownTime > 200
  ) {

    dec_preset();

    lastBandDownTime = now;
  }

  // =================================================
  // RX / TX
  // =================================================

  bool nuovoStato =
    (digitalRead(rx_tx) == LOW) ?
    HIGH :
    LOW;

  if (nuovoStato != sts) {

    sts = nuovoStato;

    tunegen();
  }

  // =================================================
  // CAMBIO MODO A3
  // =================================================

  static bool ultimoModoA3 = HIGH;

  bool nuovoModoA3 =
    digitalRead(adc);

  if (nuovoModoA3 != ultimoModoA3) {

    ultimoModoA3 =
      nuovoModoA3;

    tunegen();
  }

  digitalWrite(LED, sts);

  // =================================================
  // RF
  // =================================================

  update_rf_output();

  // =================================================
  // DISPLAY
  // =================================================

  if (millis() - time_now > period) {

    displayfreq();
    layout();

    time_now = millis();
  }
}

// =================================================
// CONTROLLO RF
// =================================================

void update_rf_output() {

  si5351.output_enable(
    SI5351_CLK0,
    1
  );

  rf_on = true;
}

// =================================================
// SINTONIA
// =================================================

void set_frequency(short dir) {

  if (encoder != 1)
    return;

  if (dir == 1) {

    if (freq <= (225000000UL - fstep))
      freq += fstep;
    else
      freq = 225000000UL;

  } else {

    if (freq >= (10000UL + fstep))
      freq -= fstep;
    else
      freq = 10000UL;
  }

  saveBandFrequency();

  tunegen();
}

// =================================================
// GENERATORE
// =================================================

void tunegen() {

  si5351.output_enable(
    SI5351_CLK1,
    0
  );

  si5351.output_enable(
    SI5351_CLK2,
    0
  );

  long outputFreq;

  // =================================================
  // A3 = MODO
  // =================================================

  if (digitalRead(adc) == LOW) {

    // ===============================================
    // MODO SINGLE
    // RX = TX = FREQUENZA IMPOSTATA
    // ===============================================

    outputFreq =
      (long)freq;

  } else {

    // ===============================================
    // MODO -20 kHz
    // ===============================================

    if (sts == HIGH) {

      // TX
      outputFreq =
        (long)freq;

    } else {

      // RX
      outputFreq =
        (long)freq - rxOffset;
    }
  }

  if (outputFreq < 10000L)
    outputFreq = 10000L;

  si5351.set_freq(
    (uint64_t)outputFreq * 100ULL,
    SI5351_CLK0
  );
}

// =================================================
// STEP UP
// =================================================

void setstep() {

  stp =
    (stp + 1) % 6;

  fstep =
    steps[stp];

  EEPROM.put(
    EEPROM_LAST_STEP,
    stp
  );
}

// =================================================
// STEP DOWN
// =================================================

void setstep_down() {

  stp =
    (stp + 5) % 6;

  fstep =
    steps[stp];

  EEPROM.put(
    EEPROM_LAST_STEP,
    stp
  );
}

// =================================================
// BANDA UP
// =================================================

void inc_preset() {

  saveBandFrequency();

  count++;

  if (count > 13)
    count = 1;

  bandpresets();
}

// =================================================
// BANDA DOWN
// =================================================

void dec_preset() {

  saveBandFrequency();

  count--;

  if (count < 1)
    count = 13;

  bandpresets();
}

// =================================================
// CAMBIO BANDA
// =================================================

void bandpresets() {

  loadBandFrequency();

  si5351.pll_reset(
    SI5351_PLLA
  );

  EEPROM.put(
    EEPROM_LAST_BAND,
    count
  );

  tunegen();
}

// =================================================
// SALVA FREQUENZA
// =================================================

void saveBandFrequency() {

  int address =
    EEPROM_FREQ_BASE +
    ((count - 1) * sizeof(unsigned long));

  EEPROM.put(
    address,
    freq
  );
}

// =================================================
// CARICA FREQUENZA
// =================================================

void loadBandFrequency() {

  int address =
    EEPROM_FREQ_BASE +
    ((count - 1) * sizeof(unsigned long));

  unsigned long savedFreq;

  EEPROM.get(
    address,
    savedFreq
  );

  // -------------------------------------------------
  // Frequenza predefinita della banda.
  // Se il valore EEPROM non e' valido per quella
  // banda, viene usata la frequenza corretta.
  // -------------------------------------------------

  unsigned long defaultFreq;

  switch (count) {

    case 1:
      // 160 m
      defaultFreq = 1830000UL;
      break;

    case 2:
      // 80 m
      defaultFreq = 3500000UL;
      break;

    case 3:
      // 60 m
      defaultFreq = 5300000UL;
      break;

    case 4:
      // 40 m
      defaultFreq = 7000000UL;
      break;

    case 5:
      // 30 m
      defaultFreq = 10100000UL;
      break;

    case 6:
      // 20 m
      defaultFreq = 14000000UL;
      break;

    case 7:
      // 17 m
      defaultFreq = 18050000UL;
      break;

    case 8:
      // 15 m
      defaultFreq = 21000000UL;
      break;

    case 9:
      // 12 m
      defaultFreq = 24890000UL;
      break;

    case 10:
      // 10 m
      defaultFreq = 28000000UL;
      break;

    case 11:
      // 40.65 MHz
      defaultFreq = 40650000UL;
      break;

    case 12:
      // 6 m
      defaultFreq = 50000000UL;
      break;

    case 13:
      // 4 m
      defaultFreq = 70000000UL;
      break;

    default:
      count = 1;
      defaultFreq = 1830000UL;
      break;
  }

  // -------------------------------------------------
  // Controllo della frequenza memorizzata.
  // IMPORTANTE:
  // 10 MHz NON e' un valore valido per 160 m.
  // -------------------------------------------------

  bool valid = false;

  switch (count) {

    case 1:
      valid =
        (savedFreq >= 1800000UL &&
         savedFreq <= 2000000UL);
      break;

    case 2:
      valid =
        (savedFreq >= 3000000UL &&
         savedFreq <= 4000000UL);
      break;

    case 3:
      valid =
        (savedFreq >= 5000000UL &&
         savedFreq <= 5500000UL);
      break;

    case 4:
      valid =
        (savedFreq >= 6800000UL &&
         savedFreq <= 7300000UL);
      break;

    case 5:
      valid =
        (savedFreq >= 10000000UL &&
         savedFreq <= 10300000UL);
      break;

    case 6:
      valid =
        (savedFreq >= 14000000UL &&
         savedFreq <= 14350000UL);
      break;

    case 7:
      valid =
        (savedFreq >= 18000000UL &&
         savedFreq <= 18200000UL);
      break;

    case 8:
      valid =
        (savedFreq >= 21000000UL &&
         savedFreq <= 21500000UL);
      break;

    case 9:
      valid =
        (savedFreq >= 24800000UL &&
         savedFreq <= 25100000UL);
      break;

    case 10:
      valid =
        (savedFreq >= 28000000UL &&
         savedFreq <= 29700000UL);
      break;

    case 11:
      valid =
        (savedFreq >= 40000000UL &&
         savedFreq <= 42000000UL);
      break;

    case 12:
      valid =
        (savedFreq >= 49000000UL &&
         savedFreq <= 52000000UL);
      break;

    case 13:
      valid =
        (savedFreq >= 68000000UL &&
         savedFreq <= 72000000UL);
      break;
  }

  if (valid) {

    freq = savedFreq;

  } else {

    freq = defaultFreq;

    EEPROM.put(
      address,
      freq
    );
  }
}

// =================================================
// DISPLAY FREQUENZA
// =================================================

void displayfreq() {

  unsigned long realFreq =
    freq;

  unsigned int m =
    realFreq / 1000000;

  unsigned int k =
    (realFreq % 1000000) / 1000;

  unsigned int h =
    realFreq % 1000;

  display.clearDisplay();

  display.setTextSize(2);

  char buffer[16];

  if (m < 1) {

    display.setCursor(
      25,
      0
    );

    sprintf(
      buffer,
      "%03d.%03d",
      k,
      h
    );

  } else {

    display.setCursor(
      2,
      0
    );

    sprintf(
      buffer,
      "%2d.%03d.%03d",
      m,
      k,
      h
    );
  }

  display.print(buffer);
}

// =================================================
// LAYOUT OLED
// =================================================

void layout() {

  display.setTextColor(WHITE);

  // -------------------------------------------------
  // LINEA SOTTO LA FREQUENZA
  // -------------------------------------------------

  display.drawLine(
    0, 18,
    127, 18,
    WHITE
  );

  // -------------------------------------------------
  // BANDA
  // -------------------------------------------------

  display.setTextSize(2);

  display.setCursor(
    0,
    27
  );

  switch (count) {

    case 1:
      display.print("160m");
      break;

    case 2:
      display.print("80m");
      break;

    case 3:
      display.print("60m");
      break;

    case 4:
      display.print("40m");
      break;

    case 5:
      display.print("30m");
      break;

    case 6:
      display.print("20m");
      break;

    case 7:
      display.print("17m");
      break;

    case 8:
      display.print("15m");
      break;

    case 9:
      display.print("12m");
      break;

    case 10:
      display.print("10m");
      break;

    case 11:
      display.print("40.65");
      break;

    case 12:
      display.print("6m");
      break;

    case 13:
      display.print("4m");
      break;
  }

  // -------------------------------------------------
  // STEP
  // -------------------------------------------------

  display.setTextSize(1);

  display.setCursor(
    68,
    22
  );

  display.print("STEP");

  display.setTextSize(2);

  display.setCursor(
    68,
    30
  );

  switch (stp) {

    case 0:
      display.print("25");
      break;

    case 1:
      display.print("100");
      break;

    case 2:
      display.print("1k");
      break;

    case 3:
      display.print("10k");
      break;

    case 4:
      display.print("500k");
      break;

    case 5:
      display.print("1M");
      break;
  }

  // -------------------------------------------------
  // LINEA INFERIORE
  // -------------------------------------------------

  display.drawLine(
    0, 47,
    127, 47,
    WHITE
  );

  // -------------------------------------------------
  // RX / TX
  // -------------------------------------------------

  display.setTextSize(2);

  display.setCursor(
    0,
    50
  );

  if (digitalRead(adc) == LOW) {

    if (sts == HIGH) {

      display.print("TX");

    } else {

      display.print("RX");
    }

  } else {

    display.print("RXTX");
  }

  // -------------------------------------------------
  // MODO
  // -------------------------------------------------

  display.setTextSize(1);

  display.setCursor(
    48,
    55
  );

  if (digitalRead(adc) == LOW) {

    display.print("SINGLE");

  } else {

    display.print("-20 kHz");
  }

  display.setTextSize(1);

  display.display();
}
