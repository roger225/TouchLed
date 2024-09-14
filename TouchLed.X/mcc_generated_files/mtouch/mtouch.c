/*
    MICROCHIP SOFTWARE NOTICE AND DISCLAIMER:

    You may use this software, and any derivatives created by any person or
    entity by or on your behalf, exclusively with Microchip's products.
    Microchip and its subsidiaries ("Microchip"), and its licensors, retain all
    ownership and intellectual property rights in the accompanying software and
    in all derivatives hereto.

    This software and any accompanying information is for suggestion only. It
    does not modify Microchip's standard warranty for its products.  You agree
    that you are solely responsible for testing the software and determining
    its suitability.  Microchip has no obligation to modify, test, certify, or
    support the software.

    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS".  NO WARRANTIES, WHETHER
    EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, IMPLIED
    WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
    PARTICULAR PURPOSE APPLY TO THIS SOFTWARE, ITS INTERACTION WITH MICROCHIP'S
    PRODUCTS, COMBINATION WITH ANY OTHER PRODUCTS, OR USE IN ANY APPLICATION.

    IN NO EVENT, WILL MICROCHIP BE LIABLE, WHETHER IN CONTRACT, WARRANTY, TORT
    (INCLUDING NEGLIGENCE OR BREACH OF STATUTORY DUTY), STRICT LIABILITY,
    INDEMNITY, CONTRIBUTION, OR OTHERWISE, FOR ANY INDIRECT, SPECIAL, PUNITIVE,
    EXEMPLARY, INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, FOR COST OR EXPENSE OF
    ANY KIND WHATSOEVER RELATED TO THE SOFTWARE, HOWSOEVER CAUSED, EVEN IF
    MICROCHIP HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE
    FORESEEABLE.  TO THE FULLEST EXTENT ALLOWABLE BY LAW, MICROCHIP'S TOTAL
    LIABILITY ON ALL CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED
    THE AMOUNT OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR
    THIS SOFTWARE.

    MICROCHIP PROVIDES THIS SOFTWARE CONDITIONALLY UPON YOUR ACCEPTANCE OF
    THESE TERMS.
*/

#include <xc.h>
#include <stdint.h>

#include "mtouch.h"
#include "../mcc.h"

/* ======================================================================= *
 * Definitions
 * ======================================================================= */
#define MTOUCH_SCAN_TIMER_TICK                  0.5 //unit us
#define MTOUCH_SCAN_RELOAD                      (uint16_t)(65535-((MTOUCH_SCAN_INTERVAL*1000.0)/MTOUCH_SCAN_TIMER_TICK)) 
#define MTOUCH_LOWPOWER_SCAN_RELOAD             (uint16_t)(65535-((MTOUCH_LOWPOWER_SCAN_INTERVAL*1000.0)/MTOUCH_SCAN_TIMER_TICK)) 
#define MTOUCH_LOWPOWER_INACTIVE_TIMEOUT_CYCLE  (uint16_t)(MTOUCH_LOWPOWER_INACTIVE_TIMEOUT/MTOUCH_SCAN_INTERVAL)
#define MTOUCH_LOWPOWER_BASELINEUPDATE_CYCLE    (uint16_t)(MTOUCH_LOWPOWER_BASELINEUPDATE_TIME/MTOUCH_LOWPOWER_SCAN_INTERVAL)


/* ======================================================================= *
 * Local Variable
 * ======================================================================= */

static bool mtouch_time_toScan = false;
static bool mtouch_request_init = false;
static uint16_t mTouchScanReload = MTOUCH_SCAN_RELOAD;
static bool mtouch_lowpowerEnabled = true;
static bool mtouch_lowpowerActivated = false;
static uint16_t mtouch_inactive_counter = 0;
static uint16_t mtouch_sleep_baseline_counter = 0;
const  uint8_t mtouch_sleep_sensors[] = MTOUCH_LOWPOWER_SENSOR_LIST;

/*
 * =======================================================================
 *  Local Functions
 * =======================================================================
 */
static void MTOUCH_ScanScheduler(void);   
static void MTOUCH_Lowpower_Initialize();
static bool MTOUCH_needReburst(void);

/*
 * =======================================================================
 * MTOUCH_ScanScheduler()
 * =======================================================================
 *  The interrupt handler callback for scanrate timer  
 */
static void MTOUCH_ScanScheduler(void)         
{
  
    //schedule the next timer1 interrupt
    TMR1_WriteTimer(mTouchScanReload);
    
    //schedule the scan
    mtouch_time_toScan = true;  

}
/*
 * =======================================================================
 * MTOUCH_Service_isInProgress()
 * =======================================================================
 *  indicate the mTouch service status
 */

bool MTOUCH_Service_isInProgress()
{
    return mtouch_time_toScan;
}

/*
 * =======================================================================
 * MTOUCH_Lowpower_Initialize()
 * =======================================================================
 *  initialize the registers and settings for low power operation
 */
static void MTOUCH_Lowpower_Initialize()
{
    /* Uncomment the line below if the part have VREGCON register*/
    /* Enable low-power sleep mode for Voltage Regulator. */
    // VREGCONbits.VREGPM = 1;
}

/*
 * =======================================================================
 * MTOUCH_Init()
 * =======================================================================
 *  Root initialization routine for all enabled mTouch library modules.
 */
void MTOUCH_Initialize(void)
{   
    MTOUCH_Sensor_InitializeAll();
    MTOUCH_Button_InitializeAll();
    MTOUCH_Sensor_Sampled_ResetAll();
    MTOUCH_Sensor_Scan_Initialize();
    MTOUCH_Lowpower_Initialize();
    TMR1_SetInterruptHandler(MTOUCH_ScanScheduler);

}

/*
 * =======================================================================
 * MTOUCH_Service_Mainloop()
 * =======================================================================
 *  Root mainloop service routine for all enabled mTouch library modules.
 */
bool MTOUCH_Service_Mainloop(void)
{

    if(mtouch_request_init)
    {
        MTOUCH_Initialize();
        mtouch_request_init = false;
    }

    
    if(mtouch_time_toScan)               
    {
        if(MTOUCH_Sensor_SampleAll() == false)     
            return false;  

        if(mtouch_lowpowerActivated && mtouch_lowpowerEnabled)
        {
            mtouch_time_toScan = false;
            MTOUCH_Sensor_Sampled_ResetAll();  

            if(MTOUCH_Sensor_isAnySensorActive())
            {
                MTOUCH_Service_exitLowpower();
                mtouch_inactive_counter = 0;
            }
            else
            {
                /* Exit low power temporarily for baseline update */
                if((0u != MTOUCH_LOWPOWER_BASELINEUPDATE_CYCLE) &&
                   (++mtouch_sleep_baseline_counter == MTOUCH_LOWPOWER_BASELINEUPDATE_CYCLE)) 
                {
                    MTOUCH_Button_Baseline_ForceUpdate();
                    MTOUCH_Service_exitLowpower();
                    mtouch_sleep_baseline_counter = 0;
                    mtouch_inactive_counter = 
                            MTOUCH_LOWPOWER_INACTIVE_TIMEOUT_CYCLE - 1;
                }
                SLEEP();
                NOP();
                NOP();
            }
            return true;
        }
        else
        {
            MTOUCH_Button_ServiceAll();             /* Execute state machine for all buttons w/scanned sensors */
            mtouch_time_toScan = MTOUCH_needReburst();
            MTOUCH_Sensor_Sampled_ResetAll();  
            MTOUCH_Tick();
            if(mtouch_lowpowerEnabled)
            {
                if(MTOUCH_Button_Buttonmask_Get())
                {
                    mtouch_inactive_counter = 0;
                }
                else
                {
                    if(++mtouch_inactive_counter == 
                       MTOUCH_LOWPOWER_INACTIVE_TIMEOUT_CYCLE)
                    {
                        MTOUCH_Service_enterLowpower();
                        mtouch_sleep_baseline_counter = 0;
                        SLEEP();
                        NOP();
                        NOP();
                    }
                }
            }
            return true;
        }   
    }
    else                              
    {
        return false;                
    }
}

/*
 * =======================================================================
 * MTOUCH_Tick
 * =======================================================================
 */
void MTOUCH_Tick(void)
{
    MTOUCH_Button_Tick();
}

/*
 * =======================================================================
 * MTOUCH_Reburst
 * =======================================================================
 */
 static bool MTOUCH_needReburst(void)
 {
    bool needReburst = false;
    
    return needReburst;
 }

/*
 * =======================================================================
 * MTOUCH_Service_enterLowpower
 * =======================================================================
 */
void MTOUCH_Service_enterLowpower(void)
{
    uint8_t i;
    mtouch_lowpowerActivated = true;
    
    for(i=0;i<MTOUCH_SENSORS;i++)
    {
        MTOUCH_Sensor_Disable (i);
    }
    
    for(i=0;i < sizeof(mtouch_sleep_sensors);i++)
    {
        MTOUCH_Sensor_Enable(mtouch_sleep_sensors[i]);
    }

    mTouchScanReload = MTOUCH_LOWPOWER_SCAN_RELOAD;
    MTOUCH_Sensor_startLowpower();
}

/*
 * =======================================================================
 * MTOUCH_Service_exitLowpower
 * =======================================================================
 */
void MTOUCH_Service_exitLowpower(void)
{
    uint8_t i;
    
    mtouch_lowpowerActivated = false;
    mTouchScanReload = MTOUCH_SCAN_RELOAD;
    
    for(i=0;i<MTOUCH_SENSORS;i++)
    {
        MTOUCH_Sensor_Enable (i);
    }
    MTOUCH_Sensor_exitLowpower ();
}

/*
 * =======================================================================
 * MTOUCH_Service_LowpowerState_Get
 * =======================================================================
 */
bool MTOUCH_Service_LowpowerState_Get(void)
{
    return mtouch_lowpowerActivated;
}

/*
 * =======================================================================
 * MTOUCH_Service_disableLowpower
 * =======================================================================
 */
void MTOUCH_Service_disableLowpower(void)
{
     mtouch_lowpowerEnabled = false;
}
 
/*
 * =======================================================================
 * MTOUCH_Service_enableLowpower
 * =======================================================================
 */
void MTOUCH_Service_enableLowpower(void)
{
    mtouch_lowpowerEnabled = true;
}

/*
 * =======================================================================
 * Request the initialization of mTouch library
 * note: this is designed to use in the Interrupt context so that the compiler
 *  does not duplicate the MTOUCH_Initialize() and causing possible hardware 
 *  stack overflow.
 * =======================================================================
 */
 void MTOUCH_requestInitSet(void)
 {
     mtouch_request_init = true;
 }

 bool MTOUCH_requestInitGet(void)
 {
     return mtouch_request_init;
 }