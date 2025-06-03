#include <IRremote.hpp>

#include <EEPROM.h>
#include <DS1302.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Bonezegei_DHT11.h>
#include <Adafruit_Sensor.h>  // include Adafruit sensor library
#include <Adafruit_BMP280.h>
#include <SPI.h>


#define IR_RECEIVE_PIN 2
#define BMP_SCK   13
#define BMP_MISO  12
#define BMP_MOSI  11
#define BMP_CS    10
#define BMP280_ADDRESS 0x77


enum DisplayMode{DISPLAY_MODE_TIME_DATE, DISPLAY_MODE_TEMPERATURE, DISPLAY_MODE_HUMIDITY, DISPLAY_MODE_PRESSURE, DISPLAY_MODE_ALTITUDE};

enum TempMode{TEMP_CELCIUS, TEMP_FAHRENHEIT};

enum DateTimeMode{DATE_TIME, DATE,TIME};

enum PressureMode{HPA, MMHG};

struct Memory{
  DisplayMode field1;
  TempMode field2;
  DateTimeMode field3;
  PressureMode field4;
};

Bonezegei_DHT11 dht(A0);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_BMP280 bmp(BMP_CS);

DS1302 rtc(3, 4, 5);

const uint8_t number[11] = {15,82,80,16,86,84,20,78,76,12,15};
// Періоди оновлення
const unsigned int SENSOR_UPDATE_INTERVAL = 5000;  // Інтервал зчитування датчиків (мс)
const unsigned int DISPLAY_UPDATE_INTERVAL = 1000; // Інтервал оновлення дисплея (мс)

// Глобальні змінні для асинхронності
unsigned long lastSensorUpdate = 0;
unsigned long lastDisplayUpdate = 0;

// Дані датчиків
float currentTemp = 0.0;
float currentHumidity = 0.0;
float currentPressure = 0.0;
float currentAltitude = 0.0;

// Стани
uint8_t firstRowType = DISPLAY_MODE_TIME_DATE; // Тип змінної на першому рядку
uint8_t tempType = TEMP_CELCIUS;
uint8_t DateTimeType = DATE_TIME;
uint8_t pressureType = HPA;
bool scrollDirectionCalc = true; // true - вперед, false - назад
bool scrollDirectionFirst = true; // true - вперед, false - назад
bool scrollDirectionSecond = true; // true - вперед, false - назад

bool isPaused = false; // Пауза оновлення даних
int eeAddress = 0;   //Location we want the data to be put.

bool isCalculatorMode = false; // Режим калькулятора
String calculatorInput = ""; // Поточне введення

int offsetCalc = 0;
int offsetFirstRow = 0;
int offsetSecondRow = 0;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  // Get data from memory
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  Memory obj;
  EEPROM.get(eeAddress, obj);
  firstRowType = obj.field1; // Тип змінної на першому рядку
  tempType = obj.field2;
  DateTimeType = obj.field3;
  pressureType = obj.field4;

  if (!bmp.begin())
  {  
    Serial.println("Could not find a valid BMP280 sensor, check wiring!");
    delay(200);
  }

  lcd.init();
  lcd.backlight();
  
  dht.begin();

  updateSensors();

  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
}

void loop() {

  // Асинхронне оновлення даних датчиків
  if (!isPaused && !isCalculatorMode && millis() - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL) {
    lastSensorUpdate = millis();
    updateSensors();
  }

  // Асинхронне оновлення дисплея
  if (!isPaused && !isCalculatorMode && millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = millis();
    updateLCD();
  }

  if (IrReceiver.decode()) {
    remoteControl();
    delay(100);
    IrReceiver.resume();
  }
}

void updateSensors() {
  dht.getData();
  currentTemp = dht.getTemperature();
  currentHumidity = dht.getHumidity();
  currentPressure = bmp.readPressure();
  currentAltitude = bmp.readAltitude(1024.0); // Налаштуйте для вашого тиску на рівні моря
}

void updateLCD() {
  lcd.clear();

  // Форматуємо дату та час у вигляді рядка
  String date = rtc.getDateStr();
  String time = rtc.getTimeStr();
  // Вибір формату відображення (дата, час або разом)
  String dateTime;
  if (DateTimeType == DATE_TIME) {
    dateTime = date + " " + time;
  } else if (DateTimeType == DATE) {
    dateTime = date;
  } else { // TIME
    dateTime = time;
  }
  
  char type_temp = 'C';
  float Temp = currentTemp;
  if(tempType != TEMP_CELCIUS){
    Temp = (currentTemp *9)/5 + 32;
    type_temp = 'F';
  }

  String type_pressure = "hPA";
  float pressure = currentPressure/100;
  if(pressureType != HPA){
    pressure = currentPressure * 0.7500615;
    type_pressure = "mmHg";
  }
  
  // Перший рядок
  String firstRowText = "";
  switch (firstRowType) {
    case DISPLAY_MODE_TIME_DATE:
      firstRowText = dateTime;
      break;
    case DISPLAY_MODE_TEMPERATURE:
      firstRowText = "Temp: " + String(Temp) + (char)223 + type_temp;
      break;
    case DISPLAY_MODE_HUMIDITY:
      firstRowText = "Humidity: " + String(currentHumidity) + "%";
      break;
    case DISPLAY_MODE_PRESSURE:
      firstRowText = "Pressure: " + String(pressure) + type_pressure;
      break;
    case DISPLAY_MODE_ALTITUDE:
      firstRowText = "Altitude: " + String(currentAltitude) + "m";
      break;
  }

  // Другий рядок 
  String secondRowText = "";
  switch ((firstRowType + 1) % 5) {
    case DISPLAY_MODE_TIME_DATE:
      secondRowText = dateTime;
      break;
    case DISPLAY_MODE_TEMPERATURE:
      secondRowText = "Temp: " + String(Temp) + (char)223 + type_temp;
      break;
    case DISPLAY_MODE_HUMIDITY:
      secondRowText = "Humidity: " + String(currentHumidity) + "%";
      break;
    case DISPLAY_MODE_PRESSURE:
      secondRowText = "Pressure: " + String(pressure) + type_pressure;
      break;
    case DISPLAY_MODE_ALTITUDE:
      secondRowText = "Altitude: " + String(currentAltitude) + "m";
      break;
  }

  handleScrolling(firstRowText.length(), secondRowText.length());
  lcd.setCursor(0, 0);
  lcd.print(firstRowText.substring(offsetFirstRow, offsetFirstRow + 16));
  lcd.setCursor(0, 1);
  lcd.print(secondRowText.substring(offsetSecondRow, offsetSecondRow + 16));
  
}

void handleScrolling(int firstRowLength, int secondRowLength) {
  // Прокрутка першого рядка
  if (firstRowLength > 16) {
    if (scrollDirectionFirst) {
      offsetFirstRow++;
      if (offsetFirstRow >= firstRowLength - 16) {
        scrollDirectionFirst = false;
      }
    } else {
      offsetFirstRow--;
      if (offsetFirstRow <= 0) {
        scrollDirectionFirst = true;
      }
    }
  } else {
    offsetFirstRow = 0;  // Скидаємо зсув, якщо текст короткий
  }

  // Прокрутка другого рядка
  if (secondRowLength > 16) {
    if (scrollDirectionSecond) {
      offsetSecondRow++;
      if (offsetSecondRow >= secondRowLength - 16) {
        scrollDirectionSecond = false;
      }
    } else {
      offsetSecondRow--;
      if (offsetSecondRow <= 0) {
        scrollDirectionSecond = true;
      }
    }
  } else {
    offsetSecondRow = 0;  // Скидаємо зсув, якщо текст короткий
  }
}

void remoteControl() {
  uint16_t command = IrReceiver.decodedIRData.command;
  Serial.println(command);

  if (isCalculatorMode) {
    handleCalculatorInput(command);
    return;
  }

  switch (command) {
    case 22: // UP
        firstRowType = (firstRowType + 1) % 5;
      break;

    case 6: // DOWN
      if(firstRowType - 1 < 0)
        firstRowType = 4;
      else
        firstRowType = (firstRowType - 1) % 5;
      break;

    case 27: // LEFT
      if(firstRowType == DISPLAY_MODE_TIME_DATE)
        DateTimeType = (DateTimeType - 1 + 3) % 3;
      else if(firstRowType == DISPLAY_MODE_TEMPERATURE)
        tempType = (tempType - 1 + 2) % 2;
      else if(firstRowType == DISPLAY_MODE_PRESSURE)
        pressureType = (pressureType - 1 +2) %2;
      break;

    case 90: // RIGHT
      if(firstRowType == DISPLAY_MODE_TIME_DATE)
        DateTimeType = (DateTimeType + 1) % 3;
      else if(firstRowType == DISPLAY_MODE_TEMPERATURE)
        tempType = (tempType + 1) % 2;
       else if(firstRowType == DISPLAY_MODE_PRESSURE)
        pressureType = (pressureType + 1) %2;
      break;

    case 26: // STOP
      isPaused = !isPaused;
      break;
    case 69: // MENU
      isCalculatorMode = true;
      calculatorInput = "";
      
      int value;
      eeAddress += sizeof(Memory);
      EEPROM.get(eeAddress, value);
      eeAddress = 0;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Calc Mode: "+String(value));
      lcd.setCursor(0, 1);
      lcd.blink_on();
      offsetCalc=0;
      break;
    default:
      Serial.println("UNDEFINED");
  }
}

void handleCalculatorInput(uint16_t command) {  
  // Введення чисел та операцій
  for (int i = 0; i <= 10; i++) {
    if (command == number[i]) { // Якщо команда є цифрою
      calculatorInput += String(i); // Додаємо цифру до введення
      break;
    }
  }

  // Додавання оператора "+" до введення
  if (command == 17) { 
    if (!(calculatorInput==NULL) && 
        (calculatorInput[calculatorInput.length() - 1] != '+' && 
         calculatorInput[calculatorInput.length() - 1] != '-')) {
      calculatorInput += "+"; // Додаємо оператор
    }
  }

  // Додавання оператора "-" до введення
  if (command == 81) { 
    if (!(calculatorInput == NULL) && 
        (calculatorInput[calculatorInput.length() - 1] != '+' && 
         calculatorInput[calculatorInput.length() - 1] != '-')) {
      calculatorInput += "-"; // Додаємо оператор
    }
  }

  // Операція EXIT
  if (command == 69) { 
    isCalculatorMode = false;
    lcd.clear();
    lcd.blink_off();
    return;
  }


  // Обчислення результату "="
  if (command == 5) { 
    int result = executeLastOperation(calculatorInput); // Викликаємо обчислення
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Result: ");
    lcd.setCursor(0, 1);
    lcd.print(result);

    calculatorInput = "";     // Очищаємо введення
    offsetCalc=0;
    Memory obj = {
      firstRowType,
      tempType,
      DateTimeType
    };

    EEPROM.put(eeAddress, obj);
    eeAddress += sizeof(Memory);
    EEPROM.put(eeAddress, result);
    eeAddress = 0;

    return;
  }

  calculatorScroll(calculatorInput.length());
  lcd.setCursor(0, 1);
  lcd.print(calculatorInput.substring(offsetCalc, offsetCalc+16));

}

void calculatorScroll(int n){
  if(n > 16){
    if (offsetCalc < n-16){
      offsetCalc++;
    }
    else{
      offsetCalc = 0;
    }
  }
  else {
    offsetCalc =0;
  }
}

int executeLastOperation(const String &input) {
  int total = 0;            // Підсумкове значення
  String currentNumber = ""; // Поточне число
  char lastOperator = '+';  // Останній оператор

  for (int i = 0; i < input.length(); i++) {
    char c = input[i];

    if (isdigit(c)) { // Якщо це цифра
      currentNumber += c; // Додаємо до поточного числа
    } else if (c == '+' || c == '-') { // Якщо це оператор
      if (!(currentNumber == NULL)) {
        int number = currentNumber.toInt(); // Перетворюємо число
        if (lastOperator == '+') {
          total += number; // Додаємо до загального
        } else if (lastOperator == '-') {
          total -= number; // Віднімаємо від загального
        }
        currentNumber = "";   // Скидаємо поточне число
      }
      lastOperator = c; // Оновлюємо останній оператор
    }
  }

  // Обробляємо останнє число
  if (!(currentNumber == NULL)) {
    int number = currentNumber.toInt();
    if (lastOperator == '+') {
      total += number;
    } else if (lastOperator == '-') {
      total -= number;
    }
  }

  return total; // Повертаємо результат
}
// end of code.
