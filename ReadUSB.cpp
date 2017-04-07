#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <string>
#include <iostream>
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <vector>
#include <queue>
#include <ctime>
using namespace std;

class Temperature
{
public:
    double temperature;
    time_t time;
    Temperature(double T, time_t now)
    {
        temperature = T;
        time = now;
    }
};

struct cmpMax
{
    bool operator()(const Temperature& a, const Temperature& b)
    {
        return a.temperature < b.temperature;
    }
} ;

struct cmpMin
{
    bool operator()(const Temperature& a, const Temperature& b)
    {
        return a.temperature > b.temperature;
    }
} ;

int limit = 10; // time limit of keeping records
int fd;
bool end;
bool isCelsius;
bool canGet;
queue<Temperature> record; // record temperature and time
double nowT; // real time temperature
double total; // for calculating average temperature
priority_queue<Temperature, vector<Temperature>, cmpMax> maxT;
priority_queue<Temperature, vector<Temperature>, cmpMin> minT; // use two priority queues to record the highest and lowest temperature

/*
This code configures the file descriptor for use as a serial port.
*/
void configure(int fd)
{
    struct  termios pts;
    tcgetattr(fd, &pts);
    cfsetospeed(&pts, 9600);
    cfsetispeed(&pts, 9600);
    tcsetattr(fd, TCSANOW, &pts);
}

void initUSB()
{
    string file = "/dev/ttyACM0"; // auto detect USB port, need to change on different computer
    char portN = '1';
    while(!end)
    {
        char* argv = &file[0];
        fd = open(argv, O_RDWR | O_NOCTTY | O_NDELAY);

        if (fd < 0)
        {
            sleep(1);
            cout << "\nFailed to open port number " << file << endl;
            file[11] = portN;
            if(portN == '5')
            {
                portN = '0';
            }
            else
            {
                portN++;
            }
        }
        else
        {
            cout << "\nSuccessfully opened " << argv << " for reading/writing" << endl;
            break;
        }
    }
    // try to open the file for reading and writing

    configure(fd);
    canGet = true;
    isCelsius = true;
    sleep(6); // wait for sensor to stabilize
}

void* reading(void* p)
{
    nowT = 0;
    total = 0;
    char buffer[200];
    time_t now, pre = time(NULL);
    while(!end)
    {
        now = time(NULL);
        if(now - pre >= 1)
        {
            int get = read(fd, buffer, 199);
            if(get)
            {
                pre = now;
                double T;
                vector<string> tokens;
                split(tokens, buffer, boost::is_any_of(" "), boost::token_compress_on);
                for(unsigned int i=0; i<tokens.size(); i++)
                {
                    if(tokens[i]=="temperature" && tokens[i+1]=="is" && tokens[i+3]=="degrees")
                    {
                        string temp = tokens[i+2];
                        T = atof(&temp[0]);
                        now = time(NULL);
                        nowT = T; // record the recent temperature
                        total += T; // record the total temperature in the queue
                        Temperature newT(T, now);
                        record.push(newT);
                        maxT.push(newT);
                        minT.push(newT);
                        cout<< "\nGet temp: " << T << endl;
                        break;
                    }
                }
            }
            else
            {
                now = time(NULL);
                if(now - pre < 5)
                {
                    continue;
                }
                canGet = false;
                cout << "\nDisconnected from sensor! Trying to reconnect..." << endl;
                initUSB();
            }
        }

        while(!record.empty() && (now - record.front().time > limit))
        {
            total -= record.front().temperature; // if the temperature is recorded an hour ago, then reduce it from total and pop from queue
            record.pop();
        }
        while(!maxT.empty() && (now - maxT.top().time > limit))
        {
            maxT.pop();
        }
        while(!minT.empty() && (now - minT.top().time > limit))
        {
            minT.pop();
        }
    }
    return NULL;
}

void quit()
{
    end = true;
}

void setCF()
{
    char buffer[3];
    buffer[0] = 'S';
    int n = strlen(buffer);
    if(write(fd, buffer, n) != n)
    {
        cout << "\nWrite failed!" << endl;
    }
    else
    {
        cout << "\nWritten to sensor." << endl;
        if(isCelsius)
        {
            isCelsius = false;
        }
        else
        {
            isCelsius = true;
        }
    }
}

double getNow()
{
    if(isCelsius)
    {
        return nowT;
    }
    return nowT * 1.8 + 32.0;
}

double getAvg()
{
    double avg;
    int n = record.empty()? 1 : record.size();
    avg = total/(double)n;
    if(isCelsius)
    {
        return avg;
    }
    return avg * 1.8 + 32.0;
}

double getMax()
{
    double T = maxT.empty()? 0.0 : maxT.top().temperature;
    if(isCelsius)
    {
        return T;
    }
    return T * 1.8 + 32.0;
}

double getMin()
{
    double T = minT.empty()? 0.0 : minT.top().temperature;
    if(isCelsius)
    {
        return T;
    }
    return T * 1.8 + 32.0;
}

bool canGetT()
{
    return canGet;
}

string getJson()
{
    ostringstream real, avg, maxT, minT;
    real << getNow();
    avg << getAvg();
    maxT << getMax();
    minT << getMin();
    string head = "{\n";
    string mid = "\"\n";
    string tail = "}\n";
    string realS = "\"real\": \"";
    string avgS = "\"average\": \"";
    string maxS = "\"max\": \"";
    string minS = "\"min\": \"";
    string degreeS = "\"degree\": \"";
    string CorF = isCelsius ? "C" : "F";
    string response =  head + realS + real.str() + mid + avgS + avg.str() + mid + maxS + maxT.str() + mid + minS + minT.str() + mid + degreeS + CorF + mid + tail;
    return response;
}
