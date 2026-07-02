import sys
import re
import collections
import serial
from PyQt5 import QtWidgets, QtCore
import pyqtgraph as pg

# =====================================================================
# НАЛАШТУВАННЯ ПОРТУ
# =====================================================================
SERIAL_PORT = 'COM5'  # Твій робочий порт Arduino Nano
BAUD_RATE   = 115200
MAX_POINTS  = 150     # Кількість точок на діаграмі (ширина розгортки екрана)

class RSSILiveDiagram(QtWidgets.QWidget):
    def init(self):
        super().init()
        
        # Підключення до Arduino Nano
        try:
            self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.01)
        except Exception as e:
            print(f"Помилка відкриття порту {SERIAL_PORT}: {e}")
            print("Переконайся, що Монітор порту в Arduino IDE закритий!")
            sys.exit(1)
            
        # Кільцевий буфер для збереження історії значень RSSI
        self.rssi_data = collections.deque([0] * MAX_POINTS, maxlen=MAX_POINTS)
        self.x_axis = list(range(MAX_POINTS))
        
        # Парсери рядків Serial порту
        self.status_pattern = re.compile(r"RSSI:\s*(\d+)")
        self.best_pattern   = re.compile(r"RSSI=(\d+)")
        
        self.info_text = "Очікування сигналу від RX5808..."
        
        self.init_layout()
        
        # Таймер для оновлення діаграми: 40 мс = 25 кадрів на секунду (FPS)
        self.ui_timer = QtCore.QTimer()
        self.ui_timer.setInterval(40) 
        self.ui_timer.timeout.connect(self.refresh_diagram)
        self.ui_timer.start()

    def init_layout(self):
        self.setWindowTitle('FPV RSSI Real-Time Diagram')
        self.resize(900, 500)
        self.setStyleSheet("background-color: #141414; color: #E0E0E0;")
        
        main_layout = QtWidgets.QVBoxLayout()
        
        # Текстове табло над діаграмою
        self.label_status = QtWidgets.QLabel(self.info_text)
        self.label_status.setStyleSheet("font-size: 18px; font-family: monospace; font-weight: bold; color: #00FFCC; padding: 5px;")
        main_layout.addWidget(self.label_status)
        
        # Налаштування віджета діаграми pyqtgraph
        self.graph_widget = pg.PlotWidget()
        self.graph_widget.setBackground('#1A1A1A')
        
        # Стилізація сітки діаграми
        self.graph_widget.showGrid(x=True, y=True, alpha=0.25)
        self.graph_widget.setYRange(0, 700)  # Максимальна межа АЦП Arduino (~700)
        
        # Назви осей
        self.graph_widget.setLabel('left', 'Потужність сигналу (RSSI)', color='#A0A0A0')
        self.graph_widget.setLabel('bottom', 'Часова шкала (останні семпли)', color='#A0A0A0')
        
        # Створення лінії діаграми (яскравий колір, товщина лінії = 2.5)
        chart_pen = pg.mkPen(color='#00FFCC', width=2.5)
        self.curve = self.graph_widget.plot(self.x_axis, list(self.rssi_data), pen=chart_pen)
        
        main_layout.addWidget(self.graph_widget)
        self.setLayout(main_layout)

    def refresh_diagram(self):
        try:
            # Вигрібаємо всі дані з буфера порту, щоб уникнути накопичення затримок
            while self.ser.in_waiting > 0:
                raw_line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if not raw_line:
                    continue
                
                # Пошук стандартного виводу стану
                match_status = self.status_pattern.search(raw_line)
                if match_status:
                    val = int(match_status.group(1))
                    self.rssi_data.append(val)
                    
                    # Залишаємо інформаційну частину рядка без символів [|||...]
                    self.info_text = f"📡 {raw_line.split('[')[0].strip()}"
                    continue
                
                # Пошук результату автосканування
                match_best = self.best_pattern.search(raw_line)
                if match_best:
                    val = int(match_best.group(1))
                    self.rssi_data.append(val)
                    self.info_text = f"🏆 Знайдено: {raw_line}"
                    continue
                
                # Якщо йде сканування
                if raw_line.startswith('.') or "Scanning" in raw_line:
                    self.info_text = "🔍 Сканування всіх 40 каналів..."
                    
        except Exception as serial_err:
            self.info_text = f"Помилка прийому: {serial_err}"

        # Оновлюємо текстову інформацію та малюємо нову точку на діаграмі
        self.label_status.setText(self.info_text)
        self.curve.setData(self.x_axis, list(self.rssi_data))

    def closeEvent(self, event):
        if self.ser.isOpen():
            self.ser.close()
        event.accept()

if name == 'main':
    app = QtWidgets.QApplication(sys.argv)
    diagram = RSSILiveDiagram()
    diagram.show()
    sys.exit(app.exec_())  # Виправлений запуск головного циклу