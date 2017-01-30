/***************************************************************************************************
 * Copyright (c) 2016, Imagination Technologies Limited and/or its affiliated group companies
 * and/or licensors
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions
 *    and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to
 *    endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file  sesame_gateway.c
 * @brief Sesame controller application acts as a AwaLWM2M client and observes the IPSO resource for
          digital output. On receipt of awaLWM2M notification, Ci40 will change the relay state
          according to current IPSO resource state. It also notifies Digital Input objects change 
          received from OptoClick sensor.
 */

/***************************************************************************************************
 * Includes
 **************************************************************************************************/

#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/time.h>
#include <letmecreate/letmecreate.h>
#include <awa/client.h>
#include <awa/common.h>

#include "log.h"

/***************************************************************************************************
 * Definitions
 **************************************************************************************************/

/** Calculate size of array. */
#define ARRAY_SIZE(x) ((sizeof x) / (sizeof *x))

#define OPERATION_PERFORM_TIMEOUT 1000
//! @cond Doxygen_Suppress

#define DOOR_OBJECT_ID (13201)
#define DOOR_OBJ_INSTANCE_OPEN (0)
#define DOOR_OBJ_INSTANCE_CLOSE (1)
#define DOOR_OBJ_INSTANCE_TRIGGER (2)
#define DOOR_TRIGGER_RESOURCE_ID (5523)
#define DOOR_COUNTER_RESOURCE_ID (5501)
#define DOOR_COUNTER_RESET_RESOURCE_ID (5505)
#define DOOR_DURATION_RESOURCE_ID (5521)
#define RELAY_MIKROBUS_INDEX MIKROBUS_1
#define RELAY_CLICK_IDX RELAY2_CLICK_RELAY_1

#define OPTO_OBJECT_ID (3200)
#define OPTO_RESOURCE_ID (5500)
#define OPTO_OBJ_INSTANCE_DOOR_OPENED (0)
#define OPTO_OBJ_INSTANCE_DOOR_CLOSED (1)
#define OPTO_MIKROBUS_INDEX MIKROBUS_2
#define OPTO_CHANNEL_DOOR_OPENED OPTO_CLICK_CHANNEL_1
#define OPTO_CHANNEL_DOOR_CLOSED OPTO_CLICK_CHANNEL_4

#define OPERATION_TIMEOUT (5000)
#define URL_PATH_SIZE (16)

//! @endcond

void ChangeRelayState(bool state);
void GarageDoorTrigger(void);

/***************************************************************************************************
 * Globals
 **************************************************************************************************/

/** Set default debug level to info. */
int g_debugLevel = LOG_INFO;
///** Set default debug stream to NULL. */
FILE *g_debugStream = NULL;
/** Determines whether we should keep main loop running. */
static volatile int g_keepRunning = 1;
/** Keeps current OptoClick 'Opened' state. */
uint8_t g_optoOpenedState = 0;
/** Keeps current OptoClick 'Closed' state. */
uint8_t g_optoClosedState = 0;
/** Measured time of begin/end of door open/close */
struct timeval g_openBegin, g_openEnd, g_closeBegin, g_closeEnd, g_sleepBegin, g_sleepEnd;
/** Count of open/close/trigger actions for door */
AwaInteger g_doorOpenCount = 0;
AwaInteger g_doorCloseCount = 0;
AwaInteger g_doorTriggerCount = 0;
/** Duration of open/close actions*/
AwaFloat g_doorOpenDuration = 0.0;
AwaFloat g_doorCloseDuration = 0.0;
AwaClientSession *session;
bool g_relayState = false;


/***************************************************************************************************
 * Implementation
 **************************************************************************************************/

static char *getPath(char *buf, int objectID, int instanceID, int resourceID)
{
    sprintf(buf, "/%d/%d/%d", objectID, instanceID, resourceID);
}

static void doorTriggerCallback(const AwaExecuteArguments *arguments, void *context)
{

    LOG(LOG_INFO, "Execute %d/%d/%d", DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_TRIGGER, DOOR_TRIGGER_RESOURCE_ID);
    g_openBegin.tv_sec = 0;
    g_openBegin.tv_usec = 0;
    g_closeBegin.tv_sec = 0;
    g_closeBegin.tv_usec = 0;
    GarageDoorTrigger();
}

static void doorCounterResetCallback(const AwaExecuteArguments *arguments, void *context)
{
    int objectInstanceID = *((int *)context);

    char buf[20];
    AwaClientSetOperation *operation = AwaClientSetOperation_New(session);
    AwaClientSetOperation_AddValueAsInteger(operation, buf, g_doorOpenCount);
    getPath(&buf, DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_CLOSE, DOOR_DURATION_RESOURCE_ID);
    AwaClientSetOperation_AddValueAsFloat(operation, buf, g_doorOpenDuration);
    AwaClientSetOperation_Perform(operation, OPERATION_PERFORM_TIMEOUT);
    AwaClientSetOperation_Free(&operation);

    switch (objectInstanceID)
    {
    case DOOR_OBJ_INSTANCE_OPEN:
        LOG(LOG_INFO, "Execute %d/%d/%d", DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_OPEN, DOOR_COUNTER_RESET_RESOURCE_ID);
        g_doorOpenCount = 0;
        getPath(&buf, DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_OPEN, DOOR_COUNTER_RESET_RESOURCE_ID);
        AwaClientSetOperation_AddValueAsInteger(operation, buf, g_doorOpenCount);
        break;

    case DOOR_OBJ_INSTANCE_CLOSE:
        LOG(LOG_INFO, "Execute %d/%d/%d", DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_CLOSE, DOOR_COUNTER_RESET_RESOURCE_ID);
        g_doorCloseCount = 0;
        getPath(&buf, DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_CLOSE, DOOR_COUNTER_RESET_RESOURCE_ID);
        AwaClientSetOperation_AddValueAsInteger(operation, buf, g_doorCloseCount);
        break;

    case DOOR_OBJ_INSTANCE_TRIGGER:
        LOG(LOG_INFO, "Execute %d/%d/%d", DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_TRIGGER, DOOR_COUNTER_RESET_RESOURCE_ID);
        g_doorTriggerCount = 0;
        getPath(&buf, DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_TRIGGER, DOOR_COUNTER_RESET_RESOURCE_ID);
        AwaClientSetOperation_AddValueAsInteger(operation, buf, g_doorTriggerCount);
        break;
    }
}

/**
 * @brief Create all objects and resources that belong to object.
 * @param client AWA static client
 * @return true on success, false otherwise.
 */
static bool DefineClientObjectsAndResources(AwaClientSession *session)
{
    AwaError err;
    int i;

    if (session == NULL)
    {
        LOG(LOG_ERR, "Null parameter passsed to %s()", __func__);
        return false;
    }

    // Define GarageDoor objects
    if (AwaClientSession_IsObjectDefined(session, DOOR_OBJECT_ID) == false)
    {
        AwaObjectDefinition *objectDefinition = AwaObjectDefinition_New(DOOR_OBJECT_ID, "GarageDoor", 0, AWA_MAX_ID);
        AwaObjectDefinition_AddResourceDefinitionAsNoType(objectDefinition, DOOR_TRIGGER_RESOURCE_ID, "DoorTrigger", false, AwaResourceOperations_Execute);
        AwaObjectDefinition_AddResourceDefinitionAsFloat(objectDefinition, DOOR_DURATION_RESOURCE_ID, "DoorDuration", false, AwaResourceOperations_ReadWrite, 0);
        AwaObjectDefinition_AddResourceDefinitionAsInteger(objectDefinition, DOOR_COUNTER_RESOURCE_ID, "DoorCounter", false, AwaResourceOperations_ReadWrite, 0);
        AwaObjectDefinition_AddResourceDefinitionAsNoType(objectDefinition, DOOR_COUNTER_RESET_RESOURCE_ID, "DoorCounterReset", false, AwaResourceOperations_Execute);
        AwaClientDefineOperation *operationDefine = AwaClientDefineOperation_New(session);
        AwaClientDefineOperation_Add(operationDefine, objectDefinition);
        AwaClientDefineOperation_Perform(operationDefine, OPERATION_PERFORM_TIMEOUT);
        AwaClientDefineOperation_Free(&operationDefine);
    }

    if (AwaClientSession_IsObjectDefined(session, OPTO_OBJECT_ID) == false)
    {
        AwaObjectDefinition *objectDefinition = AwaObjectDefinition_New(OPTO_OBJECT_ID, "OptoClick", 0, AWA_MAX_ID);
        AwaObjectDefinition_AddResourceDefinitionAsBoolean(objectDefinition, 5500, "DigitalInputState", false, AwaResourceOperations_ReadWrite, NULL);
        AwaClientDefineOperation *operationDefine = AwaClientDefineOperation_New(session);
        AwaClientDefineOperation_Add(operationDefine, objectDefinition);
        AwaClientDefineOperation_Perform(operationDefine, OPERATION_PERFORM_TIMEOUT);
        AwaClientDefineOperation_Free(&operationDefine);
    }

    AwaClientSetOperation *operation = AwaClientSetOperation_New(session);
    AwaClientSetOperation_CreateObjectInstance(operation, "/13201/0");
    AwaClientSetOperation_CreateOptionalResource(operation, "/13201/0/5523");
    AwaClientSetOperation_CreateOptionalResource(operation, "/13201/0/5521");
    AwaClientSetOperation_CreateOptionalResource(operation, "/13201/0/5501");
    AwaClientSetOperation_CreateOptionalResource(operation, "/13201/0/5505");

    AwaClientSetOperation_CreateObjectInstance(operation, "/13201/1");
    AwaClientSetOperation_CreateOptionalResource(operation, "/13201/1/5523");
    AwaClientSetOperation_CreateOptionalResource(operation, "/13201/1/5521");
    AwaClientSetOperation_CreateOptionalResource(operation, "/13201/1/5501");
    AwaClientSetOperation_CreateOptionalResource(operation, "/13201/1/5505");

    AwaClientSetOperation_CreateObjectInstance(operation, "/13201/2");
    AwaClientSetOperation_CreateOptionalResource(operation, "/13201/2/5523");
    AwaClientSetOperation_CreateOptionalResource(operation, "/13201/2/5521");
    AwaClientSetOperation_CreateOptionalResource(operation, "/13201/2/5501");
    AwaClientSetOperation_CreateOptionalResource(operation, "/13201/2/5505");

    AwaClientSetOperation_CreateObjectInstance(operation, "/3200/0");
    AwaClientSetOperation_CreateOptionalResource(operation, "/3200/0/5500");
    AwaClientSetOperation_CreateObjectInstance(operation, "/3200/1");
    AwaClientSetOperation_CreateOptionalResource(operation, "/3200/1/5500");

    AwaClientSetOperation_Perform(operation, OPERATION_PERFORM_TIMEOUT);
    AwaClientSetOperation_Free(&operation);

    return true;
}

/**
 * @brief Prints relay_gateway_appd usage.
 * @param *program holds application name.
 */
static void PrintUsage(const char *program)
{
    printf("Usage: %s [options]\n\n"
           " -l : Log filename.\n"
           " -v : Debug level from 1 to 5\n"
           "      fatal(1), error(2), warning(3), info(4), debug(5) and max(>5)\n"
           "      default is info.\n"
           " -h : Print help and exit.\n\n",
           program);
}

/**
 * @brief Parses command line arguments passed to temperature_gateway_appd.
 * @return -1 in case of failure, 0 for printing help and exit, and 1 for success.
 */
static int ParseCommandArgs(int argc, char *argv[], const char **fptr)
{
    int opt, tmp;
    opterr = 0;

    while (1)
    {
        opt = getopt(argc, argv, "l:v:c:");
        if (opt == -1)
        {
            break;
        }
        switch (opt)
        {
        case 'l':
            *fptr = optarg;
            break;

        case 'v':
            tmp = strtoul(optarg, NULL, 0);
            if (tmp >= LOG_FATAL && tmp <= LOG_DBG)
            {
                g_debugLevel = tmp;
            }
            else
            {
                LOG(LOG_ERR, "Invalid debug level");
                PrintUsage(argv[0]);
                return -1;
            }
            break;

        case 'h':
            PrintUsage(argv[0]);
            return 0;

        default:
            PrintUsage(argv[0]);
            return -1;
        }
    }

    return 1;
}

/**
 * @brief Handles Ctrl+C signal. Helps exit app gracefully.
 */
static void CtrlCHandler(int signal)
{
    LOG(LOG_INFO, "Exit triggered...");
    g_keepRunning = 0;
}

/**
 * @brief Turn on or off relay on click board depending on specified state.
 * @param state to be set on relay
 */
void ChangeRelayState(bool state)
{
    if (state)
        relay_click_enable_relay_1(RELAY_MIKROBUS_INDEX);
    else
        relay_click_disable_relay_1(RELAY_MIKROBUS_INDEX); //TODO:REMOVE, RELAY_CLICK_IDX);

    g_relayState = state;
    LOG(LOG_INFO, "Changed relay state on Ci40 board to %d", state);
}

static void setOptoClickStateResource(AwaClientSession *session, int instanceID, bool state) {
    AwaClientSetOperation *operation = AwaClientSetOperation_New(session);
    char buf[20];
    getPath(&buf, OPTO_OBJECT_ID, instanceID, OPTO_RESOURCE_ID);
    AwaClientSetOperation_AddValueAsBoolean(operation, buf, state);
    AwaClientSetOperation_Perform(operation, OPERATION_PERFORM_TIMEOUT);
    AwaClientSetOperation_Free(&operation);
}

/**
 * @brief Trigger garage door movement/stop.
 */
void GarageDoorTrigger()
{
    ChangeRelayState(true);
    gettimeofday(&g_sleepBegin, NULL);
}

void optoClickDoorOpenedCallback(uint8_t state)
{
    LOG(LOG_INFO, "Door-Opened state change to %d", state == GPIO_RAISING ? 1 : 0);
    if (state == GPIO_RAISING)
    {
        gettimeofday(&g_closeBegin, NULL);
    }
    else if (state == GPIO_FALLING)
    {

        if (g_openBegin.tv_sec != 0 && g_openBegin.tv_usec != 0)
        {
            gettimeofday(&g_openEnd, NULL);
            double openBegin = (double)g_openBegin.tv_sec + ((double)g_openBegin.tv_usec) / 1000000.0;
            double openEnd = (double)g_openEnd.tv_sec + ((double)g_openEnd.tv_usec) / 1000000.0;
            g_doorOpenDuration = openEnd - openBegin;
            g_doorOpenCount++;
            LOG(LOG_INFO, "Door open duration : %0.2f", g_doorOpenDuration);

            AwaClientSetOperation *operation = AwaClientSetOperation_New(session);

            char buf[20];
            getPath(&buf, DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_OPEN, DOOR_COUNTER_RESOURCE_ID);
            AwaClientSetOperation_AddValueAsInteger(operation, buf, g_doorOpenCount);
            getPath(&buf, DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_OPEN, DOOR_DURATION_RESOURCE_ID);
            AwaClientSetOperation_AddValueAsFloat(operation, buf, g_doorOpenDuration);
            AwaClientSetOperation_Perform(operation, OPERATION_PERFORM_TIMEOUT);
            AwaClientSetOperation_Free(&operation);
        }
    }
    else
    {
        LOG(LOG_ERR, "Invalid opto click state received: %d", state);
    }
    opto_click_read_channel(OPTO_MIKROBUS_INDEX, OPTO_CHANNEL_DOOR_OPENED, &g_optoOpenedState);
    setOptoClickStateResource(session, OPTO_OBJ_INSTANCE_DOOR_OPENED, g_optoOpenedState);
}

void optoClickDoorClosedCallback(uint8_t state)
{
    LOG(LOG_INFO, "Door-Closed state change to %d", state == GPIO_RAISING ? 1 : 0);
    if (state == GPIO_RAISING)
    {
        gettimeofday(&g_openBegin, NULL);
    }
    else if (state == GPIO_FALLING)
    {
        if (g_closeBegin.tv_sec != 0 && g_closeBegin.tv_usec != 0)
        {
            gettimeofday(&g_closeEnd, NULL);
            double closeBegin = (double)g_closeBegin.tv_sec + ((double)g_closeBegin.tv_usec) / 1000000.0;
            double closeEnd = (double)g_closeEnd.tv_sec + ((double)g_closeEnd.tv_usec) / 1000000.0;
            g_doorCloseDuration = closeEnd - closeBegin;
            g_doorCloseCount++;

            AwaClientSetOperation *operation = AwaClientSetOperation_New(session);

            char buf[20];
            getPath(&buf, DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_CLOSE, DOOR_COUNTER_RESOURCE_ID);
            AwaClientSetOperation_AddValueAsInteger(operation, buf, g_doorCloseCount);
            getPath(&buf, DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_CLOSE, DOOR_DURATION_RESOURCE_ID);
            AwaClientSetOperation_AddValueAsFloat(operation, buf, g_doorCloseDuration);
            AwaClientSetOperation_Perform(operation, OPERATION_PERFORM_TIMEOUT);
            AwaClientSetOperation_Free(&operation);
        }
    }
    else
    {
        LOG(LOG_ERR, "Invalid opto click state received: %d", state);
    }

    opto_click_read_channel(OPTO_MIKROBUS_INDEX, OPTO_CHANNEL_DOOR_CLOSED, &g_optoClosedState);
    setOptoClickStateResource(session, OPTO_OBJ_INSTANCE_DOOR_CLOSED, g_optoClosedState);

}



/**
 * @brief  Sesame gateway application handles door actions and notifies about 
 *         OptoClick state change. It also provides information about door 
 *         open/close count and duration.
 */
int main(int argc, char **argv)
{
    int i = i, ret;
    FILE *configFile;
    const char *fptr = NULL;

    ret = ParseCommandArgs(argc, argv, &fptr);

    if (ret <= 0)
    {
        return ret;
    }

    if (fptr)
    {
        configFile = fopen(fptr, "w");

        if (configFile != NULL)
        {
            g_debugStream = configFile;
        }
        else
        {
            LOG(LOG_ERR, "Failed to create or open %s file", fptr);
        }
    }

    signal(SIGINT, CtrlCHandler);

    LOG(LOG_INFO, "Sesame Gateway Application ...")

    LOG(LOG_INFO, "------------------------\n");

    session = AwaClientSession_New();
    AwaClientSession_Connect(session);

    if (g_keepRunning && session == NULL)
    {
        LOG(LOG_ERR, "Failed to establish client session. Exiting...");
        g_keepRunning = false;
    }

    if (g_keepRunning && !DefineClientObjectsAndResources(session))
    {
        LOG(LOG_ERR, "Failed to define client objects/resources. Exiting...");
        g_keepRunning = false;
    }

    if (g_keepRunning)
    {
        LOG(LOG_INFO, "Waiting for execute command on paths:");
        LOG(LOG_INFO, " - %d/%d/%d", DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_TRIGGER, DOOR_TRIGGER_RESOURCE_ID);
        LOG(LOG_INFO, " - %d/%d/%d", DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_OPEN, DOOR_COUNTER_RESET_RESOURCE_ID);
        LOG(LOG_INFO, " - %d/%d/%d", DOOR_OBJECT_ID, DOOR_OBJ_INSTANCE_CLOSE, DOOR_COUNTER_RESET_RESOURCE_ID);
    }

    opto_click_read_channel(OPTO_MIKROBUS_INDEX, OPTO_CHANNEL_DOOR_OPENED, &g_optoOpenedState);
    opto_click_read_channel(OPTO_MIKROBUS_INDEX, OPTO_CHANNEL_DOOR_CLOSED, &g_optoClosedState);
    setOptoClickStateResource(session, OPTO_OBJ_INSTANCE_DOOR_OPENED, g_optoOpenedState);
    setOptoClickStateResource(session, OPTO_OBJ_INSTANCE_DOOR_OPENED, g_optoClosedState);

    int doorOpenInstanceID, doorClosInstanceID, doorOperateInstanceID;
    AwaClientChangeSubscription *subscription1 = AwaClientExecuteSubscription_New("/13201/2/5523", doorTriggerCallback, NULL);
    AwaClientChangeSubscription *subscription2 = AwaClientExecuteSubscription_New("/13201/0/5505", doorCounterResetCallback, &doorOpenInstanceID);
    AwaClientChangeSubscription *subscription3 = AwaClientExecuteSubscription_New("/13201/1/5505", doorCounterResetCallback, &doorClosInstanceID);
    AwaClientChangeSubscription *subscription4 = AwaClientExecuteSubscription_New("/13201/2/5505", doorCounterResetCallback, &doorOperateInstanceID);
    AwaClientSubscribeOperation *subscribeOperation = AwaClientSubscribeOperation_New(session);
    AwaClientSubscribeOperation_AddExecuteSubscription(subscribeOperation, subscription1);
    AwaClientSubscribeOperation_AddExecuteSubscription(subscribeOperation, subscription2);
    AwaClientSubscribeOperation_AddExecuteSubscription(subscribeOperation, subscription3);
    AwaClientSubscribeOperation_AddExecuteSubscription(subscribeOperation, subscription4);
    AwaClientSubscribeOperation_Perform(subscribeOperation, OPERATION_PERFORM_TIMEOUT);
    AwaClientSubscribeOperation_Free(&subscribeOperation);

    if (g_keepRunning)
    {
        opto_click_attach_callback(OPTO_MIKROBUS_INDEX, OPTO_CHANNEL_DOOR_OPENED, optoClickDoorOpenedCallback);
        opto_click_attach_callback(OPTO_MIKROBUS_INDEX, OPTO_CHANNEL_DOOR_CLOSED, optoClickDoorClosedCallback);
        LOG(LOG_INFO, "Observing Opto Clicks state");
    }

    while (g_keepRunning)
    {
        AwaClientSession_Process(session, OPERATION_PERFORM_TIMEOUT);
        AwaClientSession_DispatchCallbacks(session);
        gettimeofday(&g_sleepEnd, NULL);
        struct timeval sleepTime;
        timersub(&g_sleepEnd, &g_sleepBegin, &sleepTime);
        if (sleepTime.tv_sec >= 3 && g_relayState)
        {
            ChangeRelayState(false);
        }
        sleep(1);
    }

    // Unsubscribe from all subscriptions
    AwaClientSubscribeOperation *cancelSubscribeOperation = AwaClientSubscribeOperation_New(session);
    AwaClientSubscribeOperation_AddCancelExecuteSubscription(cancelSubscribeOperation, subscription1);
    AwaClientSubscribeOperation_AddCancelExecuteSubscription(cancelSubscribeOperation, subscription2);
    AwaClientSubscribeOperation_AddCancelExecuteSubscription(cancelSubscribeOperation, subscription3);
    AwaClientSubscribeOperation_AddCancelExecuteSubscription(cancelSubscribeOperation, subscription4);
    
    AwaClientSubscribeOperation_Perform(cancelSubscribeOperation, OPERATION_PERFORM_TIMEOUT);
    AwaClientSubscribeOperation_Free(&cancelSubscribeOperation);
    AwaClientExecuteSubscription_Free(&subscription1);
    AwaClientExecuteSubscription_Free(&subscription2);
    AwaClientExecuteSubscription_Free(&subscription3);
    AwaClientExecuteSubscription_Free(&subscription4);
    

    // Delete all created object instances
    AwaClientDeleteOperation *deleteOperation = AwaClientDeleteOperation_New(session);
    AwaClientDeleteOperation_AddPath(deleteOperation, "/13201/0");
    AwaClientDeleteOperation_AddPath(deleteOperation, "/13201/1");
    AwaClientDeleteOperation_AddPath(deleteOperation, "/13201/2");
    AwaClientDeleteOperation_AddPath(deleteOperation, "/3200/0");
    AwaClientDeleteOperation_AddPath(deleteOperation, "/3200/1");
    AwaClientDeleteOperation_Perform(deleteOperation, OPERATION_PERFORM_TIMEOUT);
    AwaClientDeleteOperation_Free(&deleteOperation);

    // Disconnect Awa client
    AwaClientSession_Disconnect(session);
    AwaClientSession_Free(&session);

    LOG(LOG_INFO, "Sesame Gateway Application Failure");
    return -1;
}
