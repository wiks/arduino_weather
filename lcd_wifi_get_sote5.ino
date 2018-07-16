// http://www.arduino.cc/en/Tutorial/LiquidCrystalDisplay
// https://www.elektroda.pl/rtvforum/topic3091069.html
#include <LiquidCrystal440.h>
#include <EEPROM.h>

// eeprom addresses:
int eeprom_addr_token = 0;
int eeprom_addr_ssid = 32;
int eeprom_addr_passwd = 64;

// place for wifi credentials
boolean got_new_wifi_creds = false;
String SSID_ = ""; 
String PASSW = ""; 

// time in sec change screen LCD
const int one_screen_time = 15;

// eeprom egdes:
const int EEPROM_MIN_ADDR = 0;
const int EEPROM_MAX_ADDR = 511; // TODO
// for eeprom excachge:
const int BUFSIZE = 32; // TODO
char buft[BUFSIZE + 1];

// html content of weather starts and stops with:
const String opening_string = "POGODA.WIKS.EU";
const String closing_string = "EU.WIKS.POGODA";

// url of weather api server:
const String api_url = "http://pogoda.wiks.eu/api.php";
// host-url
const String host_url = "pogoda.wiks.eu";

// ---
// for own MAC and IP:
String myip = "";
String mac = "";
// for token into and from EEPROM:
String token = ""; // will be read from eeprom or -if empty - ask with MAC
// global place for read html content
String picked = ""; 
// array for fields position in html content:
int colon_divided_fields[15] = {}; 

// here result to show on LCD:
String ss_now_city2 = "";
String ss_forecast = "";
int failed_attempts = 0;

// buffer for read whole html content
long buf_max_size = 2000;
byte buf[2000];

/*
  LiquidCrystal440 Library

 The circuit:
 * LCD Enable2 pin to digital pin 13
 * LCD RS pin to digital pin 12
 * LCD Enable pin to digital pin 11
 * LCD D4 pin to digital pin 5
 * LCD D5 pin to digital pin 4
 * LCD D6 pin to digital pin 3
 * LCD D7 pin to digital pin 2
 * LCD R/W pin to ground
 * 10K resistor:
 * ends to +5V and ground
 * wiper to LCD VO pin (pin 3)

 http://www.arduino.cc/en/Tutorial/LiquidCrystalDisplay
 https://www.elektroda.pl/rtvforum/topic3091069.html
*/

// initialize the LCD440 library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
const int en2 = 13; // for LCD440
int rw = 255; // not connected
LiquidCrystal lcd(rs, rw, en, en2, d4, d5, d6, d7);

//  EEPROM ---
// https://gist.github.com/smching/05261f11da11e0a5dc834f944afd5961

/* checks range of eeprom address
 */
boolean eeprom_is_addr_ok(int addr) {
  
  return ((addr >= EEPROM_MIN_ADDR) && (addr <= EEPROM_MAX_ADDR));
}

/* Writes bytes into address
 */
boolean eeprom_write_bytes(int startAddr, const byte* array, int numBytes) {
  
  int i;
  if (!eeprom_is_addr_ok(startAddr) || !eeprom_is_addr_ok(startAddr + numBytes)) {
    return false;
  }
  for (i = 0; i < numBytes; i++) {
    EEPROM.write(startAddr + i, array[i]);
  }
  return true;
}

/* Writes a string starting at the specified address.
 * max 32chars
 * returns true if succes
 */
boolean eeprom_write_32string(int addr, const char* string) {

  //int numBytes; // actual number of bytes to be written
  //write the string contents plus the string terminator byte (0x00)
  int numBytes = strlen(string) + 1;
  if (numBytes > BUFSIZE) {
    numBytes = BUFSIZE;
  }
  //Serial.println("eeprom --> " + String(string) );
  return eeprom_write_bytes(addr, (const byte*)string, numBytes);
}

/* Reads a string starting from the specified address.
 * max 32 chars
 * returns true if succes
 */
boolean eeprom_read_32string(int addr, char* buffer) {
  
  byte ch; // byte read from eeprom
  int bytesRead = 0; // number of bytes read so far
  if (!eeprom_is_addr_ok(addr)) { // check start address
    return false;
  }
  int bufSize = BUFSIZE;
  ch = 1; // no mather but other than 0x00
  while ( (ch != 0x00) && (bytesRead < bufSize) && ( (addr + bytesRead) <= EEPROM_MAX_ADDR) ) {
    ch = EEPROM.read(addr + bytesRead);
    buffer[bytesRead] = ch; 
    bytesRead++; 
  }
  // make sure the user buffer has a string terminator, (0x00) as its last byte
  if ((ch != 0x00) && (bytesRead >= 1)) {
    buffer[bytesRead] = 0;
  }
  return true;
}

/** wait for OK ack
 */
boolean rx_ok(String descr="") {
  
  if(Serial1.find("OK")) {
    //Serial.println(descr + "--> RX ok");
    return true;
  }
  Serial.println(descr + "... ok not found ");
  return false;
}

/** send AT command, wait for ack OK
 */
boolean at_and_wait_ok(String at) {

  Serial1.println(at);
  //Serial.println(at);
  String descr = "TX: " + at + " ";
  return rx_ok(descr);
}

/* like in name
 *  
 */
boolean reset_wifi_modul() {

  boolean ret = false; // not connected
  Serial.println("RESETTING ESP8266 WIFI CHIP");
  lcd.home();  
  lcd.print("http://pogoda.wiks.eu  Dzien dobry :-)");
  while(true){
    at_and_wait_ok("AT+RST");
    if(Serial1.find("eady")) { // after reset should return 'Ready'
      Serial.println("Resetted and ready");
      return;
    }
    delay(2000);
  }
}

/** get several bytes to specified char, f.e. "
 *  specified nomber of chars is 'some_bytes'
 */
String rx_some_bytes_end_with(char divider='"', int some_bytes=16, int max_wait=3000){

  String rx_bytes = "";
  int wait = 0;
  int count_bytes = 0;
  while((wait < max_wait) && (count_bytes < some_bytes) ){
    if( Serial1.available() ) {
      char c = Serial1.read();
      if ( c != divider ) { 
        rx_bytes += String(c);
        count_bytes += 1;
      }
      if( count_bytes >= some_bytes || c == divider ){
        return rx_bytes;    
      }
    }
    wait += 5;
    delay(5);
  }
  return "";
}

/** ask about IP oraz MAC from connected router
 *  
 */
boolean ask_about_ip() {
  
  //global mac;
  
  boolean res = false;
  int max_wait = 500;
  int wait = 0;
  delay(1000);
  Serial1.flush();
  Serial1.println("AT+CIFSR");
  Serial.println("AT+CIFSR --> Get local IP address...");
  while(!Serial1.find("+CIFSR:STAIP,\"")) {
    delay(100);
    Serial.print(".");
  }
  Serial.println("");
  myip = rx_some_bytes_end_with('"');
  Serial.println("mam IP " + myip);
  while(!Serial1.find("+CIFSR:STAMAC,\"")) {
    delay(100);
    Serial.print(".");
  }
  Serial.println("");
  mac = rx_some_bytes_end_with('"', 20);
  Serial.println("MAC " + mac);
  rx_ok("(from +CIFSR)");
  Serial1.flush();
  return ( myip && mac );
}

/** waiting for info about IP and ask IP & MAC
 *  return true if both are found
 */
boolean find_ip() {
  
  Serial.print("waiting for IP i MAC [ \"WIFI GOT IP\" ]...");
  boolean have_ip_mac = false;
  while(!Serial1.find("WIFI GOT IP")){
    Serial.print(".");
    delay(1000);    
  }
  Serial.println("");
  Serial.println("pytam o IP i MAC");
  have_ip_mac = ask_about_ip();
  if(have_ip_mac == false) {
    Serial.println("not IP, MAC found..." );
  }else{
    // TODO show MAC only if not recived weather...
    //lcd.setCursor(0, 2);
    //lcd.print("MAC " + mac + " ");
    // IP local is here - totally not 
    //lcd.setCursor(22, 2);
    //lcd.print("ip " + myip);
    if(got_new_wifi_creds) {
      Serial.println("got_new_wifi_creds --> write it to EEPROM");
    }
  }
  return have_ip_mac;
}

/* read bytes into buff
 *  return number bytes read
 */
long rx_and_usbprint_bytes(byte pbuf[], int bytes) { 
  
  int i = 0;
  int index =0;
  while(i<bytes && index< buf_max_size){
    while (Serial1.available()) {
      if(index < buf_max_size){
        pbuf[index++] = Serial1.read();
      }
    }
    delay(5);
    i++;
  }
  return index;
}

/** wait for recive some_string
 */
void wait_for_somestring_recived(char* some_string, int delay_intervals=300) {

  //Serial.print("czekam na " + String(some_string) );
  while(!Serial1.find(some_string)){
    Serial.print(".");
    delay(delay_intervals);
  }
  Serial.println("");
}

/** recive number consist of some digits
 *  return integer
 */
int wait_valueint_recive(int max_digit=5, int max_blend_before_count=4) {

  int rx = 0;
  int index = 0;
  String str = "";
  if ( !Serial1.available() ) {
    delay(5);
  }
  while (Serial1.available() && (index < max_digit)) {
    char c = Serial1.read();
    if ( c >= '0' && c <= '9' ) {  
      max_blend_before_count = 0;
      index++;
      str += String(c);
    }else{
      if( max_blend_before_count-- <= 0 ) { // or not digit before number
        if (str.length()>0) {
          rx = str.toInt();
        }
        break;
      }
    }
    delay(5);
  }
  return rx;
}

/** send anounce to request,
 *  after prompt > send request and recive response
 */
long send_content_wifi(String after_cmd) {

  long rx = 0;
  long readed_bytes = 0;
  String cmd2 = "AT+CIPSEND=" + String(after_cmd.length());
  Serial1.println(cmd2);
  Serial.println(cmd2 + " <-- need send request");
  delay(1500); 
  if(!Serial1.find(">")) {
    Serial1.println("AT+CIPCLOSE");
    Serial.println("prompt ( > ) not found");
    return rx;
  }
  Serial1.println(after_cmd);
  /* +IPD,747:HTTP/1.1 200 OK */
  wait_for_somestring_recived("+IPD,", 300);
  rx = wait_valueint_recive(5);
  if(Serial1.find("OK")){
    Serial.println("to RX: " + String(rx) + " bajtów, HTTP ... 200 OK" );
  }
  readed_bytes = rx_and_usbprint_bytes(buf, rx-175);  // 17 is length of "HTTP/1.1 200 OK + \r\n"
  Serial1.flush();
  Serial.println("rx bytes: " + String(readed_bytes) );    
  return readed_bytes;
}

/** pick up from buffor content between opening_string and closing_string strings
 */
String pick_up_mycontent(int readed_bytes) {
  
  String tmp_string = "";
  boolean gotit = false;
  String clue_content = "";
  if(readed_bytes > 0) {
    for(int i=0;i<readed_bytes;i++) {
      if (buf[i] > 31 ) {
        if(gotit) {
          tmp_string += char(buf[i]);
          if(tmp_string.endsWith(closing_string + ";")) {
            tmp_string = tmp_string.substring(0, tmp_string.indexOf(";"+closing_string));
            return tmp_string;
          }
        }else{
          if (!gotit) {
            tmp_string += char(buf[i]);
            if(tmp_string.endsWith(opening_string + ";")) {
              gotit = true;
              tmp_string = "";
            }
          }
        }
      }
    }
  }  
  return "";
}

/** connect to WiFi
 */
boolean connectWiFi() {

  Serial1.println("AT+CWMODE=1");
  delay(5000);
  String cmd = "AT+CWJAP=\""+SSID_+"\",\""+PASSW+"\"";
  return at_and_wait_ok(cmd);
}

/** connect to remote server
 */
void connect_weather_server() {

  Serial.print("connecting server...");
  String cmd = "AT+CIPSTART=\"TCP\",\"" + host_url + "\",80";
  //Serial.print( cmd );
  while(!at_and_wait_ok(cmd)) {
    delay(1000);
    Serial.print(".");
  };
  Serial.println(" :-)");
}

/** connect to wifi, 
 *  find MAC, IP set transmision mode
 */
void looking_wifi_ip_mac() {

  lcd.setCursor(0, 1);
  lcd.print(" looking for WiFi: " + SSID_ );
  boolean is_connected = false;
  int i = 5;
  while(!is_connected) {
    Serial.println();
    i = 5;
    Serial.print("looking for WiFi");
    while(!Serial1.find("WIFI CONNECTED") && i > 0){
      Serial.print(".");
      delay(1000);
      i--;
    }
    if(i > 0) {
      Serial.println(" --> ?is auto-connected.");
      is_connected = true;
    }else{
      is_connected = connectWiFi(); 
      if(is_connected) {
        Serial.println(" --> ?is connected, after USER&PASS.");
      }
    }
  }
  // ------- only connected -------  
  Serial.println("looking for IP...");
  find_ip();
  Serial.flush();
  delay(500); 
  //  set the single connection mode
  Serial.print("set the single connection mode...");
  while (!at_and_wait_ok("AT+CIPMUX=0")) {
    delay(500);
    Serial.print(".");
  }; 
  Serial.println("");
}

/** get token with MAC (needed server allow to)
 */
String token_from_mac() {

    token = "";
    if(mac.length() > 10) {
      lcd.setCursor(0, 2);
      lcd.print("MAC " + mac + " ");
      lcd.setCursor(0, 3);
      lcd.print("ask about token (MAC & IP) is allowed?");
      String cmd_get = "GET " + api_url + "?m=" + mac + " HTTP/1.1\r\n\r\nHost: " + host_url + ":80\r\n";
      long readed_bytes = send_content_wifi(cmd_get);
      token = pick_up_mycontent(readed_bytes);
      if( token.length() > 4 ) {
        Serial.println( "rx-token: " + token );
        token.toCharArray(buft, BUFSIZE); 
        eeprom_write_32string(eeprom_addr_token, buft);
        Serial.print("Token read from api.weather, writen to EEPROM: " + token );
        delay(500);
      }else{
        Serial.print("No token from api.weather...");
      }
    }
    return token;
}

/** get weather from server ask with token
 *  find number of fields divided with semicolon
 */
int get_request_weather() {
  
  String cmd_get = "GET " + api_url + "?t=" + token + " HTTP/1.1\r\n\r\nHost: " + host_url + ":80\r\n";
  long readed_bytes = send_content_wifi(cmd_get);
  picked = pick_up_mycontent(readed_bytes);
  int lp_semicolon = 0;
  int max_semicolon = 15; // TODO
  for(int i=0;i<picked.length();i++) {
    if (picked[i] == ';') {
      colon_divided_fields[lp_semicolon] = i;
      lp_semicolon++;
    }
  }
  return lp_semicolon;
}

/** print content into LCD screen
 */
void show_it(String piece) {

  while( piece.length() ) {
    String piece_bread_crumb = piece.substring(0, 160);
    lcd.clear();
    lcd.print(piece_bread_crumb);
    piece = piece.substring(160);
    delay(piece_bread_crumb.length() * 30 + 3000);
  }
}

// ============================================================================

void setup() {

  // set up the LCD's number of columns and rows:
  lcd.begin(40, 4);
  long baudrate = 115200;
  // USB
  Serial.begin(baudrate);
  while(!Serial); 
  // Open serial communications w ESP8266
  Serial1.begin(baudrate);
  while(!Serial1);
  Serial.println("Start, baudrates: " + String(baudrate) );

  Serial.println("wifi credientals via USB?");
  delay(1000);
  if(Serial.find("wifi:")){
    String tmp_string = "";
    int index = 0;
    char byt = 0;
    while(!tmp_string.endsWith(":wifi") && index < 50){
      while (Serial.available()) {
        if(index < 50){
          byt = Serial.read();
          buf[index++] = byt;
          if (byt > 31) {
            tmp_string += String( byt );
          }
        }
      }
      delay(5); 
    }
    tmp_string = tmp_string.substring(0, tmp_string.indexOf(":wifi"));
    SSID_ = tmp_string.substring(0, tmp_string.indexOf(":"));
    PASSW = tmp_string.substring(tmp_string.indexOf(":") + 1);
    got_new_wifi_creds = true;
    SSID_.toCharArray(buft, BUFSIZE); 
    eeprom_write_32string(eeprom_addr_ssid, buft);
    PASSW.toCharArray(buft, BUFSIZE); 
    eeprom_write_32string(eeprom_addr_passwd, buft);
  }else{
    Serial.println("read wifi_creds from EEPROM");
    // get SSID from EEPROM:
    eeprom_read_32string(eeprom_addr_ssid, buft);
    SSID_ = String(buft);
    // get SSID from EEPROM:
    eeprom_read_32string(eeprom_addr_passwd, buft);
    PASSW = String(buft);
  }
  //reset and test if the module is ready
  reset_wifi_modul();
  looking_wifi_ip_mac();
  // get token from EEPROM:
  eeprom_read_32string(eeprom_addr_token, buft);
  token = String(buft); // "for_test_cntcXBWYJDQ0H6VwanN3Zrh"; // 
  Serial.println("Token read from EEPROM: " + token + " (length: " + token.length() + " )" );
  delay(1000);
  connect_weather_server();
  // is token from eeprom?
  if( token.length() < 1) {
    Serial.println("empty EEPROM token");
    // ask server (server should be set to allow to);
    token = token_from_mac();
  }else{
    // check is right token (try *3)
    boolean token_works = false;
    int coundtry = 10;
    while(!token_works && coundtry > 0) {
      int lp_semicolon = get_request_weather();
      if (lp_semicolon > 8) {
        token_works = true;
      }else{
        coundtry--;
        delay(10000);
      }
    }
    if( !token_works ) {
      Serial.println("not weather recided, try * 10");
      token = token_from_mac();
    }else{
      Serial.println("request with token is right!");
    }
  }
}

//------------------------------------------------------------------------------------------

void loop() {

  Serial.println("================== LOOP ===================");
  delay(500);
  String piece = "";
  String eof = "\r\n";
  // Turn on the display:
  lcd.display();
  connect_weather_server();
  int lp_semicolon = get_request_weather();
  if (lp_semicolon > 1) {
    int prev_pos = 0;
    for(int i=0;i<15;i++) {
      if(colon_divided_fields[i] > 0) {
        piece = picked.substring(prev_pos, colon_divided_fields[i]);
        if (i == 0) {
          ss_now_city2 += piece + " ";
        }
        if (i == 1) {
          ss_now_city2 += piece + " ";
        }
        if (i == 8) {
          ss_now_city2 += piece;
          failed_attempts = 0;
        }
        if (i == 9) {
          ss_forecast = "wkrotce: " + piece;
          failed_attempts = 0;
        }
        // all recived fields:
        if ( piece.length() > 0 ) {
          Serial.println( String(i) + " --> " + piece );
        }
        prev_pos = colon_divided_fields[i] + 1;
      }
    }
  }else{
    failed_attempts++;
  }
  if(failed_attempts < 5 && (ss_now_city2.length() > 1 || ss_forecast.length() > 1)){
    int count = 3;
    while(count-- > 0) {
      show_it(ss_now_city2);
      show_it(ss_forecast);
    }
  } else{
    show_it("error connection...");
  }
}


