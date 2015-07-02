/**************************************************************************************************
  Filename:       Ketamine.c
  Revised:        $Date: 2010-08-06 08:56:11 -0700 (Fri, 06 Aug 2010) $
  Revision:       $Revision: 23333 $

  Description:    This file contains the Simple BLE Peripheral sample application
                  for use with the CC2540 Bluetooth Low Energy Protocol Stack.

  Copyright 2010 - 2013 Texas Instruments Incorporated. All rights reserved.

  IMPORTANT: Your use of this Software is limited to those specific rights
  granted under the terms of a software license agreement between the user
  who downloaded the software, his/her employer (which must be your employer)
  and Texas Instruments Incorporated (the "License").  You may not use this
  Software unless you agree to abide by the terms of the License. The License
  limits your use, and you acknowledge, that the Software may not be modified,
  copied or distributed unless embedded on a Texas Instruments microcontroller
  or used solely and exclusively in conjunction with a Texas Instruments radio
  frequency transceiver, which is integrated into your product.  Other than for
  the foregoing purpose, you may not use, reproduce, copy, prepare derivative
  works of, modify, distribute, perform, display or sell this Software and/or
  its documentation for any purpose.

  YOU FURTHER ACKNOWLEDGE AND AGREE THAT THE SOFTWARE AND DOCUMENTATION ARE
  PROVIDED ?AS IS? WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED,
  INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY, TITLE,
  NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL
  TEXAS INSTRUMENTS OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER CONTRACT,
  NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR OTHER
  LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
  INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE
  OR CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT
  OF SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
  (INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.

  Should you have any questions regarding your right to use this Software,
  contact Texas Instruments Incorporated at www.TI.com.
**************************************************************************************************/

/*********************************************************************
 * INCLUDES
 */

#include <stdio.h>

#include "bcomdef.h"
#include "OSAL.h"
#include "OSAL_PwrMgr.h"

#include "OnBoard.h"
#include "hal_adc.h"
#include "hal_led.h"
#include "hal_key.h"
#include "hal_lcd.h"
#include "hal_i2c.h"
#include "hal_sensor.h"
#include "hal_uart.h"
#include "npi.h"
#include "TCS3414CS.h"
#include "eeprom.h"
#include "Application.h"


#include "gatt.h"

#include "hci.h"

#include "gapgattserver.h"
#include "gattservapp.h"
#include "devinfoservice.h"
#include "simpleGATTprofile.h"

//#if defined( CC2540_MINIDK )
//  #include "simplekeys.h"
//#endif

#include "peripheral.h"

#include "gapbondmgr.h"

#include "Ketamine.h"

#include "serialInterface.h"

#if defined FEATURE_OAD
  #include "oad.h"
  #include "oad_target.h"
#endif

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

// How often to perform periodic event
#define KTM_BROADCAST_EVT_PERIOD                        500
#define KTM_PERIODIC_EVT_PERIOD                         1000
#define KTM_ACQUIRE_PIC_PICTURE                         100
#define KTM_COLORDELAY_PERIOD                           30
#define KTM_SENDDATA_PERIOD                             800

// What is the advertising interval when device is discoverable (units of 625us, 160=100ms)
#define DEFAULT_ADVERTISING_INTERVAL          160

// Limited discoverable mode advertises for 30.72s, and then stops
// General discoverable mode advertises indefinitely

//#define DEFAULT_DISCOVERABLE_MODE             GAP_ADTYPE_FLAGS_GENERAL
#define DEFAULT_DISCOVERABLE_MODE             GAP_ADTYPE_FLAGS_LIMITED

// Minimum connection interval (units of 1.25ms, 80=100ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL     80

// Maximum connection interval (units of 1.25ms, 800=1000ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL     800

// Slave latency to use if automatic parameter update request is enabled
#define DEFAULT_DESIRED_SLAVE_LATENCY         0

// Supervision timeout value (units of 10ms, 1000=10s) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_CONN_TIMEOUT          1000

// Whether to enable automatic parameter update request when a connection is formed
#define DEFAULT_ENABLE_UPDATE_REQUEST         TRUE

// Connection Pause Peripheral time value (in seconds)
#define DEFAULT_CONN_PAUSE_PERIPHERAL         5

// Company Identifier: Texas Instruments Inc. (13)
#define TI_COMPANY_ID                         0x000D

#define INVALID_CONNHANDLE                    0xFFFF

// Length of bd addr as a string
#define B_ADDR_STR_LEN                        15

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static uint8 button_counter = 0;

static uint8 Ketamine_TaskID;   // Task ID for internal task/event processing

static gaprole_States_t gapProfileState = GAPROLE_INIT;

// GAP - SCAN RSP data (max size = 31 bytes)
static uint8 scanRspData[] =
{
  // complete name
  0x08,   // length of this data
  GAP_ADTYPE_LOCAL_NAME_COMPLETE,
  0x6b,   // 'k'
  0x65,   // 'e'
  0x74,   // 't'
  0x5f,   // '-'
  0x30,   // '0'
  0x30,   // '0'
  0x32,   // '2'

  // connection interval range
  0x05,   // length of this data
  GAP_ADTYPE_SLAVE_CONN_INTERVAL_RANGE,
  LO_UINT16( DEFAULT_DESIRED_MIN_CONN_INTERVAL ),   // 100ms
  HI_UINT16( DEFAULT_DESIRED_MIN_CONN_INTERVAL ),
  LO_UINT16( DEFAULT_DESIRED_MAX_CONN_INTERVAL ),   // 1s
  HI_UINT16( DEFAULT_DESIRED_MAX_CONN_INTERVAL ),

  // Tx power level
  0x02,   // length of this data
  GAP_ADTYPE_POWER_LEVEL,
  0       // 0dBm
};

// GAP - Advertisement data (max size = 31 bytes, though this is
// best kept short to conserve power while advertisting)
static uint8 advertData[] =
{
  // Flags; this sets the device to use limited discoverable
  // mode (advertises for 30 seconds at a time) instead of general
  // discoverable mode (advertises indefinitely)
  0x02,   // length of this data
  GAP_ADTYPE_FLAGS,
  //DEFAULT_DISCOVERABLE_MODE | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
  DEFAULT_DISCOVERABLE_MODE | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

  // service UUID, to notify central devices what services are included
  // in this peripheral
  0x03,   // length of this data
  GAP_ADTYPE_16BIT_MORE,      // some of the UUID's, but not all
  LO_UINT16( SIMPLEPROFILE_SERV_UUID ),
  HI_UINT16( SIMPLEPROFILE_SERV_UUID ),

};

// GAP GATT Attributes
static uint8 attDeviceName[GAP_DEVICE_NAME_LEN] = "Simple BLE Peripheral";

static uint8 somedata1[] =
{
  0x01,   // 'a'
  0x02,   // 'p'
  0x03,   // 'p'
  0x04,   // 'l'
  0x05,   // 'e'
};

uint8 globalState = 1;
static uint16 globalCount = 0;
static uint8 directTerminate = 0;
int advMax = 600;
int globalMax = 120;
uint8 clrCnt = 0;
uint8 disconnectCnt = 0;
uint8 resetFlag = 0;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void Ketamine_ProcessOSALMsg( osal_event_hdr_t *pMsg );
static void peripheralStateNotificationCB( gaprole_States_t newState );
static void performPeriodicTask( void );
static void simpleProfileChangeCB( uint8 paramID );
void initialParameter(void);
void readColorAfterDelay(uint8 state);
void getPictureData();
void parseBLECmd(uint8 value);

//#if defined( CC2540_MINIDK )
static void Ketamine_HandleKeys( uint8 shift, uint8 keys );
//#endif

#if (defined HAL_LCD) && (HAL_LCD == TRUE)
static char *bdAddr2Str ( uint8 *pAddr );
#endif // (defined HAL_LCD) && (HAL_LCD == TRUE)



/*********************************************************************
 * PROFILE CALLBACKS
 */

// GAP Role Callbacks
static gapRolesCBs_t Ketamine_PeripheralCBs =
{
  peripheralStateNotificationCB,  // Profile State Change Callbacks
  NULL                            // When a valid RSSI is read from controller (not used by application)
};

// GAP Bond Manager Callbacks
static gapBondCBs_t Ketamine_BondMgrCBs =
{
  NULL,                     // Passcode callback (not used by application)
  NULL                      // Pairing / Bonding state Callback (not used by application)
};

// Simple GATT Profile Callbacks
static simpleProfileCBs_t Ketamine_SimpleProfileCBs =
{
  simpleProfileChangeCB    // Charactersitic value change callback
};
/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      Ketamine_Init
 *
 * @brief   Initialization function for the Simple BLE Peripheral App Task.
 *          This is called during initialization and should contain
 *          any application specific initialization (ie. hardware
 *          initialization/setup, table initialization, power up
 *          notificaiton ... ).
 *
 * @param   task_id - the ID assigned by OSAL.  This ID should be
 *                    used to send messages and set timers.
 *
 * @return  none
 */
void Ketamine_Init( uint8 task_id )
{
  Ketamine_TaskID = task_id;

  // Setup the GAP
  VOID GAP_SetParamValue( TGAP_CONN_PAUSE_PERIPHERAL, DEFAULT_CONN_PAUSE_PERIPHERAL );
  
  // Setup the GAP Peripheral Role Profile
  {
    #if defined( CC2540_MINIDK )
      // For the CC2540DK-MINI keyfob, device doesn't start advertising until button is pressed
      uint8 initial_advertising_enable = FALSE;
    #else
      // For other hardware platforms, device starts advertising upon initialization
      uint8 initial_advertising_enable = TRUE;
    #endif

    // By setting this to zero, the device will go into the waiting state after
    // being discoverable for 30.72 second, and will not being advertising again
    // until the enabler is set back to TRUE
    //uint16 gapRole_AdvertOffTime = 0;
      
    // Advertisement time : 1.9sec
    uint16 gapRole_AdvertOffTime = 0;
    

    uint8 enable_update_request = DEFAULT_ENABLE_UPDATE_REQUEST;
    uint16 desired_min_interval = DEFAULT_DESIRED_MIN_CONN_INTERVAL;
    uint16 desired_max_interval = DEFAULT_DESIRED_MAX_CONN_INTERVAL;
    uint16 desired_slave_latency = DEFAULT_DESIRED_SLAVE_LATENCY;
    uint16 desired_conn_timeout = DEFAULT_DESIRED_CONN_TIMEOUT;

    // Set the GAP Role Parameters
    GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &initial_advertising_enable );
    GAPRole_SetParameter( GAPROLE_ADVERT_OFF_TIME, sizeof( uint16 ), &gapRole_AdvertOffTime );

    GAPRole_SetParameter( GAPROLE_SCAN_RSP_DATA, sizeof ( scanRspData ), scanRspData );
    GAPRole_SetParameter( GAPROLE_ADVERT_DATA, sizeof( advertData ), advertData );

    GAPRole_SetParameter( GAPROLE_PARAM_UPDATE_ENABLE, sizeof( uint8 ), &enable_update_request );
    GAPRole_SetParameter( GAPROLE_MIN_CONN_INTERVAL, sizeof( uint16 ), &desired_min_interval );
    GAPRole_SetParameter( GAPROLE_MAX_CONN_INTERVAL, sizeof( uint16 ), &desired_max_interval );
    GAPRole_SetParameter( GAPROLE_SLAVE_LATENCY, sizeof( uint16 ), &desired_slave_latency );
    GAPRole_SetParameter( GAPROLE_TIMEOUT_MULTIPLIER, sizeof( uint16 ), &desired_conn_timeout );
  }

  // Set the GAP Characteristics
  GGS_SetParameter( GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, attDeviceName );

  // Set advertising interval
  {
    uint16 advInt = DEFAULT_ADVERTISING_INTERVAL;

    GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MIN, advInt );
    GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MAX, advInt );
    GAP_SetParamValue( TGAP_GEN_DISC_ADV_INT_MIN, advInt );
    GAP_SetParamValue( TGAP_GEN_DISC_ADV_INT_MAX, advInt );
  }

  // Setup the GAP Bond Manager
  {
    uint32 passkey = 0; // passkey "000000"
    uint8 pairMode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
    uint8 mitm = TRUE;
    uint8 ioCap = GAPBOND_IO_CAP_DISPLAY_ONLY;
    uint8 bonding = TRUE;
    GAPBondMgr_SetParameter( GAPBOND_DEFAULT_PASSCODE, sizeof ( uint32 ), &passkey );
    GAPBondMgr_SetParameter( GAPBOND_PAIRING_MODE, sizeof ( uint8 ), &pairMode );
    GAPBondMgr_SetParameter( GAPBOND_MITM_PROTECTION, sizeof ( uint8 ), &mitm );
    GAPBondMgr_SetParameter( GAPBOND_IO_CAPABILITIES, sizeof ( uint8 ), &ioCap );
    GAPBondMgr_SetParameter( GAPBOND_BONDING_ENABLED, sizeof ( uint8 ), &bonding );
  }

  // Initialize GATT attributes
  GGS_AddService( GATT_ALL_SERVICES );            // GAP
  GATTServApp_AddService( GATT_ALL_SERVICES );    // GATT attributes
  DevInfo_AddService();                           // Device Information Service
  SimpleProfile_AddService( GATT_ALL_SERVICES );  // Simple GATT Profile
#if defined FEATURE_OAD
  VOID OADTarget_AddService();                    // OAD Profile
#endif

  // Setup the SimpleProfile Characteristic Values
  {
    uint8 charValue1 = 1;
    uint8 charValue2 = 2;
    uint8 charValue3 = 3;
    uint8 charValue4 = 4;
    uint8 charValue5[SIMPLEPROFILE_CHAR5_LEN] = { 1, 2, 3, 4, 5 };
    SimpleProfile_SetParameter( SIMPLEPROFILE_CHAR1, sizeof ( uint8 ), &charValue1 );
    SimpleProfile_SetParameter( SIMPLEPROFILE_CHAR2, sizeof ( uint8 ), &charValue2 );
    SimpleProfile_SetParameter( SIMPLEPROFILE_CHAR3, sizeof ( uint8 ), &charValue3 );
    SimpleProfile_SetParameter( SIMPLEPROFILE_CHAR4, sizeof ( uint8 ), &charValue4 );
    SimpleProfile_SetParameter( SIMPLEPROFILE_CHAR5, SIMPLEPROFILE_CHAR5_LEN, charValue5 );
  }

  HalLedSet( (HAL_LED_1 | HAL_LED_2), HAL_LED_MODE_OFF );
  P0SEL = 0;
  P0DIR = 0x9E;
  P0 = 0x01;
  
  //P1SEL = 0x3C;
  //P1DIR = 0xE7;
  //P1 = 0x3C;

  P1SEL = 0;
  P1DIR = 0xFF;
  P1 = 0x04;
  
  
  
  HalLedSet( (HAL_LED_1 | HAL_LED_2), HAL_LED_MODE_ON );

  // Register for all key events - This app will handle all key events
  RegisterForKeys( Ketamine_TaskID );
  
#if (defined HAL_LCD) && (HAL_LCD == TRUE)

#if defined FEATURE_OAD
  #if defined (HAL_IMAGE_A)
    HalLcdWriteStringValue( "BLE Peri-A", OAD_VER_NUM( _imgHdr.ver ), 16, HAL_LCD_LINE_1 );
  #else
    HalLcdWriteStringValue( "BLE Peri-B", OAD_VER_NUM( _imgHdr.ver ), 16, HAL_LCD_LINE_1 );
  #endif // HAL_IMAGE_A
#else
  HalLcdWriteString( "BLE Peripheral", HAL_LCD_LINE_1 );
#endif // FEATURE_OAD

#endif // (defined HAL_LCD) && (HAL_LCD == TRUE)

  // Register callback with SimpleGATTprofile
  VOID SimpleProfile_RegisterAppCBs( &Ketamine_SimpleProfileCBs );

  // Enable clock divide on halt
  // This reduces active current while radio is active and CC254x MCU
  // is halted
  HCI_EXT_ClkDivOnHaltCmd( HCI_EXT_ENABLE_CLK_DIVIDE_ON_HALT );

#if defined ( DC_DC_P0_7 )

  // Enable stack to toggle bypass control on TPS62730 (DC/DC converter)
  HCI_EXT_MapPmIoPortCmd( HCI_EXT_PM_IO_PORT_P0, HCI_EXT_PM_IO_PORT_PIN7 );

#endif // defined ( DC_DC_P0_7 )

//  //turn on overlapped processing
  HCI_EXT_HaltDuringRfCmd(HCI_EXT_HALT_DURING_RF_DISABLE);
  HCI_EXT_OverlappedProcessingCmd(HCI_EXT_ENABLE_OVERLAPPED_PROCESSING);
//  
//  //disable halt during RF (needed for UART / SPI)
//  HCI_EXT_HaltDuringRfCmd(HCI_EXT_HALT_DURING_RF_DISABLE);
  
  // Setup a delayed profile startup
  osal_set_event( Ketamine_TaskID, KTM_START_DEVICE_EVT );
}

/*********************************************************************
 * @fn      Ketamine_ProcessEvent
 *
 * @brief   Simple BLE Peripheral Application Task event processor.  This function
 *          is called to process all events for the task.  Events
 *          include timers, messages and any other user defined events.
 *
 * @param   task_id  - The OSAL assigned task ID.
 * @param   events - events to process.  This is a bit map and can
 *                   contain more than one event.
 *
 * @return  events not processed
 */
uint8 advCount = 0; 

uint16 Ketamine_ProcessEvent( uint8 task_id, uint16 events )
{

  VOID task_id; // OSAL required parameter that isn't used in this function

  if ( events & SYS_EVENT_MSG )
  {
    uint8 *pMsg;

    if ( (pMsg = osal_msg_receive( Ketamine_TaskID )) != NULL )
    {
      Ketamine_ProcessOSALMsg( (osal_event_hdr_t *)pMsg );

      // Release the OSAL message
      VOID osal_msg_deallocate( pMsg );
    }

    // return unprocessed events
    return (events ^ SYS_EVENT_MSG);
  }

  if ( events & KTM_START_DEVICE_EVT )
  {
    // Start the Device
    VOID GAPRole_StartDevice( &Ketamine_PeripheralCBs );

    // Start Bond Manager
    VOID GAPBondMgr_Register( &Ketamine_BondMgrCBs );
    
    osal_start_timerEx( Ketamine_TaskID, KTM_PERIODIC_EVT, KTM_BROADCAST_EVT_PERIOD );
    
    HalLedSet( HAL_LED_1 | HAL_LED_2 |  HAL_LED_3 | HAL_LED_4, HAL_LED_MODE_OFF );

    return ( events ^ KTM_START_DEVICE_EVT );
  }

  if ( events & KTM_PERIODIC_EVT )
  {
    if( gapProfileState != GAPROLE_CONNECTED )
    {
      if(resetFlag == 1){
        initialParameter();
        resetFlag = 0;
        SimpleProfile_SetParameter( SIMPLEPROFILE_CHAR1, sizeof(globalState), &globalState );
      }
      if(directTerminate == 1){
        directTerminate = 0;
        uint8 disabled = FALSE;
        GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &disabled );
        return (events ^ KTM_PERIODIC_EVT);
      }
   
      advCount = advCount + 1;
      HalLedSet( HAL_LED_1 , HAL_LED_MODE_ON );
      ST_HAL_DELAY(1000);
      HalLedSet( HAL_LED_1 , HAL_LED_MODE_OFF );
      
//      if(advCount >= advMax){
//        HalLedSet( HAL_LED_1 , HAL_LED_MODE_OFF );
//        advCount = 0;
//        globalState = 1;
//        globalCount = 0; 
//        uint8 disabled = FALSE;
//        GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &disabled );
//        return (events ^ KTM_PERIODIC_EVT);
//      }
      uint8 current_adv_enabled_status;
      uint8 new_adv_enabled_status;

      //Find the current GAP advertisement status
      GAPRole_GetParameter( GAPROLE_ADVERT_ENABLED, &current_adv_enabled_status );

      if( current_adv_enabled_status == FALSE )
      {
        new_adv_enabled_status = TRUE;
        if ( KTM_PERIODIC_EVT_PERIOD )
        {
          osal_start_timerEx( Ketamine_TaskID, KTM_PERIODIC_EVT, KTM_BROADCAST_EVT_PERIOD );
        }
      }
      else
      {
        new_adv_enabled_status = FALSE;
        if ( KTM_PERIODIC_EVT_PERIOD )
        {
          osal_start_timerEx( Ketamine_TaskID, KTM_PERIODIC_EVT, KTM_BROADCAST_EVT_PERIOD/10  ); // adjust duty cycle 8000
        }
      }
      //change the GAP advertisement status to opposite of current status
      GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &new_adv_enabled_status );
    }
    
    
    // Restart timer
    else if( KTM_PERIODIC_EVT_PERIOD )
    {
      performPeriodicTask();
      if(globalState == 6 && serialCameraState == 0x30){
        osal_start_timerEx( Ketamine_TaskID, KTM_PERIODIC_EVT, KTM_SENDDATA_PERIOD);
      }
      else{
        osal_start_timerEx( Ketamine_TaskID, KTM_PERIODIC_EVT, KTM_PERIODIC_EVT_PERIOD);  // adjust duty cycle
      }
    }

    return (events ^ KTM_PERIODIC_EVT);
  }
  if ( events & KTM_COLORDELAY_EVT ){
    readColorAfterDelay(clrCnt);
    return (events ^ KTM_COLORDELAY_EVT);
  }
  if ( events & KTM_SENDDATA_EVT ){
    return (events ^ KTM_SENDDATA_EVT);
  }

  // Discard unknown events
  return 0;
}

/*********************************************************************
 * @fn      Ketamine_ProcessOSALMsg
 *
 * @brief   Process an incoming task message.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void Ketamine_ProcessOSALMsg( osal_event_hdr_t *pMsg )
{
  switch ( pMsg->event )
  {
  //#if defined( CC2540_MINIDK )
    case KEY_CHANGE:
      Ketamine_HandleKeys( ((keyChange_t *)pMsg)->state, ((keyChange_t *)pMsg)->keys );
      break;
  //#endif // #if defined( CC2540_MINIDK )

  default:
    // do nothing
    break;
  }
}

//#if defined( CC2540_MINIDK )
/*********************************************************************
 * @fn      Ketamine_HandleKeys
 *
 * @brief   Handles all key events for this device.
 *
 * @param   shift - true if in shift/alt.
 * @param   keys - bit field for key events. Valid entries:
 *                 HAL_KEY_SW_2
 *                 HAL_KEY_SW_1
 *
 * @return  none
 */
static void Ketamine_HandleKeys( uint8 shift, uint8 keys )
{
  uint8 current_adv_enabled_status;
  GAPRole_GetParameter( GAPROLE_ADVERT_ENABLED, &current_adv_enabled_status );
  if(current_adv_enabled_status == false){
    uint8 result =  osal_stop_timerEx(Ketamine_TaskID, KTM_PERIODIC_EVT);
    if( result == INVALID_EVENT_ID){
      initialParameter();
    }
    osal_start_timerEx( Ketamine_TaskID, KTM_PERIODIC_EVT, KTM_BROADCAST_EVT_PERIOD );
  }
  else{
    attHandleValueNoti_t debugNoti; 
    sendReadBuf(&debugNoti, &button_counter, 1, 0xDD);
  }
  button_counter++;
  
//  if ( keys & HAL_KEY_SW_1 )
//  {
//    SK_Keys |= SK_KEY_LEFT;
//  }
//
//  if ( keys & HAL_KEY_SW_2 )
//  {
//
//    SK_Keys |= SK_KEY_RIGHT;

    // if device is not in a connection, pressing the right key should toggle
    // advertising on and off
    // Note:  If PLUS_BROADCASTER is define this condition is ignored and
    //        Device may advertise during connections as well. 
//#ifndef PLUS_BROADCASTER  
//    if( gapProfileState != GAPROLE_CONNECTED )
//    {
//#endif // PLUS_BROADCASTER
//      uint8 current_adv_enabled_status;
//      uint8 new_adv_enabled_status;
//
//      //Find the current GAP advertisement status
//      GAPRole_GetParameter( GAPROLE_ADVERT_ENABLED, &current_adv_enabled_status );
//
//      if( current_adv_enabled_status == FALSE )
//      {
//        new_adv_enabled_status = TRUE;
//      }
//      else
//      {
//        new_adv_enabled_status = FALSE;
//      }
//
//      //change the GAP advertisement status to opposite of current status
//      GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &new_adv_enabled_status );
//#ifndef PLUS_BROADCASTER
//    }
//#endif // PLUS_BROADCASTER
//  }
//
//  // Set the value of the keys state to the Simple Keys Profile;
//  // This will send out a notification of the keys state if enabled
//  SK_SetParameter( SK_KEY_ATTR, sizeof ( uint8 ), &SK_Keys );
}
//#endif // #if defined( CC2540_MINIDK )

/*********************************************************************
 * @fn      peripheralStateNotificationCB
 *
 * @brief   Notification from the profile of a state change.
 *
 * @param   newState - new state
 *
 * @return  none
 */
static void peripheralStateNotificationCB( gaprole_States_t newState )
{
#ifdef PLUS_BROADCASTER
  static uint8 first_conn_flag = 0;
#endif // PLUS_BROADCASTER
  
  
  switch ( newState )
  {
    case GAPROLE_STARTED:
      {
        uint8 ownAddress[B_ADDR_LEN];
        uint8 systemId[DEVINFO_SYSTEM_ID_LEN];

        GAPRole_GetParameter(GAPROLE_BD_ADDR, ownAddress);

        // use 6 bytes of device address for 8 bytes of system ID value
        systemId[0] = ownAddress[0];
        systemId[1] = ownAddress[1];
        systemId[2] = ownAddress[2];

        // set middle bytes to zero
        systemId[4] = 0x00;
        systemId[3] = 0x00;

        // shift three bytes up
        systemId[7] = ownAddress[5];
        systemId[6] = ownAddress[4];
        systemId[5] = ownAddress[3];

        DevInfo_SetParameter(DEVINFO_SYSTEM_ID, DEVINFO_SYSTEM_ID_LEN, systemId);

        #if (defined HAL_LCD) && (HAL_LCD == TRUE)
          // Display device address
          HalLcdWriteString( bdAddr2Str( ownAddress ),  HAL_LCD_LINE_2 );
          HalLcdWriteString( "Initialized",  HAL_LCD_LINE_3 );
        #endif // (defined HAL_LCD) && (HAL_LCD == TRUE)
      }
      break;

    case GAPROLE_ADVERTISING:
      {
        #if (defined HAL_LCD) && (HAL_LCD == TRUE)
          HalLcdWriteString( "Advertising",  HAL_LCD_LINE_3 );
        #endif // (defined HAL_LCD) && (HAL_LCD == TRUE)
      }
      break;

    case GAPROLE_CONNECTED:
      {
        //reset adv counter once connected
        advCount = 0;
        #if (defined HAL_LCD) && (HAL_LCD == TRUE)
          HalLcdWriteString( "Connected",  HAL_LCD_LINE_3 );
        #endif // (defined HAL_LCD) && (HAL_LCD == TRUE)
          // Set timer for first periodic event
          // osal_start_timerEx( Ketamine_TaskID, KTM_PERIODIC_EVT, 4000 );
          
#ifdef PLUS_BROADCASTER
        // Only turn advertising on for this state when we first connect
        // otherwise, when we go from connected_advertising back to this state
        // we will be turning advertising back on.
        if ( first_conn_flag == 0 ) 
        {
          uint8 adv_enabled_status = 1;
          GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8), &adv_enabled_status); // Turn on Advertising
          first_conn_flag = 1;
        }
#endif // PLUS_BROADCASTER
      }
      break;

    case GAPROLE_CONNECTED_ADV:
      {
        #if (defined HAL_LCD) && (HAL_LCD == TRUE)
          HalLcdWriteString( "Connected Advertising",  HAL_LCD_LINE_3 );
        #endif // (defined HAL_LCD) && (HAL_LCD == TRUE)
      }
      break;      
    case GAPROLE_WAITING:
      {
        #if (defined HAL_LCD) && (HAL_LCD == TRUE)
          HalLcdWriteString( "Disconnected",  HAL_LCD_LINE_3 );
        #endif // (defined HAL_LCD) && (HAL_LCD == TRUE)
      }
      break;

    case GAPROLE_WAITING_AFTER_TIMEOUT:
      {
        #if (defined HAL_LCD) && (HAL_LCD == TRUE)
          HalLcdWriteString( "Timed Out",  HAL_LCD_LINE_3 );
        #endif // (defined HAL_LCD) && (HAL_LCD == TRUE)
          
#ifdef PLUS_BROADCASTER
        // Reset flag for next connection.
        first_conn_flag = 0;
#endif //#ifdef (PLUS_BROADCASTER)
      }
      break;

    case GAPROLE_ERROR:
      { 
        #if (defined HAL_LCD) && (HAL_LCD == TRUE)
          HalLcdWriteString( "Error",  HAL_LCD_LINE_3 );
        #endif // (defined HAL_LCD) && (HAL_LCD == TRUE)
      }
      break;

    default:
      {
        #if (defined HAL_LCD) && (HAL_LCD == TRUE)
          HalLcdWriteString( "",  HAL_LCD_LINE_3 );
        #endif // (defined HAL_LCD) && (HAL_LCD == TRUE)
      }
      break;

  }

  gapProfileState = newState;

#if !defined( CC2540_MINIDK )
  VOID gapProfileState;     // added to prevent compiler warning with
                            // "CC2540 Slave" configurations
#endif


}

/*********************************************************************
 * @fn      performPeriodicTask
 *
 * @brief   Perform a periodic application task. This function gets
 *          called every five seconds as a result of the KTM_PERIODIC_EVT
 *          OSAL event. In this example, the value of the third
 *          characteristic in the SimpleGATTProfile service is retrieved
 *          from the profile, and then copied into the value of the
 *          the fourth characteristic.
 *
 * @param   none
 *
 * @return  none
 */    

/*Variables for application control*/
uint8 buf[20];
bool eepResult = false;
uint16 firstThres = 75;
uint16 secondThres = 60;
bool isfirstSaliva = FALSE;
bool isSecondSaliva = FALSE;
attHandleValueNoti_t noti; 
attHandleValueNoti_t notiColor;
uint8 waitCamera = 0;
uint8 serialCameraState = 0;


static void performPeriodicTask( void )
{ 
  int i;
  for(i = 0; i < 20; i++){
    buf[i] = 0;
  }
  
  globalCount++;
//  writeTestPaperId(somedata1, 5);
  
//  if(globalCount > globalMax){
//    // Terminate after globalState is not changed for 10 min.
//    initialParameter();
//    GAPRole_TerminateConnection();
//    SimpleProfile_SetParameter( SIMPLEPROFILE_CHAR1, sizeof(globalState), &globalState );
//    globalCount = 0;
//  }
  
  
  if(globalState < 3){
    eepResult = i2c_eeprom_read_buffer(EEPROM_ADDR, 0, buf, 5);
    if(eepResult == TRUE ){
      sendReadBuf(&noti, buf, 5, 0xFB);
      HalLedSet( HAL_LED_3 , HAL_LED_MODE_ON );
    }
    else{
      sendReadBuf(&noti, buf, 0, 0xFA);
      HalLedSet( HAL_LED_3 , HAL_LED_MODE_OFF );
    }
  }
  
  /*
  if(eepResult == false){
    sendReadBuf(&noti, buf, 0, 0xFA);
    //return;
  }
  */
  
  switch (globalState){
  case 1:

    HalLedSet( HAL_LED_2 , HAL_LED_MODE_ON );
    ST_HAL_DELAY(1000);
    HalLedSet( HAL_LED_2 , HAL_LED_MODE_OFF );
    
    initialParameter();
    break;
    
  case 2:
    HalLedSet( HAL_LED_1 , HAL_LED_MODE_ON );
    ST_HAL_DELAY(1000);
    HalLedSet( HAL_LED_1 , HAL_LED_MODE_OFF );
    HalAdcInit ();
    HalAdcSetReference (HAL_ADC_REF_AVDD);
    uint16 adcvalue = HalAdcRead (HAL_ADC_CHANNEL_5, HAL_ADC_RESOLUTION_8);
    buf[0] = adcvalue & 0xFF;
    buf[1] = (adcvalue >> 8) & 0xFF;
    if(isSecondSaliva == FALSE){
      if(isfirstSaliva == FALSE){
        sendReadBuf(&noti, buf, 2, 0xFC);
        if(adcvalue > firstThres)
          isfirstSaliva = TRUE;
      }else{
        sendReadBuf(&noti, buf, 2, 0xFD);
        if(adcvalue < secondThres)
          isSecondSaliva = TRUE;
      }
    }else{
      sendReadBuf(&noti, buf, 2, 0xFE);
    }
    break;
    
  case 3:
    //P0SEL &= ~0x38;
    //P0DIR |= 0x38;
    P1_2 = 1;
    
    if(clrCnt == 0){
      HalLedSet( HAL_LED_2 , HAL_LED_MODE_ON );
      ST_HAL_DELAY(1250);
      notiColor.handle = 0x2E;  
      notiColor.len = 17;
      for(i = 0; i < 17; i++){
        notiColor.value[i] = 0;
      }
      HalColorInit(COLOR_SENSOR_ADDR); //0x39
      setReadReg(COLOR_SENSOR_ADDR);
      osal_start_timerEx( Ketamine_TaskID, KTM_COLORDELAY_EVT, KTM_COLORDELAY_PERIOD );
    }
    else{
      HalLedSet( HAL_LED_1 , HAL_LED_MODE_ON );
      ST_HAL_DELAY(1250);
      HalColorInit(COLOR_SENSOR_ADDR2);
      setReadReg(COLOR_SENSOR_ADDR2);
      osal_start_timerEx( Ketamine_TaskID, KTM_COLORDELAY_EVT, KTM_COLORDELAY_PERIOD );
    }
    break;
    
  case 4:
    if( disconnectCnt == 0){
      GAPRole_TerminateConnection();
      disconnectCnt++;
      resetFlag = 1;
    }
    HalLedSet( HAL_LED_1 | HAL_LED_2 , HAL_LED_MODE_ON );
    ST_HAL_DELAY(1000);
    HalLedSet( HAL_LED_1 | HAL_LED_2 , HAL_LED_MODE_OFF );
    
    break;
    
  case 5:
    directTerminate = 1;
    if( disconnectCnt == 0){
      GAPRole_TerminateConnection();
      disconnectCnt++;
      resetFlag = 1;
    }
    HalLedSet( HAL_LED_1 | HAL_LED_2 , HAL_LED_MODE_ON );
    ST_HAL_DELAY(1000);
    HalLedSet( HAL_LED_1 | HAL_LED_2 , HAL_LED_MODE_OFF );
    break;
    
  case 6:{
    HalUARTInit(); 
    NPI_InitTransport(cSerialPacketParser);
    
    HalLedInit();
    HalLedSet( HAL_LED_1 , HAL_LED_MODE_ON );
    ST_HAL_DELAY(125);
    
    switch (serialCameraState){
    case 0:{
      osal_pwrmgr_device(PWRMGR_ALWAYS_ON);
      uint8 cmd[] = {0xaa,0x0d|cameraAddr,0x00,0x00,0x00,0x00};
      clearRxBuf();
      sendCmd(cmd, 6);
      break;
    }
    case 0x10:{
      uint8 cmd[] = { 0xaa, 0x01 | cameraAddr, 0x00, 0x07, 0x00, PIC_FMT };
      clearRxBuf();
      sendCmd(cmd, 6);
      break;
    }
    case 0x20:{
      uint8 cmd[] = { 0xaa, 0x06 | cameraAddr, 0x08, PIC_PKT_LEN & 0xff, (PIC_PKT_LEN>>8) & 0xff ,0};
      clearRxBuf();
      sendCmd(cmd, 6);
      break;
    }
    case 0x21:{
      uint8 cmd[] = { 0xaa, 0x05 | cameraAddr, 0, 0, 0, 0};
      clearRxBuf();
      sendCmd(cmd, 6);
      break;
    }
    case 0x22:{
      uint8 cmd[] = { 0xaa, 0x04 | cameraAddr, 0x1, 0, 0, 0};
      clearRxBuf();
      sendCmd(cmd, 6);
      break;
    }
    case 0x23:{
      break;
    }
    case 0x24:{
      if(waitBLEAck == 5){
        serialCameraState = 0x30;
        waitBLEAck = 0;
      }
      else{
        notifyPicInfo();
      }
      break;
    }
    case 0x30:{
      waitCamera++;
      waitCamera %= 10;
      if(waitCamera == 9){
        serialCameraState = 0x10;
        waitBLEAck = 0;
        break;
      }
      
      if(waitBLEAck == 0){
        if(tmpPktIdx < pktCnt){
//          uint8 cmd[] = { 0xaa, 0x0e | cameraAddr, 0x00, 0x00, 0x00, 0x00 };
//          cmd[4] = tmpPktIdx & 0xff;
//          cmd[5] = (tmpPktIdx >> 8) & 0xff;
//          
//          clearRxBuf();
//          pktRxByteOffset = 0;
//          sendCmd(cmd, 6);
          getPictureData();
        }
      }else if(waitBLEAck == 5){
        tmpPktIdx++;
        waitBLEAck = 0;
        
        if(tmpPktIdx == pktCnt-1)
          isLastPkt = 1;
        else if(tmpPktIdx == pktCnt){
          globalState = 7;
          SimpleProfile_SetParameter( SIMPLEPROFILE_CHAR1, sizeof(globalState), &globalState );
          serialCameraState = 0;
        }
        else{
        }
      }
     

//      HalLedSet( HAL_LED_2 , HAL_LED_MODE_ON );
//      ST_HAL_DELAY(1000);
//      HalLedSet( HAL_LED_2, HAL_LED_MODE_OFF );
      break;
    }
    }
    break;
  }
  case 7:{
    HalUARTSuspend();
    osal_pwrmgr_device(PWRMGR_BATTERY);
    HalLedSet( HAL_LED_1 | HAL_LED_2 , HAL_LED_MODE_ON );
    ST_HAL_DELAY(1000);
    HalLedSet( HAL_LED_1 | HAL_LED_2, HAL_LED_MODE_OFF );
    break;
  }
  case 8:{
    osal_pwrmgr_device( PWRMGR_BATTERY );
    break;
  }
  case 9:{
    osal_pwrmgr_device( PWRMGR_ALWAYS_ON );
    break;
  }
  default:
    break;
  }
} 

void readColorAfterDelay(uint8 state){
  if(state == 0){
    struct RGBC rgbc = ReadRGB(COLOR_SENSOR_ADDR);
    notiColor.value[0] = 0xFF;
    notiColor.value[1] = rgbc.red & 0xFF;
    notiColor.value[2] = (rgbc.red >> 8) & 0xFF;
    notiColor.value[3] = rgbc.green & 0xFF;
    notiColor.value[4] = (rgbc.green >> 8) & 0xFF;
    notiColor.value[5] = rgbc.blue & 0xFF;
    notiColor.value[6] = (rgbc.blue >> 8) & 0xFF;
    notiColor.value[7] = rgbc.clear & 0xFF;
    notiColor.value[8] = (rgbc.clear >> 8) & 0xFF;
    clrCnt = 1;
    HalLedSet( HAL_LED_2 , HAL_LED_MODE_OFF );     //(4) larry
    P1_2 = 0;
    
    eepResult = i2c_eeprom_read_buffer(EEPROM_ADDR, 0, buf, 5);
    if(eepResult == TRUE ){
      sendReadBuf(&noti, buf, 5, 0xFB);
      HalLedSet( HAL_LED_3 , HAL_LED_MODE_ON );
    }
    else{
      sendReadBuf(&noti, buf, 0, 0xFA);
      HalLedSet( HAL_LED_3 , HAL_LED_MODE_OFF );
    }
  }
  else{
    struct RGBC rgbc2 = ReadRGB(COLOR_SENSOR_ADDR2);
    notiColor.value[9] = rgbc2.red & 0xFF;
    notiColor.value[10] = (rgbc2.red >> 8) & 0xFF;
    notiColor.value[11] = rgbc2.green & 0xFF;
    notiColor.value[12] = (rgbc2.green >> 8) & 0xFF;
    notiColor.value[13] = rgbc2.blue & 0xFF;
    notiColor.value[14] = (rgbc2.blue >> 8) & 0xFF;
    notiColor.value[15] = rgbc2.clear & 0xFF;
    notiColor.value[16] = (rgbc2.clear >> 8) & 0xFF;
    HalLedSet( HAL_LED_1 , HAL_LED_MODE_OFF );       //  (2) larry
    GATT_Notification(0, &notiColor, FALSE);
    clrCnt = 0;
    P1_2 = 0;
  }
}

/*********************************************************************
 * @fn      simpleProfileChangeCB
 *
 * @brief   Callback from SimpleBLEProfile indicating a value change
 *
 * @param   paramID - parameter ID of the value that was changed.
 *
 * @return  none
 */
static void simpleProfileChangeCB( uint8 paramID )
{
  HalLedSet( HAL_LED_1 | HAL_LED_2 , HAL_LED_MODE_ON );
  ST_HAL_DELAY(1000);
  HalLedSet( HAL_LED_1 | HAL_LED_2 , HAL_LED_MODE_OFF );
  
  uint8 newValue;
  globalCount = 0;
  switch( paramID )
  {
    case SIMPLEPROFILE_CHAR1:
      SimpleProfile_GetParameter( SIMPLEPROFILE_CHAR1, &newValue );
      globalState = newValue;
      
      if(newValue == 6){
        serialCameraState = 0;
        waitCamera = 0;
        waitBLEAck = 0;
      }

      break;

    case SIMPLEPROFILE_CHAR3:
      SimpleProfile_GetParameter( SIMPLEPROFILE_CHAR3, &newValue );
      parseBLECmd(newValue);
      //waitBLEAck = newValue;
      
      break;

    default:
      // should not reach here!
      break;
  }
}

#if (defined HAL_LCD) && (HAL_LCD == TRUE)
/*********************************************************************
 * @fn      bdAddr2Str
 *
 * @brief   Convert Bluetooth address to string. Only needed when
 *          LCD display is used.
 *
 * @return  none
 */
char *bdAddr2Str( uint8 *pAddr )
{
  uint8       i;
  char        hex[] = "0123456789ABCDEF";
  static char str[B_ADDR_STR_LEN];
  char        *pStr = str;

  *pStr++ = '0';
  *pStr++ = 'x';

  // Start from end of addr
  pAddr += B_ADDR_LEN;

  for ( i = B_ADDR_LEN; i > 0; i-- )
  {
    *pStr++ = hex[*--pAddr >> 4];
    *pStr++ = hex[*pAddr & 0x0F];
  }
  *pStr = 0;
  return str;
}
#endif // (defined HAL_LCD) && (HAL_LCD == TRUE)

/*********************************************************************
 * @fn      OpenUART
 *
 * @brief   Open Pic32 when connected.
 *
 * @return  none
 */
void OpenUART(void)
{
  HalLedSet( HAL_LED_1 | HAL_LED_2 |  HAL_LED_3 | HAL_LED_4, HAL_LED_MODE_OFF );
  //HalUARTInit();              // Init UART on ISR
  //NPI_InitTransport(cSerialPacketParser);
}

/*********************************************************************
 * @fn      CloseUART
 *
 * @brief   Close Pic32 when disconnected.
 *
 * @return  none
 */
void CloseUART(void)
{
  HalLedSet( HAL_LED_1 | HAL_LED_2 | HAL_LED_3 | HAL_LED_4, HAL_LED_MODE_OFF );
  // P1SEL &= ~0x30;       // Turn off UART on P1_4 and p1_5
  // P1DIR &= ~0x30;
}

/*********************************************************************
*********************************************************************/

void initialParameter(void){
  isfirstSaliva = FALSE;
  isSecondSaliva = FALSE;
  eepResult = false;
  globalState = 1;
  clrCnt = 0;
  disconnectCnt = 0;
}

void parseBLECmd(uint8 value){
  if( (value & 0xF0) == 0){
    waitBLEAck = value;
  }
  else{
    //requestDataFrom(value & 0x0F);
    tmpPktIdx = value & 0x0F;
    getPictureData();
    waitBLEAck = 0xF0;
  }
}

void getPictureData(){
  uint8 cmd[] = { 0xaa, 0x0e | cameraAddr, 0x00, 0x00, 0x00, 0x00 };
  cmd[4] = tmpPktIdx & 0xff;
  cmd[5] = (tmpPktIdx >> 8) & 0xff;
          
  clearRxBuf();
  pktRxByteOffset = 0;
  sendCmd(cmd, 6);
}
