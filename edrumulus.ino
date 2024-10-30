/******************************************************************************\
 * Copyright (c) 2020-2024
 * Author(s): Volker Fischer
 ******************************************************************************
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
\******************************************************************************/


#define USE_MIDI
#define USE_TINYUSB
#define USE_WIFI
// ESP32 default pin definition ("-1" means that this channel is unused):
// For older prototypes or custom implementations, simply change the GPIO numbers in the table below
// to match your hardware (note that the GPIO assignment of Prototype 2 is the same as Prototype 4).
// clang-format off
// analog pins setup:               snare | kick | hi-hat | hi-hat-ctrl | crash | tom1 | ride | tom2 | tom3
static int analog_pins4[]         = {  1,      6,      5,        7,          3,      2,     9,    11,    13 };
static int analog_pins_rimshot4[] = {  4,     -1,      8,       -1,         10,     -1,    12,    -1,    -1 };
// clang-format on

// if you want to use less number of pads, simply adjust number_pads4 value
// const int number_pads4 = sizeof ( analog_pins4 ) / sizeof ( int ); // example: use all inputs defined in analog_pins4
//const int number_pads4 = 8; // example: do not use tom3 and shrink number of pads from 9 to 8
const int number_pads4 = 1; // example: just one single pad

#define MIDI_QUEUE_LEN 5

#ifdef USE_WIFI

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include <SPIFFS.h>
#include <FS.h>
#include "GenUtils.h"

#endif

#include "edrumulus.h"

#ifdef USE_MIDI 
#  ifdef ESP_PLATFORM
#    include <MIDI.h>
#    ifdef USE_TINYUSB
#      include <Adafruit_TinyUSB.h>
      Adafruit_USBD_MIDI usb_midi;
      MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);
#    else
MIDI_CREATE_DEFAULT_INSTANCE();
#    endif
#    define MYMIDI MIDI
#    define MIDI_CONTROL_CHANGE_TYPE midi::ControlChange
#    define MIDI_SEND_AFTER_TOUCH sendAfterTouch
#    define MIDI_SERIAL 38400
#  endif
#  ifdef TEENSYDUINO
#    define MYMIDI usbMIDI
#    define MIDI_CONTROL_CHANGE_TYPE usbMIDI.ControlChange
#    define MIDI_SEND_AFTER_TOUCH sendAfterTouchPoly
#  endif
#endif

// local variables and defines
Edrumulus edrumulus;
const int midi_channel      = 10;           // default for edrums is 10
const int hihat_pad_idx     = 2;            // this definition should not be changed
const int hihatctrl_pad_idx = 3;            // this definition should not be changed
int       number_pads       = number_pads4; // initialization value, may be overwritten by get_prototype_pins()
int       status_LED_pin    = 0;            // initialization value, will be set in get_prototype_pins()
bool      is_status_LED_on  = false;        // initialization value
int       selected_pad      = 0;            // initialization value

#ifdef USE_MIDI 
typedef enum
{
  MIDI_UNKNOWN,
  MIDI_NOTEON,
  MIDI_NOTEOFF,
  MIDI_CONTROL,
  MIDI_AFTERTOUCH,
  NUM_MIDI_TYPES
} MIDI_MSG_TYPE;

typedef struct 
{
  MIDI_MSG_TYPE Type;
  char Note;
  char Vel;
  char Chan;
} MidiMessage;

xQueueHandle MidiTxQueue = NULL;

#endif

#ifdef USE_WIFI

typedef struct 
{
  int32_t PosX;
  int32_t PosY;
  uint32_t Power;
  uint32_t NumHits;
} HIT_DATA;

HIT_DATA LastHit = {0};

void SetupWebpages(void);

void DoDrumXYJson(void);
void DoPadHitDataJson(void);
void DoVisualisationPage(void);
void DoStatusJson(void);
void DoPadSettingsJson(void);

void DoRootPage(void);

char *DefaultSSID = "Alien";
char *DefaultPassword = "the pheasant has no agenda";
char *DefaultNetName = "EDrumulus";
WiFiServer ApServer(80);
WebServer server(80);
uint32_t CurrentDisplayPad = 0;

#endif
int32_t MaxPeak = 0;
float MaxPeakF = 0;
static uint32_t LoopCounter = 0, MidiSends = 0;
static uint32_t LoopsPerSecond = 0, MidiPerSecond = 0, IdlesPerSecond = 0;

void EDrumulusTask(void *pArg);
void MidiTask(void *pArg);
bool SendMidiMsg(MIDI_MSG_TYPE Type, char Note, char Vel, char Chan);


void setup()
{
  for (int i = 0; i < 4; i++)
  {
    neopixelWrite(RGB_BUILTIN, 255, 255, 255);
    delay(100);
    neopixelWrite(RGB_BUILTIN, 0, 0, 0);
    delay(100);
  }
  
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }
  Serial.begin(115200);
  Serial.printf("\n\n\rStarting eDrumulus\r\n\n");
  
  // get the pin-to-pad assignments
  int*      analog_pins         = analog_pins4;         // initialize with the default setup
  int*      analog_pins_rimshot = analog_pins_rimshot4; // initialize with the default setup
  const int prototype           = Edrumulus_hardware::get_prototype_pins(&analog_pins,
                                                               &analog_pins_rimshot,
                                                               &number_pads,
                                                               &status_LED_pin);
  analog_pins         = analog_pins4;         // override get_prototype_pins
  analog_pins_rimshot = analog_pins_rimshot4; 
  number_pads = 1;
  
  // initialize GPIO port for status LED and set it to on during setup
  pinMode(status_LED_pin, OUTPUT);
  digitalWrite(status_LED_pin, HIGH);

  edrumulus.setup(number_pads, analog_pins, analog_pins_rimshot);
  digitalWrite(status_LED_pin, LOW); // set board LED to low right after setup is done

#ifdef ESP_PLATFORM
  preset_settings(); // for ESP32, the load/save of settings is not supported, preset instead
#else
  read_settings();
#endif

#if 0 //MIDI_SERIAL
  if (prototype == 5)
  {
    Serial.begin(115200); // faster communication on prototype 5
  }
  else
  {
    Serial.begin(MIDI_SERIAL);
  }
#else
  //Serial.begin(115200);
#endif


#if defined(USE_SERIAL_DEBUG_PLOTTING) && defined(ESP_PLATFORM)
  number_pads = min(number_pads, 7); // only max. 7 pads are supported for ESP32 serial debug plotting
#endif

#ifdef USE_WIFI
  SetupWebpages();
#endif

#ifdef USE_MIDI
  MidiTxQueue = xQueueCreate(MIDI_QUEUE_LEN, sizeof(MidiMessage));
  if((!MidiTxQueue))
  {
    Serial.println("FATAL MIDI ISSUE!! Could not create MidiQueue");
    return;
  }
  
  xTaskCreate(MidiTask, "MidiTask", 2048, NULL, 5, NULL);
#endif
  xTaskCreate(EDrumulusTask, "EDrumulusTask", 4096, NULL, 4, NULL);

}


void EDrumulusTask(void *pArg)
{
  uint32_t TickDelay = pdMS_TO_TICKS(1);
  while(1)
  { 
    // this function is blocking at the system sampling rate
    edrumulus.process();
    LoopCounter ++;
    
    // status LED handling
    if (edrumulus.get_status_is_overload() || edrumulus.get_status_is_error())
    {
      
      if (!is_status_LED_on)
      {
        digitalWrite(status_LED_pin, HIGH);
        is_status_LED_on = true;
  #ifdef USE_MIDI
        if (edrumulus.get_status_is_error())
        {
          const int dc_offset_error_channel = edrumulus.get_status_dc_offset_error_channel();
          if (dc_offset_error_channel >= 0)
          {
            // > 63 means DC offset error and pad/input index is coded in one value
            SendMidiMsg(MIDI_NOTEOFF, 125, 64 + dc_offset_error_channel, 1);
          }
          else
          {
            // 1 means to set error state
            SendMidiMsg(MIDI_NOTEOFF, 125, 1, 1);
          }
        }
  #endif
      }
    }
    else
    {
      if (is_status_LED_on)
      {
        digitalWrite(status_LED_pin, LOW);
        is_status_LED_on = false;
  #ifdef USE_MIDI
        SendMidiMsg(MIDI_NOTEOFF, 125, 0, 1); // 0 means that all errors are cleared
  #endif
      }
    }


#ifdef USE_MIDI
    // send MIDI note to drum synthesizer
    for (int pad_idx = 0; pad_idx < number_pads; pad_idx++)
    {
      if (edrumulus.get_peak_found(pad_idx))
      {
        // get current MIDI note and velocity (maybe note will be overwritten later on)
        const int midi_velocity = edrumulus.get_midi_velocity(pad_idx);
        int       midi_note     = edrumulus.get_midi_note(pad_idx);

        // send midi positional control message if positional sensing is enabled for the current pad
        if (edrumulus.get_pos_sense_is_used(pad_idx))
        {
          const int midi_pos = edrumulus.get_midi_pos(pad_idx);
          SendMidiMsg(MIDI_CONTROL, 16, midi_pos, midi_channel); // positional sensing
          if(pad_idx == CurrentDisplayPad)
          {
            LastHit.PosX = midi_pos;
          }
        }

        // send Hi-Hat control message right before each Hi-Hat pad hit
        if (pad_idx == hihat_pad_idx)
        {
          const int  midi_ctrl_ch    = edrumulus.get_midi_ctrl_ch(hihatctrl_pad_idx);
          const int  midi_ctrl_value = edrumulus.get_midi_ctrl_value(hihatctrl_pad_idx);
          const bool hi_hat_is_open  = edrumulus.get_midi_ctrl_is_open(hihatctrl_pad_idx);
          SendMidiMsg(MIDI_CONTROL, midi_ctrl_ch, midi_ctrl_value, midi_channel);

          // if Hi-Hat is open, overwrite MIDI note
          if (hi_hat_is_open)
          {
            midi_note = edrumulus.get_midi_note_open(pad_idx);
          }
        }
        /* This stores data for the webpage display */
        if(pad_idx == CurrentDisplayPad)
        {
          LastHit.Power = midi_velocity;
          LastHit.NumHits ++;
        }
        
        SendMidiMsg(MIDI_NOTEON, midi_note, midi_velocity, midi_channel); // (note, velocity, channel)
        SendMidiMsg(MIDI_NOTEOFF, midi_note, 0, midi_channel);            // we need a note off
      }

      if (edrumulus.get_control_found(pad_idx))
      {
        const int midi_ctrl_ch    = edrumulus.get_midi_ctrl_ch(pad_idx);
        const int midi_ctrl_value = edrumulus.get_midi_ctrl_value(pad_idx);
        SendMidiMsg(MIDI_CONTROL, midi_ctrl_ch, midi_ctrl_value, midi_channel);
      }

      if (edrumulus.get_choke_on_found(pad_idx))
      {
        // special case: if MIDI note open rim is set to zero, we use NoteOn instead of aftertouch
        // for cymbal choke (#85), where the MIDI note for NoteOn is defined by MIDI note open norm
        if (edrumulus.get_midi_note_open_rim(pad_idx) == 0)
        {
          // special case: if grabbed edge found, we send a MIDI NoteOn
          const int midi_choke_noteon = edrumulus.get_midi_note_open_norm(pad_idx);
          SendMidiMsg(MIDI_NOTEON, midi_choke_noteon, 127, midi_channel);
          SendMidiMsg(MIDI_NOTEOFF, midi_choke_noteon, 0, midi_channel); // we need a note off
        }
        else
        {
          // if grabbed edge found, polyphonic aftertouch at 127 is transmitted for all notes of the pad
          SendMidiMsg(MIDI_AFTERTOUCH, edrumulus.get_midi_note_norm(pad_idx), 127, midi_channel);
          SendMidiMsg(MIDI_AFTERTOUCH, edrumulus.get_midi_note_rim(pad_idx), 127, midi_channel);
          SendMidiMsg(MIDI_AFTERTOUCH, edrumulus.get_midi_note_open_norm(pad_idx), 127, midi_channel);
          SendMidiMsg(MIDI_AFTERTOUCH, edrumulus.get_midi_note_open_rim(pad_idx), 127, midi_channel);
        }
      }
      else if (edrumulus.get_choke_off_found(pad_idx))
      {
        // if released edge found, polyphonic aftertouch at 0 is transmitted for all notes of the pad
        SendMidiMsg(MIDI_AFTERTOUCH, edrumulus.get_midi_note_norm(pad_idx), 0, midi_channel);
        SendMidiMsg(MIDI_AFTERTOUCH, edrumulus.get_midi_note_rim(pad_idx), 0, midi_channel);
        SendMidiMsg(MIDI_AFTERTOUCH, edrumulus.get_midi_note_open_norm(pad_idx), 0, midi_channel);
        SendMidiMsg(MIDI_AFTERTOUCH, edrumulus.get_midi_note_open_rim(pad_idx), 0, midi_channel);
      }
    }
#endif /* USE_MIDI */
  }
}

bool SendMidiMsg(MIDI_MSG_TYPE Type, char Note, char Vel, char Chan)
{
  if(MidiTxQueue != NULL)
  {
    MidiMessage Msg = {.Type = Type, .Note = Note, .Vel = Vel, .Chan = Chan}; 
    
    if (xQueueSend(MidiTxQueue, &Msg, 0) != pdPASS)
    {
      Serial.println("SendMidiMsg() Out of Midi TX Queue space");
      return false;
    }
    MidiSends ++;
    return true;
  }
}

#ifdef USE_MIDI
void MidiTask(void *pArg)
{
  uint32_t TickDelay = pdMS_TO_TICKS(1);
  uint32_t TxDelay = pdMS_TO_TICKS(2);

#  ifdef USE_TINYUSB
  TinyUSBDevice.setProductDescriptor("Edrumulus");
#  endif
  MYMIDI.begin();

  while(1)
  {
    MidiMessage Msg;
    if (xQueueReceive(MidiTxQueue, &Msg, TxDelay) == pdTRUE)
    {
      //Serial.println("Midi send");
      switch(Msg.Type)
      {
        case(MIDI_NOTEOFF):
          MYMIDI.sendNoteOff(Msg.Note, Msg.Vel, Msg.Chan);
          break;
        case(MIDI_NOTEON):
          MYMIDI.sendNoteOn(Msg.Note, Msg.Vel, Msg.Chan);
          break;
        case(MIDI_CONTROL):
          MYMIDI.sendControlChange(Msg.Note, Msg.Vel, Msg.Chan);
          break;
        case(MIDI_AFTERTOUCH):
          MYMIDI.MIDI_SEND_AFTER_TOUCH(Msg.Note, Msg.Vel, Msg.Chan);
          break;
        default:
          Serial.printf("Invalid midi message %d,%d:%d:%d\r\n", Msg.Type, Msg.Note, Msg.Vel, Msg.Chan);
          break;
      }
    }
    else
    {
      if (MYMIDI.read(midi_channel))
      {
        if (MYMIDI.getType() == MIDI_CONTROL_CHANGE_TYPE)
        {
          const int controller = MYMIDI.getData1();
          const int value      = MYMIDI.getData2();

          // controller 102: pad type
          if (controller == 102)
          {
            edrumulus.set_pad_type(selected_pad, static_cast<Pad::Epadtype>(value));
            edrumulus.write_setting(selected_pad, 0, value);

            // on a pad type change, return all parameters of the selected pad
            confirm_setting(controller, value, true);
          }

          // controller 103: threshold
          if (controller == 103)
          {
            edrumulus.set_velocity_threshold(selected_pad, value);
            edrumulus.write_setting(selected_pad, 1, value);
            confirm_setting(controller, value, false);
          }

          // controller 104: sensitivity
          if (controller == 104)
          {
            edrumulus.set_velocity_sensitivity(selected_pad, value);
            edrumulus.write_setting(selected_pad, 2, value);
            confirm_setting(controller, value, false);
          }

          // controller 105: positional sensing threshold
          if (controller == 105)
          {
            edrumulus.set_pos_threshold(selected_pad, value);
            edrumulus.write_setting(selected_pad, 3, value);
            confirm_setting(controller, value, false);
          }

          // controller 106: positional sensing sensitivity
          if (controller == 106)
          {
            edrumulus.set_pos_sensitivity(selected_pad, value);
            edrumulus.write_setting(selected_pad, 4, value);
            confirm_setting(controller, value, false);
          }

          // controller 107: rim shot threshold
          if (controller == 107)
          {
            edrumulus.set_rim_shot_threshold(selected_pad, value);
            edrumulus.write_setting(selected_pad, 5, value);
            confirm_setting(controller, value, false);
          }

          // controller 108: select pad
          if ((controller == 108) && (value < MAX_NUM_PADS))
          {
            selected_pad = value;

            // on a pad selection, return all parameters of the selected pad
            confirm_setting(controller, value, true);
          }

          // controller 109: MIDI curve type
          if (controller == 109)
          {
            edrumulus.set_curve(selected_pad, static_cast<Pad::Ecurvetype>(value));
            edrumulus.write_setting(selected_pad, 6, value);
            confirm_setting(controller, value, false);
          }

          // controller 110: spike cancellation level
          if (controller == 110)
          {
            edrumulus.set_spike_cancel_level(value);
            edrumulus.write_setting(number_pads, 0, value);
            confirm_setting(controller, value, false);
          }

          // controller 111: enable/disable rim shot and positional sensing support
          if (controller == 111)
          {
            switch (value)
            {
              case 0:
                edrumulus.set_rim_shot_is_used(selected_pad, false);
                edrumulus.write_setting(selected_pad, 7, false);
                edrumulus.set_pos_sense_is_used(selected_pad, false);
                edrumulus.write_setting(selected_pad, 8, false);
                break;
              case 1:
                edrumulus.set_rim_shot_is_used(selected_pad, true);
                edrumulus.write_setting(selected_pad, 7, true);
                edrumulus.set_pos_sense_is_used(selected_pad, false);
                edrumulus.write_setting(selected_pad, 8, false);
                break;
              case 2:
                edrumulus.set_rim_shot_is_used(selected_pad, false);
                edrumulus.write_setting(selected_pad, 7, false);
                edrumulus.set_pos_sense_is_used(selected_pad, true);
                edrumulus.write_setting(selected_pad, 8, true);
                break;
              case 3:
                edrumulus.set_rim_shot_is_used(selected_pad, true);
                edrumulus.write_setting(selected_pad, 7, true);
                edrumulus.set_pos_sense_is_used(selected_pad, true);
                edrumulus.write_setting(selected_pad, 8, true);
                break;
            }
            confirm_setting(controller, value, false);
          }

          // controller 112: normal MIDI note
          if (controller == 112)
          {
            edrumulus.set_midi_note_norm(selected_pad, value);
            edrumulus.write_setting(selected_pad, 9, value);
            confirm_setting(controller, value, false);
          }

          // controller 113: MIDI note for rim
          if (controller == 113)
          {
            edrumulus.set_midi_note_rim(selected_pad, value);
            edrumulus.write_setting(selected_pad, 10, value);
            confirm_setting(controller, value, false);
          }

          // controller 114: cross talk cancellation
          if (controller == 114)
          {
            edrumulus.set_cancellation(selected_pad, value);
            edrumulus.write_setting(selected_pad, 11, value);
            confirm_setting(controller, value, false);
          }

          // controller 115: apply preset settings and store these to the EEPROM
          if (controller == 115)
          {
            preset_settings();
            write_all_settings();
            confirm_setting(controller, value, false);
          }

          // controller 116: normal MIDI note open (Hi-Hat)
          if (controller == 116)
          {
            edrumulus.set_midi_note_open_norm(selected_pad, value);
            edrumulus.write_setting(selected_pad, 12, value);
            confirm_setting(controller, value, false);
          }

          // controller 117: MIDI note open (Hi-Hat) for rim
          if (controller == 117)
          {
            edrumulus.set_midi_note_open_rim(selected_pad, value);
            edrumulus.write_setting(selected_pad, 13, value);
            confirm_setting(controller, value, false);
          }

          // controller 118: mask time
          if (controller == 118)
          {
            edrumulus.set_mask_time(selected_pad, value);
            edrumulus.write_setting(selected_pad, 14, value);
            confirm_setting(controller, value, false);
          }

          // controller 119: rim shot boost
          if (controller == 119)
          {
            edrumulus.set_rim_shot_boost(selected_pad, value);
            edrumulus.write_setting(selected_pad, 15, value);
            confirm_setting(controller, value, false);
          }

          // controller 120: pad coupling
          if (controller == 120)
          {
            edrumulus.set_coupled_pad_idx(selected_pad, value);
            edrumulus.write_setting(selected_pad, 16, value);
            confirm_setting(controller, value, false);
          }

          // controller 121: rim positional sensing threshold
          if (controller == 121)
          {
            edrumulus.set_rim_pos_threshold(selected_pad, value);
            edrumulus.write_setting(selected_pad, 17, value);
            confirm_setting(controller, value, false);
          }

          // controller 122: rim positional sensing sensitivity
          if (controller == 122)
          {
            edrumulus.set_rim_pos_sensitivity(selected_pad, value);
            edrumulus.write_setting(selected_pad, 18, value);
            confirm_setting(controller, value, false);
          }
        }
      }
      vTaskDelay(TickDelay);
    }
  }
}
#endif

void preset_settings()
{
  // default MIDI note assignments
  edrumulus.set_midi_notes(0, 38, 40); // snare
  edrumulus.set_midi_notes(1, 36, 36); // kick
  edrumulus.set_midi_notes(hihat_pad_idx, 22 /*42*/, 22);
  edrumulus.set_midi_notes_open(hihat_pad_idx, 26 /*46*/, 26);
  edrumulus.set_midi_notes(hihatctrl_pad_idx, 44, 44); // Hi-Hat pedal hit
  edrumulus.set_midi_notes(4, 49, 55);                 // crash
  edrumulus.set_midi_notes(5, 48, 50);                 // tom 1
  edrumulus.set_midi_notes(6, 51, 53 /*59*/);          // ride (edge: 59, bell: 53)
  edrumulus.set_midi_notes(7, 45, 47);                 // tom 2
  edrumulus.set_midi_notes(8, 43, 58);                 // tom 3

  // default drum kit setup
  edrumulus.set_pad_type(0, Pad::PDX8);  // snare
  edrumulus.set_pad_type(1, Pad::KD7);  // kick
  edrumulus.set_pad_type(2, Pad::PD6);  // Hi-Hat
  edrumulus.set_pad_type(3, Pad::FD8);  // Hi-Hat-ctrl
  edrumulus.set_pad_type(4, Pad::CY6);  // crash
  edrumulus.set_pad_type(5, Pad::TP80); // tom 1
  edrumulus.set_pad_type(6, Pad::CY8);  // ride
  edrumulus.set_pad_type(7, Pad::TP80); // tom 2
  edrumulus.set_pad_type(8, Pad::TP80); // tom 3
}

void loop()
{
  static unsigned long StatsTime = 0;
  static int32_t Idles = 0;

  static uint8_t R = 0, G = 0, B = 0;
  neopixelWrite(RGB_BUILTIN, 0, 255 - R, R);

  R++;
  
  //G = ((G + 2)) & 0xFF;
  //B = ((B + 3)) & 0xFF;

  if((millis() - StatsTime) >= 1000)
  {
    StatsTime = millis();

    Serial.printf("Stats: Loops %d, Midi %d\r\n", LoopCounter, MidiSends);
    LoopsPerSecond = LoopCounter;
    MidiPerSecond = MidiSends;
    IdlesPerSecond = Idles;
    LoopCounter = 0, MidiSends = 0, Idles = 0;
  }

#ifdef USE_WIFI
  server.handleClient();
#endif
   Idles ++;
  delay(5);
}

#ifdef USE_MIDI
// give feedback to the controller GUI via MIDI Note Off
void confirm_setting(const int  controller,
                     const int  value,
                     const bool send_all)
{
  if (send_all)
  {
    // return all parameters of the selected pad
    MYMIDI.sendNoteOff(102, static_cast<int>(edrumulus.get_pad_type(selected_pad)), 1);
    MYMIDI.sendNoteOff(103, edrumulus.get_velocity_threshold(selected_pad), 1);
    MYMIDI.sendNoteOff(104, edrumulus.get_velocity_sensitivity(selected_pad), 1);
    MYMIDI.sendNoteOff(105, edrumulus.get_pos_threshold(selected_pad), 1);
    MYMIDI.sendNoteOff(106, edrumulus.get_pos_sensitivity(selected_pad), 1);
    MYMIDI.sendNoteOff(107, edrumulus.get_rim_shot_threshold(selected_pad), 1);
    MYMIDI.sendNoteOff(108, selected_pad, 1);
    MYMIDI.sendNoteOff(109, static_cast<int>(edrumulus.get_curve(selected_pad)), 1);
    MYMIDI.sendNoteOff(110, edrumulus.get_spike_cancel_level(), 1);
    MYMIDI.sendNoteOff(111, edrumulus.get_rim_shot_is_used(selected_pad) + 2 * edrumulus.get_pos_sense_is_used(selected_pad), 1);
    MYMIDI.sendNoteOff(112, edrumulus.get_midi_note_norm(selected_pad), 1);
    MYMIDI.sendNoteOff(113, edrumulus.get_midi_note_rim(selected_pad), 1);
    MYMIDI.sendNoteOff(114, edrumulus.get_cancellation(selected_pad), 1);
    MYMIDI.sendNoteOff(116, edrumulus.get_midi_note_open_norm(selected_pad), 1);
    MYMIDI.sendNoteOff(117, edrumulus.get_midi_note_open_rim(selected_pad), 1);
    MYMIDI.sendNoteOff(118, edrumulus.get_mask_time(selected_pad), 1);
    MYMIDI.sendNoteOff(119, edrumulus.get_rim_shot_boost(selected_pad), 1);
    MYMIDI.sendNoteOff(120, edrumulus.get_coupled_pad_idx(selected_pad), 1);
    MYMIDI.sendNoteOff(121, edrumulus.get_rim_pos_threshold(selected_pad), 1);
    MYMIDI.sendNoteOff(122, edrumulus.get_rim_pos_sensitivity(selected_pad), 1);
    // NOTE: 125 reserved for error message
    MYMIDI.sendNoteOff(126, VERSION_MINOR, 1);
    MYMIDI.sendNoteOff(127, VERSION_MAJOR, 1);
  }
  else
  {
    // return only the given parameter
    MYMIDI.sendNoteOff(controller, value, 1); // can be checked, e.g., in the log file
  }
}
#endif



#ifdef USE_WIFI
void SetupWebpages(void)
{

  SPIFFS.begin();

  ConnectToWifi(DefaultSSID, DefaultPassword, DefaultNetName);

  char result[16];
  sprintf(result, "%d.%d.%d.%d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);

  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  MDNS.begin(DefaultNetName);


  MDNS.addService("http", "tcp", 80);

  WiFiClient Client = server.client();
  Client.setNoDelay(1);

  server.on("/", HTTP_GET, DoRootPage);
  server.on("/index.html", HTTP_GET, DoRootPage);
  server.on("/visualisation.html", HTTP_GET, DoVisualisationPage);

  server.serveStatic("/support", SPIFFS, "/support", "max-age=31536000");

  server.on("/status/status.json", HTTP_GET, DoStatusJson);
  server.on("/status/padsettings.json", HTTP_GET, DoPadSettingsJson);
  server.on("/status/drumxy.json", HTTP_GET, DoDrumXYJson);
  server.on("/status/pad_hit_data.json", HTTP_GET, DoPadHitDataJson);
  server.begin();

}

String AddJSonArray(int32_t *Arr, uint32_t Len)
{
  String Content = "";
  if((NULL != Arr) && (Len > 0))
  {
    for (int i = 0; i < Len; i++)
    {
      
      Content += String(Arr[i]);
      if(i < (Len - 1))
      {
        Content += ",";
      }
    }
  }
  else{
    Serial.println("AddJSonArray: invalid array");
  }
  return Content;
}

String AddJSonElement(char *Name, int32_t Val)
{
  return "\"" + String(Name) + "\":" + String(Val);
}

String AddJSonArrayXY(int32_t *ArrX, int32_t *ArrY, uint32_t Len)
{
  if((NULL != ArrX) && (NULL != ArrY) && (Len > 0))
  {
    String Content = "";
    for (int i = 0; i < Len; i++)
    {
      Content += "{\"x\":" + String(ArrX[i]) + ",\"y\":" + String(ArrY[i]) + "}";
      if(i < (Len - 1))
      {
        Content += ",";
      }
    }
    return Content;
  }
  else{
    Serial.println("AddJSonArrayXY: invalid array");
    return "";
  }
}

/* [
 {"x": 300, "y": 256, "size": 60, "id": "circle1"},
 {"x": 500, "y": 256, "size": 20, "id": "circle2"}
] */
void DoDrumXYJson(void)
{
  String Content = "{\n  \"NumHits\":";
  Content += String(LastHit.NumHits) + ",\n";
  Content += "  \"HitData\": [\n  {";
  Content += AddJSonElement("x", LastHit.PosX + 256);
  Content += ",";
  Content += AddJSonElement("y", LastHit.PosY * 2 + 256);
  Content += ",";
  int Power = LastHit.Power / 2;
  if(Power < 5)
    Power = 5;
  Content += AddJSonElement("size", Power);
  Content += ", \"id\": \"circle1\"";
  Content += "  }\n  ]\n}";
  server.send(200, "application/json", Content);
}

void DoPadHitDataJson(void)
{
  String Content = "{\n";
  Content += "\"powerValues\": [";
  Content += GET_DEBUG_BUFFER(0);
  Content += "],\n \"thresholdPoints\": [";
  Content += GET_DEBUG_BUFFER(1);
  Content += "],\n \"positionPoints\": [";
  Content += GET_DEBUG_BUFFER(2);
  Content += "],\n \"Line4\": [";
  Content += GET_DEBUG_BUFFER(3);
  Content += "  ]\n}";
  DEBUG_FINISHED_PLOTTING();
  
  server.send(200, "application/json", Content);
}
void DoVisualisationPage(void)
{
  File file = SPIFFS.open("/visualisation.html", "r");
  size_t sent = server.streamFile(file, "text/html");
  if (0 == sent)
  {
    Serial.println("DoVisualisationPage file length 0");
  }
  file.close();
}


#define STATUS_TABLE_ROWS 6
WEBTABLE_ROW StatusTableRows[STATUS_TABLE_ROWS] = {
  {"ADC Loops/second",      false,      &LoopsPerSecond,      "%d",    sizeof(int32_t)},
  {"Midi messages/second",  false,      &MidiPerSecond,       "%d",    sizeof(int32_t)},
  {"Idles/second",          false,      &IdlesPerSecond,       "%d",    sizeof(int32_t)},
  {"NumHits",               false,      &LastHit.NumHits,     "%d",    sizeof(int32_t)},
  {"MaxPeak",               false,      &MaxPeak,             "%d",    sizeof(int32_t)},
  {"MaxPeakF",              false,      &MaxPeakF,            "%f",    sizeof(float)}
};

WEBTABLE StatusTable = { "Status Table", "status", STATUS_TABLE_ROWS, 1000, 4, StatusTableRows};
void DoStatusJson(void)
{
  WebTableWriteJson(&StatusTable, &server);
  MaxPeak = 0;
  MaxPeakF = 0.0f;
}

uint32_t PadType = 0;
int32_t VelocityThreshold = 0;
int32_t VelocitySensitivity = 0;
int32_t PosThreshold = 0;
int32_t PosSensitivity = 0;
int32_t SpikeCancelLevel = 0;
int32_t MaskTime = 0;

#define PAD_SETTINGS_TABLE_ROWS 8
WEBTABLE_ROW PadSettingsTableRows[PAD_SETTINGS_TABLE_ROWS] = {
  {"Selected Pad",          true,      &selected_pad,          "%d",    sizeof(uint32_t)},
  {"Pad Type",              true,      &PadType,               "%d",    sizeof(uint32_t)},
  {"Velocitiy Sensitivity", true,      &VelocitySensitivity,   "%d",    sizeof(int32_t)},
  {"Velocity Threshold",    true,      &VelocityThreshold,     "%d",    sizeof(int32_t)},
  {"Pos Sensitivity",       true,      &PosSensitivity,        "%d",    sizeof(int32_t)},
  {"Pos Threshold",         true,      &PosThreshold,          "%d",    sizeof(int32_t)},
  {"Mask Time",             true,      &MaskTime,              "%d",    sizeof(int32_t)},
  {"Spike Cancel Level",    true,      &SpikeCancelLevel,      "%d",    sizeof(int32_t)}
};


WEBTABLE PadSettings = { "Pad Settings Table", "pad_settings", PAD_SETTINGS_TABLE_ROWS, 60 * 1000, 4, PadSettingsTableRows};

void DoPadSettingsJson(void)
{
  
  PadType = (uint32_t)edrumulus.get_pad_type(selected_pad);
  VelocityThreshold   = edrumulus.get_velocity_threshold(selected_pad);
  VelocitySensitivity = edrumulus.get_velocity_sensitivity(selected_pad);
  PosSensitivity = edrumulus.get_pos_sensitivity(selected_pad);
  PosThreshold = edrumulus.get_pos_threshold(selected_pad);
  SpikeCancelLevel = edrumulus.get_spike_cancel_level();
  MaskTime = edrumulus.get_mask_time(selected_pad);
  
  WebTableWriteJson(&PadSettings, &server);
}
void DoRootPage(void)
{
  File file = SPIFFS.open("/index.html", "r");
  size_t sent = server.streamFile(file, "text/html");
  if (0 == sent)
  {
    Serial.println("DoRootPage file length 0");
  }
  file.close();
  
  if (server.args() != 0)
  {
    if (WebTableProcessSet(&PadSettings, &server))
    {
      edrumulus.set_pad_type(selected_pad, static_cast<Pad::Epadtype>(PadType));
      edrumulus.write_setting(selected_pad, 0, PadType);
    
      edrumulus.set_velocity_threshold(selected_pad, VelocityThreshold);
      edrumulus.write_setting(selected_pad, 1, VelocityThreshold);
      
      edrumulus.set_velocity_sensitivity(selected_pad, VelocitySensitivity);
      edrumulus.write_setting(selected_pad, 2, VelocitySensitivity);
      
      edrumulus.set_pos_threshold(selected_pad, PosThreshold);
      edrumulus.write_setting(selected_pad, 3, PosThreshold);
      
      edrumulus.set_pos_sensitivity(selected_pad, PosSensitivity);
      edrumulus.write_setting(selected_pad, 4, PosSensitivity);

      edrumulus.set_spike_cancel_level(SpikeCancelLevel);
      edrumulus.write_setting(number_pads, 0, SpikeCancelLevel);
      
      edrumulus.set_mask_time(selected_pad, MaskTime);
      edrumulus.write_setting(selected_pad, 14, MaskTime);
    }
  }
}

#endif

void read_settings()
{
  for (int i = 0; i < number_pads; i++)
  {
    // NOTE that it is important that set_pad_type() is called first because it resets all other parameters
    edrumulus.set_pad_type(i, static_cast<Pad::Epadtype>(edrumulus.read_setting(i, 0)));
    edrumulus.set_velocity_threshold(i, edrumulus.read_setting(i, 1));
    edrumulus.set_velocity_sensitivity(i, edrumulus.read_setting(i, 2));
    edrumulus.set_pos_threshold(i, edrumulus.read_setting(i, 3));
    edrumulus.set_pos_sensitivity(i, edrumulus.read_setting(i, 4));
    edrumulus.set_rim_shot_threshold(i, edrumulus.read_setting(i, 5));
    edrumulus.set_curve(i, static_cast<Pad::Ecurvetype>(edrumulus.read_setting(i, 6)));
    edrumulus.set_rim_shot_is_used(i, edrumulus.read_setting(i, 7));
    edrumulus.set_pos_sense_is_used(i, edrumulus.read_setting(i, 8));
    edrumulus.set_midi_note_norm(i, edrumulus.read_setting(i, 9));
    edrumulus.set_midi_note_rim(i, edrumulus.read_setting(i, 10));
    edrumulus.set_cancellation(i, edrumulus.read_setting(i, 11));
    edrumulus.set_midi_note_open_norm(i, edrumulus.read_setting(i, 12));
    edrumulus.set_midi_note_open_rim(i, edrumulus.read_setting(i, 13));
    edrumulus.set_mask_time(i, edrumulus.read_setting(i, 14));
    edrumulus.set_rim_shot_boost(i, edrumulus.read_setting(i, 15));
    edrumulus.set_coupled_pad_idx(i, edrumulus.read_setting(i, 16));
    edrumulus.set_rim_pos_threshold(i, edrumulus.read_setting(i, 17));
    edrumulus.set_rim_pos_sensitivity(i, edrumulus.read_setting(i, 18));
  }
  edrumulus.set_spike_cancel_level(edrumulus.read_setting(number_pads, 0));
}

void write_all_settings()
{
  for (int i = 0; i < number_pads; i++)
  {
    edrumulus.write_setting(i, 0, edrumulus.get_pad_type(i));
    edrumulus.write_setting(i, 1, edrumulus.get_velocity_threshold(i));
    edrumulus.write_setting(i, 2, edrumulus.get_velocity_sensitivity(i));
    edrumulus.write_setting(i, 3, edrumulus.get_pos_threshold(i));
    edrumulus.write_setting(i, 4, edrumulus.get_pos_sensitivity(i));
    edrumulus.write_setting(i, 5, edrumulus.get_rim_shot_threshold(i));
    edrumulus.write_setting(i, 6, edrumulus.get_curve(i));
    edrumulus.write_setting(i, 7, edrumulus.get_rim_shot_is_used(i));
    edrumulus.write_setting(i, 8, edrumulus.get_pos_sense_is_used(i));
    edrumulus.write_setting(i, 9, edrumulus.get_midi_note_norm(i));
    edrumulus.write_setting(i, 10, edrumulus.get_midi_note_rim(i));
    edrumulus.write_setting(i, 11, edrumulus.get_cancellation(i));
    edrumulus.write_setting(i, 12, edrumulus.get_midi_note_open_norm(i));
    edrumulus.write_setting(i, 13, edrumulus.get_midi_note_open_rim(i));
    edrumulus.write_setting(i, 14, edrumulus.get_mask_time(i));
    edrumulus.write_setting(i, 15, edrumulus.get_rim_shot_boost(i));
    edrumulus.write_setting(i, 16, edrumulus.get_coupled_pad_idx(i));
    edrumulus.write_setting(i, 17, edrumulus.get_rim_pos_threshold(i));
    edrumulus.write_setting(i, 18, edrumulus.get_rim_pos_sensitivity(i));
  }
  edrumulus.write_setting(number_pads, 0, edrumulus.get_spike_cancel_level());
}
