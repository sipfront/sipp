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

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <sstream>

#include "mqtt_logger.hpp"

#define JQ(k) "\"" #k "\":\""
#define JQV(k, v) JQ(k) << (v) << "\""
#define JQC(k, v) JQV(k, v) << ","

void print_count_mqtt()
{
    std::stringstream jsonData;

    if (!main_scenario || !main_scenario->stats)
        return;

    if (!mqtt_ready)
        return; 

    if (mqtt_handler == NULL) {
        cerr << "Unable to use uninitialized MQTT handler!" << endl;
        return;
    }

	struct timeval currentTime, startTime;
	GET_TIME(&currentTime);
	main_scenario->stats->getStartTime(&startTime);
	unsigned long globalElapsedTime =
		CStat::computeDiffTimeInMs(&currentTime, &startTime);

	jsonData
		<< "{"
		<< JQC(CurrentTime, CStat::formatTime(&currentTime))
		<< JQC(ElapsedTime, CStat::msToHHMMSSus(globalElapsedTime));

    for (unsigned int index = 0; index < main_scenario->messages.size(); index++) {
        message* curmsg = main_scenario->messages[index];
        if (curmsg->hide) {
            continue;
        }

        if (SendingMessage* src = curmsg->send_scheme) {
            std::stringstream ss;
            ss << index << "_" << (src->isResponse() ? src->getCode() : src->getMethod()) << "_";
            std::string prefix;
            ss >> prefix;

            jsonData 
                << "\"" << prefix << "Sent\":" << curmsg->nb_sent << ","
                << "\"" << prefix << "Retrans\":" << curmsg->nb_sent_retrans << ",";
            if (curmsg->retrans_delay) {
                jsonData
                    << "\"" << prefix << "Timeout\":" << curmsg->nb_timeout << ",";
            }
            if (lose_packets) {
                fprintf(f, "%sLost%s", temp_str, stat_delimiter);
                jsonData
                    << "\"" << prefix << "Lost\":" << curmsg->nb_lost<< ",";
            }
        } else if (curmsg->recv_response || curmsg->recv_request) {
            std::stringstream ss;
            ss << index << "_" << (curmsg->recv_response ? curmsg->recv_response : curmsg->recv_request) << "_";
            std::string prefix;
            ss >> prefix;

            jsonData 
                << "\"" << prefix << "Recv\":" << curmsg->nb_recv << ","
                << "\"" << prefix << "Retrans\":" << curmsg->nb_recv_retrans << ","
                << "\"" << prefix << "Timeout\":" << curmsg->nb_timeout << ","
                << "\"" << prefix << "Unexp\":" << curmsg->nb_unexp << ",";
                if (lose_packets) {
                    jsonData << "\"" << prefix << "Lost\":" << curmsg->nb_lost << ",";
                }
        } else if (curmsg->pause_distribution || curmsg->pause_variable) {
            std::stringstream ss;
            ss << index << "_Pause_";
            std::string prefix;
            ss >> prefix;

            jsonData 
                << "\"" << prefix << "Sessions\":" << curmsg->sessions << ","
                << "\"" << prefix << "Unexp\":" << curmsg->nb_unexp << ",";
        } else if (curmsg->M_type == MSG_TYPE_NOP) {
            /* No output. */
        } else if (curmsg->M_type == MSG_TYPE_RECVCMD) {
            std::stringstream ss;
            ss << index << "_RecvCmd";
            std::string prefix;
            ss >> prefix;

            jsonData 
                << "\"" << prefix << "\":" << curmsg->M_nbCmdRecv << ","
                << "\"" << prefix << "_Timeout\":" << curmsg->nb_timeout << ",";
        } else if (curmsg->M_type == MSG_TYPE_SENDCMD) {
            std::stringstream ss;
            ss << index << "_SendCmd";
            std::string prefix;
            ss >> prefix;

            jsonData 
                << "\"" << prefix << "\":" << curmsg->M_nbCmdSent << ",";
        } else {
            ERROR("Unknown count file message type:");
        }
    }

    std::string jsonDataStr = jsonData.str();
    jsonDataStr.pop_back(); // remove trailing comma
    jsonDataStr += "}\n";

    int ret = mosquitto_publish(mqtt_handler, NULL, mqtt_countstats_topic,
            jsonDataStr.length(), jsonDataStr.c_str(),
            2, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        WARNING("MQTT: failed to publish counts: %s\n", mosquitto_strerror(ret));
    }
}

void print_error_codes_mqtt()
{
    std::stringstream jsonData;

    if (!main_scenario || !main_scenario->stats) {
        return;
    }

    // Print time and elapsed time to file
    struct timeval currentTime, startTime;
    GET_TIME(&currentTime);
    main_scenario->stats->getStartTime(&startTime);
    unsigned long globalElapsedTime =
        CStat::computeDiffTimeInMs(&currentTime, &startTime);

	jsonData
		<< "{"
		<< JQC(CurrentTime, CStat::formatTime(&currentTime))
		<< JQC(ElapsedTime, CStat::msToHHMMSSus(globalElapsedTime))
        << "\"ErrorCodes\":[";

    // Print comma-separated list of all error codes seen since the last time
    // this function was called
    for (; main_scenario->stats->error_codes.size() != 0;) {
        jsonData
            << main_scenario->stats
            ->error_codes[main_scenario->stats->error_codes.size() - 1] 
            << ",";
        main_scenario->stats->error_codes.pop_back();
    }

    std::string jsonDataStr = jsonData.str();
    jsonDataStr.pop_back();
    jsonDataStr += "]}\n";

    int ret = mosquitto_publish(mqtt_handler, NULL, mqtt_codestats_topic,
            jsonDataStr.length(), jsonDataStr.c_str(),
            2, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        WARNING("MQTT: failed to publish counts: %s\n", mosquitto_strerror(ret));
    }
}

