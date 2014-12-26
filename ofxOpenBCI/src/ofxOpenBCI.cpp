///////////////////////////////////////////////////////////////////////////////
//
// This class configures and manages the connection to the OpenBCI shield for
// the Arduino.  The connection is implemented via a Serial connection.
// The OpenBCI is configured using single letter text commands sent from the
// PC to the Arduino.  The EEG data streams back from the Arduino to the PC
// continuously (once started).  This class defaults to using binary transfer
// for normal operation.
//
// Created: Chip Audette, Oct 2013
//
// Note: this class does not care whether you are using V1 or V2 of the OpenBCI
// board because the Arduino itself handles the differences between the two.  The
// command format to the Arduino and the data format from the Arduino are the same.
//
// Serial C++ code adapted from Jon Weisz's code here
//          available here: https://github.com/jon-weisz/openbci/tree/master/libopenbci
// Morphed from OpenBCI_ADS1299 to ofxOpenBCI by Daniel Goodwin, May 2014
/////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <time.h>
#include <sys/time.h>

#include "ofxOpenBCI.h"

#define byte char
#define IS_MAC 1
#define MAX_LEFTOVER_SERIAL_BYTES 200
#define COUNT_TO_MICROVOLT .02232

//Assuming most apps will run 30-60Hz, setting the counter to be 900 means that
//If the app has looked for data for 30 seconds minutes without seeing anything, then the streaming needs
//To be started again

#define MAX_MISSED_CYCLES 900

string command_stop = "s";
string command_startText = "x";
string command_startBinary = "b";
string command_startBinary_wAux = "n";
string command_activateFilters = "F";
string command_deactivateFilters = "f";
string command_start_test_signal = "+";
string command_showSettings = "?";
string command_softReset = "v";
string command_deactivated_channel_initializer[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
string command_activated_channel_initializer[] = {"!", "@", "#", "$", "%", "^", "&", "*"};
std::vector<string> command_deactivate_channel(command_deactivated_channel_initializer, command_deactivated_channel_initializer + 8);
std::vector<string> command_activate_channel(command_activated_channel_initializer, command_activated_channel_initializer + 8);

//A string variable here keeps track if there is currently a port in use
//This allows two OpenBCI units to be used in parallel
string ofxOpenBCI::usedPort;

ofxOpenBCI::ofxOpenBCI()
{
    cout << "Trying to set it up...\n";
    dataMode = DATAMODE_BIN;
    missedCyclesCounter = 0;
    curBuffIndex = 0;


    //Loop through all available
    //TODO: determine if mac/pc to automatically seed deviceQueryString
    #if IS_MAC
    string deviceQueryString = ".*/tty[.]usb.*";
    #else
    string deviceQueryString = "COM*";
    #endif

    std::vector<ofSerialDeviceInfo> devicesInfo = serialDevice.getDeviceList();

    for(std::size_t i = 0; i < devicesInfo.size(); ++i) {
        cout << "Trying to connect to: " << devicesInfo[i].getDeviceName() <<"\n";

        if(devicesInfo[i].getDeviceName() == usedPort)
            continue;

        bool success = serialDevice.setup(devicesInfo[i].getDeviceName(),115200);
        if(success) {
            ofLogNotice("ofApp::setup") << "Successfully setup " << devicesInfo[i].getDeviceName();
            ofxOpenBCI::usedPort = devicesInfo[i].getDeviceName();
            break;
        } else {
            ofLogNotice("ofApp::setup") << "Unable to setup " << devicesInfo[i].getDeviceName();
        }
    }
}

//ASSUMES A ONE CHARACTER INPUT!
void ofxOpenBCI::sendSignalToBoard(string input)
{
//    serialDevice.writeBytes((unsigned char*)(input + "\n").c_str(),2);
    serialDevice.writeByte(input[0]);
    serialDevice.flush();
    return;
}

//start the data transfer using the current mode
void ofxOpenBCI::startStreaming()
{
    //sendSignalToBoard(command_showSettings);
    //sendSignalToBoard(command_softReset);
    //stopDataTransfer();

    switch (dataMode) {
        case DATAMODE_BIN:
            sendSignalToBoard(command_startBinary);
            cout << "Processing: OpenBCI_ADS1299: starting binary\n";
            break;
        case DATAMODE_BIN_WAUX:
            sendSignalToBoard(command_startBinary_wAux);
            cout << "Processing: OpenBCI_ADS1299: starting binary wAux\n";
            break;
        case DATAMODE_TXT:
            sendSignalToBoard(command_startText);
            cout << "Processing: OpenBCI_ADS1299: starting text\n";
            break;
    }
}

void ofxOpenBCI::toggleFilter(bool turnOn)
{
    if (turnOn){
        sendSignalToBoard(command_activateFilters);
        cout << "Processing: OpenBCI_ADS1299: engaging filter\n";
    } else {
        sendSignalToBoard(command_deactivateFilters);
        cout << "Processing: OpenBCI_ADS1299: deactivating filter\n";
    }
}

void ofxOpenBCI::triggerTestSignal(bool turnOn)
{
    sendSignalToBoard(command_start_test_signal);
    cout << "Generating test signal\n";
}

void ofxOpenBCI::stopStreaming()
{
    sendSignalToBoard(command_stop);
    serialDevice.flush(); // clear anything in the com port's buffer
    cout << "Streaming stop\n";
}

//read from the serial port

//simple public interface for a reading singe char of data from BCI device and updating
//the datapacket packet as necessary. (echo writes to console)
//AT a sample rate of 250Hz we see about ~150 bytes per call of the update function
void ofxOpenBCI::update(bool echoChar)
{
    struct timeval tv;
    gettimeofday(&tv, 0);

    if (!serialDevice.isInitialized())
        throw;

    int bytesAvailable = serialDevice.available();
    if (bytesAvailable == -1)
        return;

    //First, see if there's anything in the leftover buffer and push into the currBuffer
    curBuffIndex = 0;
    currBuffer.clear();
    currBuffer.resize(leftoverBytes.size());
    for (unsigned i=0; i<leftoverBytes.size(); ++i) {
        currBuffer[curBuffIndex++] = leftoverBytes[i];
    }

    //Then, get all bytes off the serial port
    //printf("UPDATE START (%i) bytes: %dus\n",bytesAvailable, (int)tv.tv_usec);
    uint8_t inByte_arrayBIG[bytesAvailable];
    serialDevice.readBytes(inByte_arrayBIG, bytesAvailable);

    currBuffer.resize(currBuffer.size()+bytesAvailable);
    for (int i = 0; i<bytesAvailable; ++i) {
        byte inByte = inByte_arrayBIG[i];

        if (echoChar) //cout << inByte << " ";
            printf("%02X ",inByte);

        //accumulate the data in the buffer
        currBuffer[curBuffIndex++] = inByte;

    }//Finish the for loop around the input bytes

    //If there was no data on the wire, then we're done
    if (curBuffIndex == 0) {
        //printf("No data on wire\n");
        return;
    }
    curBuffIndex--; //Decrement to the last entered byte

    //Roll back the pointer to the last end byte seen. Store any extra bytes between the end of the buffer
    //and the last endIdx in the leftoverBytes array.
    int lastPacketEndIdx = curBuffIndex;
    while (currBuffer[lastPacketEndIdx] != BYTE_END) {
        lastPacketEndIdx--;

        //And if there's no complete packets in this data
        if (lastPacketEndIdx < 0){
            //cout << "No complete packets found of " << currBuffer.size() << "bytes\n";
            break;
        }
    }
//    printf("So lastPacketEndIdx points to a byte: %2X\n",currBuffer[lastPacketEndIdx]);

    //Push the extra data into the leftOVerArray
    leftoverBytes.clear();
    leftoverBytes.resize(currBuffer.size()-lastPacketEndIdx-1);
    for (unsigned i=lastPacketEndIdx+1; i<currBuffer.size(); ++i) {
        leftoverBytes[i-lastPacketEndIdx-1] = currBuffer[i];
    }
//    printf("Left %i bytes in the leftover vector\n", leftoverBytes.size());

    //To avoid any compounding buffer issues, we set a max count in the lefover
    //If it's too big we simply clear it
    if (leftoverBytes.size() > MAX_LEFTOVER_SERIAL_BYTES) {
        leftoverBytes.clear();
    }

    //Process all the packets we know we have
    //Go forward in the input array, from 0 the last end byte (that we found above)
    unsigned nextIndexToTry = 0;
    int tempResult = 0;
    while ((int)nextIndexToTry < lastPacketEndIdx) {

        if (currBuffer[nextIndexToTry] == BYTE_START) {
            if (currBuffer.size() < 32 + nextIndexToTry)
                return;//not enough samples
            //If the message is succesfully created, nextIndexToTry is updated to be
            tempResult = interpretBinaryMessageForward(nextIndexToTry);
            if (tempResult==-1){ //No end byte was seen for that packet and packet's length byte
                nextIndexToTry++;
            } else {   //We have just processed a packet successfully
                nextIndexToTry = tempResult;
            }
        } else {// if(currBuffer[nextIndexToTry]==BYTE_END){
            nextIndexToTry++;
        }
    }
    //printf("UPDATE END\n");
    return;
}

//Takes the latest data out of the output queue and returns it as a vector
vector<dataPacket_ADS1299> ofxOpenBCI::getData()
{
    //To be returned in the function
    vector<dataPacket_ADS1299> output;

    int currentSize = outputPacketBuffer.size();
    output.resize(currentSize);

    //printf("Sees %i objects in the outputPacketBuffer", currentSize);
    for (int i = 0; i < currentSize; ++i) {
        output[i] = outputPacketBuffer.front();
        outputPacketBuffer.pop();
    }

    if (output.size() == 0) {
        missedCyclesCounter++;
        if (missedCyclesCounter > MAX_MISSED_CYCLES) {
            printf("SEES NO DATA ON DEVICE SO STARTING STREAMING AGAIN");
//            startStreaming();
            missedCyclesCounter = 0;
        }
    }
    return output;
}

//Accessor for to tell client that a new data packet has been parsed from the byte stream.
bool ofxOpenBCI::isNewDataPacketAvailable()
{
    return outputPacketBuffer.size() > 0;
}

//activate or deactivate an EEG channel...channel counting is zero through nchan-1
void ofxOpenBCI::changeChannelState(unsigned Ichan,bool activate)
{
    bool serialPortConnected = true;
    if (serialPortConnected) {
        if ((Ichan >= 0) && (Ichan < command_activate_channel.size())) {
            if (activate) {
                sendSignalToBoard(command_activate_channel[Ichan]);
            } else {
                sendSignalToBoard(command_deactivate_channel[Ichan]);
            }
        }
    }
}

//deactivate an EEG channel...channel counting is zero through nchan-1


//interpret the data. Only called when the last seen byte was BYTE_END
/*
A Packet looks like this:
Byte 1: 0xA0
Byte 2: Sample Number
Bytes 3-5: Data value for EEG channel 1
Bytes 6-8: Data value for EEG channel 2
Bytes 9-11: Data value for EEG channel 3
Bytes 12-14: Data value for EEG channel 4
Bytes 15-17: Data value for EEG channel 5
Bytes 18-20: Data value for EEG channel 6
Bytes 21-23: Data value for EEG channel 6
Bytes 24-26: Data value for EEG channel 8
Bytes 27-28: Data value for accelerometer channel X
Bytes 29-30: Data value for accelerometer channel Y
Bytes 31-32: Data value for accelerometer channel Z
Byte 33: 0xC0
*/

//Rewrote the interpretBinaryMessage fxn to avoid stumbling over a BYTE_END or BYTE_START on accident
int ofxOpenBCI::interpretBinaryMessageForward(int startIdx)
{
    //assume curBuffIndex has already been incremented to the next starting spot
    int endIdx = -1;
    int nInt32 = 8;
    unsigned char n_bytes = 32; //this is the number of bytes in the payload

    if (currBuffer[startIdx] != BYTE_START)
        throw;

    //Counting forward, do we see the BYTE_END?
    if (currBuffer[startIdx + n_bytes] != BYTE_END) {
        printf("Bad packet: %i, %i. Got: %.02x \n", startIdx, endIdx, currBuffer[startIdx + n_bytes]);
        return endIdx;
    }

    endIdx = startIdx + n_bytes;

    dataPacket_ADS1299 dataPacket(nInt32);
    dataPacket.timestamp = time(NULL);
    dataPacket.sampleIndex = (int)currBuffer[startIdx+1];

    //Full doc here: http://docs.openbci.com/05-OpenBCI_Streaming_Data_Format
    startIdx += 2;  //increment the start index
    for (int i = 0; i < nInt32; i++) {
        dataPacket.values[i] = interpret24bitAsInt32(&currBuffer[startIdx]);
        startIdx += 3;  //increment the start index
    }

    outputPacketBuffer.push(dataPacket);

    //printf("\n");
    return endIdx;
}

int ofxOpenBCI::interpret24bitAsInt32(byte byteArray[])
{
    int newInt = (
        ((0xFF & byteArray[0]) << 16) |
        ((0xFF & byteArray[1]) << 8) |
        (0xFF & byteArray[2]));
    if ((newInt & 0x00800000) > 0) {
        newInt |= 0xFF000000;
    } else {
        newInt &= 0x00FFFFFF;
    }
    return newInt;
}

int ofxOpenBCI::interpret16bitAsInt32(byte byteArray[])
{
    int newInt = (
        ((0xFF & byteArray[0]) << 8) |
        (0xFF & byteArray[1]));
    if ((newInt & 0x00008000) > 0) {
        newInt |= 0xFFFF0000;
    } else {
        newInt &= 0x0000FFFF;
    }
    return newInt;
}

int ofxOpenBCI::interpretTextMessage()
{
    //still have to code this!
    curBuffIndex=0;  //reset buffer counter back to zero to start refilling the buffer
    return 0;
}
