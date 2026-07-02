# Smart FPV Analog RX & Spectral Analyzer

**Проєкт:** апаратно-програмний комплекс для прийому аналогового FPV-відеосигналу 5.8 GHz та сканування радіочастотного спектра в реальному часі.

---

## Про Проєкт

Цей репозиторій містить вихідний код прошивки, десктопного додатку та файли проєкту друкованої плати для розумного FPV-приймача. Система побудована на базі мікроконтролера **ATmega328P**, високочастотного модуля **RX5808** та розширеної системи керування живленням для забезпечення стабільних аналогових вимірювань.

Проєкт поєднує низькорівневе програмування вбудованих систем, розробку 4-шарової PCB з високими вимогами до розведення RF, силових та аналогових вузлів, а також десктопний аналізатор на Python. Проєкт розроблено в рамках інженерно-практичної роботи програми робототехніки Інституту ІТ та бізнесу.

Плата спроєктована в KiCad і замовлена через JLCPCB. На момент підготовки репозиторію плата ще не доставлена, тому в репозиторії подані проєктні матеріали: схема, PCB-layout, production-файли, 3D-рендери та скріншоти програмної частини.

---

## Основні Характеристики системи

### Hardware

- **RF-модуль:** приймач **RX5808 5.8 GHz** із керуванням через SPI для вибору частоти.
- **Мікроконтролер:** **ATmega328P** із зовнішнім кварцовим резонатором 16 MHz.
- **Power Management:** контролер заряду **Texas Instruments BQ25703A**, апаратний захист акумулятора **HY2120-CB**, шунти для вимірювання струму.
- **Інтерфейси:** **USB Type-C**, **CP2102N USB-to-UART**, торцевий **SMA edge-mount** роз'єм для FPV-антени.

### Software

- **Firmware C/C++:** прошивка для ATmega328P із керуванням RX5808 через SPI, зчитуванням RSSI через ADC, I2C-взаємодією з BQ25703A та serial logging.
- **Python GUI:** десктопний додаток для візуалізації RSSI в реальному часі та перевірки рівня сигналу на обраній частоті.

---

## Основні можливості

- прийом аналогового FPV-сигналу 5.8 GHz через RX5808;
- перемикання FPV-частот через SPI-керування RX5808;
- зчитування RSSI для оцінки рівня сигналу;
- виведення band, channel, frequency та RSSI через serial log;
- візуалізація RSSI в desktop analyzer у реальному часі;
- підготовлена 4-шарова PCB з окремими RF, MCU та power-management блоками;
- production package для виготовлення плати.

---

## Архітектура Друкованої Плати (PCB Layout)

Плата розроблена як 4-шарова PCB товщиною 1.6 мм з урахуванням суворих правил трасування:

1. **Ізоляція шумів:** силовий імпульсний блок з BQ25703A максимально віддалений від аналогової частини приймача RX5808.
2. **Розділення функціональних блоків:** RF, MCU, USB/UART та power-management частини винесені в окремі зони плати.
3. **Екранування:** GND-полігони використовуються для зменшення шумів і покращення стабільності аналогових вимірювань.
4. **Production-ready структура:** підготовлено BOM, positions, designators, IPC netlist і production archive для виготовлення плати.

---

## Firmware

Прошивка для ATmega328P реалізує:

- SPI-керування RX5808;
- вибір FPV-частоти;
- зчитування RSSI через ADC;
- I2C-взаємодію з power-management контролером;
- serial logging для перевірки каналу, частоти та RSSI.

Файл прошивки: `CODE/fpv_receiver.ino`

---

## Desktop analyzer

Python-додаток використовується для перегляду RSSI в реальному часі. Він дає змогу візуально порівнювати рівень сигналу на різних каналах і бачити стабільність прийому.

Використані технології: Python, PyQt5, pyqtgraph, pyserial, NumPy.

Файл додатку: `CODE/main.py`

---

## Структура репозиторію

```text
.
├── CODE/                         # firmware and desktop analyzer
│   ├── fpv_receiver.ino
│   └── main.py
├── IMAGES/                       # renders, schematics and analyzer screenshots
├── Custom_Libs/                  # custom KiCad symbols and footprints
├── production/                   # BOM, positions, designators, netlist
├── atmega328.kicad_pcb           # PCB layout
├── atmega328.kicad_sch           # main schematic
├── mcu_atmega_spi.kicad_sch      # MCU/SPI schematic block
├── rx5808_rf.kicad_sch           # RF schematic block
├── power_usb_battery_buck.kicad_sch
└── production.zip
```

---

## Візуальні матеріали

### PCB та схема

![Front 3D view](IMAGES/front_3d_view.png)

![Back 3D view](IMAGES/back_3d_view.png)

![PCB layout](IMAGES/pcb.png)

![Power block schematic](IMAGES/power_block_sch.png)

![RF block schematic](IMAGES/RF_block_sch.png)

![ATmega328P block schematic](IMAGES/atmega328_block_sch.png)

### RSSI analyzer

![Low signal RSSI](IMAGES/rx5808_rssi_realtime_low_signal.png)

![Detected signal RSSI](IMAGES/rx5808_rssi_realtime_detected_signal.png)

![Serial RSSI log](IMAGES/rx5808_serial_rssi_log.png)

---

## Production files

Папка `production/` містить матеріали для виготовлення та перевірки плати:

- `bom.csv`;
- `positions.csv`;
- `designators.csv`;
- `netlist.ipc`;
- `atmega328.zip`.

Також у корені є `production.zip` як готовий архів production package.

---

## Поточний статус

- schematic design completed;
- 4-layer PCB layout completed;
- production files generated;
- PCB ordered through JLCPCB;
- firmware and desktop analyzer added to the repository;
- RSSI analyzer screenshots included.
