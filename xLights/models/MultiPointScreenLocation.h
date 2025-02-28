#pragma once

/***************************************************************
 * This source files comes from the xLights project
 * https://www.xlights.org
 * https://github.com/smeighan/xLights
 * See the github commit history for a record of contributing
 * developers.
 * Copyright claimed based on commit dates recorded in Github
 * License: https://github.com/smeighan/xLights/blob/master/License.txt
 **************************************************************/

#include "PolyPointScreenLocation.h"

//Location that uses multiple points
class MultiPointScreenLocation : public PolyPointScreenLocation {
public:
    MultiPointScreenLocation();
    virtual ~MultiPointScreenLocation();

    virtual bool SupportsCurves() const override {return false;}

protected:

};
