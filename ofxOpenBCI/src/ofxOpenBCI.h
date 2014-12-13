//
//  ofxOpenBCI.h
//
//
//  Created by Daniel Goodwin on 4/08/13.
//
//

#include <string.h>
#include "ofMain.h"
#include "ofSerial.h"

//using namespace ofx::IO;

#define OPENBCI_BAUDRATE 115200
#define byte char

const int DATAMODE_TXT = 0;
const int DATAMODE_BIN_WAUX = 1;
const int DATAMODE_BIN = 2;

const int STATE_NOCOM = 0;
const int STATE_COMINIT = 1;
const int STATE_SYNCWITHHARDWARE = 2;
const int STATE_NORMAL = 3;
const int STATE_STOPPED = 4;
const int COM_INIT_MSEC = 3000; //you may need to vary this for your computer or your Arduino

const byte BYTE_START = byte(0xA0);
const byte BYTE_END = byte(0xC0);
const byte CHAR_END = byte(0xA0);  //line feed?
const int LEN_SERIAL_BUFF_CHAR = 1000;
const int MIN_PAYLOAD_LEN_INT32 = 1; //8 is the normal number, but there are shorter modes to enable Bluetooth


struct dataPacket_ADS1299 {

    public:
    vector<float> values;
    int sampleIndex;
    //Timestamp is the universal time (as opposed to relative to an initial timestamp)
    time_t timestamp;
    dataPacket_ADS1299() {}
    dataPacket_ADS1299(int nValues) : values(nValues, 0){}

    int printToConsole() {
        cout <<"printToConsole: dataPacket = ";
        cout << sampleIndex;
        for (unsigned i=0; i < values.size(); i++) {
            cout << ", " << values[i];
        }
        cout << "\n";
        return 0;
    }
    int copyTo(dataPacket_ADS1299 & target) {
        target.sampleIndex = sampleIndex;
        for (unsigned i=0; i < values.size(); i++) {
            target.values[i] = values[i];
        }
        return 0;
    }
};


class filterConstants {
public:
    std::vector<double> b, a;
    string name;

    filterConstants(const std::vector<double> & b_given, const std::vector<double> & a_given, const string & name_given): b(b_given), a(a_given), name(name_given){

    }
};

//--------------This is the OpenBCI OpenFrameworks code ------------------//
class ofxOpenBCI {

public:
    ofxOpenBCI();
    void init();
    void update(bool echoChar);
    void toggleFilter(bool turnOn);
    void triggerTestSignal(bool turnOn);
    void changeChannelState(unsigned Ichan,bool activate);
    vector<dataPacket_ADS1299> getData();
    int startStreaming();
    int stopStreaming();
    bool connectionIsAlive();
    bool isNewDataPacketAvailable();
    int interpretBinaryMessageForward (int endInd);

    ofSerial serialDevice;
    int dataMode;
    static string usedPort;
    bool filterApplied;
    int streamingMode;
    int missedCyclesCounter;
    vector<bool> enabledChannels;

private:
    int interpretTextMessage();
    int curBuffIndex;
    int interpretAsInt32(byte byteArray[]);
    vector<byte>leftoverBytes;
    vector<byte> currBuffer;
    queue<dataPacket_ADS1299> outputPacketBuffer;
    void sendSignalToBoard(string input);
};
