/**
 * 7 Segment Clock for ESP8266
 * 
 * 2015-12-26 Florian Knodt · www.adlerweb.info
 * 
 * NTP-Client based on PD-code by Michael Margolis, Tom Igoe and Ivan Grokhotkov
 * 
 */

/** Includes **/
    #include <ESP8266WiFi.h>
    #include <WiFiUdp.h>
    #include <WiFiClient.h>
    #include <ESP8266WebServer.h>
    #include <ESP8266mDNS.h>
    #include <Ticker.h>
    #include <EEPROM.h>

/** CONFIG **/
    const byte DATA=12;
    const byte SHCP=14;
    const byte RESET=2;
    const byte STCP=5;
    
    const byte C_CLK=13;
    const byte C_RST=15;
    
    const char ssid[] =  "YOUR_NETWORK";  //  your network SSID (name)
    const char pass[] =  "YOUR_PASSWORD";  //  your network SSID (name)
    const char lssid[] = "SEGMENTCLOCK"; //
    const char lpass[] = "BitBastelei";
    
    const byte DOT=4;

/** Init **/
    
    byte mode = 1;
    
    unsigned long offset_clock = 0;
    unsigned long offset_count = 0;
    unsigned long offset_stop = 0;
    
    int timezone = 0;
    
    byte inbuffer[11];
    byte outbuffer[11];
    byte bufpos=0;
    
    boolean showfrac = true;
    volatile boolean ticker = false;
    volatile boolean ostat = false;
    boolean stopwatch = false;
    boolean dontp = true;
    boolean dontpm = true;
    boolean softap = false;
    
    Ticker systick;

    ESP8266WebServer server(80);

/** Buffer handling **/
    /* There are two buffers for all segments:
     *  - inbuffer prepared incoming data for later use
     *  - outbuffer contains data currently displayed
     */

    //Clear all buffers
    void buf_clear() {
      for(byte i=0; i<11; i++) {
        inbuffer[i] = 0;
        outbuffer[i] = 0;
      }
    }

    //Clear incoming buffer
    void buf_iclear() {
      for(byte i=0; i<11; i++) {
        inbuffer[i] = 0;
      }
    }

    // Clear output buffer (=clear display)
    void buf_oclear() {
      for(byte i=0; i<11; i++) {
        outbuffer[i] = 0;
      }
    }

/** WIFI Manager **/

    /* Try to connect to an existing AP. If no connection
     * can be established after 10 seconds the function exists
     * and reconfigures the settings for SoftAP (but doesn't
     * activate it)
     */
    void wifi_trymanaged() {
      unsigned int maxtime = 10000;
    
      //WiFi.mode(WIFI_OFF);
      //WiFi.disconnect();
      //WiFi.softAPdisconnect();
      
      // We start by connecting to a WiFi network
      Serial.print(F("Connecting to "));
      Serial.println(ssid);
        
      //WiFi.mode(WIFI_STA);
      WiFi.begin(ssid, pass);
    
      while (WiFi.status() != WL_CONNECTED && maxtime > 0) {
        seg_update();
        delay(1);
        maxtime--;
      }
      if(maxtime == 0) {
        Serial.println(F("ERROR"));
        WiFi.mode(WIFI_OFF);
        softap = true;
        return;
      }
    
      Serial.println(F("WiFi connected - IP:"));
      Serial.println(WiFi.localIP());
    }

    /* Create a virtual access point for local control */
    void wifi_softap() {
      // We start by connecting to a WiFi network
      //WiFi.mode(WIFI_OFF);
      //WiFi.disconnect();
      //WiFi.softAPdisconnect();
      Serial.print(F("Starting local AP with SSID "));
      Serial.println(lssid);
      WiFi.softAP(lssid, lpass);
    
      Serial.println(F("WiFi connected - IP:"));
      Serial.println(WiFi.softAPIP());
    
      softap = true;
    }

/** NTP Handling **/
    IPAddress timeServerIP; // time.nist.gov NTP server address
    const char* ntpServerName = "de.pool.ntp.org";
    
    const byte NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
    unsigned int ntpLocalPort = 2390;
    byte packetBuffer[ NTP_PACKET_SIZE]; // buffer to hold incoming and outgoing packets
    
    WiFiUDP udp; // A UDP instance to let us send and receive packets over UDP
    
    unsigned long wifi_ntpSend(IPAddress& address)
    {
      Serial.println("sending NTP packet...");
      // set all bytes in the buffer to 0
      memset(packetBuffer, 0, NTP_PACKET_SIZE);
      // Initialize values needed to form NTP request
      // (see URL above for details on the packets)
      packetBuffer[0] = 0b11100011;   // LI, Version, Mode
      packetBuffer[1] = 0;     // Stratum, or type of clock
      packetBuffer[2] = 6;     // Polling Interval
      packetBuffer[3] = 0xEC;  // Peer Clock Precision
      // 8 bytes of zero for Root Delay & Root Dispersion
      packetBuffer[12]  = 49;
      packetBuffer[13]  = 0x4E;
      packetBuffer[14]  = 49;
      packetBuffer[15]  = 52;
    
      // all NTP fields have been given values, now
      // you can send a packet requesting a timestamp:
      udp.beginPacket(address, 123); //NTP requests are to port 123
      udp.write(packetBuffer, NTP_PACKET_SIZE);
      udp.endPacket();

      dontp = false;
      dontpm = false;
    }
    
    void wifi_ntp() {
      if(dontpm) {
        buf_oclear();
        outbuffer[0] = 171;
        outbuffer[1] = 216;
        outbuffer[2] = 155;
        seg_update();
      }
    
      WiFi.hostByName(ntpServerName, timeServerIP);
      wifi_ntpSend(timeServerIP); // send an NTP packet to a time server
    
      seg_delay(2000);
      int cb = udp.parsePacket();
      if (!cb) {
        Serial.println(F("no packet"));
        outbuffer[7] = 218;
        outbuffer[8] = 144;
        outbuffer[9] = 144;
        seg_delay(1000);
        return;
      }else{
        Serial.print("packet received, length=");
        Serial.println(cb);
        // We've received a packet, read the data from it
        udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
    
        //the timestamp starts at byte 40 of the received packet and is four bytes,
        // or two words, long. First, esxtract the two words:
    
        unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
        unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
        // combine the four bytes (two words) into a long integer
        // this is NTP time (seconds since Jan 1 1900):
        unsigned long temptime = highWord << 16 | lowWord;
        Serial.print("Seconds since Jan 1 1900 = " );
        Serial.println(temptime);
    
        temptime += timezone; //Zeitzone einrechnen
        temptime %= 86400; //Tage rauslöschen
    
        unsigned long tempmillis = millis();
        tempmillis %= 86400000; //Tage filtern
        tempmillis /= 1000;
    
        if(tempmillis > temptime) temptime += 86400;
    
        unsigned long newoffset = temptime - tempmillis;

        int diff = 0;
        if(offset_clock != 0) diff = offset_clock - newoffset;

        offset_clock = (temptime - tempmillis) * 1000;

        if((long)offset_count+diff > 0) offset_count += diff;
        
        Serial.print(F("Millis: "));
        Serial.print(millis());
        Serial.print(F("Diff: "));
        Serial.print(diff);
        Serial.print(F("NTP offset: "));
        Serial.println(offset_clock);
      }
    }

/** Mode Switching **/
    String mode_0() {
      buf_oclear();
      mode = 0;
      return F("Switched to manual mode");
    }
    
    String mode_1() {
      buf_oclear();
      mode = 1;
      return F("Switched to time mode");
    }
    
    String mode_2() {
      buf_oclear();
      mode = 2;
      return F("Switched to countdown mode");
    }
    
    String mode_3() {
      buf_oclear();
      mode = 3;
      return F("Switched to stopwatch mode");
    }

/** HTTP Server **/
    
    /** SVG GENERATOR **/
        
        void httpSvg() {
          String out = "";
          unsigned int state=server.arg("i").toInt();
        
          out += F("<svg width=\"60\" height=\"100\" xmlns=\"http://www.w3.org/2000/svg\">");
          if(state & 1) {
            out += F("<line stroke=\"black\" ");
            out += F("x1=\"45\" y1=\"10\" x2=\"45\" y2=\"40\" />");
          }
          if(state & 2) {
            out += F("<line stroke=\"black\" ");
            out += F("x1=\"10\" y1=\"5\" x2=\"40\" y2=\"5\" />");
          }
          if(state & 4) { //DOT
            //out += F("<line stroke=\"black\" ");
            out += F("<ellipse fill=\"black\" cx=\"55\" cy=\"95\" rx=\"5\" ry=\"5\" />");
          }
          if(state & 8) {
            out += F("<line stroke=\"black\" ");
            out += F("x1=\"5\" y1=\"10\" x2=\"5\" y2=\"40\" />");
          }
          if(state & 16) {
            out += F("<line stroke=\"black\" ");
            out += F("x1=\"10\" y1=\"50\" x2=\"40\" y2=\"50\" />");
          }
          if(state & 32) {
            out += F("<line stroke=\"black\" ");
            out += F("x1=\"45\" y1=\"60\" x2=\"45\" y2=\"90\" />");
          }
          if(state & 64) {
            out += F("<line stroke=\"black\" ");
            out += F("x1=\"10\" y1=\"95\" x2=\"40\" y2=\"95\" />");
          }
          if(state & 128) {
            out += F("<line stroke=\"black\" ");
            out += F("x1=\"5\" y1=\"60\" x2=\"5\" y2=\"90\" />");
          }
          out += F("</svg>");
        
          server.send ( 200, "image/svg+xml", out);
        }

    // CSS split out due to String length restriction
    void httpCSS() {
      server.send ( 200, "text/css", F("@import url(\"http://fonts.googleapis.com/css?family=Source+Sans+Pro:600,900\");\
            \
            body {\
                font-family: 'Source Sans Pro', sans-serif;\
                background: #ddd;\
                font-size: 115%;\
                line-height: 1.5em;\
                color: #777;\
            }\
            \
            h1,\
            h2,\
            h3 {\
                color: #888;\
            }\
            \
            h2 {\
                font-size: 1.5em;\
            }\
            h3 {\
                text-transform: uppercase;\
                font-size: 0.9em;\
                font-weight: bolder;\
            }\
            \
            a {\
                text-decoration: none;\
                border-bottom: dotted 1px;\
                color: inherit;\
            }\
            \
            a:hover {\
                border-bottom-color: transparent;\
                color: #5FCEC0\
            }\
            \
            .box {\
                background-color: #bbb;\
                border: 1px solid #888\
            }\
            \
            @media (min-width: 600px) {\
                .box {\
                    float: left;\
                    width: 50%;\
                    padding: 0 20px;\
                    box-sizing: border-box;\
                }\
                \
            }"));
    }

    // JS split out due to String length restriction
    void httpJS() {
      server.send ( 200, "application/javascript", F("function loadDoc() {\
                var xhttp = new XMLHttpRequest();\
                xhttp.onreadystatechange = function() {\
                    if (xhttp.readyState == 4 && xhttp.status == 200) {\
                        var info = xhttp.responseText.split(\"\\n\");\
                        document.getElementById(\"O\").innerHTML = '';\
                        document.getElementById(\"I\").innerHTML = '';\
                        document.getElementById(\"mode\").innerHTML = info[0];\
                        document.getElementById(\"t\").value = info[1];\
                        document.getElementById(\"c\").value = info[2];\
                        document.getElementById(\"z\").value = info[3];\
                        var svg1 = info[4].split(' ');\
                        var svg2 = info[5].split(' ');\
                        for(var i=0; i<10; i++) {\
                            var tmp = document.createElement(\"img\");\
                            tmp.setAttribute('src', '/svg?i='+svg1[i]);\
                            tmp.setAttribute('height', '100');\
                            tmp.setAttribute('width', '60');\
                            document.getElementById(\"O\").appendChild(tmp);\
                            \
                            tmp = document.createElement(\"img\");\
                            tmp.setAttribute('src', '/svg?i='+svg2[i]);\
                            tmp.setAttribute('height', '100');\
                            tmp.setAttribute('width', '60');\
                            document.getElementById(\"I\").appendChild(tmp);\
                        }\
                    }\
                };\
                xhttp.open(\"GET\", \"data\", true);\
                xhttp.send();\
            }"));
    }

    // Main page @todo just a dummy ATM
    void httpIndex() {
      
      server.send ( 200, "text/html", F("<html> <head> <title>ESP8266 Clock</title> <link rel=\"stylesheet\" href=\"css.css\"> <script src=\"js.js\"></script> </head> <body onload=\"loadDoc();\"> <h1>ESPClock</h1> <h2>Current:</h2> <h3>Display:</h3> <span id=\"O\">Loading...</span> <h3>Register:</h3> <span id=\"I\">Loading...</span> <hr> <div class=\"box\"> <h2>Mode:<span id=\"mode\">N/A</span></h2> <a href=\"m?m=0\">0:Manual</a><br> <a href=\"m?m=1\">1:Clock</a><br> <a href=\"m?m=2\">2:CntDn</a><br> <a href=\"m?m=3\">3:StopW</a> </div> <div class=\"box\"> <h2>Manual</h2> <a href=\"h\">Hundredth off</a><br> <a href=\"H\">Hundredth on</a><br> <a href=\"F\">FLUSH</a><br> <a href=\"n\">NTP</a><br> ASCII:<form method=\"GET\" action=\"a\"> <input type=\"text\" name=\"a\"> <input type=\"submit\" value=\"S\"> </form> RAW:<form method=\"GET\" action=\"r\"> <input type=\"text\" name=\"r\"> <input type=\"submit\" value=\"S\"> </form> </div> <div class=\"box\"> <h2>Offsets</h2> <h3>Time<h3> <form method=\"GET\" action=\"t\"> <input type=\"text\" name=\"t\" id=\"t\"> <input type=\"submit\" value=\"S\"> </form> <h3>Countdown<h3> <form method=\"GET\" action=\"c\"> <input type=\"text\" name=\"c\" id=\"c\"> <input type=\"submit\" value=\"S\"> </form> <h3>Timezone<h3> <form method=\"GET\" action=\"z\"> <input type=\"text\" name=\"z\" id=\"z\"> <input type=\"submit\" value=\"S\"> </form> </div> <div class=\"box\"> <h2>Stopwatch</h2> <a href=\"s\">STOP</a><br> <a href=\"S\">START</a><br> <a href=\"C\">CLEAR</a><br> </div> <div class=\"box\"> <a href=\"M\">Managed Mode</a><br> <a href=\"A\">SoftAP Mode</a> </div> </body></html>"));
    }

    // Switch mode - requested mode as /m?m=0
    void httpMode() {
      String out = "";
      switch(server.arg("m").toInt()) {
        case 0:
          out = mode_0();
          break;
        case 1:
          out = mode_1();
          break;
        case 2:
          out = mode_2();
          break;
        case 3:
          out = mode_3();
          break;
        default:
          out = F("ERROR");
      }
    
      server.send ( 200, "text/html", out);
    }
    
    // Request buffer flush - output: new screen content as decimal string
    void httpFlush() {
      String out = "";
      out += "Output:";
      for(byte i=0; i<11; i++) {
        outbuffer[i] = inbuffer[i];
        out += " ";
        out += String(outbuffer[i], 10);
      }
      bufpos=0;
      buf_iclear();
    
      server.send ( 200, "text/html", out);
    }

    /** HTTP Stopwatch **/
        void httpSwStop() {
          stopwatch = false;
          server.send ( 200, "text/html",F("Stopwatch stopped"));
        }
        
        void httpSwStart() {
          stopwatch = true;
          server.send ( 200, "text/html",F("Stopwatch started"));
        }
        
        void httpSwClear() {
          stopwatch = false;
          offset_stop = 0;
          server.send ( 200, "text/html", F("Stopwatch cleared"));
        }

    /** HTTP Fraction on/off **/
        void httpFracOn() {
          String out = "";
          showfrac = true;
          out += F("Switched fractions to ");
          out += "on";
        
          server.send ( 200, "text/html", out);
        }
        
        void httpFracOff() {
          String out = "";
          showfrac = false;
          out += F("Switched fractions to ");
          out += "off";
        
          server.send ( 200, "text/html", out);
        }

    /** NTP Request **/
        void httpNtp() {
          dontp = true;
          dontpm = true;
          server.send ( 200, "text/html", F("Requested"));
        }

    /** Time Offset **/
        void httpOffsetTime() {
          offset_clock = server.arg("t").toInt() - millis();;
          
          server.send ( 200, "text/html", F("Offset changed"));
        }

    /** Countdown Offset **/
        void httpOffsetCount() {
          offset_count = server.arg("c").toInt()  + millis();
          
          server.send ( 200, "text/html", F("Offset changed"));
        }

    /** Timezone Offset **/
        void httpOffsetTimezone() {
          timezone = server.arg("z").toInt();
          
          EEPROM.put(0, timezone);
          EEPROM.commit();
          
          server.send ( 200, "text/html", F("Offset changed"));
        }

    /** ASCII Add **/
        void httpAddASCII() {
          String input = server.arg("a");
          byte tc;

          if(input.length() > 12) { //10 Items + CRNL
            server.send ( 200, "text/html", F("Input overflow"));
            return;
          }
          
          for(byte i=0; i<input.length(); i++) {
            tc = input.charAt(i);
            if(tc == 10 || tc == 13) break; //Abort on newline

            if(tc == '.' && bufpos > 0) {
              bufpos--;
              inbuffer[bufpos] += DOT;
            }else{
              inbuffer[bufpos] = seg_chr(tc);
            }
            bufpos++;

            if(bufpos > 10) {
              server.send ( 200, "text/html", F("Buffer overflow"));
              bufpos=0;
              buf_iclear();
            }
          }
          
          server.send ( 200, "text/html", F("Input added"));
        }

    /** RAW Add **/
        void httpAddRAW() {
          inbuffer[bufpos] = server.arg("r").toInt();
          bufpos++;

          if(bufpos > 10) {
            server.send ( 200, "text/html", F("Buffer overflow"));
            bufpos=0;
            buf_iclear();
          }
          
          server.send ( 200, "text/html", F("Input added"));
        }

    /** WiFi to Managed **/
        void httpWifiManaged() {
          wifi_trymanaged();
          server.send ( 200, "text/html", F("Requested"));
        }

    /** WiFi to SoftAP **/
        void httpWifiSoft() {
          wifi_softap();
          server.send ( 200, "text/html", F("Requested"));
        }

    /** Variable data dump **/
        void httpData() {
          char temp[192];
          snprintf ( temp, 192, "%d\n%d\n%d\n%d\n%d %d %d %d %d %d %d %d %d %d\n%d %d %d %d %d %d %d %d %d %d\n", mode, offset_clock, offset_count, timezone, outbuffer[0], outbuffer[1], outbuffer[2], outbuffer[3], outbuffer[4], outbuffer[5], outbuffer[6], outbuffer[7], outbuffer[8], outbuffer[9], inbuffer[0], inbuffer[1], inbuffer[2], inbuffer[3], inbuffer[4], inbuffer[5], inbuffer[6], inbuffer[7], inbuffer[8], inbuffer[9]);
          server.send ( 200, "text/html", temp );
        }
        
/** Intterupt handling systicks **/
    void doSystick() {
      ticker = true;
    }

    void doOstat() {
      ostat = true;
    }
        
/** Intterupt handling ntpresync **/
    void doNtp() {
      if(!softap) dontp=true; 
    }
        
/** Intterupt handling systicks **/
    void doStats() {
      Serial.print("M:");
      Serial.print(mode);
      Serial.print(" - Out: ");
      for(byte i=0; i<10; i++) {
        Serial.print(outbuffer[i]);
        Serial.print(" ");
      }
      Serial.println();
    }

/** Bit banging seven Segment control */
    
    // Reset decade counter (= Fist segment active)
    void cnt_reset() {
      digitalWrite(C_RST, LOW);
      digitalWrite(C_RST, HIGH);
      digitalWrite(C_RST, LOW);
    }

    // Initialize decade counter
    void cnt_init() {
      digitalWrite(C_CLK, LOW);
      cnt_reset();
    }

    // Clock decade counter - next output will be activated
    void cnt_clk() {
      digitalWrite(C_CLK, LOW);
      digitalWrite(C_CLK, HIGH);
      digitalWrite(C_CLK, LOW);
    }

    // Translate ASCII-character to binary pattern
    byte seg_chr(byte ascii) {
      switch(ascii) {
        case '0': case 0:     return 235;
        case '1': case 1:     return 33;
        case '2': case 2:     return 211;
        case '3': case 3:     return 115;
        case '4': case 4:     return 57;
        case '5': case 5:     return 122;
        case '6': case 6:     return 250;
        case '7': case 7:     return 35;
        case '8': case 8:     return 251;
        case '9': case 9:     return 123;
        case ' ':             return 0;
        case '-':             return 16;
        case '_':             return 64;
        case ',':             return 128;
        case '\'':            return 8;
        case '`':             return 1;
        case '.':             return 4;
        case '"':             return 9;
        case '=':             return 80;
        case 167:             return 27;  //°
        case 230:             return 153; //µ
        case '|':             return 136;
        case 'a': case 'A':   return 187;
        case 'b': case 'B':   return 248;
        case 'c':             return 208;
        case 'C':             return 202;
        case 'd': case 'D':   return 241;
        case 'e':             return 219;
        case 'E':             return 218;
        case 'f': case 'F':   return 154;
        case 'g': case 'G':   return 250;
        case 'h':             return 184;
        case 'H':             return 185;
        case 'i':             return 128;
        case 'I':             return 136;
        case 'j': case 'J':   return 225;
        case 'l':             return 33;
        case 'L':             return 200;
        case 'n':             return 176;
        case 'N':             return 171;
        case 'o':             return 240;
        case 'O':             return 235;
        case 'p': case 'P':   return 155;
        case 'q': case 'Q':   return 59;
        case 'r': case 'R':   return 144;
        case 's': case 'S':   return 122;
        case 't': case 'T':   return 216;
        case 'u':             return 224;
        case 'U':             return 233;
        case 'y': case 'Y':   return 57;
        case '?':             return 147;
    
        default:
          Serial.print("Unknown character: ");
          Serial.println(ascii);
          return 0;
      }
    }

    // Reset shift register to all zero (no latch!)
    void seg_reset() {
      digitalWrite(RESET, HIGH);
      digitalWrite(RESET, LOW);
      digitalWrite(RESET, HIGH);
    }

    // Prepare shift register for use
    void seg_init() {
      digitalWrite(STCP, LOW);
      digitalWrite(SHCP, LOW);
      digitalWrite(DATA, LOW);
      seg_reset();
    }

    // Write data to shift register
    void seg_set(byte data) {
      digitalWrite(STCP, LOW);
      shiftOut(DATA, SHCP, MSBFIRST, data);
      digitalWrite(STCP, HIGH);
      digitalWrite(STCP, LOW);
    }

    // Pulse all segments once for 50µs each
    void seg_update() {
      seg_reset();
      seg_set(0);
      cnt_reset();
    
      for(byte i=0; i<10; i++) {
        seg_set(outbuffer[i]);
        delayMicroseconds(150);
        seg_set(0);
        delayMicroseconds(25);
        cnt_clk();
      }
    }

    // wait for defined number of milliseconds but keep updating segments and handling WiFi/HTTP
    void seg_delay(unsigned int dly) {
      unsigned long target = millis() + dly;
      while(millis() < target) {
        seg_update();
        server.handleClient();
        delay(1);
      }
    }

/** Clock Logic **/
    void proc_clock() {
      unsigned long temp = millis() + offset_clock;

      if(ostat) Serial.print("OTime=");
      if(ostat) Serial.print(temp);
      if(ostat) Serial.print(" D=");
    
      unsigned int days = temp / 86400000;
      temp = temp % 86400000;
      
      if(ostat) Serial.print(days);
      if(ostat) Serial.print(" H=");
      
      byte hours = temp / 3600000;
      temp = temp % 3600000;
      
      if(ostat) Serial.print(hours);
      if(ostat) Serial.print(" M=");
    
      byte minutes = temp / 60000;
      temp = temp % 60000;
      
      if(ostat) Serial.print(minutes);
      if(ostat) Serial.print(" S=");
    
      byte seconds = temp / 1000;
      temp = temp % 1000;
      
      if(ostat) Serial.print(seconds);
      if(ostat) Serial.print(" h=");
    
      byte frac = temp / 100;
      
      if(ostat) Serial.print(frac);
  
      /*
      This would calculate days since boot or something. Since
      7 segment display can not really handy weekdays it's somewhat
      useless for now
      outbuffer[0] = seg_chr((byte)(days / 100));
      outbuffer[1] = seg_chr((byte)((days % 100) / 10));
      outbuffer[2] = seg_chr((byte)(days % 10)) + DOT;*/
    
      outbuffer[0] = 0;
      outbuffer[1] = 0;
      outbuffer[2] = 0;
    
      outbuffer[3] = seg_chr((byte)(hours / 10));
      outbuffer[4] = seg_chr((byte)(hours % 10)) + DOT;
    
      outbuffer[5] = seg_chr((byte)(minutes / 10));
      outbuffer[6] = seg_chr((byte)(minutes % 10)) + DOT;
    
      outbuffer[7] = seg_chr((byte)(seconds / 10));
      outbuffer[8] = seg_chr((byte)(seconds % 10));
    
      if(showfrac) {
        outbuffer[8] += DOT;
        outbuffer[9] = seg_chr((byte)(frac));
      }else{
        outbuffer[9] = seg_chr(' ');
      }
    }

/** Countdown logic **/
    void proc_count() {
      if(offset_count <= millis()) { // Target reached, everything is zero…
        if(ostat) Serial.print("CNT0");
        
        outbuffer[0] = seg_chr('0');
        outbuffer[1] = seg_chr('0');
        outbuffer[2] = seg_chr('0') + DOT;
    
        outbuffer[3] = seg_chr('0');
        outbuffer[4] = seg_chr('0') + DOT;
    
        outbuffer[5] = seg_chr('0');
        outbuffer[6] = seg_chr('0') + DOT;
    
        outbuffer[7] = seg_chr('0');
        outbuffer[8] = seg_chr('0') + DOT;
    
        outbuffer[9] = seg_chr('0');
      }else{
        unsigned long temp = offset_count - millis();

        if(ostat) Serial.print("OTarget=");
        if(ostat) Serial.print(temp);
        if(ostat) Serial.print(" D=");
    
        unsigned int days = temp / 86400000;
        temp = temp % 86400000;
      
        if(ostat) Serial.print(days);
        if(ostat) Serial.print(" H=");
    
        byte hours = temp / 3600000;
        temp = temp % 3600000;
      
        if(ostat) Serial.print(hours);
        if(ostat) Serial.print(" M=");
    
        byte minutes = temp / 60000;
        temp = temp % 60000;
      
        if(ostat) Serial.print(minutes);
        if(ostat) Serial.print(" S=");
    
        byte seconds = temp / 1000;
        temp = temp % 1000;
      
        if(ostat) Serial.print(seconds);
        if(ostat) Serial.print(" h=");
    
        byte frac = temp / 100;
      
        if(ostat) Serial.print(frac);
    
        outbuffer[0] = seg_chr((byte)(days / 100));
        outbuffer[1] = seg_chr((byte)((days % 100) / 10));
        outbuffer[2] = seg_chr((byte)(days % 10)) + DOT;
    
        outbuffer[3] = seg_chr((byte)(hours / 10));
        outbuffer[4] = seg_chr((byte)(hours % 10)) + DOT;
    
        outbuffer[5] = seg_chr((byte)(minutes / 10));
        outbuffer[6] = seg_chr((byte)(minutes % 10)) + DOT;
    
        outbuffer[7] = seg_chr((byte)(seconds / 10));
        outbuffer[8] = seg_chr((byte)(seconds % 10));
    
        if(showfrac) {
          outbuffer[8] += DOT;
          outbuffer[9] = seg_chr((byte)(frac));
        }else{
          outbuffer[9] = seg_chr(' ');
        }
      }
    }

/** Stopwatch logic **/
    void proc_stop() {
      if(stopwatch) offset_stop++;
      unsigned long temp = offset_stop;

      if(ostat) Serial.print("OStop=");
      if(ostat) Serial.print(temp);
      if(ostat) Serial.print(" D=");
    
      unsigned int days = temp / 864000;
      temp = temp % 864000;
      
      if(ostat) Serial.print(days);
      if(ostat) Serial.print(" H=");  
    
      byte hours = temp / 36000;
      temp = temp % 36000;
      
      if(ostat) Serial.print(hours);
      if(ostat) Serial.print(" M=");
    
      byte minutes = temp / 600;
      temp = temp % 600;
      
      if(ostat) Serial.print(minutes);
      if(ostat) Serial.print(" S=");
    
      byte seconds = temp / 10;
      temp = temp % 10;
      
      if(ostat) Serial.print(seconds);
      if(ostat) Serial.print(" h=");
    
      byte frac = temp;
      
      if(ostat) Serial.print(frac);
    
      outbuffer[0] = seg_chr((byte)(days / 100));
      outbuffer[1] = seg_chr((byte)((days % 100) / 10));
      outbuffer[2] = seg_chr((byte)(days % 10)) + DOT;
    
      outbuffer[0] = 0;
      outbuffer[1] = 0;
      outbuffer[2] = 0;
    
      outbuffer[3] = seg_chr((byte)(hours / 10));
      outbuffer[4] = seg_chr((byte)(hours % 10)) + DOT;
    
      outbuffer[5] = seg_chr((byte)(minutes / 10));
      outbuffer[6] = seg_chr((byte)(minutes % 10)) + DOT;
    
      outbuffer[7] = seg_chr((byte)(seconds / 10));
      outbuffer[8] = seg_chr((byte)(seconds % 10));
    
      if(showfrac) {
        outbuffer[8] += DOT;
        outbuffer[9] = seg_chr((byte)(frac));
      }else{
        outbuffer[9] = seg_chr(' ');
      }
    }

/** Incoming serial data processing **/
    void proc_serial() {
      if(!Serial.available()) return;
      
      byte incoming = Serial.read();
    
      switch(incoming) {
        case '!':
        case 'F':
        {
          Serial.print("Output:");
          for(byte i=0; i<11; i++) {
            outbuffer[i] = inbuffer[i];
            Serial.print(" 0x");
            Serial.print(outbuffer[i], HEX);
          }
          bufpos=0;
          buf_iclear();
          Serial.println();
        }
        break;
    
        case '0':
          Serial.println(mode_0());
        break;
        case '1':
          Serial.println(mode_1());
        break;
        case '2':
          Serial.println(mode_2());
        break;
        case '3':
          Serial.println(mode_3());
        break;
    
        case 'a':
          if(incoming == '.' && bufpos > 0) {
            bufpos--;
            inbuffer[bufpos] += DOT;
          }else{
            inbuffer[bufpos] = seg_chr(Serial.read());
            Serial.print(F("Added: 0x"));
            Serial.println(inbuffer[bufpos], HEX);
          }
          bufpos++;
        break;
        case 'r':
          inbuffer[bufpos] = Serial.parseInt();;
          Serial.print(F("Added: 0x"));
          Serial.println(inbuffer[bufpos], HEX);
        break;
    
        case 't':
        {
          unsigned long offset_temp = 0;
          Serial.print(F("New offset? "));
          Serial.println(F("µs since midnight"));
          offset_temp = Serial.parseInt();
          offset_clock = offset_temp - millis();
        }
        break;
        case 'c':
        {
          unsigned long offset_temp = 0;
          Serial.print(F("New offset? "));
          Serial.println(F("µs to target time"));
          offset_temp = Serial.parseInt();
          offset_count = offset_temp + millis();
        }
        break;
        case 'z':
        {
          Serial.println(F("New timezone difference (minutes)? "));
          timezone = Serial.parseInt();
          EEPROM.put(0, timezone);
          EEPROM.commit();
        }
        break;
    
        case 's':
          stopwatch = false;
          Serial.print(F("Stopwatch stopped"));
        break;
        case 'S':
          stopwatch = true;
          Serial.print(F("Stopwatch started"));
        break;
        case 'C':
          stopwatch = false;
          offset_stop = 0;
          Serial.print(F("Stopwatch cleared"));
        break;
    
        case 'H':
          showfrac = true;
          Serial.print(F("Switched fractions to "));
          Serial.println(showfrac, HEX);
        break;
        case 'h':
          showfrac = false;
          Serial.print(F("Switched fractions to "));
          Serial.println(showfrac, HEX);
        break;
    
        case 'n':
          dontp = true;
          dontpm = true;
          Serial.println(F("NTP requested"));
        break;

        case 'M':
          wifi_trymanaged();
          Serial.println(F("Managed Mode requested"));
        break;

        case 'A':
          wifi_softap();
          Serial.println(F("SoftAP requested"));
        break;
    
        case 'i':
          if(softap) {
            Serial.println(F("Local AP Mode"));
          }else{
            Serial.println(F("Managed Mode"));
          }
          Serial.println(WiFi.localIP());
        break;
      }
    
      if(bufpos > 10) {
        Serial.println(F("Buffer overflow"));
    
        bufpos=0;
        buf_iclear();
      }
    }

void setup() {

  pinMode(DATA, OUTPUT);
  pinMode(SHCP, OUTPUT);
  pinMode(STCP, OUTPUT);
  pinMode(RESET, OUTPUT);

  pinMode(C_CLK, OUTPUT);
  pinMode(C_RST, OUTPUT);

  buf_clear();

  seg_init();
  cnt_init();

  Serial.begin(115200);
  Serial.print("BOOT");
  Serial.setDebugOutput(true);

  //BOOT
  outbuffer[0] = 248;
  outbuffer[1] = 240;
  outbuffer[2] = 240;
  outbuffer[3] = 216;

  EEPROM.begin(4); //Int should have 2 bytes, 4 is minimum so sizeof(int) may not work
  EEPROM.get(0, timezone); //Load timezone setting from EEPROM

  Serial.print(".");

  MDNS.begin ( "SEG_CLOCK" );
  Serial.print(".");
  
  server.on ( "/",    httpIndex );

  server.on ( "/css.css",  httpCSS );
  server.on ( "/js.js",    httpJS );
  
  server.on ( "/svg", httpSvg );
  
  server.on ( "/m",   httpMode );
  
  server.on ( "/F",   httpFlush );
  
  server.on ( "/s",   httpSwStop );
  server.on ( "/S",   httpSwStart );
  server.on ( "/C",   httpSwClear );
  
  server.on ( "/H",   httpFracOn );
  server.on ( "/h",   httpFracOff );
  
  server.on ( "/n",   httpNtp );
  
  server.on ( "/t",   httpOffsetTime );
  server.on ( "/c",   httpOffsetCount );
  server.on ( "/z",   httpOffsetTimezone );
  
  server.on ( "/a",   httpAddASCII );
  server.on ( "/r",   httpAddRAW );
  
  server.on ( "/M",   httpWifiManaged );
  server.on ( "/A",   httpWifiSoft );

  server.on ( "/data",   httpData );

  server.begin();

  Serial.println(".");

  seg_delay(1000);

  //CONNECT
  outbuffer[0]  = 202;
  outbuffer[1]  = 235;
  outbuffer[2]  = 171;
  outbuffer[3]  = 171;
  outbuffer[4]  = 218;
  outbuffer[5]  = 202;
  outbuffer[6]  = 216;

  wifi_trymanaged();
  if(softap) wifi_softap();

  Serial.println("Starting UDP");
  udp.begin(ntpLocalPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  //System tick every 100ms
  systick.attach(0.1, doSystick);

  //Output status every 2s
  systick.attach(2, doOstat);

  //NTP resync every 
  systick.attach(10800, doNtp);

  //Statistics via RS232 every 10s
  systick.attach(10, doStats);
}

unsigned long last_t = 0;
unsigned long last_p = 0;
unsigned long last_n = 0;

void loop() {
  //Workarround - der Ticker-Mist läuft nicht mit globalen variabeln?! :/
  if(last_t < millis()) {
    ticker = true;
    last_t = millis() + 100;
  }

  if(!softap && last_p < millis()) {
    ostat = true;
    last_p = millis() + 2000;
  }

  if(!softap && last_n < millis()) {
    dontp = true;
    last_n = millis() + 108000000UL;
  }
  
  if(ticker) {
    boolean postat = ostat;
    switch (mode) {
      case 0:
        if(postat) Serial.print("M0 ");
        break;
      case 1:        
        if(postat) Serial.print("M1 ");
        proc_clock();
        break;
      case 2:
        if(postat) Serial.print("M2 ");
        proc_count();
        break;
      case 3:
        if(postat) Serial.print("M3 ");
        proc_stop();
        break;
      default:
        Serial.print("Mode Error");
    }
    if(postat) ostat = false;
    ticker = false;
    if(postat) Serial.println();
  }

  if(!softap && dontp) wifi_ntp();

  seg_update();
  server.handleClient();
  proc_serial();
}
