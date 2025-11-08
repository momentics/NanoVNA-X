# Структура меню NanoVNA-X

## CAL
- **MECH CAL**
  - OPEN -> применение эталона
  - SHORT
  - LOAD
  - ISOLN
  - THRU
  - DONE -> запись результатов во flash
  - DONE IN RAM -> применение без сохранения
- CAL RANGE -> отображение/возврат сохранённого диапазона и числа точек
- CAL POWER -> AUTO или фиксированные токи 2/4/6/8 mA
- SAVE CAL -> выбор слота калибровки и запись коэффициентов
- CAL APPLY -> включение/выключение коррекции
- ENHANCED RESPONSE -> переключение алгоритма enhanced-response
- LOAD STD `[__VNA_Z_RENORMALIZATION__]` -> ввод номинального сопротивления нагрузки
- CAL RESET -> очистка текущей калибровки
- **SAVE/RECALL**
  - SAVE CAL
  - RECALL CAL
  - CAL APPLY
  - CAL RESET

## STIMULUS
- START / STOP / CENTER / SPAN -> ввод пределов свипа
- CW FREQ -> непрерывный режим на фиксированной частоте
- FREQ STEP -> шаг перестройки для джога
- JOG STEP -> AUTO или ручной ввод
- SET POINTS -> ввод произвольного количества точек
- Кнопки `%d PTS` -> пресеты из массива `POINTS_SET`
- MORE PTS -> доступ к расширенному списку точек

## DISPLAY
- TRACES -> управление TRACE 0…3, а также слотами сохранённых трасс `[STORED_TRACES > 0]`
- FORMAT S11 -> LOGMAG, PHASE, DELAY, SMITH, SWR, RESISTANCE, REACTANCE, |Z|, ветка MORE -> (POLAR, LINEAR, REAL, IMAG, Q FACTOR, CONDUCTANCE, SUSCEPTANCE, |Y|) -> MORE -> (Z PHASE, SERIES/SHUNT/PARALLEL представления)
- FORMAT S21 -> LOGMAG, PHASE, DELAY, SMITH, POLAR, LINEAR, REAL, IMAG, ветка MORE -> (series/shunt R/X/|Z|, Q)
- CHANNEL -> выбор канала CH0/CH1 для активной трассы
- SCALE -> AUTO SCALE, TOP, BOTTOM, SCALE/DIV, REFERENCE POSITION, E-DELAY, S21 OFFSET, SHOW GRID `[__USE_GRID_VALUES__]`, DOT GRID `[__USE_GRID_VALUES__]`
- MARKERS
  - SELECT MARKER, ALL OFF, DELTA
  - TRACKING
  - SEARCH (MAXIMUM/MINIMUM), SEARCH ← LEFT, SEARCH -> RIGHT
  - MOVE START / MOVE STOP / MOVE CENTER / MOVE SPAN
  - MARKER E-DELAY

## MEASURE
- TRANSFORM -> включение/отключение временной области, LOW PASS IMPULSE / LOW PASS STEP / BANDPASS, окно, VELOCITY FACTOR
- DATA SMOOTH `[__USE_SMOOTH__]` -> OFF и доступные коэффициенты усреднения
- MEASURE `[__VNA_MEASURE_MODULE__]` -> OFF, L/C MATCH, CABLE (S11), RESONANCE (S11), SHUNT LC (S21), SERIES LC (S21), SERIES XTAL (S21), FILTER (S21) с переходом в соответствующие подменю
- IF BANDWIDTH -> список преднастроек полосы ПЧ
- PORT-Z `[__VNA_Z_RENORMALIZATION__]` -> задание опорного импеданса

## SYSTEM
- TOUCH CAL
- TOUCH TEST
- BRIGHTNESS `[__LCD_BRIGHTNESS__]`
- SAVE CONFIG
- VERSION
- DATE/TIME `[__USE_RTC__]` -> SET DATE, SET TIME, RTC CAL, RTC 512 Hz/LED2
- **DEVICE**
  - THRESHOLD
  - TCXO
  - VBAT OFFSET
  - IF OFFSET `[USE_VARIABLE_OFFSET_MENU]`
  - REMEMBER STATE `[__USE_BACKUP__]`
  - FLIP DISPLAY `[__FLIP_DISPLAY__]`
  - DFU `[__DFU_SOFTWARE_MODE__]`
  - MORE
    - MODE (Si5351 / MS5351 / SWC5351)
    - SEPARATOR `[__DIGIT_SEPARATOR__]`
    - USB DEVICE UID `[__USB_UID__]`
    - CLEAR CONFIG -> CLEAR ALL AND RESET
- CONNECTION `[__USE_SERIAL_CONSOLE__]` -> выбор интерфейса (USB/UART) и скорости

## SD CARD
- LOAD `[__SD_FILE_BROWSER__]` -> файловый браузер с фильтрацией по расширению (SCREENSHOT, S1P, S2P, CAL)
- SAVE S1P -> запись S-параметров CH0 в файл S1P
- SAVE S2P -> запись обоих каналов в S2P
- SCREENSHOT -> снимок экрана (BMP или TIFF в зависимости от переключателя IMAGE FORMAT)
- SAVE CALIBRATION -> сохранение активного набора калибровки на карту
- IMAGE FORMAT `[__SD_CARD_DUMP_TIFF__]` -> переключение между `BMP` и `TIF`
- FORMAT SD `[FF_USE_MKFS]` -> размонтирование, запуск FatFs mkfs (`FM_FAT`), повторное монтирование и отчет о результате

## Управление свипом
- Кнопка `%s SWEEP` отображает текущее состояние («PAUSE SWEEP»/«RESUME SWEEP») и немедленно переключает движок свипа.
