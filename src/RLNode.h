/**
 * RLNode.h
 * Version: 1.0.0
 *
 * This library is created for use with an embedded RealLogger client.
 * The template is created for use with arduino and has the following dependencies to compile:
 *   Library          :   Tested for version
 *   - PubSubClient   :   - 2.8.0
 *   - ArduinoJson    :   - 6.21.2
 *
 * Owned by: RealTest AB
 * Author: Gustav Radbrandt, Simon Friberg, Ulrik Åkesson
 */
 
 /* Boiler plate for Arduino Library */
#ifndef RLNode_h
#define RLNode_h

#include "Arduino.h"
/* ******************************** */

#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "Client.h"


#define MAX_GENERAL_STRING_LENGTH 20
#define MAX_SHORT_STRING_LENGTH 10
#define MAX_TOPIC_LENGTH 128
#define MAX_DESCRIPTION_LENGTH 50
#define MAX_CHANNEL_COUNT 4
#define MAX_JSON_SIZE 812
#define MAX_SERIALIZED_JSON_SIZE 812
#define MAX_RES_TIME_OUT 30000

#define SENSOR_FUNCTION void (*sensorFunction)(char* outputString, float k, float m, bool* forcePublish)
void intTochar(int int_current,char * outputString  );
// ******************************************************************
// Base channel class (Abstract)
// Includes common functions and attributes needed for each channel
class RLChannel
{
private:
    SENSOR_FUNCTION;
public:
    // Base constructor for general channel setup
    RLChannel(const char* type, const float maxSampleRate, SENSOR_FUNCTION);
    RLChannel& setSensorFunction(SENSOR_FUNCTION);
    void addChannelConfig();  // Add/update channel configuration information to JSON structure
    void addChannelPropertiesByID();  // Add/update channel properties to JSON structure
    void updateConfig();  // Update information about channel and current configuration
    void publishData();  // Update loop, check if channel should send a value
    unsigned long PreviousTime;  // Used to avoid publishing multiple times in the same millisecond
    unsigned long ActivationTime;  // Used for adding a delay to channel after reconfiguration
    int ID;
    float MaxSampleRate = 0.0f;
    char PreviousOutputString[MAX_GENERAL_STRING_LENGTH];
protected:
    bool Active = false;  // Determines if the channel should publish or not
    // Configurations
    char PublishTopic[MAX_TOPIC_LENGTH] = "\0";
    float SampleRate = 0.0f;
    float CalibrationValueK = 0.0f;
    float CalibrationValueM = 0.0f;
    // Properties
    char Type[MAX_GENERAL_STRING_LENGTH] = "\0";
    char Status[MAX_SHORT_STRING_LENGTH] = "Idle";
    char Sensor_ID[MAX_SHORT_STRING_LENGTH] = "\0";
    char Description[MAX_DESCRIPTION_LENGTH] = "\0";
    char Unit[MAX_SHORT_STRING_LENGTH] = "\0";
};

// ****************************************************************
// RLNode class
// Handles the node as a whole and contains a list of connected channels
class RLNode
{
public:
    // Connect to MQTT, set buffer size, set topic names, subscribe to topics, etc.
    void begin(Client& client, const char* mac, const char* server, uint16_t port);
    void begin(Client& client, const char* mac, const char* server, uint16_t port, const char* nodename);
    void begin(Client& client, const char* mac, const char* server, uint16_t port, const char* username, const char* password);
    void begin(Client& client, const char* mac, const char* server, uint16_t port, const char* username, const char* password, const char* nodename);
    void begin(Client& client, const char* mac, const char* server, uint16_t port, const char* username,const char* password, const char* nodename, const char* nodetype);
    void addChannel(RLChannel* newChannel);
    // Update loop, check for messages on MQTT and check if any channel should send a value
    void loop();
    int SetNodeStartupInfo();
    int requestNodeConfig();
    int GetChannelConfig();
    int SetChannelProperties();
    void mqttCallback(char* topic, char* payload);
    void mqttPublishData(char* topic, char* payload);
    void mqttPublishJson(char* topic);
    unsigned long Time;
    bool ResponseReceived = false;

protected:
    void responseIdentificationPoll();
    void responseSetNodeConfig();
    void responseSetChannelConfig();
    void nodeConfigChanged();
    void generateCorrelationData();
    void setSubscriptionTopicNames();
    void RLNodeMqttReconnect(const char* mac);

    char MAC[13] = "\0";  // MAC-address, used as identifier
    char LowerCaseMAC[13] = "\0";  // MAC-address, used as identifier
    char MQTTUsername[MAX_GENERAL_STRING_LENGTH] = "\0";  // Username for MQTT server
    char MQTTPassword[MAX_GENERAL_STRING_LENGTH] = "\0";  // Password for MQTT server
    char NodeName[MAX_GENERAL_STRING_LENGTH] = "\0";  // Node name, used as readable identifier
    char NodeType[MAX_GENERAL_STRING_LENGTH] = "\0";
    // NodeLocation removed
    // char NodeLocation[MAX_TOPIC_LENGTH] = "\0";  // Node location, used to set unique topic name 
    char Status[MAX_SHORT_STRING_LENGTH] = "Idle";
    char ResponseTopic[MAX_TOPIC_LENGTH] = "\0";  // Locally set from incoming messages
    // Internal communication topics to listen to
    char Topic_Response[MAX_TOPIC_LENGTH] = "\0";  // Response topic used in requests
    char Topic_IdentificationPoll[MAX_TOPIC_LENGTH] = "\0";
    char Topic_SetNodeConfig[MAX_TOPIC_LENGTH] = "\0";
    char Topic_SetChannelConfig[MAX_TOPIC_LENGTH] = "\0";
    char Topic_NodeConfigChanged[MAX_TOPIC_LENGTH] = "\0";
    char SerializedJson[MAX_SERIALIZED_JSON_SIZE] = "\0";
    char CorrelationData[MAX_GENERAL_STRING_LENGTH] = "\0";
    int ChannelCount = 0;
    RLChannel *Channels[MAX_CHANNEL_COUNT];
};

extern RLNode logNode;  // Instance of the RLNode to be used
extern DynamicJsonDocument jsonDoc;
extern DynamicJsonDocument nodeInformation;

extern PubSubClient mqttClient;

// Reroutes the callback for mqtt subscriptions to the callback in logNode
void RLNodeMqttCallback(char* topic, byte* payload, unsigned int length);

/* Boiler plate for Arduino Library */
#endif
/* ******************************** */