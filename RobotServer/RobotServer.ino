#include <Servo.h>

/*
 * ssid , and pass are the network name and passowrd that the arduino connects to 
 */
char ssid[] = "\"loopback\"";
char pass[] = "\"robocop!\"";
char static_ip[] = "\"192.168.2.80\",\"192.168.2.1\"";

//        X       X
//      X           X
//    X  A        B   X
//            X
//           XXX
//            X
//    X  C         D  X
//      X  ARDUINO  X
//        X BOARD X


/*
 * initialize servo motors A, B, C, D
 */
Servo motorA, motorB, motorC, motorD;

/*
 * refresh rate of the server 
 */
int refresh = 15;

/*
 * setup should connect to loopback AP and setup server
 */
void setup() {

  Serial.begin(115200);

  Serial.println("AT");
  Serial.println("AT+RST");
  
  connectToAP();
  initServer();

  /*
   * connect each motor to its respective pin as seen on the arduino
   */
  motorA.attach(8);
  motorB.attach(9);
  motorC.attach(10);
  motorD.attach(11);
}

/*
 * connecttoAP() uses AT+ commands to connect to an AP 
 * in station mode outlined in the documentation below:
 * https://www.espressif.com/sites/default/files/documentation/4a-esp8266_at_instruction_set_en.pdf
 */
void connectToAP() {

  /*
   * Delay for half a second to allow for start up of the physical device
   * Sets WIFI mode to Station only, saves as default.
   * waits for an "OK" response before continuing
   */
  delay(500);
  Serial.println("AT+CWMODE_DEF=1"); 
  getOK();
  
 // Serial.println("AT+CWAUTOCONN=1");  //auto-connect to AP on startup
 // getOK();

  /*
   * set a static IP for Arduino, to facilitate reliable communication
   * waits for an "OK" response before continuing
   */
  Serial.print("AT+CIPSTA_DEF="); 
  Serial.println(static_ip); 
  getOK();

  /*
   * connects to network with SSID:loopback, PASS:robocop!
   * waits for an "OK" response before continuing
   */
  Serial.print("AT+CWJAP_DEF=");
  Serial.print(ssid);
  Serial.print(",");
  Serial.println(pass);
  getOK();
}
/*
* initializes server
* each "AT+" command waits for an "OK" response before coninuing
* then waits for 20 ms to ensure all server values are set correctly
*/
void initServer() {

  /*
   * sets server to a normal transmission mode
   */
  Serial.println("AT+CIPMODE=0");
  getOK();
  delay(20);

  /*
   * allows for multiple connections (although rn its only 1)
   */
  Serial.println("AT+CIPMUX=1");
  getOK();
  delay(20);
  
//  Serial.println("AT+CIPSERVERMAXCONN=3");  //allow 2 simultaneous connections to the server
 // getOK();
 // delay(20);

  /*
   * create server at port 8080
   */
  Serial.println("AT+CIPSERVER=1,8080");
  getOK();
  delay(20);
}

/*
 * getOK() reads the serial transmission line for an "OK" 
 * if "OK" is not found this funciton will hang
 */
void getOK() {
  char buf[2] = "";
  while(1) {
    buf[0] = Serial.read();
    if(buf[0] == 'O') {

      buf[1] = Serial.read();
      if(buf[1] == 'K') {
        break;
      }
    }
    delay(20);
  }
}

/*
 * After the server is connected to an AP and initialized, all incoming connections will be sent to loop()
 * loop() busy waits for a connection, so that it can send instructions to the servo motors
 */
void loop() {
  
  /*
   * if there is no connection coming in from the Serial transmission
   * Write 1500 ms to each motor, essentially stopping them until there 
   * is a new connection
   */
  if (!Serial.available()) {

    motorA.writeMicroseconds(1500);
    motorB.writeMicroseconds(1500);
    motorC.writeMicroseconds(1500);
    motorD.writeMicroseconds(1500);
  }
  /*
   * findUntil returns TRUE if arg1 is found before timeout
   * FALSE if arg2 is found before timeout
   * FALSE if neither are found before timeout
   */
  if (Serial.findUntil("CONNECT", "ERROR")) {
    int motorSpeeds[4];
    char speedA[5], speedB[5], speedC[5], speedD[5];
    /*
     * All motor speeds will be correctly calculated on the client side
     * 4 motor commands will be sent, each containing 4 chracters, for a total of 16 bytes
     */
    if (Serial.findUntil("16:", "ERROR")) {

      /*
       * set the motor speads based on the message sent by the client
       */
      Serial.readBytes(speedA, 4);
      speedA[4] = 0;
      
      Serial.readBytes(speedB, 4);
      speedB[4] = 0;
      
      Serial.readBytes(speedC, 4);
      speedC[4] = 0;
      
      Serial.readBytes(speedD, 4);
      speedD[4] = 0;

      /*
       * convert motor speeds from char[] to ints
       */
      motorSpeeds[0] = atoi(speedA);
      motorSpeeds[1] = atoi(speedB);
      motorSpeeds[2] = atoi(speedC);
      motorSpeeds[3] = atoi(speedD);
      /*
      Serial.print("speedA = ");
      Serial.println(motorSpeeds[0]);
      Serial.print("speedB = ");
      Serial.println(motorSpeeds[1]);
      Serial.print("speedC = ");
      Serial.println(motorSpeeds[2]);
      Serial.print("speedD = ");
      Serial.println(motorSpeeds[3]);
      Serial.println();
      */

      /*
       * send motor speeds to motors
       */
      motorA.writeMicroseconds(motorSpeeds[0]);
      motorB.writeMicroseconds(motorSpeeds[1]);
      motorC.writeMicroseconds(motorSpeeds[2]);
      motorD.writeMicroseconds(motorSpeeds[3]);

      /*
       * wait for "refresh" seconds before listening to new incoming commands
       * in order to ensure motors have enough time to execute given a command
       */
      delay(refresh);
    }
  }
}
