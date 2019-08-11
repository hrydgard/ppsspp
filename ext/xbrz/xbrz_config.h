// ****************************************************************************
// * This file is part of the xBRZ project. It is distributed under           *
// * GNU General Public License: https://www.gnu.org/licenses/gpl-3.0         *
// * Copyright (C) Zenju (zenju AT gmx DOT de) - All Rights Reserved          *
// *                                                                          *
// * Additionally and as a special exception, the author gives permission     *
// * to link the code of this program with the following libraries            *
// * (or with modified versions that use the same licenses), and distribute   *
// * linked combinations including the two: MAME, FreeFileSync, Snes9x, ePSXe *
// * You must obey the GNU General Public License in all respects for all of  *
// * the code used other than MAME, FreeFileSync, Snes9x, ePSXe.              *
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
    double luminanceWeight            = 1;
    double equalColorTolerance        = 30;
    double centerDirectionBias        = 4;
    double dominantDirectionThreshold = 3.6;
    double steepDirectionThreshold    = 2.2;
    double newTestAttribute           = 0; //unused; test new parameters
};
}

#endif
