#include "Pagedata.h"

class ThreeValuesPage : public Page
{
public:
    virtual void display(CommonData &commonData, PageData &pageData)
    {
        GwLog *logger = commonData.logger;
        for (int i = 0; i < 3; i++)
        {
            GwApi::BoatValue *value = pageData.values[i];
            if (value == NULL)
                continue;
            LOG_DEBUG(GwLog::LOG, "drawing at threeValuesPage(%d), p=%s,v=%f",
                      i,
                      value->getName().c_str(),
                      value->valid ? value->value : -1.0);
        }
    };
};

static Page *createPage(CommonData &){
    return new ThreeValuesPage();
}
/**
 * with the code below we make this page known to the PageTask
 * we give it a type (name) that can be selected in the config
 * we define which function is to be called
 * and we provide the number of user parameters we expect
 * this will be number of BoatValue pointers in pageData.values
 */
PageDescription registerThreeValuesPage(
    "threeValues",
    createPage,
    3
);