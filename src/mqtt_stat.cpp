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
 *
 */

#include <sstream>

#include "config.h"
#include "sipp.hpp"

#include "mqtt_stat.hpp"

#define JQ(k) "\"" #k "\":\""
#define JQV(k, v) JQ(k) << (v) << "\""
#define JQC(k, v) JQV(k, v) << ","

/*
    __________________________________________________________________________

    C L A S S    M Q T T S t a t
    __________________________________________________________________________
*/

MQTTStat::~MQTTStat()
{
}

MQTTStat::MQTTStat()
{
    CStat();
    CStat::init();
}

void MQTTStat::dumpData ()
{
    int ret;
    long   localElapsedTime, globalElapsedTime ;
    struct timeval currentTime;
    float  averageCallRate;
    float  realInstantCallRate;
    unsigned long numberOfCall;
    std::stringstream jsonData;

    if (!mqtt_ready)
        return;

    if (mqtt_handler == NULL) {
        cerr << "Unable to use uninitialized MQTT handler!" << endl;
        exit(EXIT_FATAL_ERROR);
    }

    // computing the real call rate
    GET_TIME (&currentTime);
    globalElapsedTime   = computeDiffTimeInMs (&currentTime, &M_startTime);
    localElapsedTime    = computeDiffTimeInMs (&currentTime, &M_plStartTime);

    // the call rate is for all the call : incoming and outgoing
    numberOfCall        = (M_counters[CPT_C_IncomingCallCreated] +
                           M_counters[CPT_C_OutgoingCallCreated]);
    averageCallRate     = (globalElapsedTime > 0 ?
                           1000*(float)numberOfCall/(float)globalElapsedTime :
                           0.0);
    numberOfCall        = (M_counters[CPT_PL_IncomingCallCreated] +
                           M_counters[CPT_PL_OutgoingCallCreated]);
    realInstantCallRate = (localElapsedTime  > 0 ?
                           1000*(float)numberOfCall / (float)localElapsedTime :
                           0.0);

    jsonData
        << "{"
        << JQC(StartTime, formatTime(&M_startTime, true))
        << JQC(LastResetTime, formatTime(&M_plStartTime, true))
        << JQC(CurrentTime, formatTime(&currentTime, true))
        << JQC(ElapsedTime(P), msToHHMMSS(localElapsedTime))
        << JQC(ElapsedTime(C), msToHHMMSS(globalElapsedTime))
        << JQC(TargetRate, (users >= 0) ? users : rate)
        << JQC(CallRate(P), realInstantCallRate)
        << JQC(CallRate(C), averageCallRate)
        << JQC(IncomingCall(P), M_counters[CPT_PL_IncomingCallCreated])
        << JQC(IncomingCall(C), M_counters[CPT_C_IncomingCallCreated])
        << JQC(OutgoingCall(P), M_counters[CPT_PL_OutgoingCallCreated])
        << JQC(OutgoingCall(C), M_counters[CPT_C_OutgoingCallCreated])
        << JQC(TotalCallCreated, (M_counters[CPT_C_IncomingCallCreated] + M_counters[CPT_C_OutgoingCallCreated]))
        << JQC(CurrentCall, M_counters[CPT_C_CurrentCall])
        << JQC(SuccessfulCall(P), M_counters[CPT_PL_SuccessfulCall])
        << JQC(SuccessfulCall(C), M_counters[CPT_C_SuccessfulCall])
        << JQC(FailedCall(P), M_counters[CPT_PL_FailedCall])
        << JQC(FailedCall(C), M_counters[CPT_C_FailedCall])
        << JQC(FailedCannotSendMessage(P), M_counters[CPT_PL_FailedCallCannotSendMessage])
        << JQC(FailedCannotSendMessage(C), M_counters[CPT_C_FailedCallCannotSendMessage])
        << JQC(FailedMaxUDPRetrans(P), M_counters[CPT_PL_FailedCallMaxUdpRetrans])
        << JQC(FailedMaxUDPRetrans(C), M_counters[CPT_C_FailedCallMaxUdpRetrans])
        << JQC(FailedTcpConnect(P), M_counters[CPT_PL_FailedCallTcpConnect])
        << JQC(FailedTcpConnect(C), M_counters[CPT_C_FailedCallTcpConnect])
        << JQC(FailedTcpClosed(P), M_counters[CPT_PL_FailedCallTcpClosed])
        << JQC(FailedTcpClosed(C), M_counters[CPT_C_FailedCallTcpClosed])
        << JQC(FailedUnexpectedMessage(P), M_counters[CPT_PL_FailedCallUnexpectedMessage])
        << JQC(FailedUnexpectedMessage(C), M_counters[CPT_C_FailedCallUnexpectedMessage])
        << JQC(FailedCallRejected(P), M_counters[CPT_PL_FailedCallCallRejected])
        << JQC(FailedCallRejected(C), M_counters[CPT_C_FailedCallCallRejected])
        << JQC(FailedCmdNotSent(P), M_counters[CPT_PL_FailedCallCmdNotSent])
        << JQC(FailedCmdNotSent(C), M_counters[CPT_C_FailedCallCmdNotSent])
        << JQC(FailedRegexpDoesntMatch(P), M_counters[CPT_PL_FailedCallRegexpDoesntMatch])
        << JQC(FailedRegexpDoesntMatch(C), M_counters[CPT_C_FailedCallRegexpDoesntMatch])
        << JQC(FailedRegexpShouldntMatch(P), M_counters[CPT_PL_FailedCallRegexpShouldntMatch])
        << JQC(FailedRegexpShouldntMatch(C), M_counters[CPT_C_FailedCallRegexpShouldntMatch])
        << JQC(FailedRegexpHdrNotFound(P), M_counters[CPT_PL_FailedCallRegexpHdrNotFound])
        << JQC(FailedRegexpHdrNotFound(C), M_counters[CPT_C_FailedCallRegexpHdrNotFound])
        << JQC(FailedOutboundCongestion(P), M_counters[CPT_PL_FailedOutboundCongestion])
        << JQC(FailedOutboundCongestion(C), M_counters[CPT_C_FailedOutboundCongestion])
        << JQC(FailedTimeoutOnRecv(P), M_counters[CPT_PL_FailedTimeoutOnRecv])
        << JQC(FailedTimeoutOnRecv(C), M_counters[CPT_C_FailedTimeoutOnRecv])
        << JQC(FailedTimeoutOnSend(P), M_counters[CPT_PL_FailedTimeoutOnSend])
        << JQC(FailedTimeoutOnSend(C), M_counters[CPT_C_FailedTimeoutOnSend])
        << JQC(FailedTestDoesntMatch(P), M_counters[CPT_PL_FailedCallTestDoesntMatch])
        << JQC(FailedTestDoesntMatch(C), M_counters[CPT_C_FailedCallTestDoesntMatch])
        << JQC(FailedTestShouldntMatch(P), M_counters[CPT_PL_FailedCallTestShouldntMatch])
        << JQC(FailedTestShouldntMatch(C), M_counters[CPT_C_FailedCallTestShouldntMatch])
        << JQC(FailedStrcmpDoesntMatch(P), M_counters[CPT_PL_FailedCallStrcmpDoesntMatch])
        << JQC(FailedStrcmpDoesntMatch(C), M_counters[CPT_C_FailedCallStrcmpDoesntMatch])
        << JQC(FailedStrcmpShouldntMatch(P), M_counters[CPT_PL_FailedCallStrcmpShouldntMatch])
        << JQC(FailedStrcmpShouldntMatch(C), M_counters[CPT_C_FailedCallStrcmpShouldntMatch])
        << JQC(OutOfCallMsgs(P), M_G_counters[CPT_G_PL_OutOfCallMsgs - E_NB_COUNTER - 1])
        << JQC(OutOfCallMsgs(C), M_G_counters[CPT_G_C_OutOfCallMsgs - E_NB_COUNTER - 1])
        << JQC(DeadCallMsgs(P), M_G_counters[CPT_G_PL_DeadCallMsgs - E_NB_COUNTER - 1])
        << JQC(DeadCallMsgs(C), M_G_counters[CPT_G_C_DeadCallMsgs - E_NB_COUNTER - 1])
        << JQC(Retransmissions(P), M_counters[CPT_PL_Retransmissions])
        << JQC(Retransmissions(C), M_counters[CPT_C_Retransmissions])
        << JQC(AutoAnswered(P), M_G_counters[CPT_G_PL_AutoAnswered - E_NB_COUNTER - 1])
        << JQC(AutoAnswered(C), M_G_counters[CPT_G_C_AutoAnswered - E_NB_COUNTER - 1])
        << JQC(Warnings(P), M_G_counters[CPT_G_PL_Warnings - E_NB_COUNTER - 1])
        << JQC(Warnings(C), M_G_counters[CPT_G_C_Warnings - E_NB_COUNTER - 1])
        << JQC(FatalErrors(P), M_G_counters[CPT_G_PL_FatalErrors - E_NB_COUNTER - 1])
        << JQC(FatalErrors(C), M_G_counters[CPT_G_C_FatalErrors - E_NB_COUNTER - 1])
        << JQC(WatchdogMajor(P), M_G_counters[CPT_G_PL_WatchdogMajor - E_NB_COUNTER - 1])
        << JQC(WatchdogMajor(C), M_G_counters[CPT_G_C_WatchdogMajor - E_NB_COUNTER - 1])
        << JQC(WatchdogMinor(P), M_G_counters[CPT_G_PL_WatchdogMinor - E_NB_COUNTER - 1])
        << JQC(WatchdogMinor(C), M_G_counters[CPT_G_C_WatchdogMinor - E_NB_COUNTER - 1]);

    for (int i = 1; i <= nRtds(); i++) {
        jsonData
            << "\"ResponseTime" << M_revRtdMap[i] << "(P)\":\""
            << msToHHMMSSus((unsigned long)computeRtdMean(i, GENERIC_PL)) << "\","
            << "\"ResponseTime" << M_revRtdMap[i] << "(C)\":\""
            << msToHHMMSSus((unsigned long)computeRtdMean(i, GENERIC_C)) << "\","
            << "\"ResponseTime" << M_revRtdMap[i] << "StDev(P)\":\""
            << msToHHMMSSus((unsigned long)computeRtdStdev(i, GENERIC_PL)) << "\","
            << "\"ResponseTime" << M_revRtdMap[i] << "StDev(C)\":\""
            << msToHHMMSSus((unsigned long)computeRtdStdev(i, GENERIC_C)) << "\",";
    }

    jsonData
        << JQC(CallLength(P), msToHHMMSSus((unsigned long)computeMean(CPT_PL_AverageCallLength_Sum, CPT_PL_NbOfCallUsedForAverageCallLength)))
        << JQC(CallLength(C), msToHHMMSSus((unsigned long)computeMean(CPT_C_AverageCallLength_Sum, CPT_C_NbOfCallUsedForAverageCallLength)))
        << JQC(CallLengthStDev(P), msToHHMMSSus((unsigned long)computeStdev(CPT_PL_AverageCallLength_Sum, CPT_PL_NbOfCallUsedForAverageCallLength, CPT_PL_AverageCallLength_Squares)))
        << JQC(CallLengthStDev(C), msToHHMMSSus((unsigned long)computeStdev(CPT_C_AverageCallLength_Sum, CPT_C_NbOfCallUsedForAverageCallLength, CPT_C_AverageCallLength_Squares)));


    for (unsigned int i = 1; i < M_genericMap.size() + 1; i++) {
        jsonData
            << "\"" << M_revGenericMap[i] << "(P)\":\"" << M_genericCounters[GENERIC_TYPES * i + GENERIC_PL] << "\","
            << "\"" << M_revGenericMap[i] << "(C)\":\"" << M_genericCounters[GENERIC_TYPES * i + GENERIC_C] << "\",";
    }

    for (int i = 1; i <= nRtds(); i++) {
        char s[80];
        sprintf(s, "ResponseTimeRepartition%s", M_revRtdMap[i]);

        for(int j = 0; j < (M_SizeOfResponseTimeRepartition - 1); j++) {
            jsonData << "\"" << s << "_<" << M_ResponseTimeRepartition[i - 1][j].borderMax << "\":\"" << M_ResponseTimeRepartition[i - 1][j].nbInThisBorder << "\",";
        }
        jsonData << "\"" << s << "_>=" << M_ResponseTimeRepartition[i - 1][M_SizeOfResponseTimeRepartition - 1].borderMax << "\":\"" << M_ResponseTimeRepartition[i - 1][M_SizeOfResponseTimeRepartition - 1].nbInThisBorder << "\",";
    }

    for(int j = 0; j < (M_SizeOfCallLengthRepartition - 1); j++) {
        jsonData << "\"" << "CallLengthRepartition" << "_<" << M_CallLengthRepartition[j].borderMax << "\":\"" << M_CallLengthRepartition[j].nbInThisBorder << "\",";
    }
    jsonData << "\"" << "CallLengthRepartition" << "_>=" << M_CallLengthRepartition[M_SizeOfCallLengthRepartition - 1].borderMax << "\":\"" << M_CallLengthRepartition[M_SizeOfCallLengthRepartition - 1].nbInThisBorder << "\"";

    jsonData << "}" << endl;

    std::string jsonDataStr = jsonData.str();

    ret = mosquitto_publish(mqtt_handler, NULL, mqtt_stats_topic,
                   jsonDataStr.length(), jsonDataStr.c_str(),
                   2, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        WARNING("MQTT: failed to publish stats: %s\n", mosquitto_strerror(ret));
    }

}

void MQTTStat::dumpDataRtt ()
{
    std::stringstream jsonData;
    int ret;

    if (!mqtt_ready)
        return;

    if (mqtt_handler == NULL) {
        cerr << "Unable to use uninitialized MQTT handler!" << endl;
        exit(EXIT_FATAL_ERROR);
    }

    jsonData
        << "{"
        << "\"Date_ms\":[";
    for (unsigned int i = 0; i < M_counterDumpRespTime ; i++) {
        jsonData << "\"" << M_dumpRespTime[i].date << "\"";
        if (i < M_counterDumpRespTime - 1)
            jsonData << ",";
        M_dumpRespTime[i].date = 0.0;
    }

    jsonData << "],\"response_time_ms\":[";
    for (unsigned int i = 0; i < M_counterDumpRespTime ; i++) {
        jsonData << "\"" << M_dumpRespTime[i].rtt << "\"";
        if (i < M_counterDumpRespTime - 1)
            jsonData << ",";
        M_dumpRespTime[i].rtt = 0.0;
    }

    jsonData << "],\"rtd_no\":[";
    for (unsigned int i = 0; i < M_counterDumpRespTime ; i++) {
        jsonData << "\"" << M_revRtdMap[M_dumpRespTime[i].rtd_no] << "\"";
        if (i < M_counterDumpRespTime - 1)
            jsonData << ",";
        M_dumpRespTime[i].rtd_no = 0;
    }
    jsonData << "]}" << endl;

    M_counterDumpRespTime = 0;

    std::string jsonDataStr = jsonData.str();

    ret = mosquitto_publish(mqtt_handler, NULL, mqtt_rttstats_topic,
                   jsonDataStr.length(), jsonDataStr.c_str(),
                   2, false);
    if (ret != MOSQ_ERR_SUCCESS) {
        WARNING("MQTT: failed to publish rtt stats: %s\n", mosquitto_strerror(ret));
    }
}


