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

    WARNING("MQTTStat::dumpData\n");

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

#define JQ(k) "\"" #k "\":\""
#define JQV(k, v) JQ(k) << (v) << "\""
#define JQC(k, v) JQV(k, v) << ","
    jsonData
            << "{"
            << JQC(StartTime, formatTime(&M_startTime))
            << JQC(LastResetTime, formatTime(&M_plStartTime))
            << JQC(CurrentTime, formatTime(&currentTime))
            << JQC(ElapsedTime(P), msToHHMMSS(localElapsedTime))
            << JQC(ElapsedTime(C), msToHHMMSS(globalElapsedTime))
            << JQC(TargetRate, (users >= 0) ? users : rate)
            << JQV(CallRate(P), realInstantCallRate) << "}" << endl;
        /*
            << JQC(CallRate(C)
            << JQC(IncomingCall(P)
            << JQC(IncomingCall(C)
            << JQC(OutgoingCall(P)
            << JQC(OutgoingCall(C)
            << JQC(TotalCallCreated
            << JQC(CurrentCall
            << JQC(SuccessfulCall(P)
            << JQC(SuccessfulCall(C)
            << JQC(FailedCall(P)
            << JQC(FailedCall(C)
            << JQC(FailedCannotSendMessage(P)
            << JQC(FailedCannotSendMessage(C)
            << JQC(FailedMaxUDPRetrans(P)
            << JQC(FailedMaxUDPRetrans(C)
            << JQC(FailedTcpConnect(P)
            << JQC(FailedTcpConnect(C)
            << JQC(FailedTcpClosed(P)
            << JQC(FailedTcpClosed(C)
            << JQC(FailedUnexpectedMessage(P)
            << JQC(FailedUnexpectedMessage(C)
            << JQC(FailedCallRejected(P)
            << JQC(FailedCallRejected(C)
            << JQC(FailedCmdNotSent(P)
            << JQC(FailedCmdNotSent(C)
            << JQC(FailedRegexpDoesntMatch(P)
            << JQC(FailedRegexpDoesntMatch(C)
            << JQC(FailedRegexpShouldntMatch(P)
            << JQC(FailedRegexpShouldntMatch(C)
            << JQC(FailedRegexpHdrNotFound(P)
            << JQC(FailedRegexpHdrNotFound(C)
            << JQC(FailedOutboundCongestion(P)
            << JQC(FailedOutboundCongestion(C)
            << JQC(FailedTimeoutOnRecv(P)
            << JQC(FailedTimeoutOnRecv(C)
            << JQC(FailedTimeoutOnSend(P)
            << JQC(FailedTimeoutOnSend(C)
            << JQC(FailedTestDoesntMatch(P)
            << JQC(FailedTestDoesntMatch(C)
            << JQC(FailedTestShouldntMatch(P)
            << JQC(FailedTestShouldntMatch(C)
            << JQC(FailedStrcmpDoesntMatch(P)
            << JQC(FailedStrcmpDoesntMatch(C)
            << JQC(FailedStrcmpShouldntMatch(P)
            << JQC(FailedStrcmpShouldntMatch(C)
            << JQC(OutOfCallMsgs(P)
            << JQC(OutOfCallMsgs(C)
            << JQC(DeadCallMsgs(P)
            << JQC(DeadCallMsgs(C)
            << JQC(Retransmissions(P)
            << JQC(Retransmissions(C)
            << JQC(AutoAnswered(P)
            << JQC(AutoAnswered(C)
            << JQC(Warnings(P)
            << JQC(Warnings(C)
            << JQC(FatalErrors(P)
            << JQC(FatalErrors(C)
            << JQC(WatchdogMajor(P)
            << JQC(WatchdogMajor(C)
            << JQC(WatchdogMinor(P)
            << JQ(WatchdogMinor(C)


        ////// agranig for now util here

        for (int i = 1; i <= nRtds(); i++) {
            char s_P[80];
            char s_C[80];

            sprintf(s_P, "ResponseTime%s(P)%s", M_revRtdMap[i], stat_delimiter);
            sprintf(s_C, "ResponseTime%s(C)%s", M_revRtdMap[i], stat_delimiter);

            (*M_outputStream) << s_P << s_C;

            sprintf(s_P, "ResponseTime%sStDev(P)%s", M_revRtdMap[i], stat_delimiter);
            sprintf(s_C, "ResponseTime%sStDev(C)%s", M_revRtdMap[i], stat_delimiter);

            (*M_outputStream) << s_P << s_C;
        }

        (*M_outputStream) << "CallLength(P)" << stat_delimiter
                          << "CallLength(C)" << stat_delimiter;
        (*M_outputStream) << "CallLengthStDev(P)" << stat_delimiter
                          << "CallLengthStDev(C)" << stat_delimiter;
        for (unsigned int i = 1; i < M_genericMap.size() + 1; i++) {
            (*M_outputStream) << M_revGenericMap[i] << "(P)" << stat_delimiter;
            (*M_outputStream) << M_revGenericMap[i] << "(C)" << stat_delimiter;
        }
        for (int i = 1; i <= nRtds(); i++) {
            char s[80];

            sprintf(s, "ResponseTimeRepartition%s", M_revRtdMap[i]);
            (*M_outputStream) << sRepartitionHeader(M_ResponseTimeRepartition[i - 1],
                                                    M_SizeOfResponseTimeRepartition,
                                                    s);
        }
        (*M_outputStream) << sRepartitionHeader(M_CallLengthRepartition,
                                                M_SizeOfCallLengthRepartition,
                                                "CallLengthRepartition");
        (*M_outputStream) << endl;
        M_headerAlreadyDisplayed = true;
    }

    // content
    (*M_outputStream) << formatTime(&M_startTime)               << stat_delimiter;
    (*M_outputStream) << formatTime(&M_plStartTime)             << stat_delimiter;
    (*M_outputStream) << formatTime(&currentTime)               << stat_delimiter
                      << msToHHMMSS(localElapsedTime)           << stat_delimiter;
    (*M_outputStream) << msToHHMMSS(globalElapsedTime)          << stat_delimiter;
    if (users >= 0) {
        (*M_outputStream) << users                                << stat_delimiter;
    } else {
        (*M_outputStream) << rate                                 << stat_delimiter;
    }
    (*M_outputStream) << realInstantCallRate                    << stat_delimiter
                      << averageCallRate                        << stat_delimiter
                      << M_counters[CPT_PL_IncomingCallCreated] << stat_delimiter
                      << M_counters[CPT_C_IncomingCallCreated]  << stat_delimiter
                      << M_counters[CPT_PL_OutgoingCallCreated] << stat_delimiter
                      << M_counters[CPT_C_OutgoingCallCreated]  << stat_delimiter
                      << (M_counters[CPT_C_IncomingCallCreated]+
                          M_counters[CPT_C_OutgoingCallCreated])<< stat_delimiter
                      << M_counters[CPT_C_CurrentCall]          << stat_delimiter
                      << M_counters[CPT_PL_SuccessfulCall]      << stat_delimiter
                      << M_counters[CPT_C_SuccessfulCall]       << stat_delimiter
                      << M_counters[CPT_PL_FailedCall]          << stat_delimiter
                      << M_counters[CPT_C_FailedCall]           << stat_delimiter
                      << M_counters[CPT_PL_FailedCallCannotSendMessage]   << stat_delimiter
                      << M_counters[CPT_C_FailedCallCannotSendMessage]    << stat_delimiter
                      << M_counters[CPT_PL_FailedCallMaxUdpRetrans]       << stat_delimiter
                      << M_counters[CPT_C_FailedCallMaxUdpRetrans     ]   << stat_delimiter
                      << M_counters[CPT_PL_FailedCallTcpConnect]          << stat_delimiter
                      << M_counters[CPT_C_FailedCallTcpConnect]           << stat_delimiter
                      << M_counters[CPT_PL_FailedCallTcpClosed]          << stat_delimiter
                      << M_counters[CPT_C_FailedCallTcpClosed]           << stat_delimiter
                      << M_counters[CPT_PL_FailedCallUnexpectedMessage]   << stat_delimiter
                      << M_counters[CPT_C_FailedCallUnexpectedMessage]    << stat_delimiter
                      << M_counters[CPT_PL_FailedCallCallRejected]        << stat_delimiter
                      << M_counters[CPT_C_FailedCallCallRejected]         << stat_delimiter
                      << M_counters[CPT_PL_FailedCallCmdNotSent]          << stat_delimiter
                      << M_counters[CPT_C_FailedCallCmdNotSent]           << stat_delimiter
                      << M_counters[CPT_PL_FailedCallRegexpDoesntMatch]   << stat_delimiter
                      << M_counters[CPT_C_FailedCallRegexpDoesntMatch]    << stat_delimiter
                      << M_counters[CPT_PL_FailedCallRegexpShouldntMatch] << stat_delimiter
                      << M_counters[CPT_C_FailedCallRegexpShouldntMatch]  << stat_delimiter
                      << M_counters[CPT_PL_FailedCallRegexpHdrNotFound]   << stat_delimiter
                      << M_counters[CPT_C_FailedCallRegexpHdrNotFound]    << stat_delimiter
                      << M_counters[CPT_PL_FailedOutboundCongestion]      << stat_delimiter
                      << M_counters[CPT_C_FailedOutboundCongestion]       << stat_delimiter
                      << M_counters[CPT_PL_FailedTimeoutOnRecv]           << stat_delimiter
                      << M_counters[CPT_C_FailedTimeoutOnRecv]            << stat_delimiter
                      << M_counters[CPT_PL_FailedTimeoutOnSend]           << stat_delimiter
                      << M_counters[CPT_C_FailedTimeoutOnSend]            << stat_delimiter
                      << M_counters[CPT_PL_FailedCallTestDoesntMatch]   << stat_delimiter
                      << M_counters[CPT_C_FailedCallTestDoesntMatch]    << stat_delimiter
                      << M_counters[CPT_PL_FailedCallTestShouldntMatch] << stat_delimiter
                      << M_counters[CPT_C_FailedCallTestShouldntMatch]  << stat_delimiter
                      << M_counters[CPT_PL_FailedCallStrcmpDoesntMatch]   << stat_delimiter
                      << M_counters[CPT_C_FailedCallStrcmpDoesntMatch]    << stat_delimiter
                      << M_counters[CPT_PL_FailedCallStrcmpShouldntMatch] << stat_delimiter
                      << M_counters[CPT_C_FailedCallStrcmpShouldntMatch]  << stat_delimiter
                      << M_G_counters[CPT_G_PL_OutOfCallMsgs - E_NB_COUNTER - 1]                << stat_delimiter
                      << M_G_counters[CPT_G_C_OutOfCallMsgs - E_NB_COUNTER - 1]                 << stat_delimiter
                      << M_G_counters[CPT_G_PL_DeadCallMsgs - E_NB_COUNTER - 1]                 << stat_delimiter
                      << M_G_counters[CPT_G_C_DeadCallMsgs - E_NB_COUNTER - 1]                  << stat_delimiter
                      << M_counters[CPT_PL_Retransmissions]               << stat_delimiter
                      << M_counters[CPT_C_Retransmissions]                << stat_delimiter
                      << M_G_counters[CPT_G_PL_AutoAnswered - E_NB_COUNTER - 1]                  << stat_delimiter
                      << M_G_counters[CPT_G_C_AutoAnswered - E_NB_COUNTER - 1]                   << stat_delimiter
                      << M_G_counters[CPT_G_PL_Warnings - E_NB_COUNTER - 1]                  << stat_delimiter
                      << M_G_counters[CPT_G_C_Warnings - E_NB_COUNTER - 1]                   << stat_delimiter
                      << M_G_counters[CPT_G_PL_FatalErrors - E_NB_COUNTER - 1]                  << stat_delimiter
                      << M_G_counters[CPT_G_C_FatalErrors - E_NB_COUNTER - 1]                   << stat_delimiter
                      << M_G_counters[CPT_G_PL_WatchdogMajor - E_NB_COUNTER - 1]                  << stat_delimiter
                      << M_G_counters[CPT_G_C_WatchdogMajor - E_NB_COUNTER - 1]                   << stat_delimiter
                      << M_G_counters[CPT_G_PL_WatchdogMinor - E_NB_COUNTER - 1]                  << stat_delimiter
                      << M_G_counters[CPT_G_C_WatchdogMinor - E_NB_COUNTER - 1]                   << stat_delimiter;

    // SF917289 << M_counters[CPT_C_UnexpectedMessage]    << stat_delimiter;
    for (int i = 1; i <= nRtds(); i++) {
        (*M_outputStream) << msToHHMMSSus( (unsigned long)computeRtdMean(i, GENERIC_PL)) << stat_delimiter;
        (*M_outputStream) << msToHHMMSSus( (unsigned long)computeRtdMean(i, GENERIC_C)) << stat_delimiter;
        (*M_outputStream) << msToHHMMSSus( (unsigned long)computeRtdStdev(i, GENERIC_PL)) << stat_delimiter;
        (*M_outputStream) << msToHHMMSSus( (unsigned long)computeRtdStdev(i, GENERIC_C)) << stat_delimiter;
    }
    (*M_outputStream)
            << msToHHMMSSus( (unsigned long)computeMean(CPT_PL_AverageCallLength_Sum, CPT_PL_NbOfCallUsedForAverageCallLength) ) << stat_delimiter;
    (*M_outputStream)
            << msToHHMMSSus( (unsigned long)computeMean(CPT_C_AverageCallLength_Sum, CPT_C_NbOfCallUsedForAverageCallLength) ) << stat_delimiter;
    (*M_outputStream)
            << msToHHMMSSus( (unsigned long)computeStdev(CPT_PL_AverageCallLength_Sum,
                              CPT_PL_NbOfCallUsedForAverageCallLength,
                              CPT_PL_AverageCallLength_Squares )) << stat_delimiter;
    (*M_outputStream)
            << msToHHMMSSus( (unsigned long)computeStdev(CPT_C_AverageCallLength_Sum,
                              CPT_C_NbOfCallUsedForAverageCallLength,
                              CPT_C_AverageCallLength_Squares )) << stat_delimiter;

    for (unsigned int i = 0; i < M_genericMap.size(); i++) {
        (*M_outputStream) << M_genericCounters[GENERIC_TYPES * i + GENERIC_PL] << stat_delimiter;
        (*M_outputStream) << M_genericCounters[GENERIC_TYPES * i + GENERIC_C] << stat_delimiter;
    }

    for (int i = 0; i < nRtds(); i++) {
        (*M_outputStream)
                << sRepartitionInfo(M_ResponseTimeRepartition[i],
                                    M_SizeOfResponseTimeRepartition);
    }
    (*M_outputStream)
            << sRepartitionInfo(M_CallLengthRepartition,
                                M_SizeOfCallLengthRepartition);
    (*M_outputStream) << endl;

    // flushing the output file to let the tail -f working !
    (*M_outputStream).flush();

    */

    std::string jsonDataStr = jsonData.str();

    ret = mosquitto_publish(mqtt_handler, NULL, mqtt_stats_topic,
				   jsonDataStr.length(), jsonDataStr.c_str(),
                   2, false);
	if (ret != MOSQ_ERR_SUCCESS) {
		WARNING("MQTT: failed to publish message: %s\n", mosquitto_strerror(ret));
	}

}

void MQTTStat::dumpDataRtt ()
{
    WARNING("MQTTStat::dumpDataRtt\n");

    if (mqtt_handler == NULL) {
        cerr << "Unable to use uninitialized MQTT handler!" << endl;
        exit(EXIT_FATAL_ERROR);
    }

    WARNING("MQTTStat::dumpDataRtt data goes here\n");
    return;

    if(M_headerAlreadyDisplayedRtt == false) {
        (*M_outputStreamRtt) << "Date_ms" << stat_delimiter
                             << "response_time_ms" << stat_delimiter
                             << "rtd_no" << endl;
        M_headerAlreadyDisplayedRtt = true;
    }

    for (unsigned int L_i = 0; L_i < M_counterDumpRespTime ; L_i ++) {
        (*M_outputStreamRtt) <<  M_dumpRespTime[L_i].date   << stat_delimiter ;
        (*M_outputStreamRtt) <<  M_dumpRespTime[L_i].rtt    << stat_delimiter ;
        (*M_outputStreamRtt) <<  M_revRtdMap[M_dumpRespTime[L_i].rtd_no] << endl;
        (*M_outputStreamRtt).flush();
        M_dumpRespTime[L_i].date = 0.0;
        M_dumpRespTime[L_i].rtt = 0.0;
        M_dumpRespTime[L_i].rtd_no = 0;
    }

    // flushing the output file
    (*M_outputStreamRtt).flush();

    M_counterDumpRespTime = 0;
}


