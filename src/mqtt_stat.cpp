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


int MQTTStat::init ()
{
    int ret = CStat::init();
    if (ret != 1)
        return ret;

    // MQTT specific init goes here

    return(1);
}

// agranig: TODO: do we need this?
void MQTTStat::setTopic(const char* P_name)
{
    CStat::setFileName(P_name);
}

void MQTTStat::initRtt(const char* P_name, const char* P_extension,
                    unsigned long P_report_freq_dumpRtt)
{
    // TODO: can we skip extension/pid in mqtt context? do we need other method for that?
    
    int sizeOf, sizeOfExtension;

    if(P_name != NULL) {
        sizeOf = strlen(P_name) ;
        if(sizeOf > 0) {
            //  4 for '_rtt' and 6 for pid
            sizeOf += 10 ;
            sizeOfExtension = strlen(P_extension);
            if(M_fileNameRtt != NULL)
                delete [] M_fileNameRtt;
            sizeOf += sizeOfExtension;
            M_fileNameRtt = new char[sizeOf+1];
            sprintf (M_fileNameRtt, "%s_%ld_rtt%s", P_name, (long) getpid(),P_extension);
        } else {
            cerr << "new file name length is null - "
                 << "keeping the default filename : "
                 << DEFAULT_FILE_NAME << endl;
        }
    } else {
        cerr << "new file name is NULL ! - keeping the default filename : "
             << DEFAULT_FILE_NAME << endl;
    }

    // initiate the table dump response time
    M_report_freq_dumpRtt = P_report_freq_dumpRtt ;

    M_dumpRespTime = new T_value_rtt [P_report_freq_dumpRtt] ;

    if ( M_dumpRespTime == NULL ) {
        cerr << "Memory allocation failure" << endl;
        exit(EXIT_FATAL_ERROR);
    }

    for (unsigned L_i = 0 ; L_i < P_report_freq_dumpRtt; L_i ++) {
        M_dumpRespTime[L_i].date = 0.0;
        M_dumpRespTime[L_i].rtd_no = 0;
        M_dumpRespTime[L_i].rtt = 0.0;
    }
}

MQTTStat::MQTTStat ()
{
    CStat();

    init();
}

void MQTTStat::dumpData ()
{
    long   localElapsedTime, globalElapsedTime ;
    struct timeval currentTime;
    float  averageCallRate;
    float  realInstantCallRate;
    unsigned long numberOfCall;

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

    if(M_outputStream == NULL) {
        // if the file is still not opened, we opened it now
        M_outputStream = new ofstream(M_fileName);
        M_headerAlreadyDisplayed = false;

        if(M_outputStream == NULL) {
            cerr << "Unable to open stat file '" << M_fileName << "' !" << endl;
            exit(EXIT_FATAL_ERROR);
        }

#ifndef __osf__
        if(!M_outputStream->is_open()) {
            cerr << "Unable to open stat file '" << M_fileName << "' !" << endl;
            exit(EXIT_FATAL_ERROR);
        }
#endif

    }

    if(M_headerAlreadyDisplayed == false) {
        // header - it's dump in file only one time at the beginning of the file
        (*M_outputStream) << "StartTime" << stat_delimiter
                          << "LastResetTime" << stat_delimiter
                          << "CurrentTime" << stat_delimiter
                          << "ElapsedTime(P)" << stat_delimiter
                          << "ElapsedTime(C)" << stat_delimiter
                          << "TargetRate" << stat_delimiter
                          << "CallRate(P)" << stat_delimiter
                          << "CallRate(C)" << stat_delimiter
                          << "IncomingCall(P)" << stat_delimiter
                          << "IncomingCall(C)" << stat_delimiter
                          << "OutgoingCall(P)" << stat_delimiter
                          << "OutgoingCall(C)" << stat_delimiter
                          << "TotalCallCreated" << stat_delimiter
                          << "CurrentCall" << stat_delimiter
                          << "SuccessfulCall(P)" << stat_delimiter
                          << "SuccessfulCall(C)" << stat_delimiter
                          << "FailedCall(P)" << stat_delimiter
                          << "FailedCall(C)" << stat_delimiter
                          << "FailedCannotSendMessage(P)" << stat_delimiter
                          << "FailedCannotSendMessage(C)" << stat_delimiter
                          << "FailedMaxUDPRetrans(P)" << stat_delimiter
                          << "FailedMaxUDPRetrans(C)" << stat_delimiter
                          << "FailedTcpConnect(P)" << stat_delimiter
                          << "FailedTcpConnect(C)" << stat_delimiter
                          << "FailedTcpClosed(P)" << stat_delimiter
                          << "FailedTcpClosed(C)" << stat_delimiter
                          << "FailedUnexpectedMessage(P)" << stat_delimiter
                          << "FailedUnexpectedMessage(C)" << stat_delimiter
                          << "FailedCallRejected(P)" << stat_delimiter
                          << "FailedCallRejected(C)" << stat_delimiter
                          << "FailedCmdNotSent(P)" << stat_delimiter
                          << "FailedCmdNotSent(C)" << stat_delimiter
                          << "FailedRegexpDoesntMatch(P)" << stat_delimiter
                          << "FailedRegexpDoesntMatch(C)" << stat_delimiter
                          << "FailedRegexpShouldntMatch(P)" << stat_delimiter
                          << "FailedRegexpShouldntMatch(C)" << stat_delimiter
                          << "FailedRegexpHdrNotFound(P)" << stat_delimiter
                          << "FailedRegexpHdrNotFound(C)" << stat_delimiter
                          << "FailedOutboundCongestion(P)" << stat_delimiter
                          << "FailedOutboundCongestion(C)" << stat_delimiter
                          << "FailedTimeoutOnRecv(P)" << stat_delimiter
                          << "FailedTimeoutOnRecv(C)" << stat_delimiter
                          << "FailedTimeoutOnSend(P)" << stat_delimiter
                          << "FailedTimeoutOnSend(C)" << stat_delimiter
                          << "FailedTestDoesntMatch(P)" << stat_delimiter
                          << "FailedTestDoesntMatch(C)" << stat_delimiter
                          << "FailedTestShouldntMatch(P)" << stat_delimiter
                          << "FailedTestShouldntMatch(C)" << stat_delimiter
                          << "FailedStrcmpDoesntMatch(P)" << stat_delimiter
                          << "FailedStrcmpDoesntMatch(C)" << stat_delimiter
                          << "FailedStrcmpShouldntMatch(P)" << stat_delimiter
                          << "FailedStrcmpShouldntMatch(C)" << stat_delimiter
                          << "OutOfCallMsgs(P)" << stat_delimiter
                          << "OutOfCallMsgs(C)" << stat_delimiter
                          << "DeadCallMsgs(P)" << stat_delimiter
                          << "DeadCallMsgs(C)" << stat_delimiter
                          << "Retransmissions(P)" << stat_delimiter
                          << "Retransmissions(C)" << stat_delimiter
                          << "AutoAnswered(P)" << stat_delimiter
                          << "AutoAnswered(C)" << stat_delimiter
                          << "Warnings(P)" << stat_delimiter
                          << "Warnings(C)" << stat_delimiter
                          << "FatalErrors(P)" << stat_delimiter
                          << "FatalErrors(C)" << stat_delimiter
                          << "WatchdogMajor(P)" << stat_delimiter
                          << "WatchdogMajor(C)" << stat_delimiter
                          << "WatchdogMinor(P)" << stat_delimiter
                          << "WatchdogMinor(C)" << stat_delimiter;

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

} /* end of logData () */

void MQTTStat::dumpDataRtt ()
{
    if(M_outputStreamRtt == NULL) {
        // if the file is still not opened, we opened it now
        M_outputStreamRtt = new ofstream(M_fileNameRtt);
        M_headerAlreadyDisplayedRtt = false;

        if(M_outputStreamRtt == NULL) {
            cerr << "Unable to open rtt file '" << M_fileNameRtt << "' !" << endl;
            exit(EXIT_FATAL_ERROR);
        }

#ifndef __osf__
        if(!M_outputStreamRtt->is_open()) {
            cerr << "Unable to open rtt file '" << M_fileNameRtt << "' !" << endl;
            exit(EXIT_FATAL_ERROR);
        }
#endif
    }

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


