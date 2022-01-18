#include "Pagedata.h"

void apparentWindPage(CommonData &commonData, PageData &pageData){
    GwLog *logger=commonData.logger;
    for (int i=0;i<2;i++){
        GwApi::BoatValue *value=pageData.values[i];
        if (value == NULL) continue;
        LOG_DEBUG(GwLog::LOG,"drawing at apparentWindPage(%d), p=%s,v=%f",
            i,
            value->getName().c_str(),
            value->valid?value->value:-1.0
        );
    }
};

/**
 * with the code below we make this page known to the PageTask
 * we give it a type (name) that can be selected in the config
 * we define which function is to be called
 * and we provide the number of user parameters we expect (0 here)
 * and will will provide the names of the fixed values we need
 */
PageDescription registerApparentWindPage(
    "apparentWind",
    apparentWindPage,
    0,
    {"AWS","AWD"}
);