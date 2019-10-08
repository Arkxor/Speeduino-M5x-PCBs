
#include <SPI.h>
#include <mcp_can.h>

const int SPI_CS_PIN = 10;   // pin10 is wired as CS pin in the PCB
MCP_CAN CAN(SPI_CS_PIN);     // Set CS pin in CAN library

static uint32_t oldtime=millis();   // for the timeout
byte SpeedyResponse[76]; //The data buffer for the serial3 data
byte ByteNumber;  // pointer to which byte number we are reading currently
byte rpmLSB;   //RPM Least significant byte for RPM message
byte rpmMSB;  //RPM most significant byte for RPM message
byte pwLSB;   //RPM Least significant byte for RPM message
byte pwMSB;  //RPM most significant byte for RPM message
byte CEL;   //timer for how long CEL light be kept on
int CLT;   // to store coolant temp
unsigned int RPM;   //RPM from speeduino

byte TPS,tempLight;   //TPS value and overheat light on/off

uint8_t data[8];    // data that will be sent to CAN bus

void setup(){
 Serial3.begin(115200);  // baudrate for Speeduino is 115200
 Serial.begin(9600); // for debugging

 
      while (CAN_OK != CAN.begin(CAN_500KBPS))              // init can bus : baudrate = 500k
    {
        Serial.println("CAN BUS Shield init fail");
        Serial.println(" Init CAN BUS Shield again");
        delay(100); //TBD figure out if this can be shorter or even needed
    }
    Serial.println("CAN BUS Shield init ok!");

  // send this message to get rid of EML light
  
  data[0]= 0x02;  //error State
  data[1]= 0x00;  //LSB Fuel consumption
  data[2]= 0x00;  //MSB Fuel Consumption
  data[3]= 0x00;  //Overheat light
  data[4]= 0x7E;
  data[5]= 0x10;
  data[6]= 0x00;
  data[7]= 0x18;
	CAN.sendMsgBuf(0x545,0, 8, data);

// zero the data to be sent by CAN bus, so it's not just random garbage

 CLT = 0;
 rpmLSB = 0;
 rpmMSB = 0;
 pwLSB = 0;
 pwMSB = 0;
 CEL = 0;
 
  // initialize timer1 to send CAN data in 30Hz rate
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = 0;

  TCNT1 = 63456;            // preload timer 65536-16MHz/256/30Hz
  TCCR1B |= (1 << CS12);
  TIMSK1 |= (1 << TOIE1);
  interrupts();

 requestData(); // all set. Start requesting data from speeduino
}

//Send A to request data from Speeduio
void requestData() {
  Serial3.write("A");
}

//display the needed values in serial monitor for debugging
void displayData(){
      Serial.print ("RPM-"); Serial.print (RPM); Serial.print("\t");
      Serial.print ("pwLSB-"); Serial.print (pwLSB); Serial.print("\t");
      Serial.print ("CLT-"); Serial.print (CLT); Serial.print("\t");
      Serial.print ("TPS-"); Serial.print (TPS); Serial.println("\t");
}

void processData(){   // necessary conversion for the data before sending to CAN BUS

    RPM            = ((SpeedyResponse [16] << 8) | (SpeedyResponse [15])); // RPM low & high (Int) TBD: probaply no need to split high and low bytes etc. this could be all simpler
    pwLSB          = SpeedyResponse[22];
    pwMSB          = SpeedyResponse[23];
    TPS            = SpeedyResponse[25];  // Byte
	
    RPM = RPM * 6.4; // RPM conversion factor for e46/e39 cluster
    rpmMSB = RPM >> 8;	//split to high and low byte
    rpmLSB = RPM;
    CLT = (SpeedyResponse[8])*1.3+14;	// CLT conversion factor for e46/e39 cluster
    // actual calculation is (SpeedyResponse[8] -40)*4/3+64, but upper one has almost same result faster
  		
    if(CLT>229){ // overheat light on if value is 229 or higher
      tempLight = 8;  // hex 08 = Overheat light on
    }
    else {
      tempLight = 0; // hex 00 = overheat light off
    }
}

ISR(TIMER1_OVF_vect)        // Send can messages every 30Hz from timer interrupt
{
  TCNT1 = 63456;            // preload timer
  //Send RPM
  
  data[1]= 0x07;
  data[2]= rpmLSB; //RPM LSB
  data[3]= rpmMSB; //RPM MSB
  data[4]= 0x65;
  data[5]= 0x12;
  data[6]= 0x0;
  data[7]= 0x62;
  CAN.sendMsgBuf(0x316,0, 8, data);

  //Send CLT and TPS
  
  data[0]= 0x07;
  data[1]= CLT; //Coolant temp
  data[2]= 0xB2;
  data[3]= 0x19;
  data[4]= 0x0;
  data[5]= TPS; //TPS value. Don't know scaling or does this even have any effect to anyhting
  data[6]= 0x0;
  data[7]= 0x0;
  CAN.sendMsgBuf(0x329,0, 8, data);

  // Send fuel consumption and error lights
  if (CEL < 60){
    data[0]= 0x02;  //error State
    CEL++;
  }
  else{
    data[0]= 0x00;  //error State
  }
  data[1]= pwLSB;  //LSB Fuel consumption (needs conversion, but this is what it is for now)
  data[2]= pwLSB;  //MSB Fuel Consumption
  data[3]= tempLight ;  //Overheat light
  data[4]= 0x7E;
  data[5]= 0x10;
  data[6]= 0x00;
  data[7]= 0x18;
  CAN.sendMsgBuf(0x545,0, 8, data);
}

void loop() {
 if (Serial3.available () > 0) {  // read bytes from serial3
   SpeedyResponse[ByteNumber ++] = Serial3.read();
 }
 if (ByteNumber > 75){          // After 75 bytes all the data from speeduino has been received so time to process it (A + 74 databytes)
   oldtime = millis();          // All ok. zero out timeout calculation
   ByteNumber = 0;              // zero out the byte number pointer
   processData();               // do the necessary processing for received data
   displayData();               // only required for debugging
   requestData();               //restart data reading
 }
   if ( (millis()-oldtime) > 70) { // timeout if for some reason reading serial3 fails
    oldtime = millis();
    ByteNumber = 0;             // zero out the byte number pointer
    requestData();              //restart data reading
    }
}
