/**
 * Copyright 2019 Wyres
 * Licensed under the Apache License, Version 2.0 (the "License"); 
 * you may not use this file except in compliance with the License. 
 * You may obtain a copy of the License at
 *    http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, 
 * software distributed under the License is distributed on 
 * an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, 
 * either express or implied. See the License for the specific 
 * language governing permissions and limitations under the License.
*/
/**
 * Generic IO handling module for app-core
 */

#include "os/os.h"
#include "bsp/bsp.h"

#include "wyres-generic/wutils.h"
#include "wyres-generic/timemgr.h"
#include "wyres-generic/ledmgr.h"
#include "wyres-generic/gpiomgr.h"
#include "wyres-generic/rebootmgr.h"
#include "wyres-generic/movementmgr.h"
#include "wyres-generic/sensormgr.h"

#include "app-core/app_core.h"
#include "app-core/app_msg.h"

// Use the PTI module id, as won't have both at same time
#define MY_MOD_ID   (APP_MOD_PTI)

typedef enum { IO_DIN=0, IO_DOUT, IO_BUTTON, IO_STATE, IO_AIN, IO_PWMOUT } IO_TYPE;

// Number of managed IOs related to the syscfg defines being 0-7 : don't change it...
#define NB_IOS  (8)

// define our specific ul tags that only our app needs to decode
#define UL_APP_IO_STATE (APP_CORE_UL_APP_SPECIFIC_START)
#define DL_APP_IO_SET (APP_CORE_DL_APP_SPECIFIC_START)

// COntext data
static struct appctx {
    struct mio {
        int8_t gpio;        // if not -1, then active
        const char* name;
        IO_TYPE type;
        GPIO_IDLE_TYPE pull;
        uint8_t valueDL;
        uint8_t valueUL;
    } ios[NB_IOS];
} _ctx;

static void defineIO(int ioid, int gpio, const char* name, IO_TYPE t, GPIO_IDLE_TYPE pull, uint8_t initialValue);
static void initIOs();
static void deinitIOs();
static void readIOs();
static uint8_t readIO(int ioid);
static void writeIO(int ioid, uint8_t v);
static void iosetAction(uint8_t* v, uint8_t l);
static void buttonChangeCB(void* ctx, SR_BUTTON_STATE_t currentState, SR_BUTTON_PRESS_TYPE_t currentPressType);
static void stateInputChangeCB(void* ctx, SR_BUTTON_STATE_t currentState, SR_BUTTON_PRESS_TYPE_t currentPressType);

// My api functions
static uint32_t start() {
    log_debug("MIO:start IO check : 1s");
    return 1*1000;
}

static void stop() {
    log_debug("MIO:done");
}
static void off() {
    // ensure sensors are low power mode
    deinitIOs();
}
static void deepsleep() {
    // ensure sensors are off
    deinitIOs();
}
static bool getData(APP_CORE_UL_t* ul) {
    log_info("MIO: UL ");
    // Read values
    readIOs();
    // write to UL TS and current states
    /* structure equiv:
     * uint8_t[8] io analog read 1 byte per IO. 0 if its an output
     * uint8_t device state : 0=deactivated, 1=activated
     */
    uint8_t ds[NB_IOS+1];
    for(int i=0;i<NB_IOS;i++) {
        ds[i] = _ctx.ios[i].valueUL;
        _ctx.ios[i].valueUL = 0;      // reset value to ensure we get latest button press types
    }
    ds[NB_IOS] = (AppCore_isDeviceActive()?1:0);
    app_core_msg_ul_addTLV(ul, UL_APP_IO_STATE, 12, &ds[0]);
    return true;       // all critical!
}
static void tick() {
    // NOOP currently
}

static APP_CORE_API_t _api = {
    .startCB = &start,
    .stopCB = &stop,
    .offCB = &off,
    .deepsleepCB = &deepsleep,
    .getULDataCB = &getData,
    .ticCB = &tick,    
};
// Initialise module
void mod_io_init(void) {
    MYNEWT_VAL(IO_0);
    MYNEWT_VAL(IO_1);
    MYNEWT_VAL(IO_2);
    MYNEWT_VAL(IO_3);
    MYNEWT_VAL(IO_4);
    MYNEWT_VAL(IO_5);
    MYNEWT_VAL(IO_6);
    MYNEWT_VAL(IO_7);
    // hook app-core for env data
    AppCore_registerModule("IO", MY_MOD_ID, &_api, EXEC_PARALLEL);
    AppCore_registerAction(DL_APP_IO_SET, iosetAction);
    initIOs();
    log_info("MIO: io operation initialised");
}


// internals
static void defineIO(int ioid, int gpio, const char* name, IO_TYPE t, GPIO_IDLE_TYPE pull, uint8_t initialValue) {
    _ctx.ios[ioid].gpio = gpio;
    _ctx.ios[ioid].name = name;
    _ctx.ios[ioid].type = t;
    _ctx.ios[ioid].pull = pull;
    _ctx.ios[ioid].valueDL = initialValue;
}
static void initIOs() {
    for(int i=0;i<NB_IOS;i++) {
        if (_ctx.ios[i].gpio>=0) {
            switch (_ctx.ios[i].type) {
                case IO_DIN: {
                    GPIO_define_in(_ctx.ios[i].name, _ctx.ios[i].gpio, _ctx.ios[i].pull, LP_DOZE, HIGH_Z);
                    break;
                }
                case IO_AIN: {
                    GPIO_define_adc(_ctx.ios[i].name, _ctx.ios[i].gpio, _ctx.ios[i].gpio, LP_DOZE, HIGH_Z);
                    break;
                }
                case IO_BUTTON: {
                    SRMgr_defineButton(_ctx.ios[i].gpio);
                    // add cb for button press, context is id
                    SRMgr_registerButtonCB(_ctx.ios[i].gpio, buttonChangeCB, (void*)i);
                    break;
                }
                case IO_STATE: {
                    SRMgr_defineButton(_ctx.ios[i].gpio);
                    // add cb for change of state, context is id
                    SRMgr_registerButtonCB(_ctx.ios[i].gpio, stateInputChangeCB, (void*)i);
                    break;
                }
                case IO_DOUT: {
                    // config
                    GPIO_define_out(_ctx.ios[i].name, _ctx.ios[i].gpio, _ctx.ios[i].valueDL, LP_DOZE, HIGH_Z);
                    break;
                }
                case IO_PWMOUT: {
//TBD                    GPIO_define_pwm(_ctx.ios[i].name, _ctx.ios[i].gpio, _ctx.ios[i].valueDL, LP_DOZE, HIGH_Z);
                    break;
                }
                default: {
                    // ignore
                    break;
                }
            }
        }
    }
}
static void deinitIOs() {
    // Not required, GPIO mgr takes care of low powering
}

// Read an io
static uint8_t readIO(int ioid) {
    if (ioid>=0 && ioid<NB_IOS) {
        if (_ctx.ios[ioid].gpio>=0) {
            switch (_ctx.ios[ioid].type) {
                case IO_DIN: {
                    _ctx.ios[ioid].valueUL = (uint8_t)GPIO_read(_ctx.ios[ioid].gpio);
                    break;
                }
                case IO_AIN: {
                    _ctx.ios[ioid].valueUL = (uint8_t)GPIO_readADC(_ctx.ios[ioid].gpio);
                    break;
                }
                // Button dealt with by callback, its value is the last press type (not the press/release 1/0 value)
                default: {
                    // ignore
                    break;
                }
            }
        }
        return _ctx.ios[ioid].valueUL;
    }
    return 0;
}
// write an io
static void writeIO(int ioid, uint8_t val) {
    if (ioid>=0 && ioid<NB_IOS) {
        if (_ctx.ios[ioid].gpio>=0) {
            switch (_ctx.ios[ioid].type) {
                case IO_DOUT: {
                    GPIO_write(_ctx.ios[ioid].gpio, _ctx.ios[ioid].valueDL);
                    break;
                }
                case IO_PWMOUT: {
    // TODO                GPIO_writePWM(_ctx.ios[ioid].gpio, _ctx.ios[ioid].valueDL);
                    break;
                }
                default: {
                    // ignore
                    break;
                }
            }
        }
    }    
}

// Read all input type IOs
static void readIOs() {
    for(int i=0;i<NB_IOS;i++) {
        readIO(i);      // deals with invalid or output cases by ignoring them
    }
}

// DL action setting output ios
static void iosetAction(uint8_t* v, uint8_t l) {
    // Check got the right number of bytes
    if (l==NB_IOS) {
        for(int i=0;i<NB_IOS; i++) {
            if (_ctx.ios[i].type==IO_DOUT || _ctx.ios[i].type==IO_PWMOUT) {
                _ctx.ios[i].valueDL = v[i];
                writeIO(i, v[i]);
                log_info("DL io %d on gpio %d set to %d", i, _ctx.ios[i].gpio, v[i]);
            }
        }
        log_info("DL ios set");
    } else {
        log_warn("DL ios not set as wrong length %d", l);
    }
}

// callback each time button changes state
static void buttonChangeCB(void* ctx, SR_BUTTON_STATE_t currentState, SR_BUTTON_PRESS_TYPE_t currentPressType) {
    if (currentState==SR_BUTTON_RELEASED) {
        if (AppCore_isDeviceActive()) {
            // flag the button that caused the UL
            int bid = (int)ctx;
            if (bid>=0 && bid<NB_IOS) {
                log_info("MIO:button %d released, duration %d ms, press type:%d", bid, 
                    (SRMgr_getLastButtonReleaseTS(_ctx.ios[bid].gpio)-SRMgr_getLastButtonPressTS(_ctx.ios[bid].gpio)),
                    SRMgr_getLastButtonPressType(_ctx.ios[bid].gpio));
                _ctx.ios[bid].valueUL = currentPressType;
                // ask for immediate UL with only us consulted
                AppCore_forceUL(MY_MOD_ID);
            } else {
                log_warn("MIO:button release but bad id %d", ctx);
            }
        } else {
            log_info("MIO:button release ignore not active");
        }
    } else {
        log_info("MIO:button pressed");
    }
}

// For an input where we want to signal each change of state 
static void stateInputChangeCB(void* ctx, SR_BUTTON_STATE_t currentState, SR_BUTTON_PRESS_TYPE_t currentPressType) {
    if (AppCore_isDeviceActive()) {
        // find input that caused the state change
        int bid = (int)ctx;
        if (bid>=0 && bid<NB_IOS) {
            log_info("MIO:state input %d changed to %d", bid, currentState);
            _ctx.ios[bid].valueUL = currentState;
            // ask for immediate UL with only us consulted
            AppCore_forceUL(MY_MOD_ID);
        } else {
            log_warn("MIO:input state change but bad id %d", ctx);
        }
    } else {
        log_info("MIO:input state change ignore not active");
    }
}
