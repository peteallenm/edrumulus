#include <EEPROM.h>


#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>

#include "GenUtils.h"
#include "common.h"

  bool IAmAccessPoint = false;
  extern WebServer server;

  // real-time debugging support
#ifdef USE_SERIAL_DEBUG_PLOTTING
#  ifdef TEENSYDUINO // MIDI+Serial possible with the Teensy
  static const int debug_buffer_size = 500;
#  else
//#    undef USE_MIDI // only MIDI or Serial possible with the ESP32
  static const int debug_buffer_size = 200; // smaller size needed for ESP32
#  endif
  static const int number_debug_buffers = 4;
  int              debug_buffer_idx     = 0;
  int              debug_out_cnt        = 0;
  float            debug_buffer[number_debug_buffers][debug_buffer_size];

  void DEBUG_ADD_VALUES(const float value0,
                        const float value1,
                        const float value2,
                        const float value3)
  {
    if (debug_out_cnt == 1)
    {
      #if 0
      String serial_print;
      for (int i = debug_buffer_idx; i < debug_buffer_idx + debug_buffer_size; i++)
      {
        for (int j = 0; j < number_debug_buffers; j++)
        {
          serial_print += String(10.0f * log10(max(1e-3f, debug_buffer[j][i % debug_buffer_size]))) + "\t";
        }
        serial_print += "\n";
      }
      Serial.println(serial_print);
      #endif
    }
    else
    {
      debug_buffer[0][debug_buffer_idx] = value0;
      debug_buffer[1][debug_buffer_idx] = value1;
      debug_buffer[2][debug_buffer_idx] = value2;
      debug_buffer[3][debug_buffer_idx] = value3;
      debug_buffer_idx++;

      if (debug_buffer_idx == debug_buffer_size)
      {
        debug_buffer_idx = 0;
      }
    }
    
    if(debug_out_cnt > 1)
    {
      debug_out_cnt--;
    }
  }
  String GET_DEBUG_BUFFER(uint32_t j)
  {
    String Str;
    if(j >= 4)
      j = 0;
    for (int i = debug_buffer_idx; i < debug_buffer_idx + debug_buffer_size; i++)
    {
        Str += String(10.0f * log10(max(1e-3f, debug_buffer[j][i % debug_buffer_size])));
        if(i < (debug_buffer_idx + debug_buffer_size - 1))
          Str += ",";
    }
      
    return Str;
  }

  void DEBUG_FINISHED_PLOTTING(void)
  {
    debug_out_cnt = 0;
  }
  void DEBUG_START_PLOTTING()
  {
    // set debug count to have the peak shortly after the start of the range
    debug_out_cnt = debug_buffer_size - debug_buffer_size / 4;
  }
#else
  void        DEBUG_ADD_VALUES(const float, const float, const float, const float)
  {
  }
  void DEBUG_START_PLOTTING() {}
  String GET_DEBUG_BUFFER(void) { return ""; } 
#endif

uint32_t IRAM_ATTR QuickMicros()
{
  uint32_t ccount;
  asm volatile ( "rsr %0, ccount" : "=a" (ccount) );
  return ccount;
}

bool ConnectToNetwork(uint8_t available_networks, char *Network, char *Password)
{
  bool Found = false;
  int network = 0;
  while((network < available_networks) && !(Found)) {
    if ((WiFi.SSID(network).length() == strlen(Network)) && (WiFi.SSID(network).equals(Network))) {
      Found = true;
    }
    network++;
  }
  if(true == Found)
  {
    Serial.print("Connecting to ");
    Serial.println(Network);
    WiFi.begin(Network, Password);
    //WiFi.setSleepMode(WIFI_NONE_SLEEP);
  }
  return Found;
}

void ConnectToWifi(char *SSID, char *Password, char *NetName)
{
  byte available_networks = WiFi.scanNetworks();
  bool FoundNetwork = false;

  Serial.println("\n\r \nConnecting to WiFi");
  
  // First try the network stored in flash settings
  FoundNetwork = ConnectToNetwork(available_networks, SSID, Password);

  // Finally if that fails we set up an access point so at least we can modify settings.
  if(false == FoundNetwork)
  {
    Serial.println("Starting as access point");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(NetName, NetName);
    IAmAccessPoint = true;
  }
  else
  {    
    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
      delay(200);
      Serial.print(".");
    }
  }
  Serial.println("Done connect");
}

void WebTableWriteJson(WEBTABLE *WebTable, WebServer *Serv)
{ 
  if((NULL != WebTable) && (NULL != WebTable->Rows))
  {
    String Content = "{\n";
    
    Content += "  \"TableName\": \"" + String(WebTable->Name) + "\",\n";
    Content += "  \"UpdateRate\": " + String(WebTable->UpdateRate) + ",\n";
    Content += "  \"Columns\": " + String(WebTable->Columns) + ",\n";
    Content += "  \"Elements\": [\n";
    
    for(uint16_t i = 0; i < WebTable->NumEntries; i++)
    {
      WEBTABLE_ROW *Row = &(WebTable->Rows[i]);
      Content += "         { \"" + String(Row->Text) + "\": [\"";

      // Special case for handling strings
      if('s' == Row->PrintfChar[strlen(Row->PrintfChar) - 1])
      {
        char *Str = (char *)Row->Value;
        Content += String(Str);
      }
      else if('f' == Row->PrintfChar[strlen(Row->PrintfChar) - 1])
      {
        char Str[16] = {'\0'};
        if(Row->Len == sizeof(float))
        {
          float *P = (float *)Row->Value;
          snprintf(Str, sizeof(Str), Row->PrintfChar, *(P));
        }
        else if(Row->Len == 8)
        {
          double *P = (double *)Row->Value;
          snprintf(Str, sizeof(Str), Row->PrintfChar, *(P));
        }
        else
        {
          strcpy(Str, "Invalid float Len");
        }
        Str[sizeof(Str) - 1] = '\0';
        Content += String(Str);
      }
      else
      {
        char Str[16] = {'\0'};
        if(Row->Len == 1)
        {
          char *P = (char *)Row->Value;
          snprintf(Str, sizeof(Str), Row->PrintfChar, *(P));
        }
        else if(Row->Len == 2)
        {
          int16_t *P = (int16_t *)Row->Value;
          snprintf(Str, sizeof(Str), Row->PrintfChar, *(P));
        }
        else if(Row->Len == 4)
        {
          int32_t *P = (int32_t *)Row->Value;
          snprintf(Str, sizeof(Str), Row->PrintfChar, *(P));
        }
        else if(Row->Len == 8)
        {
          int64_t *P = (int64_t *)Row->Value;
          snprintf(Str, sizeof(Str), Row->PrintfChar, *(P));
        }
        else
        {
          strcpy(Str, "Invalid Len");
        }
        Str[sizeof(Str) - 1] = '\0';
        Content += String(Str);
      }
      
      
      Content += "\", \"" + String(WebTable->prefix) + String(i) + "\", \"" + String((int)(Row->Editable)) + "\"] }";
      if(i < (WebTable->NumEntries - 1))
      {
        Content += ",";
      }
      Content += "\n";
    }
    
    Content += "  ]\n";
    Content += "}\n";
    Serv->send(200, "application/json", Content);
  }
  else
  {
    Serv->send(200, "application/json", "WebTable or WebTable->Rows is NULL");
  }
}

bool WebTableProcessSet(WEBTABLE *WebTable, WebServer *Serv)
{
  bool SetSomething = false;
  if((NULL != WebTable) && (NULL != Serv))
  {
    //Serial.println("WebTableProcessSet()");
    for(uint16_t i = 0; i < WebTable->NumEntries; i++)
    {
      if(true == WebTable->Rows[i].Editable)
      {
        char ArgName[20];
        snprintf(ArgName, sizeof(ArgName), "%s%d", WebTable->prefix, i);
        
        if(Serv->hasArg(ArgName))
        {
          SetSomething = true;
          //Serial.print("Found arg ");
          //Serial.println(ArgName);
          
          String ArgText = Serv->arg(ArgName);
          switch(WebTable->Rows[i].PrintfChar[strlen(WebTable->Rows[i].PrintfChar) - 1])
          {
            case('d'):
            {
              if(sizeof(int8_t) == WebTable->Rows[i].Len)
              {
                int8_t *Data = (int8_t *)WebTable->Rows[i].Value;
                *Data = (int8_t)(ArgText.toInt());
              }
              else if(sizeof(int16_t) == WebTable->Rows[i].Len)
              {
                int16_t *Data = (int16_t *)WebTable->Rows[i].Value;
                *Data = (int16_t)(ArgText.toInt());
              }
              else if(sizeof(int32_t) == WebTable->Rows[i].Len)
              {
                int32_t *Data = (int32_t *)WebTable->Rows[i].Value;
                *Data = (int32_t)(ArgText.toInt());
              }
              else if(sizeof(int64_t) == WebTable->Rows[i].Len)
              {
                int64_t *Data =(int64_t *) WebTable->Rows[i].Value;
                *Data = (int64_t)(ArgText.toInt());
              }
              else
              {
                Serial.print("WebTableProcessSet() Invalid integer length ");
                Serial.println(WebTable->Rows[i].Len);
              }
              break;
            }
            case('u'):
            {
              if(sizeof(uint16_t) == WebTable->Rows[i].Len)
              {
                uint16_t *Data = (uint16_t *)WebTable->Rows[i].Value;
                *Data = (uint16_t)(ArgText.toInt());
              }
              else if(sizeof(uint32_t) == WebTable->Rows[i].Len)
              {
                uint32_t *Data = (uint32_t *)WebTable->Rows[i].Value;
                *Data = (uint32_t)(ArgText.toInt());
              }
              else if(sizeof(uint64_t) == WebTable->Rows[i].Len)
              {
                uint64_t *Data = (uint64_t *)WebTable->Rows[i].Value;
                *Data = (uint64_t)(ArgText.toInt());
              }
              else
              {
                Serial.print("WebTableProcessSet() Invalid u_integer length ");
                Serial.println(WebTable->Rows[i].Len);
              }
              break;
            }
            case('x'):
            {
              char Buf[8];
              ArgText.toCharArray(Buf, sizeof(Buf));
              uint64_t Val = strtol(Buf, 0, 16);
              if(sizeof(uint8_t) == WebTable->Rows[i].Len)
              {
                uint8_t *Data = (uint8_t *)WebTable->Rows[i].Value;
                *Data = (uint8_t)Val;
              }
              else if(sizeof(uint16_t) == WebTable->Rows[i].Len)
              {
                uint16_t *Data = (uint16_t *)WebTable->Rows[i].Value;
                *Data = (uint16_t)Val;
              }
              else if(sizeof(int32_t) == WebTable->Rows[i].Len)
              {
                uint32_t *Data = (uint32_t *)WebTable->Rows[i].Value;
                *Data = Val;
              }
              else if(sizeof(int64_t) == WebTable->Rows[i].Len)
              {
                uint64_t *Data =(uint64_t *) WebTable->Rows[i].Value;
                *Data = (uint64_t)Val;
              }
              else
              {
                Serial.print("WebTableProcessSet() Invalid integer length ");
                Serial.println(WebTable->Rows[i].Len);
              }
              break;
            }
            case('c'):
            {
              if(sizeof(char) == WebTable->Rows[i].Len)
              {
                char *Data = (char *)WebTable->Rows[i].Value;
                *Data = (char)(ArgText.charAt(0));
              }
              else
              {
                Serial.print("WebTableProcessSet() Invalid char length ");
                Serial.println(WebTable->Rows[i].Len);
              }
              break;
            }
            case('f'):
            {
              if(sizeof(float) == WebTable->Rows[i].Len)
              {
                float *Data = (float *)WebTable->Rows[i].Value;
                *Data = (float)(ArgText.toFloat());
              }
              else if(sizeof(double) == WebTable->Rows[i].Len)
              {
                double *Data = (double *)WebTable->Rows[i].Value;
                *Data = (double)(ArgText.toFloat());
              }
              else
              {
                Serial.print("WebTableProcessSet() Invalid float length ");
                Serial.println(WebTable->Rows[i].Len);
              }
              break;
            }
            case('s'):
            {
              char *Data = (char *)WebTable->Rows[i].Value;
              ArgText.toCharArray(Data, WebTable->Rows[i].Len);
              break;
            }
            default:
            {
              Serial.print("WebTableProcessSet() Unsupported type ");
              Serial.println(WebTable->Rows[i].PrintfChar);
              break;
            }
          }
          
        }
        else
        {
          //Serial.println("Not found");
        }
      }
    }
  }
  else
  {
    Serial.println("WebTableProcessSet() Invalid WebTable or Serv");
  }
  return SetSomething;
}
