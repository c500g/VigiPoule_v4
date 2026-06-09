
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "RTClib.h"
#include <EEPROM.h>

/* last upload : 09/06/2026
Sketch uses 18190 bytes (59%) of program storage space. Maximum is 30720 bytes.
Global variables use 1320 bytes (64%) of dynamic memory, leaving 728 bytes for local variables. Maximum is 2048 bytes.
*/

/*  GESTION CLIGNOTEMENTS ERREURS: pb sur VigiPoule mais reset erreur apres 20min
 *                1= pb fil cassé  
 *                2= pb capteur hall ou d'aimant
 *                3= pb de moteur (courant trop faible, moteur deconnecté)
 *                4= pb de moteur (moteur bloqué, courant trop fort, superieur a shunt_max)    le shunt de fonctionnement est environ = 70
 *  GESTION CLIGNOTEMENTS DES WARNINGS: VigiPoule fonctionnel mais avec un probleme
 *                1= fonctionnement ok !
 *                2= module horaire absent ou non fonctionnel
 *                3= module horaire non calibré            
 *                
 */


/* FONCTIONNEMENT :
 *  
 *  en pré-installation:  - upload du code dans Arduino avec ligne date dans EEPROM DS3231
 *                        - optionnel:alignement du meplat axe moteur vers le haut (pour pouvoir y visser la poulie) en alimentant le moteur en direct
 *                        - mettre la porte en bas
 *                        - mettre MS1 sur OFF
 *                  
 *  a la mise sous tension: 
 *  
 *     1/ dans le SETUP : verficition magic_number , si !=153, upload automatique dans l'EEPROM les parametres usine
 *            si doorState=3 , c'etait une coupure courant donc on ouvre la porte.
 *            sinon, si c'est une premiere mise en route (doorState=0), il faut mettre la porte en bas et appui long sur le bouton pour calibrer la hauteur_porte puis faire MS1 ON
 *        
 *     2/ Cycle de fonctionnement:
 *            les microswitch permettent de définir des modes de fonctionnement:
 *            MS1=0   calibrage:   bouton: - appui court = fait descendre la porte 1 cm
 *                                           - appui moyen = fait remonter la porte 1 cm
 *                                           - appui long=   recalibrage porte (UP); il faut que la porte soit en bas
 *            MS1=1     => fonctionnement normal tout automatique
 *                            bouton: - appui court = fait ouvre ou ferme la porte jusqu'au prochain horraire prévu
 *                                    - appui long  = effacement erreur
 *            MS2 (0/1) => non utlisé

*/


// set pin number:
const byte hallPin=     3;
const byte motorBPin=   5;
const byte motorAPin=   6;
const byte MS2Pin=      7;
const byte MS1Pin=      8;
const byte buttonPin=   11;
const byte ledGPin =    12;
const byte lightPin=    A0;
const byte ledRPin =    A1;
const byte extPin =     A3;
const byte shuntPin=    A7;

const byte btnPins[] = {0, 3, 4, 5}; // Les pin nb des 4 boutons sur la serviceBox

byte menu=0, page=0;
bool bloque_porte=0;
int shunt=0;

// generic variables

byte error, warning, button;
byte doorState = 0;             // doorState 0= jamais utilisé, 1= en haut, 2= en bas, 3= en cours de mouvement
bool  ledR = LOW,ledG = LOW;
int8_t value[50];

// Adresses I2C (à ajuster selon tes soudures A0-A2)
#define ADDR_LCD 0x27 
#define ADDR_PCF 0x20 

// Initialisation LCD 16x2
LiquidCrystal_I2C lcd(ADDR_LCD, 16, 2);
RTC_DS3231 rtc;
DateTime date;  

// time variables /constants

const byte menu_taille[]=   { 6, 5, 35, 8, 4, 20,1};
  
const int DELAY_2h=7200;                          // 7200 sec = 2h   , DELAY pour ouvrir/fermer porter a nouveau avec "light"
const int DELAY_20m= 1200;                        // 1200 sec = 20min de DELAY avant de ressayer d'ouvrir une porte bloquée
const int DELAY_2m=120;                           // 120 sec= 2 min, si on a ouvert manuellement, la porte se refermera 2 min apres (si bon timing)

byte currentHour, closingHour, openingHour; 
unsigned long previousPush=0;
unsigned long lastDoorMove;
unsigned long tempsMove;

byte MATIN, SOIR, HAUTEUR_PORTE;
byte SHUNT_MAX, SHUNT_MIN;    // on fait 10 mesures que l'on somme

void setup() {
  byte magicNumber;
  // set the digital pin as output:
  pinMode(ledRPin, OUTPUT);
  pinMode(ledGPin, OUTPUT);
  pinMode(motorAPin, OUTPUT);
  pinMode(motorBPin, OUTPUT);
  pinMode(hallPin, INPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(MS1Pin, INPUT_PULLUP);
  pinMode(MS2Pin, INPUT_PULLUP);
  Serial.begin(9600);
  
  Wire.begin();
  rtc.begin();

  // A DECOCHER POUR UPLOAD CODE AVEC MISE A JOUR HORLOGE DU DS3231: il faut que le module horloge soit branché !
  //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  magicNumber=   EEPROM.read(0);  if (magicNumber !=153)  { doorState=0; reset_usine(); }  // premiere mise en service: ecriture des variables en EEPROM
  doorState=     EEPROM.read(1); 
  HAUTEUR_PORTE= EEPROM.read(2);   
  MATIN=         EEPROM.read(5);
  SOIR=          EEPROM.read(6);
  SHUNT_MAX=     EEPROM.read(8);
  SHUNT_MIN=     EEPROM.read(9);
  //error=         EEPROM.read(4);           l'erreur est remise a 0 apres redemarrage mais on garde l'info de l'erreur avec le clignotement 
  if (doorState==3) MOVE(0);                           // si coupure courant pendant ouverture / fermeture => on re-ouvre sans recalibrer
  }
/*
void serial_monitor() {
  Serial.print(" - previousPush= ");  Serial.print(previousPush);
  Serial.print(" - error= "); Serial.print(error);
  Serial.print(" - ledR= "); Serial.print(ledR);
//  Serial.print(" - hall= ");  Serial.print(hall);
  //Serial.print(" - button= ");  Serial.print(appui);
  Serial.print(" - warning= ");  Serial.print(warning);
  Serial.print F("doorState= ");   Serial.println(doorState);
}
*/

void get_time() {
  //Serial.println(millis());
  static byte old_sec =99;
  static byte pre_warning=0;
  date= rtc.now();
  currentHour=date.hour();
  openingHour=EEPROM.read(8+date.month()*2);
  closingHour= EEPROM.read(9+date.month()*2); 
  
  //float temp=rtc.getTemperature();  
  if (date.second()!=(old_sec+1)) { pre_warning++; if (pre_warning==5) { warning=2; EEPROM.update(3,2); }}
  else { pre_warning=0; warning=1; EEPROM.update(3,1); }
  if (date.year()>=00 && date.year()<26) { warning=3; EEPROM.update(3,3);}
  //Serial.print("GET TIME ");Serial.print("date.sec=");Serial.print(date.second());Serial.print(" old sec=");Serial.print(old_sec);Serial.print(" prewarn="); Serial.println(pre_warning);
  old_sec=date.second();  
  }

inline void read_button() { 
   // état de la variable rendu par cette fonction :  0=pas d'appui, 1= appui court, 2= appui 2 sec, 3=appui long, 10=appui en cours
   bool pushedButton;                                                                        // attention le button est en pullup (0= bouton appuyé)
   int tempsAppui=0;
   pushedButton = digitalRead(buttonPin);                                                    // get button
   if (button !=0) tempsAppui= millis()-previousPush;                                        // on calcule le temps du clic
   if (pushedButton==0 )     { if (button==0) { previousPush=millis(); delay(100); }         // bouton cliqué, on fixe le timer du début si ce n'est pas fait
                               if (tempsAppui>5000 )    { button=3;  delay(20); }            // si c'est > 5 sec, on arrete car c'est l'appui 3
                               else button=10;  }                                            // appui en cours ...                              
   if (pushedButton==1 && button==10)  { if (tempsAppui < 500 )   button=1 ;                 // appui court
                                        else button=2;                                       // appui moyen ( >1 et <5s)
                                        delay(100);            }          
   if (pushedButton==1 && button==3) button=0;
   }

void allumage_led() {
  int compteur=millis()%5000;                     // on se base sur un cycle global de 5 sec
  int compteur2=compteur%800;                     // chaque clignotement se passe sur 800ms
  if ((compteur/800) < EEPROM.read(4))   ledR=1;             // les 1ers 400ms on allume en cas d'erreur enregistrée (led Rouge)
  if ((compteur/800) < warning && ledR==0) ledG=1;           // les 1ers 400ms on allume en cas de warning (led Verte)
  if (compteur2>400)   {  ledR=0; ledG=0; }       // puis on eteint
  digitalWrite(ledRPin, ledR);
  digitalWrite(ledGPin, ledG);
  }

void action_button_normal() {
  if (button==1) {  bloque_porte=1; 
                    if (doorState==2)        MOVE(0); 
                    else if (doorState==1)   MOVE(1); } 
  if (button==2) { lcd.init(); lcd.backlight(); lcd.setCursor(0, 0); lcd.print F("- SERVICE BOX -"); lcd.setCursor(0, 1); lcd.print F(" VigiPoule v4 !"); delay(1700);} // allumage serviceBox
  if (button==3) {  error=0; EEPROM.update(4,0); }
  if (button==1 or button==2) button=0;
}

void action_button_calib() {
  if (button==1) { digitalWrite(motorAPin,0); digitalWrite(motorBPin,1);  delay(200);  digitalWrite(motorAPin,0); digitalWrite(motorBPin,0);}  // moteur descend un peu
  if (button==2) { digitalWrite(motorAPin,1); digitalWrite(motorBPin,0);  delay(200);  digitalWrite(motorAPin,0); digitalWrite(motorBPin,0);}  // moteur monte un peu
  if (button==3) { HAUTEUR_PORTE=210; doorState=0; MOVE(0); }   // pout etre sur qu'il n'y aura pas d'arret trop tot avec erreur=2
  if (button==1 or button==2) button=0;
}

void MOVE(bool sens) {                    // sens=0  => monte   sens=1 => descend
  SHUNT_MAX=     EEPROM.read(8);
  SHUNT_MIN=     EEPROM.read(9);
  byte previous_doorState= doorState;
  analogReference(INTERNAL);      // Active les 1.1V internes pour lecture shunt plus précise
  doorState=3; EEPROM.update(1,doorState);
  //unsigned long timerStart=millis();
  lastDoorMove=millis();
  bool fin=0;
  digitalWrite(motorAPin,!sens); digitalWrite(motorBPin,sens);
  do {  shunt=0; for (byte i=0;i<10;i++) shunt+=analogRead(shuntPin);               // mesure du courant moteur
        tempsMove=millis()-lastDoorMove;
        if (sens==0) fin=!digitalRead(hallPin);                                      // fin normale de montée detectée par capteur hall 
        if ((tempsMove>(uint32_t)HAUTEUR_PORTE*980) && sens==1)            fin=1;    // fin normale de la descente
        if (tempsMove>(uint32_t)HAUTEUR_PORTE*1500)                      { fin=1; error=1; error_log(shunt); }           // arret sécu (surement fil cassé)
        if ((tempsMove>(uint32_t)HAUTEUR_PORTE*1100) && shunt>SHUNT_MAX) { fin=1; error=2; error_log(shunt); }           // arret sécu, moteur bloqué à l'arrivée (pb capteur hall)
        if (tempsMove>2000 && shunt<SHUNT_MIN) { if (confirm_shunt()==1) { fin=1; error=3; error_log(shunt); }  }        // arret sécu pb moteur = ne tourne pas
        if (tempsMove>1000 && shunt>SHUNT_MAX)                           { fin=1; error=4; error_log(shunt); }           // arret sécu pb moteur = moteur bloqué au depart surchauffe
        
         //Serial.print F(" - shunt= ");  Serial.println(shunt);
        serviceBox();      
      }
  while (fin==0) ;
  if (previous_doorState==0) { HAUTEUR_PORTE=(millis()-lastDoorMove)/1000; EEPROM.update(2,HAUTEUR_PORTE);  }       // mesure+enregistrement hauteur porte en mode "calibration" 
  analogReference(DEFAULT); // ré-active les 5V internes
  digitalWrite(motorAPin,1); digitalWrite(motorBPin,1);
  delay(100);
  digitalWrite(motorAPin,0); digitalWrite(motorBPin,0);
  doorState=1+sens;             // cas normal
  if (error==3 || error==4) doorState=previous_doorState;
  EEPROM.update(1,doorState);
  }

bool confirm_shunt() {
  delay(500);
  bool confirm=0;
  shunt=0; 
  for (byte i=0;i<10;i++) shunt+=analogRead(shuntPin);               // mesure du courant moteur
  if (shunt<SHUNT_MIN) confirm=1;
  if (shunt>SHUNT_MAX) confirm=1;
  return confirm;
}

void error_log(byte info) {
  EEPROM.update(4,error);
  byte compt_log= EEPROM.read(7);
  byte offset=40+(compt_log*4);
  // On prépare les valeurspacker de mois/jour/heure en timestamp perso
  date= rtc.now();
  uint16_t m = date.month();  // 4 bits
  uint16_t j = date.day();    // 5 bits
  uint16_t h = date.hour();   // 5 bits
  uint16_t packed = (m << 10) | (j << 5) | h;
  EEPROM.put(offset, packed); // Enregistre les 2 octets de la date en timestamp d'un coup
  EEPROM.update(offset+2,error);
  EEPROM.update(offset+3,info);
  compt_log++; if (compt_log >= 30) compt_log = 0; EEPROM.update(7,compt_log);
}

void serviceBox() {
 static bool serviceBox_init=0;
 if (currentHour==23) serviceBox_init=0;
 int8_t boutonPresse = -1;
 static byte modif, sec;
 static unsigned long previousPush2;
 date= rtc.now();
 Wire.requestFrom(ADDR_PCF, 1);
 if (Wire.available()) {
      byte etat = Wire.read();
      for (int i = 0; i < 4; i++) { if (bitRead(etat, btnPins[i])) {  boutonPresse = i+1;   break;  } } // On scanne tes 4 boutons (P0, P3, P4, P5) ; On sort dès qu'un bouton est trouvé
      if (boutonPresse==1 && modif==0)   {   page=(page-1+menu_taille[menu])%menu_taille[menu];   serviceBox_affiche_page();   }   
      if (boutonPresse==2 && modif==1)   {   value[page]++;    serviceBox_affiche_page();  }     
      if (boutonPresse==1 && modif==1)   {   value[page]--;   serviceBox_affiche_page();   }   
      if (boutonPresse==2 &&  modif==0)  {   page=(page+1)%menu_taille[menu];   serviceBox_affiche_page();   }    
      if (boutonPresse==3) {
           delay(250);
           if (menu==1) { if (modif==0)         { modif=1;  }     // reglage horraire / date
                          else if (modif==1)    { modif=0; confirm_new_date(); serviceBox_affiche_page();    }
                        }  
           if (menu==2) { if (modif==0)         { modif=1;  }     // parametres EEPROM
                          else if (modif==1)    { modif=0; confirm_new_value(); serviceBox_affiche_page();    }
                       }  
           if (menu==3)   { action_IO();  }     // check I/O
           if (menu==5)   { serviceBox_affiche_page();   }     // affiche log erreur
           if (menu==6)   { blue_led(1); lcd.setCursor(0, 1); lcd.print F("*** reseting ***");  reset_usine(); blue_led(0); boutonPresse=4; }   // reset et revient menu principal (simulation bouton retour)
           if (menu==0)   { menu=menu+1+page; page=0; serviceBox_affiche_page();  }
      }
      if (boutonPresse==4 )            { menu=0; value[page]=0; page=0; modif=0;  serviceBox_affiche_page();  }
      if (menu==4)           { if (millis()-previousPush2>500) {serviceBox_affiche_page(); previousPush2=millis(); } }
      }
 } 
 

void reset_usine() {
  byte eeprom_usine[]={153,0,45,0,0,40,20,0,120,10,9,18,9,18,9,19,8,20,8,21,7,21,7,21,8,21,8,20,9,19,10,18,10,17,0,0,0};
  //                                               jan  feb  mar  apr  may  jun  jul  aug  sep  oct  nov   dec
  eeprom_usine[1]=doorState;    // on remet la valeur de la doorState
  byte val;
  for (byte i=0; i<=254; i++)    {
        if (i<sizeof(eeprom_usine)) val=eeprom_usine[i];
        else val=0;
        EEPROM.update(i, val);  
        //Serial.print(i);  Serial.print F(" "); Serial.println(val);
        }
  delay(1250);
  
  error=0; EEPROM.update(4,0);
  }

void blue_led(bool etat) {
  static byte etat_pcf = 0xFF; // On garde l'état en mémoire
  if (etat) bitClear(etat_pcf, 7); else bitSet(etat_pcf, 7);
  Wire.beginTransmission(ADDR_PCF);
  Wire.write(etat_pcf);
  Wire.endTransmission();
   }
   
void action_IO() {
  char l[6]; // Un tableau de char au lieu d'une String
  unsigned long timer = millis();
  blue_led(1); 
  
  if (page == 2 || page == 3) analogReference(INTERNAL); 
  
  while ((millis() - timer) < 3000) { 
    float temp = rtc.getTemperature();
    byte light = map(analogRead(lightPin), 1023, 0, 100, 0);
    shunt = 0; for (byte i = 0; i < 10; i++) shunt += analogRead(shuntPin);
    bool hall = digitalRead(hallPin);    
    
    switch (page) {
      case 0: digitalWrite(ledGPin, 1);  break;
      case 1: digitalWrite(ledRPin, 1);  break;
      // On utilise sprintf pour convertir les chiffres en texte dans 'l'
      case 2: digitalWrite(motorAPin, 1); digitalWrite(motorBPin, 0); sprintf(l, "%d", shunt); break;
      case 3: digitalWrite(motorAPin, 0); digitalWrite(motorBPin, 1); sprintf(l, "%d", shunt); break;
      case 4: sprintf(l, "%d", hall); break;
      case 5: sprintf(l, "%d", button); break;
      case 6: sprintf(l, "%d", light); break;
      case 7: dtostrf(temp, 2, 1, l); break; // dtostrf est nécessaire pour les float (temp)
    }
    
    lcd.setCursor(12, 1); lcd.print("    "); // On efface la zone (4 espaces)
    lcd.setCursor(12, 1); lcd.print(l);      // On affiche la valeur
    delay(250);
  }
  
  analogReference(DEFAULT);   
  digitalWrite(motorAPin, 0); digitalWrite(motorBPin, 0);
  digitalWrite(ledGPin, 0); digitalWrite(ledRPin, 0);
  blue_led(0);                                 
}

void confirm_new_value() {
  byte eeprom_value=EEPROM.read(page);
  EEPROM.update(page,(eeprom_value+value[page]));
  Serial.print F("*****eeprom menu=");Serial.print(menu);Serial.print F(" page="); Serial.print(page);Serial.print F(" value[page]="); Serial.println(value[page]);
  blue_led(1); 
  delay(500);
  blue_led(0); 
  value[page]=0;
  }

void confirm_new_date() {
  uint16_t new_date[]={ date.year(),date.month(),date.day(),date.hour(), date.minute()};
  new_date[page]=new_date[page]+value[page];
  Serial.print F("******** menu=");Serial.print(menu);Serial.print F(" page="); Serial.print(page);Serial.print F(" value[page]="); Serial.println(value[page]);
  rtc.adjust(DateTime(new_date[0],new_date[1],new_date[2],new_date[3],new_date[4]));  
  value[page]=0;
  blue_led(1); 
  delay(500);
  blue_led(0); 
  date= rtc.now();
  }

void serviceBox_affiche_page() {
  char l1[17] = ""; // Initialisation à vide
  char l2[17] = "";
  char date_du_jour[20];
  sprintf(date_du_jour, "%02d/%02d/%02d %02d:%02d", date.day(), date.month(), date.year() % 100, date.hour(), date.minute());
  lcd.clear();

  // --- MENU 0 : ACCUEIL ---
  if (menu == 0) {
    strncpy_P(l1,PSTR("VigiPoule menu:"),16);
    switch (page) {
      case 0: strcpy(l2, date_du_jour); break;
      case 1: strncpy_P(l2, PSTR("EEPROM param."),16); break;
      case 2: strncpy_P(l2, PSTR("I/O check"),16); break;
      case 3: strncpy_P(l2, PSTR("Debugger"),16); break;
      case 4: strncpy_P(l2, PSTR("Error_Log menu"),16); break;
      case 5: strncpy_P(l2, PSTR("Reset usine"),16); break;
    }
  }

  // --- MENU 1 : REGLAGE DATE/HEURE ---
  if (menu == 1) {
    strncpy_P(l1, PSTR("menu date:"),16);
    switch (page) {
      case 0: sprintf(l2, "year:  %d", date.year() + value[page]); break;
      case 1: sprintf(l2, "month: %d", date.month() + value[page]); break;
      case 2: sprintf(l2, "day:   %d", date.day() + value[page]); break;
      case 3: sprintf(l2, "hour:  %d", date.hour() + value[page]); break;
      case 4: sprintf(l2, "min:   %d", date.minute() + value[page]); break;
    }
  }

  
  // --- MENU 2 : LECTURE EEPROM (DÉTAILLÉ) ---
  if (menu == 2) {
    byte eeprom_value = EEPROM.read(page);
    strncpy_P(l1, PSTR("EEPROM param.:"),16);
    byte total_val = eeprom_value + value[page];
    switch (page) {
      case 0:  sprintf(l2, "Magic Nb: %d", total_val); break;
      case 1:  sprintf(l2, "DoorState: %d", total_val); break;
      case 2:  sprintf(l2, "Door size: %d", total_val); break;
      case 3:  sprintf(l2, "warn stat: %d", total_val); break;
      case 4:  sprintf(l2, "err stat: %d", total_val); break;
      case 5:  sprintf(l2, "day light: %d", total_val); break; 
      case 6:  sprintf(l2, "night lght: %d", total_val); break;  
      case 7:  sprintf(l2, "nb error: %d", total_val); break;  
      case 8:  sprintf(l2, "shunt MAX: %d", total_val); break; 
      case 9:  sprintf(l2, "shunt MIN: %d", total_val); break; 
      case 10: sprintf(l2, "Jan OPEN: %d", total_val); break; 
      case 11: sprintf(l2, "Jan CLOSE: %d", total_val); break; 
      case 12: sprintf(l2, "Feb OPEN: %d", total_val); break; 
      case 13: sprintf(l2, "Feb CLOSE: %d", total_val); break; 
      case 14: sprintf(l2, "Mar OPEN: %d", total_val); break; 
      case 15: sprintf(l2, "Mar CLOSE: %d", total_val); break; 
      case 16: sprintf(l2, "Apr OPEN: %d", total_val); break; 
      case 17: sprintf(l2, "Apr CLOSE: %d", total_val); break; 
      case 18: sprintf(l2, "May OPEN: %d", total_val); break; 
      case 19: sprintf(l2, "May CLOSD: %d", total_val); break; 
      case 20: sprintf(l2, "Jun OPEN: %d", total_val); break; 
      case 21: sprintf(l2, "Jun CLOSE: %d", total_val); break; 
      case 22: sprintf(l2, "Jul OPEN: %d", total_val); break; 
      case 23: sprintf(l2, "Jul CLOSE: %d", total_val); break; 
      case 24: sprintf(l2, "Aug OPEN: %d", total_val); break; 
      case 25: sprintf(l2, "Aug CLOSE: %d", total_val); break; 
      case 26: sprintf(l2, "Sep OPEN: %d", total_val); break; 
      case 27: sprintf(l2, "Sep CLOSE: %d", total_val); break; 
      case 28: sprintf(l2, "Oct OPEN: %d", total_val); break; 
      case 29: sprintf(l2, "Oct CLOSE: %d", total_val); break; 
      case 30: sprintf(l2, "Nov OPEN: %d", total_val); break; 
      case 31: sprintf(l2, "Nov CLOSE: %d", total_val); break; 
      case 32: sprintf(l2, "Dec OPEN: %d", total_val); break; 
      case 33: sprintf(l2, "Dec CLOSE: %d", total_val); break;
      default: sprintf(l2, "----N/A-----"); break;
    }
    }

  // --- MENU 3 : I/O CHECK ---
  if (menu == 3) {
    strncpy_P(l1, PSTR("I/O check:"),16);
    switch (page) {
      case 0: strncpy_P(l2, PSTR("Green LED !"),16); break;
      case 1: strncpy_P(l2, PSTR("Red LED !"),16); break;
      case 2: strncpy_P(l2, PSTR("Motor-UP"),16); break;
      case 3: strncpy_P(l2, PSTR("Motor-DOWN"),16); break;
      case 4: strncpy_P(l2, PSTR("HallSensor"),16); break;
      case 5: strncpy_P(l2, PSTR("Button"),16); break;
      case 6: strncpy_P(l2, PSTR("LightSensor"),16); break;
      case 7: strncpy_P(l2, PSTR("Temperature"),16); break;
    }
  }

  // --- MENU 4 : DEBUG TEMPS RÉEL ---
  if (menu == 4) {
    switch (page) {
      case 0: 
        sprintf(l1, "tpsMv=%lu", tempsMove); 
        sprintf(l2, "Ht=%d shunt=%d", HAUTEUR_PORTE,shunt); 
        break;  
      case 1: 
        sprintf(l1, "DoorState=%d", doorState); 
        sprintf(l2, "lstMv=%lu", lastDoorMove);     
        break;
      case 2: 
        sprintf(l1, "Lum Matin=%d", MATIN); 
        sprintf(l2, "Lum Soir=%d", SOIR); 
        break;
      case 3: 
        sprintf(l1, "Timing: %02d:%02d", currentHour, date.minute()); 
        sprintf(l2, "OP:%d CL:%d", openingHour, closingHour); 
        break;  
      
        
    }
  }

  // --- MENU 5 : LOGS ERREURS ---
  if (menu == 5) {
    byte base_addr = 40 + (page * 4);
    uint16_t packed; 
    EEPROM.get(base_addr, packed);
    byte h = packed & 0x1F;
    byte j = (packed >> 5) & 0x1F;
    byte m = (packed >> 10) & 0x0F;
    byte error_type = EEPROM.read(base_addr + 2);
    byte info = EEPROM.read(base_addr+3);
    const char* txt;
    switch (error_type) {
    case 1: txt = "shunt: "; break;
    case 2: 
    case 3: 
    case 4:  txt = "shunt: "; break;
    default: txt = "-------"; break;     }
    if (error_type == 0) {
        sprintf(l1, "Log %d", page + 1);
        sprintf(l2, "--- RAS ---");
    } else {
        sprintf(l1, "Log%d: %02d/%02d %02dh", page + 1, j, m, h);
        sprintf(l2, "E%d %s:%d", error_type, txt, info);
    }
  }

  // --- MENU 6 : RESET ---
  if (menu == 6) {
    strncpy_P(l1, PSTR(" --- RESET ---"),16);
    strncpy_P(l2, PSTR("  Confirm ? "),16);
  }

  // --- AFFICHAGE FINAL ---
  lcd.setCursor(0, 0); lcd.print(l1);
  lcd.setCursor(0, 1); lcd.print(l2);
  
  Serial.print(l1); Serial.print(F(" ")); Serial.print(l2);
  Serial.print(F(" menu=")); Serial.print(menu); 
  Serial.print(F(" page=")); Serial.println(page);
  delay(150);
}

void loop() {
  
  bool MS1 = !digitalRead(MS1Pin);
  byte light =map(analogRead(lightPin),1023,0,100,0); 
  static byte previousSec= 99; byte sec=(millis()/1000)%60; if (sec != previousSec) { get_time();  previousSec=sec;   }     // get time from module DS3231
  read_button();                                                                                                            // read button state
  //serial_monitor();
  if (currentHour != openingHour && currentHour != closingHour)  bloque_porte = 0;  // Le bloque_porte s'efface tout seul
  switch (MS1) {                    
    case 1:                                       // --------------- fonctionnement normal -------------------------
      allumage_led();
      action_button_normal();
      if (error !=0 && (((millis()-lastDoorMove)/1000)>DELAY_20m))  { error=0; }   // on laisse une chance au moteur de ré-essayer de l'ouvrir apres 20 minutes
      if ((warning==1) && error==0 ) {                                             // cas normal, fonctionnement du module horraire, pas d'erreur
                    if (doorState==2 && bloque_porte==0 && currentHour==openingHour  )  { bloque_porte=0; MOVE(0); }    // up
                    if (doorState==1 && bloque_porte==0 && currentHour==closingHour  )  { bloque_porte=0; MOVE(1); }    // down
                    }
      if  ((warning==2 or warning==3) && error==0 ) {                                                                                    // si le module horraire est en erreur
                    if (doorState==2 && light> MATIN && ((millis()-lastDoorMove)/1000)>DELAY_2h)  MOVE(0);      // DELAY_2h de 2h
                    if (doorState==1 && light< SOIR  && ((millis()-lastDoorMove)/1000)>DELAY_2h)  MOVE(1);      // DELAY_2h de 2h 
                    }
      break;     
    case 0:                                       // --------------- manual calib -------------------------
      action_button_calib();
      digitalWrite(ledGPin,1);
      break;
      }
    
   serviceBox();            
    
}
