// ****************************************************************************
// * This file is part of the HqMAME project. It is distributed under         *
// * GNU General Public License: http://www.gnu.org/licenses/gpl.html         *
// * Copyright (C) Zenju (zenju AT gmx DOT de) - All Rights Reserved          *
// *                                                                          *
// * Additionally and as a special exception, the author gives permission     *
// * to link the code of this program with the MAME library (or with modified *
// * versions of MAME that use the same license as MAME), and distribute      *
// * linked combinations including the two. You must obey the GNU General     *
// * Public License in all respects for all of the code used other than MAME. *
// * If you modify this file, you may extend this exception to your version   *
// * of the file, but you are not obligated to do so. If you do not wish to   *
// * do so, delete this exception statement from your version.                *
// ****************************************************************************

#ifndef XBRZ_CONFIG_HEADER_284578425345
#define XBRZ_CONFIG_HEADER_284578425345

//do NOT include any headers here! used by xBRZ_dll!!!

namespace xbrz
{
struct ScalerCfg
{
    ScalerCfg() :
        luminanceWeight_(1),
        equalColorTolerance_(30),
        dominantDirectionThreshold(3.6),
        steepDirectionThreshold(2.2),
        newTestAttribute_(0) {}

    double luminanceWeight_;
    double equalColorTolerance_;
    double dominantDirectionThreshold;
    double steepDirectionThreshold;
    double newTestAttribute_; //unused; test new parameters
};
}

#endif