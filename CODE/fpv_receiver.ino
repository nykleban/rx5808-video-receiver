/*
 * =============================================================
 *  FPV Відеоприймач RX5808 + ATmega328P
 *  Кастомна плата — Прошивка v1.0
 * =============================================================
 *
 *  АПАРАТНЕ ПІДКЛЮЧЕННЯ (згідно схеми):
 *  ─────────────────────────────────────
 *  RX5808 CH1/DATA  → PB3 (D11) — SPI_MOSI (bit-bang)
 *  RX5808 CH2/CS    → PB2 (D10) — SPI_CS
 *  RX5808 CH3/CLK   → PB5 (D13) — SPI_SCK
 *  RX5808 RSSI      → ADC6 (A6) — Аналоговий вхід
 *
 *  SW1 (Канал ▲)    → PD5 (D5)  — INPUT_PULLUP (активний LOW)
 *  SW2 (Канал ▼)    → PD6 (D6)  — INPUT_PULLUP (активний LOW)
 *  SW3 (Автоскан)   → PD7 (D7)  — INPUT_PULLUP (активний LOW)
 *
 *  LED індикація зарядки → PC3 (через R15 150Ω) — поки не використовується МК
 *
 *  I2C (BQ25703A зарядник) → PC4/SDA, PC5/SCL
 *  BQ25703A I2C адреса: 0x6B
 *
 *  UART (CP2102N) → PD0/RX, PD1/TX — для Serial Monitor
 *
 * =============================================================
 *  ЛОГІКА КЕРУВАННЯ:
 *  SW1 — наступний канал (1→2→...→8→1)
 *  SW2 — попередній канал (8→7→...→1→8)
 *  SW3 — автосканування (знаходить канал з найсильнішим RSSI)
 *
 *  Поточний канал та RSSI виводяться в Serial Monitor
 *  (115200 baud) через USB (CP2102N).
 * =============================================================
 */

#include <Wire.h>

// ─── ПІНИ ────────────────────────────────────────────────────
#define PIN_SPI_MOSI   11   // PB3
#define PIN_SPI_CS     10   // PB2
#define PIN_SPI_SCK    13   // PB5
#define PIN_RSSI       A6   // ADC6

#define PIN_BTN_UP      5   // PD5 — Канал ▲
#define PIN_BTN_DOWN    6   // PD6 — Канал ▼
#define PIN_BTN_SCAN    7   // PD7 — Автоскан

// ─── BQ25703A I2C ────────────────────────────────────────────
#define BQ25703A_ADDR       0x6B
// Регістри BQ25703A (16-бітні, little-endian)
#define REG_ADCVBAT_MSB     0x2B  // Напруга батареї (старший байт)
#define REG_ADCVBAT_LSB     0x2A  // Напруга батареї (молодший байт)
#define REG_ADC_OPTION      0x3A  // Увімкнення ADC вимірювань
#define REG_CHARGE_OPTION0  0x00  // Головні налаштування

// ─── ТАБЛИЦЯ ЧАСТОТ RX5808 ───────────────────────────────────
/*
 * Формат: кожен запис — 20-бітне число для передачі по SPI.
 * Синтез: F = (N * 32 + M) * 2, де N — ціла частина, M — дробова.
 * Таблиця покриває всі стандартні FPV діапазони:
 *   Індекс діапазону 0=Band A, 1=Band B, 2=Band E, 3=Band F(Fatshark), 4=Band R(Raceband)
 */
const uint16_t channelFreqTable[5][8] = {
  // Band A (Boscam A)
  { 5865, 5845, 5825, 5805, 5785, 5765, 5745, 5725 },
  // Band B (Boscam B)
  { 5733, 5752, 5771, 5790, 5809, 5828, 5847, 5866 },
  // Band E (DJI / Immersion)
  { 5705, 5685, 5665, 5645, 5885, 5905, 5925, 5945 },
  // Band F (Fatshark)
  { 5740, 5760, 5780, 5800, 5820, 5840, 5860, 5880 },
  // Band R (Raceband)
  { 5658, 5695, 5732, 5769, 5806, 5843, 5880, 5917 }
};

/*
 * 20-бітні SPI-слова для кожного каналу.
 * Формула: spiWord = ((F / 2) / 32) | (((F / 2) % 32) << 7)
 * Але простіше — використати готову таблицю нижче.
 *
 * Джерело: https://github.com/Marko-Bernbaum/rx5808-pro-diversity
 * (перевірені значення, сумісні з RX5808)
 */
const uint32_t channelSpiTable[5][8] = {
  // Band A
  { 0x282C, 0x2800, 0x27D4, 0x27A8, 0x277C, 0x2750, 0x2724, 0x26F8 },
  // Band B
  { 0x2596, 0x260C, 0x2682, 0x26F8, 0x276E, 0x27E4, 0x285A, 0x28D0 },
  // Band E
  { 0x246C, 0x2440, 0x2414, 0x23E8, 0x2904, 0x297A, 0x29F0, 0x2A66 },
  // Band F
  { 0x2606, 0x267C, 0x26F2, 0x2768, 0x27DE, 0x2854, 0x28CA, 0x2940 },
  // Band R
  { 0x2362, 0x23D8, 0x244E, 0x24C4, 0x253A, 0x25B0, 0x2626, 0x269C }
};

// ─── СТАН ПРОГРАМИ ───────────────────────────────────────────
uint8_t currentBand    = 4;  // Починаємо з Raceband (найпопулярніший)
uint8_t currentChannel = 0;  // Канал 1 (індекс 0)
bool    autoScanActive = false;

// Для дебаунсингу кнопок (апаратний RC вже є, але додаємо програмний)
uint32_t lastBtnTime = 0;
#define BTN_DEBOUNCE_MS  50

// Для автосканування
uint8_t  scanBand    = 0;
uint8_t  scanChannel = 0;
uint32_t scanTimer   = 0;
#define SCAN_SETTLE_MS  50    // Час стабілізації після перемикання (мс)
#define SCAN_SAMPLES     5    // Кількість зчитувань RSSI для усереднення

uint16_t bestRssi    = 0;
uint8_t  bestBand    = 0;
uint8_t  bestChan    = 0;

// Для виводу в Serial (щоб не спамити)
uint32_t lastPrintTime = 0;
#define PRINT_INTERVAL_MS  500

// ─── ПРОТОТИПИ ФУНКЦІЙ ───────────────────────────────────────
void     rx5808_SetFrequency(uint8_t band, uint8_t channel);
void     rx5808_SpiWrite(uint32_t data, uint8_t bits);
uint16_t rssi_Read(void);
uint16_t rssi_ReadAverage(uint8_t samples);
void     bq25703a_InitADC(void);
float    bq25703a_ReadVbat(void);
void     printStatus(void);
void     startAutoScan(void);
bool     autoScanStep(void);

// =============================================================
//  SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  Serial.println(F("================================="));
  Serial.println(F("  FPV Receiver RX5808 v1.0"));
  Serial.println(F("================================="));

  // Налаштування SPI пінів (bit-bang)
  pinMode(PIN_SPI_MOSI, OUTPUT);
  pinMode(PIN_SPI_CS,   OUTPUT);
  pinMode(PIN_SPI_SCK,  OUTPUT);
  digitalWrite(PIN_SPI_CS,  HIGH);  // CS неактивний
  digitalWrite(PIN_SPI_SCK, LOW);
  digitalWrite(PIN_SPI_MOSI,LOW);

  // RSSI — аналоговий вхід (ADC6 не потребує pinMode)

  // Кнопки — INPUT_PULLUP (кнопки підтягнуті до +3.3V через 10K на схемі,
  // але внутрішній pullup не зашкодить як резервний)
  pinMode(PIN_BTN_UP,   INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_SCAN, INPUT_PULLUP);

  // I2C для BQ25703A
  Wire.begin();
  delay(100);
  bq25703a_InitADC();  // Запускаємо ADC вимірювання в зарядника

  // Встановлюємо початкову частоту
  Serial.print(F("Встановлюємо початковий канал: Band R, CH1 ("));
  Serial.print(channelFreqTable[currentBand][currentChannel]);
  Serial.println(F(" MHz)"));

  rx5808_SetFrequency(currentBand, currentChannel);
  delay(50);  // Час стабілізації PLL

  Serial.println(F("Готово! Керування:"));
  Serial.println(F("  SW1 (D5) = Канал ▲"));
  Serial.println(F("  SW2 (D6) = Канал ▼"));
  Serial.println(F("  SW3 (D7) = Автосканування"));
  Serial.println(F("---------------------------------"));
}

// =============================================================
//  LOOP
// =============================================================
void loop() {
  uint32_t now = millis();

  // ── Обробка кнопок ─────────────────────────────────────────
  if ((now - lastBtnTime) > BTN_DEBOUNCE_MS) {

    // SW1 — Канал вгору
    if (digitalRead(PIN_BTN_UP) == LOW) {
      lastBtnTime = now;
      autoScanActive = false;

      currentChannel++;
      if (currentChannel >= 8) {
        currentChannel = 0;
        currentBand++;
        if (currentBand >= 5) currentBand = 0;
      }

      rx5808_SetFrequency(currentBand, currentChannel);
      Serial.print(F("▲ Band "));
      Serial.print(currentBand);
      Serial.print(F(" CH"));
      Serial.print(currentChannel + 1);
      Serial.print(F(" — "));
      Serial.print(channelFreqTable[currentBand][currentChannel]);
      Serial.println(F(" MHz"));

      // Чекаємо поки кнопку відпустять
      while (digitalRead(PIN_BTN_UP) == LOW) delay(10);
    }

    // SW2 — Канал вниз
    else if (digitalRead(PIN_BTN_DOWN) == LOW) {
      lastBtnTime = now;
      autoScanActive = false;

      if (currentChannel == 0) {
        currentChannel = 7;
        if (currentBand == 0) currentBand = 4;
        else currentBand--;
      } else {
        currentChannel--;
      }

      rx5808_SetFrequency(currentBand, currentChannel);
      Serial.print(F("▼ Band "));
      Serial.print(currentBand);
      Serial.print(F(" CH"));
      Serial.print(currentChannel + 1);
      Serial.print(F(" — "));
      Serial.print(channelFreqTable[currentBand][currentChannel]);
      Serial.println(F(" MHz"));

      while (digitalRead(PIN_BTN_DOWN) == LOW) delay(10);
    }

    // SW3 — Старт/Стоп автосканування
    else if (digitalRead(PIN_BTN_SCAN) == LOW) {
      lastBtnTime = now;

      if (!autoScanActive) {
        Serial.println(F("⟳ Автосканування запущено..."));
        startAutoScan();
      } else {
        autoScanActive = false;
        Serial.println(F("⏹ Автосканування зупинено"));
      }

      while (digitalRead(PIN_BTN_SCAN) == LOW) delay(10);
    }
  }

  // ── Автосканування ─────────────────────────────────────────
  if (autoScanActive) {
    if ((now - scanTimer) >= SCAN_SETTLE_MS) {
      scanTimer = now;
      bool done = autoScanStep();

      if (done) {
        autoScanActive = false;
        currentBand    = bestBand;
        currentChannel = bestChan;

        Serial.println(F("✓ Найкращий сигнал знайдено:"));
        Serial.print(F("  Band "));
        Serial.print(currentBand);
        Serial.print(F(" CH"));
        Serial.print(currentChannel + 1);
        Serial.print(F(" — "));
        Serial.print(channelFreqTable[currentBand][currentChannel]);
        Serial.print(F(" MHz, RSSI="));
        Serial.println(bestRssi);

        rx5808_SetFrequency(currentBand, currentChannel);
      }
    }
  }

  // ── Вивід статусу в Serial кожні 500мс ────────────────────
  if ((now - lastPrintTime) >= PRINT_INTERVAL_MS) {
    lastPrintTime = now;
    if (!autoScanActive) {
      printStatus();
    }
  }
}

// =============================================================
//  RX5808: ВСТАНОВЛЕННЯ ЧАСТОТИ ЧЕРЕЗ BIT-BANG SPI
// =============================================================
/*
 * Протокол RX5808 (нестандартний):
 *  1. Підтягуємо CS HIGH → LOW (початок транзакції)
 *  2. Передаємо 4 біти адреси регістру (LSB першим)
 *  3. Передаємо 1 біт: 1=запис, 0=читання
 *  4. Передаємо 20 біт даних (LSB першим)
 *  5. CS → HIGH (кінець транзакції)
 *
 *  Регістр синтезатора: адреса 0x01
 */
void rx5808_SetFrequency(uint8_t band, uint8_t channel) {
  uint32_t spiData = channelSpiTable[band][channel];

  // Адреса регістру синтезатора = 0x01, команда запису = 1
  // Пакет: [4 біти адреси] + [1 біт запис] + [20 біт даних] = 25 біт
  uint32_t packet = 0;
  // Адреса 0x01 (4 біти, LSB першим): 1000
  packet |= (uint32_t)0x01;       // Біти 0-3: адреса
  packet |= (uint32_t)1 << 4;     // Біт 4: запис
  packet |= (uint32_t)(spiData & 0xFFFFF) << 5;  // Біти 5-24: дані (20 біт)

  digitalWrite(PIN_SPI_CS, LOW);
  delayMicroseconds(1);

  rx5808_SpiWrite(packet, 25);

  delayMicroseconds(1);
  digitalWrite(PIN_SPI_CS, HIGH);
}

/*
 * Передача бітів по SPI (LSB першим, CPOL=0, CPHA=0)
 */
void rx5808_SpiWrite(uint32_t data, uint8_t bits) {
  for (uint8_t i = 0; i < bits; i++) {
    digitalWrite(PIN_SPI_MOSI, (data >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(PIN_SPI_SCK, HIGH);
    delayMicroseconds(1);
    digitalWrite(PIN_SPI_SCK, LOW);
    delayMicroseconds(1);
  }
  digitalWrite(PIN_SPI_MOSI, LOW);
}

// =============================================================
//  RSSI: ЗЧИТУВАННЯ РІВНЯ СИГНАЛУ
// =============================================================
/*
 * ADC6 на ATmega328P — лише аналоговий вхід (без цифрової функції).
 * Значення 0–1023: 0 = 0V, 1023 = 3.3V (AVCC).
 * Типовий RSSI від RX5808: ~0.5V (немає сигналу) — ~1.8V (сильний сигнал).
 * Відповідно: ~155 (немає) — ~560 (сильний).
 */
uint16_t rssi_Read(void) {
  return analogRead(PIN_RSSI);
}

uint16_t rssi_ReadAverage(uint8_t samples) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; i++) {
    sum += analogRead(PIN_RSSI);
    delay(2);
  }
  return (uint16_t)(sum / samples);
}

// =============================================================
//  АВТОСКАНУВАННЯ
// =============================================================
void startAutoScan(void) {
  autoScanActive = true;
  scanBand    = 0;
  scanChannel = 0;
  bestRssi    = 0;
  bestBand    = 0;
  bestChan    = 0;
  scanTimer   = millis() - SCAN_SETTLE_MS;  // Одразу перший крок

  rx5808_SetFrequency(scanBand, scanChannel);
}

/*
 * Один крок сканування. Повертає true коли всі канали перебрано.
 */
bool autoScanStep(void) {
  // Зчитуємо RSSI поточного каналу (вже дочекались SCAN_SETTLE_MS)
  uint16_t rssi = rssi_ReadAverage(SCAN_SAMPLES);

  Serial.print(F("  Скан B"));
  Serial.print(scanBand);
  Serial.print(F(" CH"));
  Serial.print(scanChannel + 1);
  Serial.print(F(" ("));
  Serial.print(channelFreqTable[scanBand][scanChannel]);
  Serial.print(F(" MHz) RSSI="));
  Serial.println(rssi);

  if (rssi > bestRssi) {
    bestRssi = rssi;
    bestBand = scanBand;
    bestChan = scanChannel;
  }

  // Наступний канал
  scanChannel++;
  if (scanChannel >= 8) {
    scanChannel = 0;
    scanBand++;
    if (scanBand >= 5) {
      return true;  // Всі канали перебрано
    }
  }

  // Встановлюємо наступну частоту (стабілізація відбудеться через SCAN_SETTLE_MS)
  rx5808_SetFrequency(scanBand, scanChannel);
  return false;
}

// =============================================================
//  BQ25703A: ІНІЦІАЛІЗАЦІЯ ADC ТА ЧИТАННЯ НАПРУГИ БАТАРЕЇ
// =============================================================
/*
 * BQ25703A — 20-бітний I2C зарядник для 1-4 cells.
 * Для читання VBAT треба спочатку включити ADC вимірювання.
 * Регістр ADC_OPTION (0x3A): біт 7 = ADC_START (одиночне вимірювання),
 * або біт 6 = ADC_FULLSCALE + біт 7 для безперервного.
 *
 * ВАЖЛИВО: BQ25703A використовує 16-бітні регістри.
 */
void bq25703a_InitADC(void) {
  // Перевіряємо чи є BQ25703A на шині
  Wire.beginTransmission(BQ25703A_ADDR);
  uint8_t err = Wire.endTransmission();

  if (err != 0) {
    Serial.println(F("⚠ BQ25703A не знайдено на I2C (адреса 0x6B)"));
    Serial.println(F("  Перевірте: живлення, підтяжки SDA/SCL, пайку"));
    return;
  }

  Serial.println(F("✓ BQ25703A знайдено"));

  // Включаємо ADC: IBAT disable, PSYS disable, VBUS enable, IDCHG enable
  // Регістр ADC_OPTION (0x3A), встановлюємо безперервний режим ADC
  // Байт LSB: EN_ADC_VBAT(bit1)=1, EN_ADC_VSYS(bit0)=1
  // Байт MSB: ADC_CONV(bit7)=1 (безперервне)
  Wire.beginTransmission(BQ25703A_ADDR);
  Wire.write(REG_ADC_OPTION);    // Адреса регістру
  Wire.write(0x03);              // LSB: увімкнути VBAT та VSYS ADC
  Wire.write(0x80);              // MSB: безперервний режим
  Wire.endTransmission();

  delay(100);  // Чекаємо першого перетворення
  Serial.println(F("✓ BQ25703A ADC запущено"));
}

/*
 * Читаємо напругу батареї (VBAT) в вольтах.
 * Регістр ADCVBAT: 8 біт (тільки MSB, LSB=0).
 * Формула: Vbat = 2.88V + значення * 64mV (згідно datasheet BQ25703A)
 */
float bq25703a_ReadVbat(void) {
  // Запит регістру ADCVBAT_MSB (0x2B)
  Wire.beginTransmission(BQ25703A_ADDR);
  Wire.write(REG_ADCVBAT_MSB);
  Wire.endTransmission(false);  // Repeated start

  Wire.requestFrom(BQ25703A_ADDR, (uint8_t)1);
  if (Wire.available()) {
    uint8_t raw = Wire.read();
    // Формула з datasheet: VBAT(V) = 2.88 + raw * 0.064
    return 2.88f + (float)raw * 0.064f;
  }
  return 0.0f;
}

/*
 * Приблизний відсоток заряду Li-Ion/Li-Po 2S батареї.
 * 2S повністю заряджена: 8.4V (4.2V * 2)
 * 2S повністю розряджена: 6.0V (3.0V * 2) — безпечна межа
 */
uint8_t batteryPercent(float vbat) {
  if (vbat >= 8.4f) return 100;
  if (vbat <= 6.0f) return 0;
  return (uint8_t)((vbat - 6.0f) / (8.4f - 6.0f) * 100.0f);
}

// =============================================================
//  ВИВІД СТАТУСУ В SERIAL MONITOR
// =============================================================
void printStatus(void) {
  uint16_t rssi = rssi_ReadAverage(3);
  float    vbat = bq25703a_ReadVbat();

  // Назви діапазонів
  const char* bandNames[] = { "A", "B", "E", "F", "R" };

  Serial.print(F("Band "));
  Serial.print(bandNames[currentBand]);
  Serial.print(F(" CH"));
  Serial.print(currentChannel + 1);
  Serial.print(F(" | "));
  Serial.print(channelFreqTable[currentBand][currentChannel]);
  Serial.print(F(" MHz | RSSI: "));
  Serial.print(rssi);

  // Проста шкала сигналу
  Serial.print(F(" ["));
  uint8_t bars = map(rssi, 0, 1023, 0, 10);
  for (uint8_t i = 0; i < 10; i++) {
    Serial.print(i < bars ? '|' : '.');
  }
  Serial.print(F("] | Bat: "));

  if (vbat > 0.0f) {
    Serial.print(vbat, 2);
    Serial.print(F("V ("));
    Serial.print(batteryPercent(vbat));
    Serial.print(F("%)"));

    // Попередження про низький заряд
    if (vbat < 6.6f) {
      Serial.print(F(" ⚠ НИЗЬКИЙ ЗАРЯД!"));
    }
  } else {
    Serial.print(F("N/A"));
  }

  Serial.println();
}

/*
 * =============================================================
 *  ІНСТРУКЦІЯ З ПРОШИВКИ:
 * =============================================================
 *
 *  ВАРІАНТ А — Через USB (CP2102N, простіший):
 *  1. Підключи плату до комп'ютера через USB Type-C
 *  2. В Arduino IDE:
 *     - Плата: "Arduino Uno" або "Arduino Nano" (залежно від bootloader)
 *     - Порт: вибери COM-порт CP2102N
 *     - Якщо використовуєш "Arduino Nano": Процесор = "ATmega328P (Old Bootloader)"
 *  3. Скетч → Завантажити (Ctrl+U)
 *
 *  ВАРІАНТ Б — Через ICSP (J5, якщо bootloader не прошитий):
 *  1. Підключи USBASP або інший AVR ISP до роз'єму J5
 *     J5 розпіновка (стандарт AVR ICSP 2x3):
 *       1=MISO  2=VCC
 *       3=SCK   4=MOSI
 *       5=RST   6=GND
 *  2. В Arduino IDE: Інструменти → Програматор → USBasp
 *  3. Спочатку: Інструменти → Записати завантажувач (щоб встановити Fuses)
 *  4. Потім: Скетч → Завантажити через програматор
 *
 *  FUSE BITS для ATmega328P (16 MHz кварц):
 *     Low:  0xFF  (зовнішній кварц 8+ MHz)
 *     High: 0xDE  (SPIEN, bootloader 512 bytes)
 *     Ext:  0xFD
 *
 *  SERIAL MONITOR: 115200 baud, NL+CR
 * =============================================================
 */
