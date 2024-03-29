#include "Wire.h"
#include <Arduino.h>
#include <DoorControllerModule.h>

const std::string DoorControllerModule::name()
{
    return "DoorController";
}

const std::string DoorControllerModule::version()
{
    return MAIN_Version;
}

void DoorControllerModule::setup()
{
    pinMode(MAIN_TST_PIN, INPUT_PULLUP);
    pinMode(MAIN_NSK_PIN, OUTPUT_4MA);
    pinMode(MAIN_HSK_PIN, OUTPUT_4MA);
    pinMode(MAIN_MLD_PIN, OUTPUT_4MA);
    pinMode(MAIN_LCK_PIN, OUTPUT_4MA);
    pinMode(DOOR_OPEN_PIN, INPUT);
    pinMode(DOOR_CLOSED_PIN, INPUT);
    pinMode(LOCK_PIN, OUTPUT_4MA);
    pinMode(SENSOR_TST_PIN, OUTPUT_4MA);
    pinMode(SENSOR_INSIDE_RAD_PIN, INPUT_PULLUP);
    pinMode(SENSOR_INSIDE_AIR_PIN, INPUT_PULLUP);
    pinMode(SENSOR_OUTSIDE_RAD_PIN, INPUT_PULLUP);
    pinMode(SENSOR_OUTSIDE_AIR_PIN, INPUT_PULLUP);

    sensorInsideRadActive = digitalRead(SENSOR_INSIDE_RAD_PIN) == SENSOR_RAD_ACTIVE;
    sensorInsideAirActive = digitalRead(SENSOR_INSIDE_AIR_PIN) == SENSOR_AIR_ACTIVE;
    sensorOutsideRadActive = digitalRead(SENSOR_OUTSIDE_RAD_PIN) == SENSOR_RAD_ACTIVE;
    sensorOutsideAirActive = digitalRead(SENSOR_OUTSIDE_AIR_PIN) == SENSOR_AIR_ACTIVE;

    attachInterrupt(digitalPinToInterrupt(SENSOR_INSIDE_RAD_PIN), DoorControllerModule::interruptSensorInsideRadChange, CHANGE);
    attachInterrupt(digitalPinToInterrupt(SENSOR_INSIDE_AIR_PIN), DoorControllerModule::interruptSensorInsideAirChange, CHANGE);
    attachInterrupt(digitalPinToInterrupt(SENSOR_OUTSIDE_RAD_PIN), DoorControllerModule::interruptSensorOutsideRadChange, CHANGE);
    attachInterrupt(digitalPinToInterrupt(SENSOR_OUTSIDE_AIR_PIN), DoorControllerModule::interruptSensorOutsideAirChange, CHANGE);

    enableExtInterface();
}

void DoorControllerModule::processInputKo(GroupObject &ko)
{
    uint16_t lAsap = ko.asap();
    switch (lAsap)
    {
        case DOR_KoDoorMode:
            doorMode = static_cast<DoorMode>((byte)KoDOR_DoorMode.value(DPT_DecimalFactor));
            KoDOR_DoorModeStatus.valueNoSend((byte)doorMode, DPT_DecimalFactor);
            logDebugP("DoorMode changed: %d", doorMode);
            break;
        case DOR_KoSwitchInside:
            // switch trigger can only be used in manual door mode
            if (doorMode != MANUAL)
                break;

            // switch trigger can only be set, not reset externally
            switchInsideTrigger = KoDOR_SwitchInside.value(DPT_Switch) ? true : switchInsideTrigger;
            logDebugP("SwitchInside triggered");
            break;
        case DOR_KoSwitchOutside:
            // switch trigger can only be used in manual door mode
            if (doorMode != MANUAL)
                break;

            // switch trigger can only be set, not reset externally
            switchOutsideTrigger = KoDOR_SwitchOutside.value(DPT_Switch) ? true : switchOutsideTrigger;
            logDebugP("SwitchOutside triggered");
            break;
        case DOR_KoDoorLock:
            lockRequested = KoDOR_DoorLock.value(DPT_Switch);
            logDebugP("LockRequested: %d", lockRequested);
            break;
    }
}

uint16_t DoorControllerModule::flashSize()
{
    // Version + DoorMode
    return 1 + 1;
}

void DoorControllerModule::readFlash(const uint8_t *data, const uint16_t size)
{
    if (size == 0) // first call - without data
        return;

    byte version = openknx.flash.readByte();
    if (version != 1) // version unknown
    {
        logDebugP("Wrong version of flash data: version %d", version);
        return;
    }

    doorMode = static_cast<DoorMode>(openknx.flash.readByte());
    KoDOR_DoorMode.valueNoSend((byte)doorMode, DPT_DecimalFactor);
    KoDOR_DoorModeStatus.valueNoSend((byte)doorMode, DPT_DecimalFactor);
    logDebugP("DoorMode read from flash: %d", doorMode);
}

void DoorControllerModule::writeFlash()
{
    openknx.flash.writeByte(1); // Version
    openknx.flash.writeByte((byte)doorMode);
}

void DoorControllerModule::interruptSensorInsideRadChange()
{
    sensorInsideRadActive = digitalRead(SENSOR_INSIDE_RAD_PIN) == SENSOR_RAD_ACTIVE;
}

void DoorControllerModule::interruptSensorInsideAirChange()
{
    sensorInsideAirActive = digitalRead(SENSOR_INSIDE_AIR_PIN) == SENSOR_AIR_ACTIVE;
}

void DoorControllerModule::interruptSensorOutsideRadChange()
{
    sensorOutsideRadActive = digitalRead(SENSOR_OUTSIDE_RAD_PIN) == SENSOR_RAD_ACTIVE;
}

void DoorControllerModule::interruptSensorOutsideAirChange()
{
    sensorOutsideAirActive = digitalRead(SENSOR_OUTSIDE_AIR_PIN) == SENSOR_AIR_ACTIVE;
}

void DoorControllerModule::enableExtInterface()
{
    EXT_I2C_BUS.setSDA(EXT_I2C_SDA);
    EXT_I2C_BUS.setSCL(EXT_I2C_SCL);
    EXT_I2C_BUS.setClock(100000);

    extMcp1.begin_I2C(EXT1_MCP23017_ADR, &EXT_I2C_BUS);
    extMcp2.begin_I2C(EXT2_MCP23017_ADR, &EXT_I2C_BUS);
    for (size_t i = 0; i < 16; i++)
    {
        extMcp1.pinMode(i, OUTPUT);

        if (i <= 8)
        {
            if (i == EXT2_KNX_PRG_SWITCH_PIN)
                extMcp2.pinMode(i, INPUT_PULLUP);
            else
                extMcp2.pinMode(i, OUTPUT);
        }
    }

    ext2B = ext2B | EXT2_B::POWER_RUN;
}

void DoorControllerModule::loop()
{
    processTestSignal();
    checkProtection();
    updateDoorState();
    processDoorStateMachine();
    updateExtensionOutputs();
}

void DoorControllerModule::processTestSignal()
{
    int testSignal = analogRead(MAIN_TST_PIN);
    if (testSignal <= MAIN_TST_THRESHOLD - MAIN_TST_THRESHOLD_MARGIN)
    {
        digitalWrite(SENSOR_TST_PIN, SENSOR_TST_INACTIVE);
        sensorTstActive = false;
        mainTstActive = false;
    }
    else if (testSignal > MAIN_TST_THRESHOLD + MAIN_TST_THRESHOLD_MARGIN)
    {
        digitalWrite(SENSOR_TST_PIN, SENSOR_TST_ACTIVE);
        sensorTstActive = true;
        mainTstActive = true;
    }
}

void DoorControllerModule::checkProtection()
{
    mainHskActive = false;
    if (ParamDOR_SafetySensorHsk == 1)
        mainHskActive = sensorInsideAirActive;
    else if (ParamDOR_SafetySensorHsk == 2)
        mainHskActive = sensorOutsideAirActive;
    else if (ParamDOR_SafetySensorHsk == 3)
        mainHskActive = sensorInsideAirActive || sensorOutsideAirActive;

    digitalWrite(MAIN_HSK_PIN, mainHskActive ? MAIN_HSK_NSK_ACTIVE : MAIN_HSK_NSK_INACTIVE);

    mainNskActive = false;
    if (ParamDOR_SafetySensorNsk == 1)
        mainNskActive = sensorInsideAirActive;
    else if (ParamDOR_SafetySensorNsk == 2)
        mainNskActive = sensorOutsideAirActive;
    else if (ParamDOR_SafetySensorNsk == 3)
        mainNskActive = sensorInsideAirActive || sensorOutsideAirActive;

    digitalWrite(MAIN_NSK_PIN, mainNskActive ? MAIN_HSK_NSK_ACTIVE : MAIN_HSK_NSK_INACTIVE);
}

void DoorControllerModule::updateDoorState()
{
    int doorOpen = analogRead(DOOR_OPEN_PIN);
    if (doorOpen <= DOOR_SENSOR_THRESHOLD - DOOR_SENSOR_THRESHOLD_MARGIN)
    {
        doorState = DoorState::OPEN;
    }
    else if (doorOpen > DOOR_SENSOR_THRESHOLD + DOOR_SENSOR_THRESHOLD_MARGIN)
    {
        if (doorState == DoorState::OPEN)
            doorState = DoorState::CLOSING;
    }

    int doorClosed = analogRead(DOOR_CLOSED_PIN);
    if (doorClosed <= DOOR_SENSOR_THRESHOLD - DOOR_SENSOR_THRESHOLD_MARGIN)
    {
        doorState = DoorState::CLOSED;
    }
    else if (doorClosed > DOOR_SENSOR_THRESHOLD + DOOR_SENSOR_THRESHOLD_MARGIN)
    {
        if (doorState == DoorState::CLOSED)
            doorState = DoorState::OPENING;
    }

    if (doorStatePrevious != doorState)
    {
        if (doorState != DoorState::UNDEFINED)
            KoDOR_DoorStatus.valueNoSend((byte)doorState, DPT_Switch_Control);

        switch (doorState)
        {
            case DoorState::UNDEFINED:
                logDebugP("DoorState: UNDEFINED");
                break;

            case DoorState::OPEN:
                KoDOR_DoorOpenClosed.valueNoSend(true, DPT_Window_Door);
                logDebugP("DoorState: OPEN (doorOpen=%d, doorClosed=%d)", doorOpen, doorClosed);
                break;

            case DoorState::CLOSED:
                KoDOR_DoorOpenClosed.valueNoSend(false, DPT_Window_Door);
                logDebugP("DoorState: CLOSED (doorOpen=%d, doorClosed=%d)", doorOpen, doorClosed);
                break;

            case DoorState::OPENING:
                logDebugP("DoorState: OPENING (doorOpen=%d, doorClosed=%d)", doorOpen, doorClosed);
                break;

            case DoorState::CLOSING:
                logDebugP("DoorState: CLOSING (doorOpen=%d, doorClosed=%d)", doorOpen, doorClosed);
                break;
        }

        doorStatePrevious = doorState;
        doorStateChanged = true;
        doorStateLastChanged = millis();
    }

    /*logDebugP("doorOpen: %d", doorOpen);
    logDebugP("doorClosed: %d", doorClosed);
    delay(500);*/
}

void DoorControllerModule::processDoorStateMachine()
{
    if (doorMode != DoorMode::AUTOMATIC &&
        doorMode != DoorMode::MANUAL)
        return;

    if (mainLckStart > 0)
    {
        if ((millis() - mainLckStart >= MAIN_SIGNAL_LENGTH))
        {
            digitalWrite(MAIN_LCK_PIN, MAIN_LCK_INACTIVE);
            mainLckActive = false;
            mainLckStart = 0;
        }
    }

    if (mainMdlStart > 0)
    {
        if ((millis() - mainMdlStart >= MAIN_SIGNAL_LENGTH))
            sendMainMld(false);
    }
    else if (lockRequested != lockActive)
    {
        if (doorStateMachine == DoorStateMachine::STATE_CLOSED ||
            doorStateMachine == DoorStateMachine::STATE_CLOSED_LOCKED)
        {
            lock(lockRequested);
        }
        else if (millis() - lastLockRequestMld >= LOCK_REQUEST_MLD_TIMEOUT)
        {
            sendMainMld(true);
            lastLockRequestMld = millis();
        }
    }

    bool triggerMld = false;
    switch (doorStateMachine)
    {
        case DoorStateMachine::STATE_UNDEFINED:
            if (doorState == DoorState::OPEN)
                doorStateMachine = DoorStateMachine::STATE_OPEN_START;
            else if (doorState == DoorState::CLOSED)
                doorStateMachine = DoorStateMachine::STATE_CLOSED;
            break;

        case DoorStateMachine::STATE_OPEN_START:
            doorOpenSince = millis();
            doorStateMachine = DoorStateMachine::STATE_OPEN;
            break;

        case DoorStateMachine::STATE_OPEN:
            if (doorState != DoorState::OPEN)
            {
                doorStateMachine = DoorStateMachine::STATE_UNDEFINED;
                break;
            }

            if (doorMode == DoorMode::AUTOMATIC)
            {
                triggerMld =
                    !sensorInsideRadActive && !sensorOutsideRadActive &&
                    !sensorInsideAirActive && !sensorOutsideAirActive &&
                    (millis() - doorOpenSince >= DOOR_OPEN_MIN);
            }
            else
            {
                triggerMld =
                    !sensorInsideAirActive && !sensorOutsideAirActive &&
                    (switchInsideTrigger || switchOutsideTrigger);
            }

            if (triggerMld)
            {
                sendMainMld(true);
                doorStateMachine = DoorStateMachine::STATE_TRANSITION;
            }

            break;

        case DoorStateMachine::STATE_CLOSED:
            if (doorState != DoorState::CLOSED)
            {
                doorStateMachine = DoorStateMachine::STATE_UNDEFINED;
                break;
            }

            if (lockActive)
            {
                doorStateMachine = DoorStateMachine::STATE_CLOSED_LOCKED;
                break;
            }

            if (doorMode == DoorMode::AUTOMATIC)
                triggerMld = sensorInsideRadActive || sensorOutsideRadActive;
            else
                triggerMld = switchInsideTrigger || switchOutsideTrigger;

            if (triggerMld)
            {
                sendMainMld(true);
                doorStateMachine = DoorStateMachine::STATE_TRANSITION;
            }

            break;

        case DoorStateMachine::STATE_CLOSED_LOCKED:
            // this should not be possible, but just in case to avoid getting stuck
            if (doorState != DoorState::CLOSED)
            {
                doorStateMachine = DoorStateMachine::STATE_UNDEFINED;
                break;
            }

            if (!lockActive)
                doorStateMachine = DoorStateMachine::STATE_CLOSED;
            break;

        case DoorStateMachine::STATE_TRANSITION:
            if (doorStateChanged ||
                (millis() - doorStateLastChanged >= DOOR_STATE_CHANGED_TIMEOUT))
            {
                if (doorState == DoorState::OPEN)
                    doorStateMachine = DoorStateMachine::STATE_OPEN_START;
                else if (doorState == DoorState::CLOSED)
                    doorStateMachine = DoorStateMachine::STATE_CLOSED;
            }
            break;
    }

    if (doorStateMachinePrevious != doorStateMachine)
    {
        switch (doorStateMachine)
        {
            case DoorStateMachine::STATE_UNDEFINED:
                logDebugP("DoorStateMachine: STATE_UNDEFINED");
                break;

            case DoorStateMachine::STATE_OPEN_START:
                logDebugP("DoorStateMachine: STATE_OPEN_START");
                break;

            case DoorStateMachine::STATE_OPEN:
                logDebugP("DoorStateMachine: STATE_OPEN");
                break;

            case DoorStateMachine::STATE_CLOSED:
                logDebugP("DoorStateMachine: STATE_CLOSED");
                break;

            case DoorStateMachine::STATE_CLOSED_LOCKED:
                logDebugP("DoorStateMachine: STATE_CLOSED_LOCKED");
                break;

            case DoorStateMachine::STATE_TRANSITION:
                logDebugP("DoorStateMachine: STATE_TRANSITION");
                break;
        }

        doorStateMachinePrevious = doorStateMachine;
    }
}

void DoorControllerModule::sendMainMld(bool active)
{
    doorStateChanged = false;
    doorStateLastChanged = millis();

    digitalWrite(MAIN_MLD_PIN, active ? MAIN_MLD_ACTIVE : MAIN_MLD_INACTIVE);
    mainMldActive = active;
    mainMdlStart = active ? millis() : 0;
}

void DoorControllerModule::lock(bool active)
{
    digitalWrite(MAIN_LCK_PIN, MAIN_LCK_ACTIVE);
    mainLckActive = true;
    mainLckStart = millis();

    digitalWrite(LOCK_PIN, active ? LOCK_ACTIVE : LOCK_INACTIVE);
    KoDOR_DoorLockStatus.valueNoSend(active, DPT_Switch);
    lockActive = active;
}

void DoorControllerModule::updateExtensionOutputs()
{
    if (mainMldActive)
        ext1B = ext1B | EXT1_B::MAIN_MLD;
    else
        ext1B = ext1B & ~EXT1_B::MAIN_MLD;

    if (mainHskActive)
        ext1B = ext1B | EXT1_B::MAIN_HSK;
    else
        ext1B = ext1B & ~EXT1_B::MAIN_HSK;

    if (mainNskActive)
        ext1B = ext1B | EXT1_B::MAIN_NSK;
    else
        ext1B = ext1B & ~EXT1_B::MAIN_NSK;

    if (mainTstActive)
        ext1B = ext1B | EXT1_B::MAIN_TST;
    else
        ext1B = ext1B & ~EXT1_B::MAIN_TST;

    if (mainLckActive)
        ext1B = ext1B | EXT1_B::MAIN_LCK;
    else
        ext1B = ext1B & ~EXT1_B::MAIN_LCK;

    if (doorState == DoorState::CLOSED)
        ext1B = ext1B | EXT1_B::DOOR_CLD;
    else
        ext1B = ext1B & ~EXT1_B::DOOR_CLD;

    if (doorState == DoorState::OPEN)
        ext1B = ext1B | EXT1_B::DOOR_OPN;
    else
        ext1B = ext1B & ~EXT1_B::DOOR_OPN;

    if (sensorInsideRadActive)
        ext1B = ext1B | EXT1_B::SENSOR_INSIDE_RAD;
    else
        ext1B = ext1B & ~EXT1_B::SENSOR_INSIDE_RAD;

    if (sensorInsideAirActive)
        ext2A = ext2A | EXT2_A::SENSOR_INSIDE_AIR;
    else
        ext2A = ext2A & ~EXT2_A::SENSOR_INSIDE_AIR;

    if (sensorTstActive)
        ext2A = ext2A | EXT2_A::SENSOR_INSIDE_TST;
    else
        ext2A = ext2A & ~EXT2_A::SENSOR_INSIDE_TST;

    if (sensorOutsideRadActive)
        ext2A = ext2A | EXT2_A::SENSOR_OUTSIDE_RAD;
    else
        ext2A = ext2A & ~EXT2_A::SENSOR_OUTSIDE_RAD;

    if (sensorOutsideAirActive)
        ext2A = ext2A | EXT2_A::SENSOR_OUTSIDE_AIR;
    else
        ext2A = ext2A & ~EXT2_A::SENSOR_OUTSIDE_AIR;

    if (sensorTstActive)
        ext2A = ext2A | EXT2_A::SENSOR_OUTSIDE_TST;
    else
        ext2A = ext2A & ~EXT2_A::SENSOR_OUTSIDE_TST;

    ext1A = ext1A & ~EXT1_A::DOOR_MODE_CLD;
    ext1A = ext1A & ~EXT1_A::DOOR_MODE_OPN;
    ext1A = ext1A & ~EXT1_A::DOOR_MODE_MAN;
    ext1A = ext1A & ~EXT1_A::DOOR_MODE_AUT;
    switch (doorMode)
    {
        case DoorMode::ALWAYS_CLOSED:
            ext1A = ext1A | EXT1_A::DOOR_MODE_CLD;
            break;
        case DoorMode::ALWAYS_OPEN:
            ext1A = ext1A | EXT1_A::DOOR_MODE_OPN;
            break;
        case DoorMode::MANUAL:
            ext1A = ext1A | EXT1_A::DOOR_MODE_MAN;
            break;
        case DoorMode::AUTOMATIC:
            ext1A = ext1A | EXT1_A::DOOR_MODE_AUT;
            break;
    }

    if (lockRequested)
        ext1A = ext1A | EXT1_A::LOCK_RQT;
    else
        ext1A = ext1A & ~EXT1_A::LOCK_RQT;

    if (lockActive)
        ext1A = ext1A | EXT1_A::LOCK_ACT;
    else
        ext1A = ext1A & ~EXT1_A::LOCK_ACT;

    if (millis() - extProgSwitchLastTrigger >= EXT2_KNX_PRG_SWITCH_DEBOUNCE &&
        extMcp2.digitalRead(EXT2_KNX_PRG_SWITCH_PIN) == EXT2_KNX_PRG_SWITCH_ACTIVE)
    {
        extProgSwitchLastTrigger = millis();
        knx.toggleProgMode();
    }

    if (knx.progMode())
        ext2A = ext2A | EXT2_A::KNX_PRG;
    else
        ext2A = ext2A & ~EXT2_A::KNX_PRG;

    if (ext1ALastSent != ext1A)
    {
        ext1ALastSent = ext1A;
        extMcp1.writeGPIOA((u_int8_t)ext1A);
        logDebugP("ext1A: %d", (u_int8_t)ext1A);
    }

    if (ext1BLastSent != ext1B)
    {
        ext1BLastSent = ext1B;
        extMcp1.writeGPIOB((u_int8_t)ext1B);
        logDebugP("ext1B: %d", (u_int8_t)ext1B);
    }

    if (ext2ALastSent != ext2A)
    {
        ext2ALastSent = ext2A;
        extMcp2.writeGPIOA((u_int8_t)ext2A);
        logDebugP("ext2A: %d", (u_int8_t)ext2A);
    }

    if (ext2BLastSent != ext2B)
    {
        ext2BLastSent = ext2B;
        extMcp2.writeGPIOB((u_int8_t)ext2B);
        logDebugP("ext2B: %d", (u_int8_t)ext2B);
    }
}

DoorControllerModule openknxDoorControllerModule;