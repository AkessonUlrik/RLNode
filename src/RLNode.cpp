/**
 * RLNode.cpp
 * Version: 1.1.0
 *
 * This library is created for use with an embedded RealLogger client.
 * The template is created for use with arduino and has the following dependencies to compile:
 *   Library      :    Tested for version
 *   - RLNode     :    - 1.1.0
 *
 * Owned by: RealTest AB
 * Author: Gustav Radbrandt, Simon Friberg, Ulrik Åkesson
 */

/* Boiler plate for Arduino Library */
#include "Arduino.h"
/* ******************************** */

#include "RLNode.h"

PubSubClient mqttClient;
// Stored in the heap, Used and then cleared for all json structures
DynamicJsonDocument jsonDoc(MAX_JSON_SIZE);
DynamicJsonDocument nodeInformation(MAX_JSON_SIZE);

// dtosrt() and itoa() are not working on Arduino MKR1010 Wifi this function is used instead to convert int to char. 
void intTochar(int int_current,char * outputString  )
{
    // Check if negative 
    int negative_sign = 0;
    if (int_current<0)
    { 
      negative_sign = 1;
      *outputString ='-';
    }
    // Count how many digits 
    int n_digits=0; 
    int abs_current = abs(int_current); 
    do{
      n_digits++;
      abs_current = abs_current/10;
    }while(abs_current>0);
    // Write the number to char array backward 
    abs_current = abs(int_current); // Restore the abs_current value from previous division. 
    for (int i = n_digits-1+negative_sign; i>=0+negative_sign; i--)
    {
      *(outputString+i) = '0' + (abs_current % 10) ;
      abs_current = abs_current /10;
    }
    // Finish
    *(outputString + n_digits + negative_sign) = '\0';
}

void strLow(char * inputStr)
{
    while (*inputStr)
    {
        *inputStr = char(tolower(*inputStr));
        inputStr++;
    }
}

RLNode logNode;
// ******************************************************************
// Base channel class (Abstract)
// Includes common functions and attributes needed for each channel
RLChannel::RLChannel(const char* type, const float maxSampleRate, SENSOR_FUNCTION)
{

    strcpy(Type, type);
    MaxSampleRate = maxSampleRate;
    setSensorFunction(sensorFunction);
}

// Sets given sensor reading function as sensorReader for the given channel
RLChannel& RLChannel::setSensorFunction(SENSOR_FUNCTION)
{
    this->sensorFunction = sensorFunction;
    return *this;
}

// Add/update channel information to JSON structure, using ID as index in nested array
// Used at startup when sending channel properties to database
void RLChannel::addChannelPropertiesByID()
{
    nodeInformation["Payload"]["Channel"]["ChannelId"] = ID;
    nodeInformation["Payload"]["Channel"]["Type"] = Type;
    nodeInformation["Payload"]["Channel"]["MaxSampleRate"] = MaxSampleRate;
    //nodeInformation["Payload"]["Channel"]["Sensor_ID"] = Sensor_ID;

}

// Update channel configuration according to received message
// Used at startup to setup channels as well as in setChannelConfig
void RLChannel::updateConfig()
{
    // Update configuration according to configuration request,
    // if PublishTopic is not missing,
    // and PublishTopic is not an empty string,
    // and SampleRate is not higher than MaxSampleRate
    // and SampleRate is not negative (or 0)

    if (!jsonDoc["Payload"]["Configuration"]["PublishTopic"].isNull() &&
        strlen(jsonDoc["Payload"]["Configuration"]["PublishTopic"]) > 0 &&
        MaxSampleRate >= jsonDoc["Payload"]["Configuration"]["SampleRate"] &&
        0 < jsonDoc["Payload"]["Configuration"]["SampleRate"])
    {
        strcpy(PublishTopic, jsonDoc["Payload"]["Configuration"]["PublishTopic"]);
        SampleRate = jsonDoc["Payload"]["Configuration"]["SampleRate"];
        CalibrationValueK = jsonDoc["Payload"]["Configuration"]["kValue"];
        CalibrationValueM = jsonDoc["Payload"]["Configuration"]["mValue"];
        strcpy(Unit, jsonDoc["Payload"]["Configuration"]["Unit"]);
        strcpy(Description, jsonDoc["Payload"]["Configuration"]["Descriptor"]);
        strcpy(Sensor_ID, jsonDoc["Payload"]["Configuration"]["Sensor_ID"]);
        Serial.println(SampleRate);
    }
    else
    {
        SampleRate = 0.0;
    }

    // If sample rate is 0, deactivate channel and set status to idle
    if (SampleRate == 0.0)
    {
        Active = false;
        strcpy(Status, "Idle");
    }
    else  // Set channel as active, status as online, and set activation timePreviousTime
    {
        Active = true;
        strcpy(Status, "Online");
        ActivationTime = millis();
        Serial.print(F("  Channel "));
        Serial.print(ID);
        Serial.print(F(" started publishing on topic "));
        Serial.println(PublishTopic);
        Serial.print(F("  Sample rate: "));
        Serial.print(SampleRate);
        Serial.println(F(" Samples/sec"));
    }
}

// Publishes data for active channels
// Called every loop cycle
void RLChannel::publishData()
{
    // Only let activated channels publish,
    // wait 1 second after configuration (according to guidelines),
    // publish every period (set by sample rate),
    // or when forced to publish,
    // and make sure to never publish multiple times at once
    
    char outputString[MAX_GENERAL_STRING_LENGTH];
    bool forcePublish = false;
    Serial.println(F("Triggering sensor function"));
    sensorFunction(outputString, CalibrationValueK, CalibrationValueM, &forcePublish);
    if (Active &&
        logNode.Time-ActivationTime >= 1000 &&
        ((unsigned long)(logNode.Time - PreviousTime) >= (1000/SampleRate) || forcePublish))
    {
        Serial.print(F("Publishing sensor data: "));
        Serial.println(outputString);
        logNode.mqttPublishData(PublishTopic, outputString);
        strcpy(PreviousOutputString,outputString);
        PreviousTime = logNode.Time;
        Serial.println(F("Data published"));
    }

}

// ****************************************************************
// RLNode class
// Handles the node as a whole and contains a list of connected channels
void RLNode::begin(Client& client, const char* mac, const char* server, uint16_t port)
{
    begin(client, mac, server, port, NULL, NULL, "", "");
}
void RLNode::begin(Client& client, const char* mac, const char* server, uint16_t port, const char* nodename)
{
    begin(client, mac, server, port, NULL, NULL, nodename, "");
}
void RLNode::begin(Client& client, const char* mac, const char* server, uint16_t port, const char* username,
                   const char* password)
{
    begin(client, mac, server, port, username, password, "", "");
}
void RLNode::begin(Client& client, const char* mac, const char* server, uint16_t port, const char* username,
                   const char* password, const char* nodename)
{
    begin(client, mac, server, port, username, password, nodename, "");
}
void RLNode::begin(Client& client, const char* mac, const char* server, uint16_t port, const char* username,
                   const char* password, const char* nodename, const char* nodetype)
{
    randomSeed(millis()*micros());  // Used for generating correlationdata
    
    // Set Mac address, mqtt username, mqtt password, and nodename
    strncpy(MAC, mac, strlen(mac) + 1);
    strncpy(MQTTUsername, username, strlen(username) + 1);
    strncpy(MQTTPassword, password, strlen(password) + 1);
    strncpy(NodeName, nodename, strlen(nodename) + 1);
    strncpy(NodeType, nodetype, strlen(nodetype) + 1);
    
    // Create lower case MAC address for use in topics
    strncpy(LowerCaseMAC, MAC, strlen(MAC) + 1);
    strLow(LowerCaseMAC);

    mqttClient.setClient(client);
    // Set the MQTT server
    mqttClient.setServer(server, port);

    // Establish the subscribe event, set keepalive time, and set buffer size
    mqttClient.setCallback(RLNodeMqttCallback);
    mqttClient.setKeepAlive(30);
    if (mqttClient.setBufferSize(812))
    {
        Serial.print(F("  MQTT buffer size set to "));
        Serial.println(mqttClient.getBufferSize());
    }
    else
    {
        Serial.println(F("[Error] Not enough memory for MQTT buffer"));
        while (true) { delay(1000); }
    }

    // Attempt to connect to the server with mac address as ID
    while (!mqttClient.connect(mac, MQTTUsername, MQTTPassword))
    {
        Serial.println(F("  Connection to MQTT Broker [Failed]"));
        Serial.println(F("    retrying in 2 seconds"));
        delay(2000);
    }
    Serial.println(F("  Connection to MQTT Broker [Established]"));

    // Publish an identification broadcast at startup
    while (!SetNodeStartupInfo())
    {
        delay(10000);
    }
    Serial.println(F("  Startup information sent"));

    // Set topic names and subscribe to them
    setSubscriptionTopicNames();
    mqttClient.subscribe(Topic_IdentificationPoll);
    mqttClient.subscribe(Topic_SetNodeConfig);
    mqttClient.subscribe(Topic_SetChannelConfig);
    mqttClient.subscribe(Topic_NodeConfigChanged);

    while (!SetChannelProperties())
    {
        delay(10000);
    }
    Serial.println(F("  Set Channel Properties completed"));
    
    while (!GetChannelConfig())
    {
        delay(10000);
    }
    Serial.println(F("  Channel configuration completed"));
}

// Append list of channels with newChannel
void RLNode::addChannel(RLChannel* newChannel)
{
    Channels[ChannelCount] = newChannel;
    (*newChannel).ID = ChannelCount+1;
    Serial.print(F("  Added channel with ID: "));
    Serial.println((*newChannel).ID);
    ChannelCount++;
}

// Update loop: calls publishData for each connected channel and checks for MQTT messages
void RLNode::loop()
{
    unsigned long t = millis();
    // Reconnect to the mqtt broker in the case of a disconnect
    if (!mqttClient.connected())
    {
        RLNodeMqttReconnect(MAC);
    }
    // Check for incoming messages
    mqttClient.loop();
    Time = millis();  // Time set here to enable multiple channels with same sample rate
    // Call loop function for each channel
    for (int i=0; i<ChannelCount; i++)
    {
        (*Channels[i]).publishData();
    }
}

// Create and send startup information
int RLNode::SetNodeStartupInfo()
{
    // Fill in general node information 
    strcpy(Topic_Response, "res/rtl/");
    strcat(Topic_Response, LowerCaseMAC);
    strcat(Topic_Response, "/setnodestartupinfo");
    nodeInformation["ResponseTopic"] = Topic_Response;
    generateCorrelationData();
    nodeInformation["CorrelationData"] = CorrelationData;
    nodeInformation["Payload"]["NodeId"] = MAC;
    nodeInformation["Payload"]["Type"] = NodeType;
    //nodeInformation["Payload"].createNestedArray("Channels");

    mqttClient.subscribe(Topic_Response);
    mqttPublishJson("req/rtl/dataaccess/setnodestartupinfo");
    nodeInformation.clear();

    // Get response(s) from dataaccess
    
    unsigned long timeout = millis();
    while(!ResponseReceived)
    {
        mqttClient.loop();
        // Timeout in case of lost messages
        if (millis() - timeout > MAX_RES_TIME_OUT)
        {
            // TODO Send error message over MQTT?
            mqttClient.unsubscribe(Topic_Response);
            return 0;
        }
    }
    ResponseReceived = 0;

    // TODO Handle processing message in other way?
    // If processing message is received, listen for next message
    if(!strncmp(jsonDoc["CmdStatus"], "Processing", 11))
    {
        timeout = millis();
        while(!ResponseReceived)
        {
            mqttClient.loop();
            // Timeout in case of lost messages
            if (millis() - timeout > MAX_RES_TIME_OUT)
            {
                // TODO Send error message over MQTT?
                mqttClient.unsubscribe(Topic_Response);
                return 0;
            }
        }
        ResponseReceived = 0;
    }
    mqttClient.unsubscribe(Topic_Response);

    return 1;
}

// Create and send request for channel configuration
int RLNode::GetChannelConfig()
{
    int j = 0;
    bool timeoutFlag;
    while (j < ChannelCount)
    {
        // Send request to dataaccess for node configuration
        strcpy(Topic_Response, "res/rtl/");
        strcat(Topic_Response, LowerCaseMAC);
        strcat(Topic_Response, "/getchannelconfiguration");
        nodeInformation["ResponseTopic"] = Topic_Response;
        generateCorrelationData();
        nodeInformation["CorrelationData"] = CorrelationData;
        nodeInformation["Payload"]["NodeId"] = MAC;
        nodeInformation["Payload"]["ChannelId"] = (Channels[j])->ID;

        mqttClient.subscribe(Topic_Response);
        mqttPublishJson("req/rtl/dataaccess/getchannelconfiguration");  // Local topic used to save memory
        nodeInformation.clear();

        // Get response(s) from dataaccess
        
        timeoutFlag = false;
        unsigned long timeout = millis();
        while(!ResponseReceived)
        {
            mqttClient.loop();
            // Timeout in case of lost messages
            if (millis() - timeout > MAX_RES_TIME_OUT)
            {
                mqttClient.unsubscribe(Topic_Response);
                timeoutFlag = true;
                break;
                //return 0;
            }
        }
        if (timeoutFlag)
        {
            continue;
        }
        
        ResponseReceived = 0;

        // TODO Handle processing message in other way?
        // If processing message is received, listen for next message
        if(!strncmp(jsonDoc["CmdStatus"], "Processing", 11))
        {
            timeout = millis();
            while(!ResponseReceived)
            {
                mqttClient.loop();
                // Timeout in case of lost messages
                if (millis() - timeout > MAX_RES_TIME_OUT)
                {
                    // TODO Send error message over MQTT?
                    mqttClient.unsubscribe(Topic_Response);
                    timeoutFlag = true;
                    break;
                    //return 0;
                }
            }
            if (timeoutFlag)
            {
                continue;
            }
            
            ResponseReceived = 0;
        }

        // Update channel configuration according to (first) response,
        // as long as publishtopic isn't empty and the channel ID is valid
        
        int channelID = int(jsonDoc["Payload"]["ChannelId"]) - 1;
        if (strlen(jsonDoc["Payload"]["Configuration"]["PublishTopic"]) > 0 &&
                !jsonDoc["Payload"]["Configuration"]["PublishTopic"].isNull() &&
                channelID < ChannelCount
                && channelID >= 0)
        {
            // Update channel configuration using ID
            (*Channels[channelID]).updateConfig();
        }

        j++;
    }

    
    return 1;
}
// Sends the properties of each channel to the dataaccess
int RLNode::SetChannelProperties()
{
    int i = 0;
    bool timeoutFlag;
    while (i<ChannelCount)
    {
        strcpy(Topic_Response, "res/rtl/");
        strcat(Topic_Response, LowerCaseMAC);
        strcat(Topic_Response, "/setchannelproperties");
        nodeInformation["ResponseTopic"] = Topic_Response;
        generateCorrelationData();
        nodeInformation["CorrelationData"] = CorrelationData;
        nodeInformation["Payload"]["NodeId"] = MAC;
        Channels[i]->addChannelPropertiesByID();

        mqttClient.subscribe(Topic_Response);
        mqttPublishJson("req/rtl/dataaccess/setchannelproperties");  // Local topic used to save memory
        nodeInformation.clear();

        // Get response(s) from dataaccess
        
        timeoutFlag = false;
        unsigned long timeout = millis();
        while(!ResponseReceived)
        {
            mqttClient.loop();
            // Timeout in case of lost messages
            if (millis() - timeout > MAX_RES_TIME_OUT)
            {
                mqttClient.unsubscribe(Topic_Response);
                timeoutFlag = true;
                break;
                //return 0;
            }
        }
        if (timeoutFlag)
        {
            continue;
        }
        
        ResponseReceived = 0;

        // TODO Handle processing message in other way?
        // If processing message is received, listen for next message
        if(!strncmp(jsonDoc["CmdStatus"], "Processing", 11))
        {
            timeout = millis();
            while(!ResponseReceived)
            {
                mqttClient.loop();
                // Timeout in case of lost messages
                if (millis() - timeout > MAX_RES_TIME_OUT)
                {
                    // TODO Send error message over MQTT?
                    mqttClient.unsubscribe(Topic_Response);
                    timeoutFlag = true;
                    break;
                    //return 0;
                }
            }
            if (timeoutFlag)
            {
                continue;
            }
            
            ResponseReceived = 0;
        }
        mqttClient.unsubscribe(Topic_Response);
        i++;
    }

    return 1;
}

// Internal callback function for incoming MQTT topics, reroutes to the right function
void RLNode::mqttCallback(char* topic, char* payload)
{
    // Save input message to jsonDoc variable
    deserializeJson(jsonDoc, payload);

    // Check topic and select the respective function
    if (!strncmp(topic, Topic_IdentificationPoll, strlen(Topic_IdentificationPoll) + 1))
        responseIdentificationPoll();
    else if (!strncmp(topic, Topic_NodeConfigChanged, strlen(Topic_NodeConfigChanged) + 1))
        nodeConfigChanged();
    else if (!strncmp(topic, Topic_Response, strlen(Topic_Response)) + 1)
        ResponseReceived = true;
    else
        Serial.println(F("No function set for this topic."));  // Should not be possible
}

// Publish payload to topic
void RLNode::mqttPublishData(char* topic, char* payload) 
{
    // Attempt to publish a value to the response topic
    Serial.print(F("Attempting to publish data: "));
    Serial.print(payload);
    Serial.print(F(" on topic: "));
    Serial.println(topic);

    if (!mqttClient.publish(topic,payload))
    {
        Serial.println(F("[Error] Failed to send."));
        delay(500);
    }
}

// Publish json to topic
void RLNode::mqttPublishJson(char* topic)
{

    serializeJson(nodeInformation, SerializedJson);

    // Attempt to publish JSON to the response topic
    if (!mqttClient.publish(topic, SerializedJson))
    {
        Serial.println(F("[Error] Failed to send."));
        delay(500);
    }
}

// Create and send response to identification poll
void RLNode::responseIdentificationPoll()
{
    // Set response topic and correlation data
    strcpy(ResponseTopic, jsonDoc["ResponseTopic"]);
    strcpy(CorrelationData, jsonDoc["CorrelationData"]);
    nodeInformation["CorrelationData"] = CorrelationData;

    // Send processing message
    nodeInformation["CmdStatus"] = "Processing";
    nodeInformation["CmdStatusText"] = "";
    nodeInformation["Payload"] = NULL;
    mqttPublishJson(ResponseTopic);

    // Send node information over given response topic
    nodeInformation["CmdStatus"] = "Done";
    nodeInformation["Payload"]["NodeName"] = NodeName;
    //nodeInformation["Payload"]["NodeLocation"] = NodeLocation;
    nodeInformation["Payload"]["MAC"] = MAC;
    mqttPublishJson(ResponseTopic);
    nodeInformation.clear();
}

// Indicates the node have gotten new configurations
void RLNode::nodeConfigChanged()
{
    Serial.println(F("  Configuration changed"));
    Serial.println(F("  Fetching new configurations"));
    while(!GetChannelConfig())
    {
        delay(10000);
    }
    Serial.println(F("  Done changing config"));

    int i;
    for (i = 0; i < ChannelCount; i++)
    {
        strcpy(Channels[i]->PreviousOutputString, "");
    }
    
}

// Generates a string of random characters to use for correlation data
void RLNode::generateCorrelationData()
{
    char data[MAX_GENERAL_STRING_LENGTH];
    memset(data, 0, sizeof(data));

    byte randomCharacter;
    for (int i = 0; i < MAX_GENERAL_STRING_LENGTH-1; i++)
    {
        randomCharacter = random(0, 62);
        data[i] = randomCharacter + 'a';
        if (randomCharacter > 51)
            data[i] = (randomCharacter - 52) + '0';
        else if (randomCharacter > 25)
            data[i] = (randomCharacter - 26) + 'A';
    }
    strcpy(CorrelationData, data);
}

// Construct topic names
void RLNode::setSubscriptionTopicNames()
{
    // identificationPoll: <Root>/identificationpoll
    strcpy(Topic_IdentificationPoll, "req/rtl/logger/identificationpoll");
    // SetNodeConfiguration: <Root>/<MAC>/identificationassignment (should probably be changed)
    strcpy(Topic_SetNodeConfig, "req/rtl/");
    strcat(Topic_SetNodeConfig, LowerCaseMAC);
    strcat(Topic_SetNodeConfig, "/identificationassignment");
    // SetChannelConfiguration: <Root>/<MAC>/setchannelconfiguration
    strcpy(Topic_SetChannelConfig, "req/rtl/");
    strcat(Topic_SetChannelConfig, LowerCaseMAC);
    strcat(Topic_SetChannelConfig, "/setchannelconfiguration");
    // NodeConfigChanged: not/rtl/<MAC>/nodeconfigchanged
    strcpy(Topic_NodeConfigChanged, "not/");
    strcat(Topic_NodeConfigChanged, MAC);
    strcat(Topic_NodeConfigChanged, "/configuration");
}

// Reconnects too the mqtt broker
void RLNode::RLNodeMqttReconnect(const char* mac)
{
    // Loop until reconnected
    while (!mqttClient.connected()) {
        Serial.println(F("Attempting to reconnect to MQTT broker..."));
        Serial.println(F("MAC       :User       :Pass"));
        Serial.print(mac);
        Serial.print(MQTTUsername);
        Serial.println(MQTTPassword);
        // Attempt to connect
        if (mqttClient.connect(mac, MQTTUsername, MQTTPassword)) {
            Serial.println(F("Connection to MQTT broker [Established]"));
            mqttClient.subscribe(Topic_IdentificationPoll);
            mqttClient.subscribe(Topic_SetNodeConfig);
            mqttClient.subscribe(Topic_SetChannelConfig);
        } else {
            Serial.println(F("Connection to MQTT broker [Failed]"));
            Serial.println(F("  retrying in 5 seconds"));
            delay(5000);
        }
    }
}

// ****************************************************************
// External
// Reroutes the callback for mqtt subscriptions to the callback in logNode
void RLNodeMqttCallback(char* topic, byte* payload, unsigned int length)
{
    Serial.print(F("Recieved "));
    Serial.print(length);
    Serial.print(F("/"));
    Serial.print(mqttClient.getBufferSize());
    Serial.print(F(" bytes on ("));
    Serial.print(topic);
    Serial.println(F(")."));

    // Call object specific callback function
    logNode.mqttCallback(topic, (char*)payload);
}