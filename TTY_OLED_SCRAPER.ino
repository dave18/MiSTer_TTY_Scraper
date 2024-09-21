/**
 * BasicHTTPClient.ino
 *
 *  Created on: 24.05.2015
 *
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>

#include <HTTPClient.h>

#include <Adafruit_ILI9341.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "FS.h"
#include "SD.h"

#include "upng.h"

#include "logo.h"

//#define XDEBUG              //print out debug message to serial console
//#define XMARQUEE            //Scrape marquees. This will not be converted to Display image (just the PNG saved to SD Card as ESP32 does not have enought memory)
//#define CABINET            //Scrape marquees. This will not be converted to Display image (just the PNG saved to SD Card as ESP32 does not have enought memory)
//#define XFLYER              //Scrape flyers. These are usually too big to fit in memory. If they fir will not be converted to Display image (just the PNG saved to SD Card)



#define VSPI_MISO 19
#define VSPI_MOSI 23
#define VSPI_SCLK 18
#define VSPI_SS   26

#define TFT_DC 25
#define TFT_RST 27


#define HSPI_MISO 2
#define HSPI_MOSI 15
#define HSPI_SCLK 14
#define HSPI_SS   13


struct entry {
  char game_name[20];
  char title[256];
  char cloneof[20];
  char manufacturer[50];
  char url_ingame[256];
  char url_title[256];
  char url_marquee[256];
  char url_cabinet[256];
  char url_flyer[256];
} game_entry;


// -------------------------------------------------------------
// ------------------------- Variables -------------------------
// -------------------------------------------------------------

// -------------------- Wifi Variables -----------------------

WiFiMulti wifiMulti;
char wSSID[255];
char wPASSWORD[255];
bool APSuccess=false;


// -------------------- Display Variables -----------------------

int DispWidth=0;
int DispHeight=0;
U8G2_FOR_ADAFRUIT_GFX u8g2;
int sdactive=0;

//#define VSPI FSPI

//uninitialized pointers to SPI objects
SPIClass *vspi = NULL;
SPIClass *hspi = NULL;
Adafruit_ILI9341 *tft=NULL;

// -------------------- General Variables -----------------------

char * scraped_data;          // Memory to hold scraped data - will dynamically allocate when needed to keep ESP32 memory usage as low as possible
upng_t* upng;                 // Buffer to decode PNG files - will dynamically allocate when needed to keep ESP32 memory usage as low as possible
unsigned char *logoBin;             // Data to send to screen - will dynamically allocate when needed to keep ESP32 memory usage as low as possible

enum scrapers {ARCADEDB};                // List of scrapers
int scraper=ARCADEDB;

enum state_machine {STATE_IDLE,SERIAL_IN,RECEIVED_CORE,CHECK_PNGS,NEED_SCRAPE,SHOW_TITLE};
int state=STATE_IDLE;
String newCommand = "";                // Received Text, from MiSTer without "\n" currently (2021-01-11)
String prevCommand = "";
String actCorename = "No Core loaded"; // Actual Received Corename
int have_title=0;
int have_ingame=0;
int core_loaded=0;
unsigned long oldMillis=0;
int dispFlipflop=0;




// -----------------------------------------------------------------------
// ----------------------------- SETUP CODE  -----------------------------
// -----------------------------------------------------------------------

void setup() {
  //Initialise Serial
  Serial.begin(115200);                      // 115200 for MiSTer ttyUSBx Device CP2102 Chip on ESP32
  Serial.flush();                            // Wait for empty Send Buffer
  Serial.setTimeout(500);                    // Set max. Serial "Waiting Time", default = 1000ms
 
  //Initalise SD Card
  hspi = new SPIClass(HSPI);
  hspi->begin(HSPI_SCLK, HSPI_MISO, HSPI_MOSI, HSPI_SS);  //SCLK, MISO, MOSI, SS
  pinMode(hspi->pinSS(), OUTPUT);  //HSPI SS
  digitalWrite(hspi->pinSS(),LOW);
  if (!SD.begin(13,*hspi)) {
#ifdef XDEBUG
    Serial.println("Card Mount Failed");    
#endif
  } else {    
    uint8_t cardType = SD.cardType();

    if (cardType == CARD_NONE) {
#ifdef XDEBUG
      Serial.println("No SD card attached");     
#endif
    } else sdactive=1;
#ifdef XDEBUG
    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
    Serial.println("MMC");
    } else if (cardType == CARD_SD) {
      Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
      Serial.println("SDHC");
    } else {
      Serial.println("UNKNOWN");
    }
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);    
#endif
  }
  

  //Initialise Wifi
  strcpy(wSSID,"");
  strcpy(wPASSWORD,"");
  GetWifiCredentials();
#ifdef XDEBUG
  Serial.printf("Wifi Credentials: SSID=%s (length: %d) PASSWORD=%s (length: %d)\n",wSSID,strlen(wSSID),wPASSWORD,strlen(wPASSWORD));
#endif
  APSuccess=wifiMulti.addAP(wSSID, wPASSWORD);  
#ifdef XDEBUG
  if (APSuccess) {
    Serial.println("Access Point Added");     
  } else Serial.println("Failed to add access point");
#endif
  

  //Initilise Display
  vspi = new SPIClass(VSPI);
  tft = new Adafruit_ILI9341(VSPI_SS, TFT_DC, TFT_RST); //Hardware SPI
  pinMode(vspi->pinSS(), OUTPUT);  //VSPI SS
  digitalWrite(vspi->pinSS(),LOW);
  tft->begin();

#ifdef XDEBUG
  // read diagnostics (optional but can help debug problems)
  uint8_t x = tft->readcommand8(ILI9341_RDMODE);
  Serial.print("Display Power Mode: 0x"); Serial.println(x, HEX);
  x = tft->readcommand8(ILI9341_RDMADCTL);
  Serial.print("MADCTL Mode: 0x"); Serial.println(x, HEX);
  x = tft->readcommand8(ILI9341_RDPIXFMT);
  Serial.print("Pixel Format: 0x"); Serial.println(x, HEX);
  x = tft->readcommand8(ILI9341_RDIMGFMT);
  Serial.print("Image Format: 0x"); Serial.println(x, HEX);
  x = tft->readcommand8(ILI9341_RDSELFDIAG);
  Serial.print("Self Diagnostic: 0x"); Serial.println(x, HEX); 
  ShowMem();
#endif

  tft->setRotation(3);
  tft->scrollTo(0);
  tft->setScrollMargins(0,319);
  tft->setAddrWindow(0,0,319,239);
  clearDisplay();
  //setContrast(contrast);                       // Set contrast of display
  tft->setTextSize(1);
  usb2oled_displayon();

  // Init U8G2 for Adafruit GFX
  u8g2.begin(*tft); 
  //u8g2.setFontMode(1);                             // Transpartent Font Mode, Background is transparent
  u8g2.setFontMode(0);                               // Non-Transpartent Font Mode, Background is overwritten
  u8g2.setForegroundColor(ILI9341_BLACK);            // apply Adafruit GFX color
  u8g2.setBackgroundColor(ILI9341_WHITE);

  // Get Display Dimensions
  DispWidth = tft->width();
  DispHeight = tft->height();

  have_title=0;
  have_ingame=0;
  core_loaded=0;
  
  show_mister();
  oldMillis=millis();

  if ((wifiMulti.run() == WL_CONNECTED)) {
      tft->drawRGBBitmap(0,0,(uint16_t*)wifilogo,64,64);                        
      u8g2.setFontMode(1);                             // Transpartent Font Mode, Background is transparent
      u8g2.setForegroundColor(ILI9341_BLACK);            // apply Adafruit GFX color
      u8g2.setBackgroundColor(0x9513);            
      u8g2.setFont(u8g2_font_helvR14_tf);
      u8g2.setCursor(80,20);
      u8g2.print(F("IP:"));
      u8g2.setCursor(110,20);
      u8g2.print(WiFi.localIP());      
  }
  else tft->drawRGBBitmap(0,0,(uint16_t*)nowifilogo,64,64);                        
  
}

// -----------------------------------------------------------------------
// ----------------------------- LOOP CODE  ------------------------------
// -----------------------------------------------------------------------

const char *arcadedb_url="http://adb.arcadeitalia.net/service_scraper.php?ajax=query_mame&lang=en&use_parent=1&game_name=";
//const char *corename="1942";

void loop() {
  //unsigned long currentMillis = millis();
  int ret;
  switch (state)  {

    // ---------------------------------------------------
    // -------- IDLE PHASE - WAIT FOR COMMAND ------------
    // ---------------------------------------------------
    case STATE_IDLE:        //we just wait for new core (plus other activity such as alternate  in-game/title images)
      // Get Serial Data
      if (Serial.available()) {
        prevCommand = newCommand;                                // Save old Command
        newCommand = Serial.readStringUntil('\n');             // Read string from serial until NewLine "\n" (from MiSTer's echo command) is detected or timeout (1000ms) happens.      
#ifdef XDEBUG
        Serial.printf("Received Corename or Command: %s\n", (char*)newCommand.c_str());
#endif
        state=SERIAL_IN;
      }
      if (millis()>oldMillis+10000) {
        dispFlipflop=1-dispFlipflop;
        if (core_loaded) {
          if ((dispFlipflop==1) && (have_ingame)) show_ingame(actCorename.c_str());
          if ((dispFlipflop==0) && (have_title)) show_title(actCorename.c_str());
        }
        oldMillis=millis();
      }
      
    break;
    
    case SERIAL_IN:                                           //Check to see if we have a new core
    // ---------------------------------------------------
    // ---------------- COMMAND RECEIVED -----------------
    // ---------------------------------------------------
    
    if (newCommand.endsWith("QWERTZ")) {                         // Process first Transmission after PowerOn/Reboot.
        // Do nothing, just receive one string to clear the buffer.
        state=STATE_IDLE;
    }                    

    // ---------------------------------------------------
    // ---------------- C O M M A N D 's -----------------
    // ---------------------------------------------------

    else if (newCommand.startsWith("LS")) {                             // Command from Serial to receive Picture Data via USB Serial from the MiSTer
      listDir(SD,"/",0);
      state=STATE_IDLE;
    }
    else if (newCommand.startsWith("CMDCOR")) {                             // Command from Serial to receive Picture Data via USB Serial from the MiSTer
      have_title=0;
      have_ingame=0;      
      String TextIn="",tT="";
      int d1=0;
      int i;  
  
#ifdef XDEBUG
      Serial.println("Called Command CMDCOR");
#endif

      TextIn=newCommand.substring(7);                    // Get Command String
      d1 = TextIn.indexOf(',');                          // Search String for ","
      if (d1==-1) {                                      // No "," found = no Effect Parameter given
        actCorename=TextIn;                              // Get Corename
        //tEffect=-1;                                      // Set Effect to -1 (Random)
#ifdef XDEBUG

        Serial.printf("Received Text: %s, Transition T:None  Name Length:%d\n", (char*)actCorename.c_str(),actCorename.length());
#endif
      }
      else {                                             // "," found = Effect Parameter given
        actCorename=TextIn.substring(0, d1);             // Extract Corename from Command String
      /*  tEffect=TextIn.substring(d1+1).toInt();          // Get Effect from Command String (set to 0 if not convertable)
        if (tEffect<-1) tEffect=-1;                      // Check Effect minimum
        if (tEffect>maxEffect) tEffect=maxEffect;        // Check Effect maximum*/        
#ifdef XDEBUG
        Serial.printf("Received Text: %s, Transition information ignored   Name Length:%d\n", (char*)actCorename.c_str(),actCorename.length());
#endif
      }
      if (!strcmp("MENU",actCorename.c_str())) {       //is this just the menu
        if (core_loaded) show_mister();
        state=STATE_IDLE;                                                  //Unprocessed input, just go back to IDLE
        break;
      }
      if (actCorename.length()>1) state=RECEIVED_CORE; else state=STATE_IDLE;    //if we have a valid core name move onto next stage, otherwise back to IDLE
      
    }
    else state=STATE_IDLE;                                                  //Unprocessed input, just go back to IDLE
    break;

    case RECEIVED_CORE:
    // ---------------------------------------------------
    // -- CORE NAME RECEIVED - CHECK IF ALREADY SCRAPED --
    // ---------------------------------------------------
    core_loaded=1;
    show_mister();                                               //replace current display with mister logo
    ret=check_core(actCorename.c_str());
    if (ret) state=CHECK_PNGS; else state=SHOW_TITLE;
    break;

    case CHECK_PNGS:
    // ---------------------------------------------------
    // --------- NO .565s - CHECK IF PNGS EXISTS ---------
    // ---------------------------------------------------

    ret=check_PNG(actCorename.c_str());
    if (ret) state=NEED_SCRAPE; else state=SHOW_TITLE;
    break;
    

    case NEED_SCRAPE:
    // ---------------------------------------------------
    // ------- CORE NAME RECEIVED - LET'S SCRAPE ---------
    // ---------------------------------------------------
      u8g2.setFontMode(1);                             // Transpartent Font Mode, Background is transparent
      u8g2.setForegroundColor(ILI9341_BLACK);            // apply Adafruit GFX color
      u8g2.setBackgroundColor(0x9513);            
      u8g2.setFont(u8g2_font_helvR14_tf);
      u8g2.setCursor(0,200);
      u8g2.print(F("Scraping"));
      u8g2.setCursor(80,200);
      u8g2.print(F(actCorename.c_str()));
      scrape_core(actCorename.c_str());
      state=STATE_IDLE;                                                  
    break;

    case SHOW_TITLE:
    show_title(actCorename.c_str());
      state=STATE_IDLE;
    break;

    default:
#ifdef XDEBUG
        Serial.printf("Shouldn't get here\n");        
#endif
    state=STATE_IDLE;
    break;

  

  }
}

void GetWifiCredentials()  {
  char temp[255];
  File file = SD.open("/credentials.txt",FILE_READ);
  if (!file) {
#ifdef XDEBUG
    Serial.println("Failed to open credentials files for reading");
#endif
    return;
  }
  char *p;
  int i=0;
  while ((file.available()) && (i<256)) {
    //temp[i]=file.read();
    file.read((unsigned char*)&temp[i],1);    
    if (temp[i]=='\n') {
      if (i>0) temp[i-1]=0; else temp[i]=0;
      p=strstr(temp,"SSID");
      if (p)  {   //found SSID
        p+=4;        
        while ((*p==' ') && (*p!='\n'))  {
          p++;
        }
        if (*p!='\n') strncpy(wSSID,p,255);
      }
      p=strstr(temp,"PASSWORD");
      if (p)  {   //found PASSWORD
        p+=8;
        while ((*p==' ') && (*p!='\n'))  {
          p++;
        }
        if (*p!='\n') strncpy(wPASSWORD,p,255);
      }
      i=0;
    }
    else i++;
  }
  file.close();
}

void show_mister()  {
  tft->drawRGBBitmap(0,0,(uint16_t*)logo,320,240);                        
}

void show_title(const char * corename){
  char *path=(char*)malloc(strlen(corename)+13);
  short w;
  short h;
    strcpy(path,"/");
    strcat(path,corename);    
    strcat(path,"/");
    strcat(path,"title.565");
    if (!SD.exists(path)) {
      free (path);
      return;   //exist with error if file not found
    }

    int s=getFilesize(SD,path);
    logoBin=(unsigned char*)malloc(s);
    readBytes(SD, path,(unsigned char**)&logoBin,s);   //readfile
    w=(short)logoBin[0] | (logoBin[1]<<8);
    h=(short)logoBin[2] | (logoBin[3]<<8);
#ifdef XDEBUG
    Serial.printf("show_title: W:%d H:%d\n",w,h);
#endif            
    clearDisplay();
    tft->drawRGBBitmap((DispWidth-w)/2,(DispHeight-h)/2,(uint16_t*)&logoBin[4],w,h);                        
    free(logoBin);    
}

void show_ingame(const char * corename){
  char *path=(char*)malloc(strlen(corename)+13);
  short w;
  short h;
    strcpy(path,"/");
    strcat(path,corename);    
    strcat(path,"/");
    strcat(path,"ingame.565");
    if (!SD.exists(path)) {
      free (path);
      return;   //exist with error if file not found
    }

    int s=getFilesize(SD,path);
    logoBin=(unsigned char*)malloc(s);
    readBytes(SD, path,(unsigned char**)&logoBin,s);   //readfile
    w=(short)logoBin[0] | (logoBin[1]<<8);
    h=(short)logoBin[2] | (logoBin[3]<<8);
#ifdef XDEBUG
    Serial.printf("show_ingame: W:%d H:%d\n",w,h);
#endif            
    clearDisplay();
    tft->drawRGBBitmap((DispWidth-w)/2,(DispHeight-h)/2,(uint16_t*)&logoBin[4],w,h);                        
    free(logoBin);    
}

int check_core(const char * corename){      //Check to see if '.565' files already exist - no need to scrape
short w;
short h;
int s;
char *path=(char*)malloc(strlen(corename)+13);
unsigned char* header;

    strcpy(path,"/");
    strcat(path,corename);    
    strcat(path,"/");
    strcat(path,"ingame.565");
    if (!SD.exists(path)) {
      free (path);
      return 1;   //exist with error if file not found
    }

    header=(unsigned char*)malloc(4);
    readBytes(SD, path,&header,4);   //get size info
    w=(short)header[0] | (header[1]<<8);
    h=(short)header[2] | (header[3]<<8);
#ifdef XDEBUG
    Serial.printf("W:%d H:%d\n",w,h);
#endif
    free(header);
    if ((w<4) || (w>DispWidth) || (h<4) || (h>DispHeight)) {
      free (path);
      return 1; //image wrong size - need to rescrape
    }
    s=getFilesize(SD,path);
#ifdef XDEBUG
    Serial.printf("Expected file size %d, received %d\n",h*w*2+4,s);
#endif
    if (s!=h*w*2+4) {
      free (path);
      return 1; //file size is wrong - rescrape
    }


    strcpy(path,"/");
    strcat(path,corename);    
    strcat(path,"/");
    strcat(path,"title.565");
    if (!SD.exists(path)) {
      free (path);
      return 1;   //exist with error if file not found
    }
    header=(unsigned char*)malloc(4);
    readBytes(SD, path,&header,4);   //get size info
    w=(short)header[0] | (header[1]<<8);
    h=(short)header[2] | (header[3]<<8);
#ifdef XDEBUG
    Serial.printf("W:%d H:%d\n",w,h);
#endif
    free(header);
    if ((w<4) || (w>DispWidth) || (h<4) || (h>DispHeight)) {
      free (path);
      return 1; //image wrong size - need to rescrape
    }
    s=getFilesize(SD,path);
#ifdef XDEBUG
    Serial.printf("Expected file size %d, received %d\n",h*w*2+4,s);
#endif
    if (s!=h*w*2+4) {
      free (path);
      return 1; //file size is wrong - rescrape
    }

    
    free(path);   //probably don't need all these manual frees as C should deallocate scope
    have_title=1;
    have_ingame=1;
    return 0;
}

int check_PNG(const char * corename){      //Check to see if '.png' files already exist - no need to scrape just convert to .565
short w;
short h;
int s;
char *path=(char*)malloc(strlen(corename)+13);


    strcpy(path,"/");
    strcat(path,corename);    
    strcat(path,"/");
    strcat(path,"ingame.png");
    if (!SD.exists(path)) {
      free (path);
#ifdef XDEBUG
      Serial.printf("Ingame PNG for Core %s not found\n",corename);
#endif
      return 1;   //exist with error if file not found
    }

    strcpy(path,"/");
    strcat(path,corename);    
    strcat(path,"/");
    strcat(path,"title.png");
    if (!SD.exists(path)) {
      free (path);
#ifdef XDEBUG
      Serial.printf("Title PNG for Core %s not found\n",corename);
#endif
      return 1;   //exist with error if file not found
    }

    //so we have both PNG files we can just convert them
    strcpy(path,"/");
    strcat(path,corename);    
    strcat(path,"/");
    strcat(path,"ingame.png");
    s=getFilesize(SD,path);     //get size of PNG file
    scraped_data=(char*)malloc(s);
    if (scraped_data==NULL) return 2;    //memory error
    readBytes(SD, path,(unsigned char**)&scraped_data,s);   //get size info
    s=readPNG(s);
    free(scraped_data); //Now we have saved and decoded the PNG we can free up the scraped memory
    if (s==0) {
      strcpy(path,"/");
      strcat(path,corename);    
      strcat(path,"/");
      strcat(path,"ingame.565");      
      create565(path);
      upng_free(upng);
      have_ingame=1;
    }

    strcpy(path,"/");
    strcat(path,corename);    
    strcat(path,"/");
    strcat(path,"title.png");
    s=getFilesize(SD,path);     //get size of PNG file
    scraped_data=(char*)malloc(s);
    if (scraped_data==NULL) return 2;    //memory error
    readBytes(SD, path,(unsigned char**)&scraped_data,s);   //get size info
    s=readPNG(s);
    free(scraped_data); //Now we have saved and decoded the PNG we can free up the scraped memory
    if (s==0) {
      strcpy(path,"/");
      strcat(path,corename);    
      strcat(path,"/");
      strcat(path,"title.565");      
      create565(path);
      upng_free(upng);
      have_title=1;
    }
    
    free(path);
    return 0;
}
  
void scrape_core(const char * corename){
  
   int ret;
   char path[255];
   char path_trailing[256];

   

   char *search_url=(char*)malloc(strlen(arcadedb_url)+strlen(corename)+1);
   char *filepath;
   strcpy(search_url,arcadedb_url);
   strcat(search_url,corename);

       
    //havepage=ReadHTML("https://www.scrapingcourse.com/ecommerce/");
    //havepage=ReadHTML("https://www.screenscraper.fr");
    //havepage=ReadHTML("https://thegamesdb.net/");
    ret=ReadHTML(search_url,0);
    free(search_url);
    if (ret==0) return;
  
    if (scraper==ARCADEDB) {
      ret=parse_ADB();
    }
    free(scraped_data);      
    if (ret!=0) return;
      
    strcpy(path,"/");
    strcat(path,corename);
    strcpy(path_trailing,path);
    strcat(path_trailing,"/");
    createDir(SD, path);   
    ret=ReadHTML(game_entry.url_ingame,1);    
#ifdef XDEBUG
  Serial.printf("Scraping URL %s\n",game_entry.url_ingame);
#endif
    if (ret>0) {
      filepath=(char*)malloc(strlen(path_trailing)+strlen("ingame.png")+1);
      strcpy(filepath,path_trailing);
      strcat(filepath,"ingame.png");
      writeBytes(SD, filepath, (unsigned char*)scraped_data,ret);
      

      ret=readPNG(ret);
      free(scraped_data); //Now we have saved and decoded the PNG we can free up the scraped memory
      if (ret==0) {        
        strcpy(filepath,path_trailing);
        strcat(filepath,"ingame.565");
        create565(filepath);
        upng_free(upng);
        have_ingame=1;
      }
      free(filepath);
    }

    ret=ReadHTML(game_entry.url_title,1);    
    if (ret>0) {
      filepath=(char*)malloc(strlen(path_trailing)+strlen("title.png")+1);
      strcpy(filepath,path_trailing);
      strcat(filepath,"title.png");
      writeBytes(SD, filepath, (unsigned char*)scraped_data,ret);
      
      ret=readPNG(ret);
      free(scraped_data); //Now we have saved and decoded the PNG we can free up the scraped memory
      if (ret==0) {
        strcpy(filepath,path_trailing);
        strcat(filepath,"title.565");
        create565(filepath);
        upng_free(upng);
        have_title=1;
      }
      free(filepath);
    }
    
#ifdef MARQUEE
    ret=ReadHTML(game_entry.url_marquee,1);    
    if (ret>0) {
      filepath=(char*)malloc(strlen(path_trailing)+strlen("marquee.png")+1);
      strcpy(filepath,path_trailing);
      strcat(filepath,"marquee.png");
      writeBytes(SD, filepath, (unsigned char*)scraped_data,ret);
      free(filepath);      
      free(scraped_data);
    }
#endif

#ifdef CABINET
    ret=ReadHTML(game_entry.url_cabinet,1);    
    if (ret>0) {
      filepath=(char*)malloc(strlen(path_trailing)+strlen("cabinet.png")+1);
      strcpy(filepath,path_trailing);
      strcat(filepath,"cabinet.png");
      writeBytes(SD, filepath, (unsigned char*)scraped_data,ret);
      free(filepath);      
      free(scraped_data);
    }
#endif
   
#ifdef XFLYER
    ret=ReadHTML(game_entry.url_flyer,1);    
    if (ret>0) {
      filepath=(char*)malloc(strlen(path_trailing)+strlen("flyer.png")+1);
      strcpy(filepath,path_trailing);
      strcat(filepath,"flyer.png");
      writeBytes(SD, filepath, (unsigned char*)scraped_data,ret);
      free(filepath);      
      free(scraped_data);
    }
#endif
    
  
  
}


int readPNG(int len) {
  upng=upng_new_from_bytes((unsigned char*)scraped_data, len);
  if (upng != NULL) {
    upng_decode(upng);
    if (upng_get_error(upng) == UPNG_EOK) {
      return 0;
    }
    else {
#ifdef XDEBUG
      Serial.printf("PNG Decode failed with error %d\n",upng_get_error(upng));      
      upng_free(upng);
#endif 
    }
  }
  else {
#ifdef XDEBUG
    Serial.println("PNG: Could not open data stream\n");
#endif  
  }
  return 1;
}

void create565(const char * fname) {
#ifdef XDEBUG
      Serial.printf("PNG Width:%d\n",upng_get_width(upng));
      Serial.printf("PNG Height:%d\n",upng_get_height(upng));
      Serial.printf("PNG Buffer Size:%d\n",upng_get_size(upng));
      Serial.printf("PNG BPP:%d\n",upng_get_bpp(upng));
      Serial.printf("PNG Bit Depth:%d\n",upng_get_bitdepth(upng));
      Serial.printf("PNG Components per Pixel:%d\n",upng_get_components(upng));
      Serial.printf("PNG Format:%d\n",upng_get_format(upng));
#endif
      logoBin=(unsigned char*)malloc(DispWidth * DispHeight*2);   //Create buffer for 565 image based on screen size
      if (logoBin==NULL) {    
 #ifdef XDEBUG
        Serial.println("Could not allocate memory for image buffer\n");
        ShowMem();
#endif
      }
      else
      {
        //*8 bit RGB Routine
        int max_x=upng_get_width(upng);
        int max_y=upng_get_height(upng);
        int cpp=upng_get_components(upng);
        unsigned char r;
        unsigned char g;
        unsigned char b;
        unsigned char *p=(unsigned char *)upng_get_buffer(upng);
        int imageWidth=upng_get_width(upng);
        int imageHeight=upng_get_height(upng);
        if (max_x>DispWidth) max_x=DispWidth;
        if (max_y>DispHeight) max_y=DispHeight;
        float xscale;
        float yscale;
        float f_xoffset;
        float f_yoffset;
        int i_xoffset;
        int i_yoffset;
        if (imageWidth>max_x) xscale=(float)imageWidth/max_x; else xscale=1.0;
        if (imageHeight>max_y) yscale=(float)imageHeight/max_y; else yscale=1.0;
        //if (y
#ifdef XDEBUG
        Serial.printf("Max_x:%d   Max_y:%d\n",max_x,max_y);
#endif
        logoBin[0]=(unsigned char)max_x & 0xff;
        logoBin[1]=(unsigned char)(max_x >>8) & 0xff;
        logoBin[2]=(unsigned char)max_y & 0xff;
        logoBin[3]=(unsigned char)(max_y >>8) & 0xff;
        appendBytes(SD, fname, logoBin,4);
        f_yoffset=0.0;
        i_yoffset=0.0;
        for (int y=0;y<max_y;y++) {
          f_xoffset=0.0;
          i_xoffset=0.0;
          for (int x=0;x<max_x;x++) {
            r=p[i_yoffset*imageWidth*cpp+i_xoffset*cpp];
            g=p[i_yoffset*imageWidth*cpp+i_xoffset*cpp+1];
            b=p[i_yoffset*imageWidth*cpp+i_xoffset*cpp+2];
            //round colours rather than floor
            if (r==0xff) r=((r & 0xf8) >> 3); else r=((r & 0xf8) >> 3) + ((r & 4)>0);
            if (g==0xff) g=((g & 0xfc) >> 2); else g=((g & 0xfc) >> 2) + ((g & 2)>0);
            if (b==0xff) b=((b & 0xf8) >> 3); else b=((b & 0xf8) >> 3) + ((b & 4)>0);
            logoBin[y*max_x*2+x*2]=(unsigned char)(g << 5) | b;
            logoBin[y*max_x*2+x*2+1]=(unsigned char)(r << 3) |  (g >> 3);

            f_xoffset+=xscale;
            i_xoffset=(int)round(f_xoffset);
            if (i_xoffset>imageWidth-1) i_xoffset=imageWidth-1;
          }
          f_yoffset+=yscale;
          i_yoffset=(int)round(f_yoffset);
          if (i_yoffset>imageHeight-1) i_yoffset=imageHeight-1;
        }                
        clearDisplay();
        tft->drawRGBBitmap((DispWidth-max_x)/2,(DispHeight-max_y)/2,(uint16_t*)logoBin,max_x,max_y);                        
        appendBytes(SD, fname, logoBin,max_x*max_y*2);
        free(logoBin);
      }

}


int parse_ADB()
{
    char temp[255];
    
    int ver;
    strncpy(temp,&scraped_data[0],11);
    temp[11]=0; 
    if (strcmp(temp,"{\"release\":")) {
#ifdef XDEBUG
        Serial.println("Invalid Arcade DB result");  
        Serial.printf("Expected {\"release\": got %s\n",temp);  
#endif
        return -1;
    }
    
    strncpy(temp,&scraped_data[11],1);
    temp[1]=0;     
    ver=atoi(temp);    
#ifdef XDEBUG
    Serial.printf("Version: %d\n",ver);
#endif

    strncpy(temp,&scraped_data[12],13);
    temp[13]=0;
        
    if (!strcmp(temp,",\"result\":[]}")) {
#ifdef XDEBUG
        Serial.println("No results found");          
#endif
        return -1;
    }
    
    strncpy(game_entry.game_name,FindEntry("game_name"),20);        
    strncpy(game_entry.title,FindEntry("title"),256);        
    strncpy(game_entry.manufacturer,FindEntry("manufacturer"),50);        
    strncpy(game_entry.cloneof,FindEntry("cloneof"),20);        
    strncpy(game_entry.url_ingame,FormatURL(FindEntry("url_image_ingame")),256);
    
    strncpy(game_entry.url_title,FormatURL(FindEntry("url_image_title")),256);
    strncpy(game_entry.url_marquee,FormatURL(FindEntry("url_image_marquee")),256);
    strncpy(game_entry.url_cabinet,FormatURL(FindEntry("url_image_cabinet")),256);
    strncpy(game_entry.url_flyer,FormatURL(FindEntry("url_image_flyer")),256);

#ifdef XDEBUG
    Serial.printf("Name: %s\n",game_entry.game_name);
    Serial.printf("Title: %s\n",game_entry.title);
    Serial.printf("Manufacturer: %s\n",game_entry.manufacturer);
    Serial.printf("Clone of: %s\n",game_entry.cloneof);
    Serial.printf("ingame: %s\n",game_entry.url_ingame);
    Serial.printf("title: %s\n",game_entry.url_title);
    Serial.printf("marquee: %s\n",game_entry.url_marquee);
    Serial.printf("cabinet: %s\n",game_entry.url_cabinet);
    Serial.printf("flyer: %s\n",game_entry.url_flyer);
#endif
    
    
    return 0;

}

char * FormatURL(const char *input){    //URLs are read with annoying control character which need removing
  if (strlen(input)<1) return "\0";
  char * temp=(char*)malloc(strlen(input)+1);
  char * tempptr=temp;
  for (int i=0;i<strlen(input);i++) {
    if (input[i]!='\\') {
      *tempptr=input[i];
      tempptr++;
    }
  }
  *tempptr=0;

  return temp;
}

char * FindEntry(const char *input) {
#ifdef XDEBUG
    Serial.printf("Find Entry \'%s\'\n",input);
    Serial.printf("Dump of scraped data\n",scraped_data);
#endif
    if (strlen(input)<1) return "\0";
    char * temp=(char*)malloc(256);
    char * ptr1;
    char * ptr2;
    ptr1=strstr(scraped_data,input);        //find relevant entry text
    if (ptr1==NULL) {
#ifdef XDEBUG
       Serial.printf("Substring %s not found in scraped webpage\n",input);
       //Serial.printf("Dump of scraped data\n",scraped_data);
#endif
       return "\0";
    }
    ptr2=strchr(ptr1,':');                  //find following :
    ptr1=strchr(ptr2,'\"');                  //find following "
    ptr1++;                                  //ptr1 now points to game_name data
    ptr2=strchr(ptr1,'\"');                  //find following "
    strncpy(temp,ptr1,ptr2-ptr1);           //copy name in temp
    temp[ptr2-ptr1]=0;                      //null terminate   
#ifdef XDEBUG
    Serial.println(temp);
#endif 
    return temp;
}



int ReadHTML(char * page,int scrape_type) {    //0=initial scrape, 1=image
  // wait for WiFi connection
  int ret=1;
  if (strlen(page)<7) return 0;             //not enough characters for http://
#ifdef XDEBUG
    Serial.printf("Scraping page %s\n",page);
#endif
  if ((wifiMulti.run() == WL_CONNECTED)) {
#ifdef XDEBUG
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
#endif
    HTTPClient http;
#ifdef XDEBUG
    Serial.print("[HTTP] begin...\n");
#endif
    http.begin(page);  //HTTP
#ifdef XDEBUG
    Serial.print("[HTTP] GET...\n");
#endif
    // start connection and send HTTP header
    http.setTimeout(20000);
    http.setConnectTimeout(20000);
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      String payload;
      // HTTP header has been send and Server response header has been handled
#ifdef XDEBUG
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);
#endif

      // file found at server
      if (httpCode == HTTP_CODE_OK) {
#ifdef XDEBUG
        Serial.printf("Page size report as %d\n",http.getSize());        
#endif
        if (http.getSize()>3500000) {
#ifdef XDEBUG
        Serial.printf("Too big for this little ESP, skipping image\n");        
#endif
          return 0;
        }
        payload = http.getString();     
#ifdef XDEBUG
        Serial.println("HTML Received\n");
    //    Serial.println(payload);
#endif
      }
      int len=payload.length();
      if (len>0) {
#ifdef XDEBUG
        printf("Actual length received: %d\n",len);
#endif
        ret=len;
        scraped_data=(char*)malloc(len+1); //+1 to allow for null termination
        if (scraped_data==NULL) {
#ifdef XDEBUG
        Serial.printf("Could not allocated memory to scrape image - skipping\n");        
#endif
          return 0;
        }        
        memcpy(scraped_data,payload.c_str(),len+1);                   
      }
    } else {
#ifdef XDEBUG
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
#endif
      ret=0;
    }

    http.end();
    return ret;
  }
}


// -----------------------------------------------------------------------
// ------------------------- SD CARD ROUTINES  ---------------------------
// -----------------------------------------------------------------------


void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
#ifdef XDEBUG
  Serial.printf("Listing directory: %s\n", dirname);
#endif
  File root = fs.open(dirname);
  if (!root) {
#ifdef XDEBUG
    Serial.println("Failed to open directory");
#endif
    return;
  }
  if (!root.isDirectory()) {
#ifdef XDEBUG
    Serial.println("Not a directory");
#endif
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void createDir(fs::FS &fs, const char *path) {
#ifdef XDEBUG
  Serial.printf("Creating Dir: %s\n", path);
#endif
  if (fs.mkdir(path)) {
#ifdef XDEBUG
    Serial.println("Dir created");
#endif
  } else {
#ifdef XDEBUG
    Serial.println("mkdir failed");
#endif
  }
}

void removeDir(fs::FS &fs, const char *path) {
#ifdef XDEBUG
  Serial.printf("Removing Dir: %s\n", path);
#endif
  if (fs.rmdir(path)) {
#ifdef XDEBUG
    Serial.println("Dir removed");
#endif
  } else {
#ifdef XDEBUG
    Serial.println("rmdir failed");
#endif
  }
}

void readFile(fs::FS &fs, const char *path) {
#ifdef XDEBUG
  Serial.printf("Reading file: %s\n", path);
#endif
  File file = fs.open(path);
  if (!file) {
#ifdef XDEBUG
    Serial.println("Failed to open file for reading");
#endif
    return;
  }
#ifdef XDEBUG
  Serial.print("Read from file: ");
#endif
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(fs::FS &fs, const char *path, const char *message) {
#ifdef XDEBUG
  Serial.printf("Writing file: %s\n", path);
#endif
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
#ifdef XDEBUG
    Serial.println("Failed to open file for writing");
#endif
    return;
  }
  if (file.print(message)) {
#ifdef XDEBUG
    Serial.println("File written");
#endif
  } else {
#ifdef XDEBUG
    Serial.println("Write failed");
#endif
  }
  file.close();
}

void readBytes(fs::FS &fs, const char *path, unsigned char **message,size_t len) {
#ifdef XDEBUG
  Serial.printf("Reading file: %s\n", path);
#endif
  File file = fs.open(path, FILE_READ);
  if (!file) {
#ifdef XDEBUG
    Serial.println("Failed to open file for reading");
#endif
    return;
  }
  if (file.read(*message,len)) {
#ifdef XDEBUG
    Serial.println("File read");
#endif
  } else {
#ifdef XDEBUG
    Serial.println("Read failed");
#endif
  }
  file.close();
}

int getFilesize(fs::FS &fs, const char *path) {
  int s;
#ifdef XDEBUG
  Serial.printf("Checking file size: %s\n", path);
#endif
  File file = fs.open(path, FILE_READ);
  if (!file) {
#ifdef XDEBUG
    Serial.println("Failed to open file for reading");
#endif
    return 0;
  }
  s=file.size();
  file.close();
  return s;
}

void writeBytes(fs::FS &fs, const char *path, const unsigned char *message,size_t len) {
#ifdef XDEBUG
  Serial.printf("Writing file: %s\n", path);
#endif
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
#ifdef XDEBUG
    Serial.println("Failed to open file for writing");
#endif
    return;
  }
  if (file.write(message,len)) {
#ifdef XDEBUG
    Serial.println("File written");
#endif
  } else {
#ifdef XDEBUG
    Serial.println("Write failed");
#endif
  }
  file.close();
}


void appendBytes(fs::FS &fs, const char *path, const unsigned char *message,size_t len) {
#ifdef XDEBUG
  Serial.printf("Appending file: %s\n", path);
#endif
  File file = fs.open(path, FILE_APPEND);
  if (!file) {
#ifdef XDEBUG
    Serial.println("Failed to open file for appending");
#endif
    return;
  }
  if (file.write(message,len)) {
#ifdef XDEBUG
    Serial.println("File append written");
#endif
  } else {
#ifdef XDEBUG
    Serial.println("Append failed");
#endif
  }
  file.close();
}


void appendFile(fs::FS &fs, const char *path, const char *message) {
#ifdef XDEBUG
  Serial.printf("Appending to file: %s\n", path);
#endif
  File file = fs.open(path, FILE_APPEND);
  if (!file) {
#ifdef XDEBUG
    Serial.println("Failed to open file for appending");
#endif
    return;
  }
  if (file.print(message)) {
#ifdef XDEBUG
    Serial.println("Message appended");
#endif
  } else {
#ifdef XDEBUG
    Serial.println("Append failed");
#endif
  }
  file.close();
}

void renameFile(fs::FS &fs, const char *path1, const char *path2) {
#ifdef XDEBUG
  Serial.printf("Renaming file %s to %s\n", path1, path2);
#endif
  if (fs.rename(path1, path2)) {
#ifdef XDEBUG
    Serial.println("File renamed");
#endif
  } else {
#ifdef XDEBUG
    Serial.println("Rename failed");
#endif
  }
}

void deleteFile(fs::FS &fs, const char *path) {
#ifdef XDEBUG
  Serial.printf("Deleting file: %s\n", path);
#endif
  if (fs.remove(path)) {
#ifdef XDEBUG
    Serial.println("File deleted");
#endif
  } else {
#ifdef XDEBUG
    Serial.println("Delete failed");
#endif
  }
}

void testFileIO(fs::FS &fs, const char *path) {
  File file = fs.open(path);
  static uint8_t buf[512];
  size_t len = 0;
  uint32_t start = millis();
  uint32_t end = start;
  if (file) {
    len = file.size();
    size_t flen = len;
    start = millis();
    while (len) {
      size_t toRead = len;
      if (toRead > 512) {
        toRead = 512;
      }
      file.read(buf, toRead);
      len -= toRead;
    }
    end = millis() - start;
    Serial.printf("%u bytes read for %lu ms\n", flen, end);
    file.close();
  } else {
#ifdef XDEBUG
    Serial.println("Failed to open file for reading");
#endif
  }

  file = fs.open(path, FILE_WRITE);
  if (!file) {
#ifdef XDEBUG
    Serial.println("Failed to open file for writing");
#endif
    return;
  }

  size_t i;
  start = millis();
  for (i = 0; i < 2048; i++) {
    file.write(buf, 512);
  }
  end = millis() - start;
  Serial.printf("%u bytes written for %lu ms\n", 2048 * 512, end);
  file.close();
}


// --------------------------------------------------------------
// ------------------- Display Routines -------------------------
// --------------------------------------------------------------


void clearDisplay()
{  
#ifdef XDEBUG
  Serial.println("Clear Display");
#endif
  tft->fillScreen(ILI9341_BLACK); 
}

// --------------------------------------------------------------
// ------------------ Switch Display off ------------------------
// --------------------------------------------------------------
void usb2oled_displayoff(void) {
#ifdef XDEBUG
  Serial.println("Switch Display Off");
#endif
  tft->enableDisplay(false);
}


// --------------------------------------------------------------
// ------------------- Switch Display on ------------------------
// --------------------------------------------------------------
void usb2oled_displayon(void) {
#ifdef XDEBUG
  Serial.println("Switch Display On");
#endif
  
  //oled.displayOn();                 // Switch Display on  
  tft->enableDisplay(true);
}

void ShowMem() {
  Serial.print("Total Heap: "); Serial.println(ESP.getHeapSize()); 
  Serial.print("Free Heap: "); Serial.println(ESP.getFreeHeap()); 
  Serial.print("Total PSRAM: "); Serial.println(ESP.getPsramSize());   
  Serial.print("Free PSRAM: "); Serial.println(ESP.getFreePsram()); 
}
