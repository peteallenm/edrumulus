#ifndef GEN_UTILS_H
#define GEN_UTILS_H

#include <stdint.h>
#include <WString.h>
#include <WebServer.h>
extern bool IAmAccessPoint;

bool ConnectToNetwork(uint8_t available_networks, char *Network, char *Password);
void ConnectToWifi(char *SSID, char *Password, char *NetName);

void DEBUG_ADD_VALUES(const float value0,
                      const float value1,
                      const float value2,
                      const float value3);
void DEBUG_START_PLOTTING();
String GET_DEBUG_BUFFER(uint32_t j);
void DEBUG_FINISHED_PLOTTING(void);

typedef struct 
{
  const char *Text;
  bool Editable;
  void  *Value;
  const char  *PrintfChar;
  //void (*SubmitFunc)(struct _WebTableParamType *Param);
  //void  *SubmitData;
  uint16_t Len; /* Length of data type */
} WEBTABLE_ROW;

typedef struct
{
  const char *Name;
  const char *prefix;
 
  uint16_t NumEntries;
  uint32_t UpdateRate;
  uint16_t Columns;
  WEBTABLE_ROW *Rows;
} WEBTABLE; 

void WebTableWriteJson(WEBTABLE *WebTable, WebServer *Serv);
bool WebTableProcessSet(WEBTABLE *WebTable, WebServer *Serv);

#endif
