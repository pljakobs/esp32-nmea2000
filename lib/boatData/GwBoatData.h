#ifndef _GWBOATDATA_H
#define _GWBOATDATA_H

#include "GwLog.h"
#include <Arduino.h>
#include <map>
#include <vector>
#define GW_BOAT_VALUE_LEN 32
#define GWSC(name) static constexpr const char* name=#name

//see https://github.com/wellenvogel/esp32-nmea2000/issues/44
//factor to convert from N2k/SI rad/s to current NMEA rad/min
#define ROT_WA_FACTOR 60

class GwJsonDocument;
class GwBoatItemBase{
    public:
        class StringWriter{
            uint8_t *buffer =NULL;
            uint8_t *wp=NULL;
            size_t bufferSize=0;
            size_t baseOffset=0;
            void ensure(size_t size);
            public:
                StringWriter();
                size_t write(uint8_t c);
                size_t write(const uint8_t* s, size_t n);
                const char * c_str() const;
                int getSize() const;
                void setBase();
                bool baseFilled();
                void reset();
        };
        static const unsigned long INVALID_TIME=60000;
        //the formatter names that must be known in js
        GWSC(formatCourse);
        GWSC(formatKnots);
        GWSC(formatWind);
        GWSC(formatLatitude);
        GWSC(formatLongitude);
        GWSC(formatXte);
        GWSC(formatFixed0);
        GWSC(formatDepth);
        GWSC(kelvinToC);
        GWSC(mtr2nm);
        GWSC(formatDop);
        GWSC(formatRot);
        GWSC(formatDate);
        GWSC(formatTime);
        typedef std::map<String,GwBoatItemBase*> GwBoatItemMap;
    protected:
        int type;
        unsigned long lastSet=0;
        unsigned long invalidTime=INVALID_TIME;
        String name;
        String format;
        StringWriter writer;
        void uls(unsigned long ts=0){
            if (ts) lastSet=ts;
            else lastSet=millis();
            writer.reset(); //value has changed
        }
        int lastUpdateSource;
    public:
        int getCurrentType(){return type;}
        unsigned long getLastSet() const {return lastSet;}
        bool isValid(unsigned long now=0) const ;
        GwBoatItemBase(String name,String format,unsigned long invalidTime=INVALID_TIME);
        virtual ~GwBoatItemBase(){}
        void invalidate(){
            lastSet=0;
        }
        const char *getDataString(){
            fillString();
            return writer.c_str();
            }
        virtual void fillString()=0;
        virtual void toJsonDoc(GwJsonDocument *doc, unsigned long minTime)=0;
        virtual size_t getJsonSize();
        virtual int getLastSource(){return lastUpdateSource;}
        virtual void refresh(unsigned long ts=0){uls(ts);}
        virtual double getDoubleValue()=0;
        String getName(){return name;}
        const String & getFormat() const{return format;}
};
class GwBoatData;
template<class T> class GwBoatItem : public GwBoatItemBase{
    protected:
        T data;
        bool lastStringValid=false;
    public:
        GwBoatItem(String name,String formatInfo,unsigned long invalidTime=INVALID_TIME,GwBoatItemMap *map=NULL);
        virtual ~GwBoatItem(){}
        bool update(T nv, int source=-1);
        bool updateMax(T nv,int sourceId=-1);
        T getData(){
            return data;
        }
        T getDataWithDefault(T defaultv){
            if (! isValid(millis())) return defaultv;
            return data;
        }
        virtual double getDoubleValue(){return (double)data;}
        virtual void fillString();
        virtual void toJsonDoc(GwJsonDocument *doc, unsigned long minTime);
        virtual int getLastSource(){return lastUpdateSource;}
};
double formatCourse(double cv);
double formatDegToRad(double deg);
double formatWind(double cv);
double formatKnots(double cv);
uint32_t mtr2nm(uint32_t m);
double mtr2nm(double m);

class GwSatInfo{
    public:
        unsigned char PRN;
        uint32_t Elevation;
        uint32_t Azimut;
        uint32_t SNR;
        unsigned long timestamp;
};
class GwSatInfoList{
    public:
        static const unsigned long lifeTime=32000;
        std::vector<GwSatInfo> sats;
        void houseKeeping(unsigned long ts=0);
        void update(GwSatInfo entry);
        int getNumSats() const{
            return sats.size();
        }
        GwSatInfo *getAt(int idx){
            if (idx >= 0 && idx < sats.size()) return &sats.at(idx);
            return NULL;
        }
        operator double(){ return getNumSats();}
};

class GwBoatDataSatList : public GwBoatItem<GwSatInfoList>
{
public:
    GwBoatDataSatList(String name, String formatInfo, unsigned long invalidTime = INVALID_TIME, GwBoatItemMap *map = NULL);
    bool update(GwSatInfo info, int source);
    virtual void toJsonDoc(GwJsonDocument *doc, unsigned long minTime);
    GwSatInfo *getAt(int idx){
        if (! isValid()) return NULL;
        return data.getAt(idx);
    }
    int getNumSats(){
        if (! isValid()) return 0;
        return data.getNumSats();
    }
    virtual double getDoubleValue(){
        return (double)(data.getNumSats());
    }

};

class GwBoatItemNameProvider
{
public:
    virtual String getBoatItemName() = 0;
    virtual String getBoatItemFormat() = 0;
    virtual unsigned long getInvalidTime(){ return GwBoatItemBase::INVALID_TIME;}
    virtual ~GwBoatItemNameProvider() {}
};
#define GWBOATDATA(type,name,time,fmt)  \
    static constexpr const char* _##name=#name; \
    GwBoatItem<type> *name=new GwBoatItem<type>(#name,GwBoatItemBase::fmt,time,&values) ;
#define GWSPECBOATDATA(clazz,name,time,fmt)  \
    clazz *name=new clazz(#name,GwBoatItemBase::fmt,time,&values) ;
class GwBoatData{
    private:
        GwLog *logger;
        GwBoatItemBase::GwBoatItemMap values;
    public:

    GWBOATDATA(double,COG,4000,formatCourse)
    GWBOATDATA(double,TWD,4000,formatCourse)
    GWBOATDATA(double,SOG,4000,formatKnots)
    GWBOATDATA(double,STW,4000,formatKnots)
    GWBOATDATA(double,TWS,4000,formatKnots)
    GWBOATDATA(double,AWS,4000,formatKnots)
    GWBOATDATA(double,MaxTws,0,formatKnots)
    GWBOATDATA(double,MaxAws,0,formatKnots)
    GWBOATDATA(double,AWA,4000,formatWind)
    GWBOATDATA(double,HDG,4000,formatCourse) //true heading
    GWBOATDATA(double,MHDG,4000,formatCourse) //magnetic heading
    GWBOATDATA(double,ROT,4000,formatRot)
    GWBOATDATA(double,VAR,4000,formatCourse) //Variation
    GWBOATDATA(double,DEV,4000,formatCourse) //Deviation
    GWBOATDATA(double,HDOP,4000,formatDop)
    GWBOATDATA(double,PDOP,4000,formatDop)
    GWBOATDATA(double,VDOP,4000,formatDop)
    GWBOATDATA(double,RPOS,4000,formatWind) //RudderPosition
    GWBOATDATA(double,PRPOS,4000,formatWind) //second rudder pos
    GWBOATDATA(double,LAT,4000,formatLatitude)
    GWBOATDATA(double,LON,4000,formatLongitude)
    GWBOATDATA(double,ALT,4000,formatFixed0) //altitude
    GWBOATDATA(double,DBS,4000,formatDepth) //waterDepth (below surface)
    GWBOATDATA(double,DBT,4000,formatDepth) //DepthTransducer
    GWBOATDATA(double,GPST,4000,formatTime) //GpsTime
    GWBOATDATA(double,WTemp,4000,kelvinToC)
    GWBOATDATA(double,XTE,4000,formatXte)
    GWBOATDATA(double,DTW,4000,mtr2nm) //distance wp
    GWBOATDATA(double,BTW,4000,formatCourse) //bearing wp
    GWBOATDATA(double,WPLat,4000,formatLatitude)
    GWBOATDATA(double,WPLon,4000,formatLongitude)
    GWBOATDATA(uint32_t,Log,16000,mtr2nm)
    GWBOATDATA(uint32_t,TripLog,16000,mtr2nm)
    GWBOATDATA(uint32_t,GPSD,4000,formatDate) //Date
    GWBOATDATA(int16_t,TZ,8000,formatFixed0)
    GWSPECBOATDATA(GwBoatDataSatList,SatInfo,GwSatInfoList::lifeTime,formatFixed0);
    public:
        GwBoatData(GwLog *logger);
        ~GwBoatData();
        template<class T> GwBoatItem<T> *getOrCreate(T initial,GwBoatItemNameProvider *provider);
        template<class T> bool update(T value,int source,GwBoatItemNameProvider *provider);
        template<class T> T getDataWithDefault(T defaultv, GwBoatItemNameProvider *provider);
        bool isValid(String name);
        double getDoubleValue(String name,double defaultv);
        GwBoatItemBase *getBase(String name);
        String toJson() const;
        String toString();
};


#endif