/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Authors : Andreas Granig <andreas@granig.com>
 */

#ifndef __MQTT_STAT_H__
#define __MQTT_STAT_H__


#include "stat.hpp"

using namespace std;

/*
__________________________________________________________________________

              M Q T T S t a t    C L A S S
__________________________________________________________________________
*/

/**
 * This class overrides CStat to dump data to MQTT instead of CSV
 * This is a singleton class.
 */

class MQTTStat : public CStat
{
public:

    /*
    ** exported methods
    */

    /**
     * Constructor.
     */
    MQTTStat ();

    /**
     * Destructor.
     */
    virtual ~MQTTStat ();

    /**
     * Dump data periodically to MQTT
     */
    virtual void dumpData ();

    virtual void dumpDataRtt ();

private:

    /**
     * Effective C++
     *
     * To prevent public copy ctor usage: no implementation
     */
    MQTTStat (const MQTTStat&);

    /**
     * Effective C++
     *
     * To prevent public operator= usage: no implementation
     */
    MQTTStat& operator=(const MQTTStat&);
};

#endif // __MQTT_STAT_H__
