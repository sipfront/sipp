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
#include <regex>

#include "mqtt_logger.hpp"

#define JQ(k) "\"" #k "\":\""
#define JQV(k, v) JQ(k) << (v) << "\""
#define JQC(k, v) JQV(k, v) << ","

static unsigned long total_errors = 0;

std::string do_replace(std::string const &in, std::string const &from, std::string const &to) {
    return std::regex_replace(in, std::regex(from), to);
}

void print_count_mqtt()
{
    std::stringstream jsonData;

    if (!main_scenario || !main_scenario->stats)
        return;

    if (!mqtt_ready)
        return; 

    if (mqtt_handler == NULL)
        return;

	struct timeval currentTime, startTime;
	GET_TIME(&currentTime);
	main_scenario->stats->getStartTime(&startTime);
	unsigned long globalElapsedTime =
		CStat::computeDiffTimeInMs(&currentTime, &startTime);

	jsonData
		<< "{"
		<< JQC(CurrentTime, CStat::formatTime(&currentTime, true))
		<< JQC(ElapsedTime, CStat::msToHHMMSSus(globalElapsedTime))
		<< "\"Counts\":[";

    bool popComma = main_scenario->messages.size() ? true : false;
    for (unsigned int index = 0; index < main_scenario->messages.size(); index++) {
        message* curmsg = main_scenario->messages[index];
        if (curmsg->hide) {
            continue;
        }

		jsonData << "{";
        if (SendingMessage* src = curmsg->send_scheme) {
            if (src->isResponse()) {
                jsonData 
                    << JQC(Type, "sendres")
                    << JQC(Name, src->getCode());
            } else {
                jsonData 
                    << JQC(Type, "sendreq")
                    << JQC(Name, src->getMethod());
            }
            jsonData
                << JQC(Sent, curmsg->nb_sent)
                << JQV(Retrans, curmsg->nb_sent_retrans);
            if (curmsg->retrans_delay) {
                jsonData << "," << JQV(Timeout, curmsg->nb_timeout);
            }
            if (lose_packets) {
                jsonData << "," << JQV(Lost, curmsg->nb_lost);
            }
        } else if (curmsg->recv_response || curmsg->recv_request) {
            if (curmsg->recv_response) {
                jsonData 
                    << JQC(Type, "recvres")
                    << JQC(Name, curmsg->recv_response);
            } else {
                jsonData 
                    << JQC(Type, "recvreq")
                    << JQC(Name, curmsg->recv_request);
            }
            jsonData
                << JQC(Recv, curmsg->nb_recv)
                << JQC(Retrans, curmsg->nb_recv_retrans)
                << JQC(Timeout, curmsg->nb_timeout)
                << JQV(Unexp, curmsg->nb_unexp);
            if (lose_packets) {
                jsonData << "," << JQV(Lost, curmsg->nb_lost);
            }
        } else if (curmsg->pause_distribution || curmsg->pause_variable) {
            jsonData 
                << JQC(Type, "pause")
                << JQC(Sessions, curmsg->sessions)
                << JQV(Unexp, curmsg->nb_unexp);
        } else if (curmsg->M_type == MSG_TYPE_NOP) {
            /* No output. */
        } else if (curmsg->M_type == MSG_TYPE_RECVCMD) {
            jsonData 
                << JQC(Type, "recvcmd")
                << JQC(RecvCmd, curmsg->M_nbCmdRecv)
                << JQV(Timeout, curmsg->nb_timeout);
        } else if (curmsg->M_type == MSG_TYPE_SENDCMD) {
            jsonData 
                << JQC(Type, "sendcmd")
                << JQV(SendCmd, curmsg->M_nbCmdSent);
        } else {
            jsonData 
                << JQV(Type, "invalid");
        }
		jsonData << "},";
    }

    std::string jsonDataStr = jsonData.str();
    if (popComma)
        jsonDataStr.pop_back();
    jsonDataStr += "]}\n";

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

    if (!mqtt_ready) {
        return; 
    }

    if (mqtt_handler == NULL) {
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
		<< JQC(CurrentTime, CStat::formatTime(&currentTime, true))
		<< JQC(ElapsedTime, CStat::msToHHMMSSus(globalElapsedTime))
        << "\"ErrorCodes\":[";

    // Print comma-separated list of all error codes seen since the last time
    // this function was called

    bool popComma = main_scenario->stats->error_codes.size() ? true : false;
    for (; main_scenario->stats->error_codes.size() != 0;) {
        jsonData
            << main_scenario->stats
            ->error_codes[main_scenario->stats->error_codes.size() - 1] 
            << ",";
        main_scenario->stats->error_codes.pop_back();
    }

    std::string jsonDataStr = jsonData.str();
    if (popComma)
        jsonDataStr.pop_back();
    jsonDataStr += "]}\n";

    int ret = mosquitto_publish(mqtt_handler, NULL, mqtt_codestats_topic,
            jsonDataStr.length(), jsonDataStr.c_str(),
            2, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        WARNING("MQTT: failed to publish counts: %s\n", mosquitto_strerror(ret));
    }
}

void print_errors_mqtt(int fatal, bool use_errno, int error, const char *fmt, va_list ap)
{
    std::stringstream jsonData;
    char buf[327680];

    if (!mqtt_ready) {
        return; 
    }

    if (mqtt_handler == NULL) {
        return;
    }

    total_errors++;

    struct timeval currentTime;
    GET_TIME(&currentTime);

    vsnprintf(buf, sizeof(buf), fmt, ap);
    std::string strbuf = do_replace(std::string(buf), "\r?\n", "\\r\\n");
    strbuf = do_replace(strbuf, "\"", "\\\"");

	jsonData
		<< "{"
		<< JQC(CurrentTime, CStat::formatTime(&currentTime, true))
        << JQC(Content, strbuf)
        << JQC(Fatal, fatal)
        << JQC(Total, total_errors);
    
    if (use_errno) {
	    jsonData << JQC(ErrnoString, strerror(error));
    } else {
	    jsonData << JQC(ErrnoString, "");
    }

    // last one is without comma
	jsonData
        << JQV(Errno, error);

    std::string jsonDataStr = jsonData.str();
    jsonDataStr += "}\n";

    mosquitto_publish(mqtt_handler, NULL, mqtt_error_topic,
            jsonDataStr.length(), jsonDataStr.c_str(),
            2, false);
}

void print_message_mqtt(struct timeval *currentTime, const char* direction, const char *transport, const char *sock_type, ssize_t msg_size, const char *msg) {
    std::stringstream jsonData;
    std::string strbuf = do_replace(std::string(msg), "\r?\n", "\\r\\n");
    strbuf = do_replace(strbuf, "\"", "\\\"");

    jsonData
        << "{"
        << JQC(CurrentTime, CStat::formatTime(currentTime, true))
        << JQC(Direction, direction)
        << JQC(Transport, transport)
        << JQC(Type, sock_type)
        << JQC(Message, strbuf);

    // last one is without comma
    jsonData
        << JQV(Size, msg_size);

    std::string jsonDataStr = jsonData.str();
    jsonDataStr += "}\n";

    int ret = mosquitto_publish(mqtt_handler, NULL, mqtt_message_topic,
            jsonDataStr.length(), jsonDataStr.c_str(),
            2, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        WARNING("MQTT: failed to publish message: %s\n", mosquitto_strerror(ret));
    }
}
