// ---
// smart house v3 SoftwareSerial
// ---

#include <SoftwareSerial.h>
#define numSensors sizeof(mySensors)/sizeof(tSensor) // подсчет количества элементов в массиве numSensors
#define numDevices sizeof(myDevices)/sizeof(tDevice) // подсчет количества элементов в массиве numDevices
#define numPhones  sizeof(smsNumbers)/sizeof(char*) // подсчет количества элементов в массиве numPhones
// постоянные
const int buttonPin = 12; // пин для кнопки
const int perimeterPin = 8; // пин для периметра
const int ledPin = 10 ; // пин индикатора
const int sysledPin = 13; // пин системного индикатора
const int tonePin = 5; // пин для динамика
const int gsmPowerPin = 4; // пин включения GSM модема
const long restTime = 15; // время в сек. в котором система будет в состоянии покоя после снятия с охраны
const long restSiren = 300; // время в сек. в котором система будет не будет повторно подавать аварийный сигнал на динамик, в случае аварии по сенсорам

// типы
typedef struct
{
  int pin;  // пин на котором расположен сенсор
  boolean state; // состояние сенсора
  boolean alarm; // состояние аварии
  char* name; // название сенсора
  char* errorMsg; // сообщение в случае включения сенсора
  char* errorOk; // сообщение в случае отключения сенсора
} 
tSensor;

// типы
typedef struct
{
  int pin;  // пин отвечающий за устройство
  boolean enabled; // состояние устройства (true - вкл., false - выкл.)
  boolean touchButton; // тип кнопки (true - сенсорная, false - фиксированя)
  char* name; // название сенсора
} 
tDevice;

// объявление сенсоров и их кодов ошибок
tSensor mySensors[] = {
  { 6, false, false, "Water", "WARNING! Found water leakage.", "Water leakage disappeared." },
  { 3, false, false,   "Gas",   "WARNING! Found gas leakage.",   "Gas leakage disappeared." }
};

// объявление устройств и их состояний
tDevice myDevices[] = {
  { 7, false,  true, "Unit 1"},
  { 2, false, false, "Unit 2"}
};

// переменные
boolean buttonState = false; // состояние кнопки
boolean perimeterState = false; // состояние периметра
boolean sensorState = false; // состояние всех сенсоров
int alarmState = 0; // состояние сигнализации ( 0 - выкл, 1 - нажата секретка, 2 - нажата секретка и открыта дверь, 3 - на охране, 4 - несанкционированное проникновение)
unsigned long currentTime;
unsigned long loopTime;
SoftwareSerial gsmSerial(9, 11); // RX, TX
//char* smsNumbers[] = {}; // запуск без телефонов, смс не будут отсылаться
char* smsNumbers[] = {"+79110000000", "+79630000000"}; // номера на которые необходимо отсылать аварийные сообщения
String secretStr = "7770:"; // кодовое слово для подачи команд по смс
String currentStr = "";

/*
 * функция программного перезапуска Arduino
 */
void(* resetFunc) (void) = 0;

/*
 * Функция отправки SMS-сообщения

 */
void sendTextMessage(String text) {
  for (int i = 0; i < numPhones; i++){
    // Устанавливает текстовый режим для SMS-сообщений
    gsmSerial.println("AT+CMGF=1");
    delay(100); // даём время на усваивание команды
    // Устанавливаем адресата: телефонный номер в международном формате
    gsmSerial.println("AT+CMGS=\""+String(smsNumbers[i])+"\""); 
    delay(300);
    // Пишем текст сообщения
    gsmSerial.print(text);
    delay(300);
    // Отправляем Ctrl+Z, обозначая, что сообщение готово
    gsmSerial.print((char)26);
    delay(5000);
  }
} 

/*
 * Функция для проигрывания аварийной мелодии thisNote раз
 */
void runSiren(int count) {
  for (int thisNote = 0; thisNote < count; thisNote++) {
    digitalWrite(ledPin, HIGH);
    // отключить проигрывать функцию проигрывания ноты на пине tonePin
    noTone(tonePin);			
    // проиграть ноту на tonePin длительностью 200 ms
    tone(tonePin, 440, 300);
    delay(300);
    digitalWrite(ledPin, LOW);
    noTone(tonePin);
    tone(tonePin, 494, 600);
    delay(600);
    //noTone(tonePin);  
    //tone(tonePin, 523, 200);
    //delay(200);  
  }
}

/*
 * Функция для проигрывания звуков на постановку и снятие сигнализации 
 */
void setAlarm(int state) {
  switch (state) {
  case 0:    
    // проиграть звук снятия с сигнализации
    noTone(tonePin);
    tone(tonePin, 1000, 100);
    delay(200);
    noTone(tonePin);
    tone(tonePin, 1000, 100);
    delay(200);
    // визуальная индикация cнятия с охраны
    digitalWrite(ledPin, HIGH); 
    delay (100);
    digitalWrite(ledPin, LOW);
    delay (70);
    digitalWrite(ledPin, HIGH);
    delay (100);
    digitalWrite(ledPin, LOW);
    break;
  case 1:    
    // проиграть звук постановки на кнопку
    noTone(tonePin);
    tone(tonePin, 1000, 200);
    delay(300);
    // визуальная индикация готовности постановки на охрану
    digitalWrite(ledPin, HIGH); 
    delay (70);
    digitalWrite(ledPin, LOW);
    break;
  case 2:    
    // проиграть звук постановки на сигнализацию
    noTone(tonePin);
    tone(tonePin, 1500, 400);
    delay(300);
    // визуальная индикация постановки на охрану
    digitalWrite(ledPin, HIGH); 
    delay (100);
    digitalWrite(ledPin, LOW);
    delay (70);
    digitalWrite(ledPin, HIGH);
    delay (100);
    digitalWrite(ledPin, LOW);
    break;
  } 
}

/*
 * Функция проверки свой-чужой
 */
boolean checkIFF(int second) {
  int i=0;
  buttonState = false; // на всякий случай обнуляем состояние секретной кнопки
  while(i <= second*10 && buttonState == false){ // цикл ожидающий нажатия i секунд на нажатие секретной кнопки
    buttonState = digitalRead(buttonPin); // читаем текущее состояние секретной кнопки
    delay(1000/10); // задержка выполнения цикла на 1/10 секунду
    i++;
  }
  return buttonState;
}

/*
 * Функция чтения всего буфера с серийного порта gsm модема
 */
String readSerial() {
  String inData = "";
  if (gsmSerial.available() > 0) {
    int h = gsmSerial.available();
    for (int i = 0; i < h; i++) {
      inData += (char)gsmSerial.read();
    }
    return inData;
  }
  else {
    return "No connection";
  }
}
/*
 * Функция чтения из буфера серийного порта gsm модема одной строки
 */
String readSerialStr() {
  String content = "";
  boolean retStr=false;
  char currentChar;
  delay(100);
  while(gsmSerial.available() && retStr==false) {
    currentChar = gsmSerial.read();  
    //if (currentChar=='\n' || currentChar=='\r') {
    if (currentChar=='\n') {
      retStr=true;
    } 
    else {
      content.concat(currentChar);
    }
  }
  return content;
}

/*
 * Функция опрос состояния системы и отправить отчет по sms
 */
void getStatus() {
  String msgReport="";
  if(alarmState==4){ // если периметр разомкнут
    msgReport += "Perimeter=Fail; ";
  } 
  else { // иначе
    msgReport += "Perimeter=Ok; ";
  }
  msgReport += "\n";
  
  for (int i = 0; i < numSensors; i++){  // в цикле опрашиваем все сенсоры
    msgReport += mySensors[i].name;
    if (mySensors[i].alarm==false){ // если состояние сенсора "покой", то
      msgReport += "=Ok; ";
    }
    else { // иначе
      msgReport += "=Fail; ";
    }
  }
  msgReport += "\n";

  for (int i = 0; i < numDevices; i++){  // в цикле опрашиваем все устройства
    msgReport += myDevices[i].name;
    if (myDevices[i].enabled==true){ // если состояние устройства вкл.
      msgReport += "=On; ";
    }
    else { // иначе
      msgReport += "=Off; ";
    }
  }

  sendTextMessage(msgReport); // отправляем смс отчет о статусе системы
  Serial.println(msgReport);
}

/*
 * Функция парсинга sms сообщение и выполнение заданных команд
 */
void parseSMS(String msg) {
  String tmp = "";
  int startPos = secretStr.length();
  int msgLength = msg.length(); 

  if ((msgLength-startPos)==numDevices) {  // если количество устройств в смс соответствует количеству подключенных устройств
    for (int i = 0; i < msgLength-startPos; i++){  // парсим каждый символ смс начиная с позиции где завершается секретное слово и до конца
      tmp = msg.substring(i+startPos, i+startPos+1); // берем один символ
      Serial.println("Device #" + String(i+1) + " = " + tmp);  
      if (tmp=="1") { // если 1, то включаем устройство
        myDevices[i].enabled = true;
        digitalWrite(myDevices[i].pin, HIGH);
        delay(1000);
        if (myDevices[i].touchButton==true) { // если кнопка сенсорная, то после паузы ставим логический 0
          digitalWrite(myDevices[i].pin, LOW);
          delay(1000);
        }
      }
      else if (tmp=="0") { // иначе если 0, то выключаем устройство
        myDevices[i].enabled = false;
        if (myDevices[i].touchButton==true) { // если кнопка сенсорная, то сначала ставим логическую 1
          digitalWrite(myDevices[i].pin, HIGH);
          delay(1000);
        }
        digitalWrite(myDevices[i].pin, LOW);
        delay(1000);
      }
    }
  }
}


/*
 * Функция выполняющаяся при первичной инициализации Arduino
 */
void setup(){
  // Начинаем последовательный обмен данными и ждем открытие порта
  Serial.begin(9600);
  //while (!Serial) {
  //  ; // ждем когда последовательный порт подлючится. Необходимо только для Leonardo
  //}
  
  // инициализация пинов как входых или выходных
  pinMode(ledPin, OUTPUT);  // выход для пина светодиода
  pinMode(sysledPin, OUTPUT); // выход для пина системного светодиода
  pinMode(gsmPowerPin, OUTPUT);  // выход для пина GSM модема
  pinMode(perimeterPin, INPUT);  //вход на пин периметра
  pinMode(buttonPin, INPUT); // вход на пин кнопки

  //Включаю GSM Модуль
  digitalWrite(gsmPowerPin, HIGH);
  delay(1000);
  digitalWrite(gsmPowerPin, LOW);
  delay(5000);

  gsmSerial.begin(9600);
  gsmSerial.flush();
  // опрашиваем gsm-модем
  gsmSerial.println("AT");
  delay(100);   
  gsmSerial.println("AT+CSCA?"); // запрашиваем номер смс-центра
  delay(300);
  gsmSerial.println("AT+CMGF=1"); // выставляем текстовым режим сообщений
  delay(300);
  gsmSerial.println("AT+IFC=1,1");  // Set Local Data Flow Control
  delay(300);
  gsmSerial.println("AT+CPBS=\"SM\""); // SIM Phonebook Memory Storage
  delay(300);
  gsmSerial.println("AT+CNMI=1,2,2,1,1"); // Включаю перехват SMS
  delay(500);

  currentTime = millis(); // считать текущие значение секунд с момента запуска Arduino
  loopTime = currentTime;
  alarmState = 0;
  // отключение аварий на всех сенсорах и их инициализация
  for (int i = 0; i < numSensors; i++){
    mySensors[i].alarm==false;
    pinMode(mySensors[i].pin, INPUT);
  }
  // инициализация устройств
  for (int i = 0; i < numDevices; i++){
    pinMode(myDevices[i].pin, OUTPUT);
    if (myDevices[i].enabled==true) {
      digitalWrite(myDevices[i].pin, HIGH);
      delay(1000);
      if (myDevices[i].touchButton==true) {
        digitalWrite(myDevices[i].pin, LOW);
        delay(1000);
      }
    }
  }

  Serial.println("System Ready ...");  // Инициализация после Сброса
  Serial.println(String(numSensors) + " sensor(s) detected.");  // Подсчет и отображение числа сенсоров
  Serial.println(String(numDevices) + " device(s) detected.");  // Подсчет и отображение числа устройств
  Serial.println(String(numPhones) + " phone number(s) detected.");  // Подсчет и отображение числа телефонных номеров для рассылки аварийных смс
  digitalWrite(sysledPin, LOW);
  delay(1000);  
  //parseSMS("5550:01");
  //getStatus();
}

/*
 * Основная функция запускающая главный цикл Arduino
 */
void loop(){
  digitalWrite(sysledPin, HIGH);
  buttonState = digitalRead(buttonPin); // считываем состояние секретной кнопки
  if(buttonState==true && alarmState==0){ // если нажата кнопка и система не на охране, то
    //if(buttonState==true && perimeterState==true && alarmState==0){ // если нажата кнопка, закрыта дверь, и система не на охране, то
    alarmState=1; // перевести систему в состояние готовности постановки на охрану
    setAlarm(1); // сообщить гудком, что нажата кнопка
    delay(1000);
    Serial.println("The system is ready for arming.");
  }
  perimeterState = digitalRead(perimeterPin); // считываем состояние периметра
  if(alarmState==1 || alarmState==2){ // если система в готовности постановки на охрану, тогда
    setAlarm(1); // сообщить гудком, что нажата кнопка
    delay(1000);
    if(perimeterState==false && alarmState==1) { // если дверь открыли после нажатия на секретную кнопку, то
      alarmState=2; // переход в крайнее положение постановки на охрану, ждем закрытия двери
      //while(perimeterState==false){ // цикл, пока не закроем дверь
      //  perimeterState = digitalRead(perimeterPin);
    }
    if(perimeterState==true && alarmState==2) { // если дверь закрыли после нажатия на секретную кнопку, то
      alarmState=3; // переход в крайнее положение постановки на охрану, ждем закрытия двери
      setAlarm(2); // сообщить гудком, что система поставлена на охрану
      Serial.println("The system is armed.");
    }
  }
  if(alarmState==3){ // если система находится на охране
    if(perimeterState==false) { // и произошло размыкание контура или открытие двери
      // выполняем проверку свой-чужой через функцию checkIFF(second), где second количество секунд для нажатия секретной кнопки
      if(checkIFF(10)==false) { // если в течении 10 секунд секретная кнопка не нажата, то
        alarmState = 4; // перевод сигнализации в состояние - несанкционированное проникновение
        sendTextMessage("WARNING! The perimeter broken, unauthorized access."); // немедленно отправляем смс сообщение о несанкционированном проникновении
        Serial.println("WARNING! The perimeter broken, unauthorized access.");
      } 
      else {
        alarmState = 0; // перевод сигнализации в состояние - снята с охраны
        setAlarm(0); // сообщить гудком, что система снята с охраны
        Serial.println("The system is disarmed.");
        delay(restTime*1000); // Система переходит в состояние покоя на restTime сек.
      } 
    }
  }
  if(alarmState==4){ // если произошло размыкание периметра и система находится в режиме тревоги
    runSiren(1); // Включаем серену 1 раз
    buttonState = digitalRead(buttonPin); // считываем еще раз состояние секретной кнопки
    currentTime = millis(); // считать текущие значение секунд с момента запуска Arduino
    if(buttonState==true){ // если секретную кнопку все же нажали после сирены, то
      if(currentTime >= (loopTime + 4000)){ //если кнопка нажата уже 4 секунды, то
        alarmState=0; // снимаем систему с охраны
        setAlarm(0); // сообщить гудком, что система снята с охраны
        Serial.println("The system is disarmed after unauthorized access.");
        delay(restTime*1000); // Система переходит в состояние покоя на restTime сек.
      } 
    }
    else {
      loopTime = currentTime; // есди кнопка не нажата, фиксируем текущее время
    }
  }

  //Опрос всех сенсоров
  for (int i = 0; i < numSensors; i++){
    mySensors[i].state = digitalRead(mySensors[i].pin);
    //Serial.println(mySensors[i].name + String(" = ") + mySensors[i].state);
    if (mySensors[i].state==true && mySensors[i].alarm==false){ // если сработал сенсор и состояние сенсоров было "покой", то
      mySensors[i].alarm=true; // перевести систему в состояние аварии по сенсору
      sendTextMessage(mySensors[i].errorMsg); // немедленно отправляем смс сообщение о сработке сенсора
      Serial.println(mySensors[i].errorMsg);
      runSiren(5); // Включить сирену 5 раза
    }
    if(mySensors[i].state==false && mySensors[i].alarm==true){ // если авария пропала и состояние сенсора было "авария", то
      mySensors[i].alarm=false; // перевести систему в состояние покоя по сенсору
      Serial.println(mySensors[i].errorOk);
    }

    if (mySensors[i].alarm==true) {
      // Подавать аварийный сигнал с сенсоров можно не чаще чем раз в restSiren секунд
      currentTime = millis(); // считать текущие значение секунд с момента запуска Arduino
      if(currentTime >= (loopTime + (restSiren*1000))){
        runSiren(5); // Включить сирену 5 раза
        loopTime = currentTime;
      } 
    }
    
    //if (Serial.available()){  // если в буфере серийного порта есть данные (для теста)
    //  gsmSerial.print((char)Serial.read());  // отправить на модем (для теста)
    //}
    
    if (gsmSerial.available()){  // если в буфере серийного порта есть данные
      currentStr=readSerialStr(); // считываем одну строку из буфера серийного порта посимвольно
      currentStr.trim();
      //Serial.println(currentStr);
      if (currentStr.startsWith("+CMT")) { // и если текущая строка начинается с "+CMT", то следующая строка является сообщением
        currentStr=readSerialStr(); // считываем еще одну строку из буфера серийного порта посимвольно
        currentStr.trim();
        //Serial.println(currentStr);
        if (currentStr.compareTo("STATUS")==0) { // если sms сообщене это "STATUS"
          Serial.println("Command received. Execute 'STATUS'");
          getStatus(); // выполнить опрос системы и отправить отчет
        }
        else if (currentStr.startsWith(secretStr)) {
          Serial.println("Command received. Execute parseSMS(\""+currentStr+"\");");
          parseSMS(currentStr);
        }
      }
    }
    
  } 
}








