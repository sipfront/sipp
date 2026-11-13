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
 *  Author : Sipfront GmbH
 */

/* What SIPp publishes about a run: see mqtt.hpp.
 *
 * The statistics message is not a second list of counters kept next to the
 * one of the statistics file: CStat writes the file's header and one line
 * into a buffer, and the two are joined into an object here. A column that
 * the file gains is in the message with it, under the same name.
 *
 * All values are strings, also the numbers, and every message has
 * "CurrentTime" as "YYYY-MM-DD HH:MM:SS.uuuuuu" in local time, whatever
 * -rfc3339 and -stat_delimiter say: who reads the messages should not have
 * to know how SIPp was started. */

#ifdef USE_MQTT

#include <fstream>
#include <sstream>

#include "sipp.hpp"
#include "mqtt.hpp"

std::string mqtt_json_escape(const std::string &in)
{
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(in.length() + 16);

    const unsigned char *s = (const unsigned char *)in.data();
    size_t len = in.length();
    for (size_t i = 0; i < len; ) {
        unsigned char c = s[i];
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 0x20 || c == 0x7f) {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 0x0f];
        } else if (c < 0x80) {
            out += (char)c;
        } else {
            /* A well-formed UTF-8 sequence is copied; anything else (a
             * stray byte of a SIP message in another charset, a sequence
             * cut short) becomes U+FFFD, so the message stays valid JSON. */
            size_t n = (c >= 0xc2 && c <= 0xdf) ? 2 : (c >= 0xe0 && c <= 0xef) ? 3 :
                       (c >= 0xf0 && c <= 0xf4) ? 4 : 0;
            bool ok = n && i + n <= len;
            for (size_t k = 1; ok && k < n; k++) {
                ok = (s[i + k] & 0xc0) == 0x80;
            }
            if (ok && n == 3) {
                /* no overlong forms, no surrogates */
                ok = !(c == 0xe0 && s[i + 1] < 0xa0) && !(c == 0xed && s[i + 1] > 0x9f);
            }
            if (ok && n == 4) {
                ok = !(c == 0xf0 && s[i + 1] < 0x90) && !(c == 0xf4 && s[i + 1] > 0x8f);
            }
            if (ok) {
                out.append((const char *)s + i, n);
                i += n;
                continue;
            }
            out += "\xef\xbf\xbd";
        }
        i++;
    }
    return out;
}

std::string mqtt_format_time(const struct timeval *tv)
{
    char buf[64];
    time_t seconds = tv->tv_sec;
    struct tm tm;

    if (!localtime_r(&seconds, &tm)) {
        return "";
    }
    snprintf(buf, sizeof(buf), "%4.4d-%2.2d-%2.2d %2.2d:%2.2d:%2.2d.%06ld",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, (long)tv->tv_usec);
    return buf;
}

std::vector<std::string> mqtt_split(const std::string &line, const std::string &delimiter)
{
    std::vector<std::string> fields;
    if (delimiter.empty()) {
        fields.push_back(line);
        return fields;
    }
    size_t start = 0;
    for (;;) {
        size_t end = line.find(delimiter, start);
        if (end == std::string::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, end - start));
        start = end + delimiter.length();
    }
}

std::string mqtt_csv_to_json(const std::string &header, const std::string &row,
                             const std::string &delimiter)
{
    std::vector<std::string> names = mqtt_split(header, delimiter);
    std::vector<std::string> values = mqtt_split(row, delimiter);
    std::string json = "{";
    bool first = true;

    for (size_t i = 0; i < names.size(); i++) {
        if (names[i].empty()) {
            /* every line of the file ends with a delimiter */
            continue;
        }
        if (!first) {
            json += ',';
        }
        first = false;
        json += '"';
        json += mqtt_json_escape(names[i]);
        json += "\":\"";
        if (i < values.size()) {
            json += mqtt_json_escape(values[i]);
        }
        json += '"';
    }
    json += '}';
    return json;
}

/* "name":"value" */
static std::string member(const char *name, const std::string &value)
{
    return std::string("\"") + name + "\":\"" + mqtt_json_escape(value) + "\"";
}

static std::string member(const char *name, unsigned long value)
{
    return member(name, std::to_string(value));
}

/* A stream that CStat::dumpData() takes for its file and that keeps what is
 * written in memory. */
class MqttStatBuffer : public std::ofstream
{
public:
    MqttStatBuffer()
    {
        /* basic_ios::rdbuf(sb): the stream writes to our buffer, not to
         * the file buffer it was born with (and never opened). */
        std::ios::rdbuf(&buffer);
    }
    std::string take()
    {
        std::string text = buffer.str();
        buffer.str("");
        clear();
        return text;
    }
private:
    std::stringbuf buffer;
};

/* The friend of CStat that reads what the statistics file would get. */
class MqttStats
{
public:
    /* The header and the line of this moment, as an object. */
    static std::string stats(CStat *stat)
    {
        /* A separator of our own while the lines are written: one that no
         * column name or value contains, whatever -stat_delimiter is. */
        static const char separator[] = "\x1f";
        static MqttStatBuffer capture;

        std::ofstream *file = stat->M_outputStream;
        bool file_has_header = stat->M_headerAlreadyDisplayed;
        const char *delimiter = stat_delimiter;

        stat->M_outputStream = &capture;
        stat->M_headerAlreadyDisplayed = false;
        stat_delimiter = separator;
        stat->dumpData();
        stat_delimiter = delimiter;
        stat->M_headerAlreadyDisplayed = file_has_header;
        stat->M_outputStream = file;

        std::string text = capture.take();
        size_t header_end = text.find('\n');
        if (header_end == std::string::npos) {
            return "";
        }
        size_t row_end = text.find('\n', header_end + 1);
        std::string header = text.substr(0, header_end);
        std::string row = text.substr(header_end + 1,
                                      row_end == std::string::npos ? std::string::npos : row_end - header_end - 1);

        /* The three times as every message has them, not as the file does
         * (date, time and epoch with tabs between, or RFC 3339). */
        struct timeval now;
        GET_TIME(&now);
        std::vector<std::string> names = mqtt_split(header, separator);
        std::vector<std::string> values = mqtt_split(row, separator);
        values.resize(names.size());
        for (size_t i = 0; i < names.size(); i++) {
            if (names[i] == "StartTime") {
                values[i] = mqtt_format_time(&stat->M_startTime);
            } else if (names[i] == "LastResetTime") {
                values[i] = mqtt_format_time(&stat->M_plStartTime);
            } else if (names[i] == "CurrentTime") {
                values[i] = mqtt_format_time(&now);
            }
        }
        std::string joined;
        for (size_t i = 0; i < values.size(); i++) {
            joined += values[i];
            joined += separator;
        }
        return mqtt_csv_to_json(header, joined, separator);
    }
};

/* "CurrentTime" and "ElapsedTime", which the counts and the error codes
 * start with. */
static std::string time_members(scenario *scn)
{
    struct timeval now, start;
    GET_TIME(&now);
    scn->stats->getStartTime(&start);
    return member("CurrentTime", mqtt_format_time(&now)) + "," +
           member("ElapsedTime", CStat::msToHHMMSSus(CStat::computeDiffTimeInMs(&now, &start)));
}

/* The counts of each message of the scenario, in its order:
 *   {"CurrentTime":..,"ElapsedTime":..,"Counts":[{"Type":"sendreq","Name":"INVITE","Sent":"10","Retrans":"0"},...]}
 * Type is sendreq, sendres, recvreq, recvres, pause, nop, recvcmd or sendcmd. */
static std::string counts_json(scenario *scn)
{
    std::string json = "{" + time_members(scn) + ",\"Counts\":[";
    bool first = true;

    for (message *msg : scn->messages) {
        if (msg->hide) {
            continue;
        }
        std::string entry;
        if (SendingMessage *src = msg->send_scheme) {
            if (src->isResponse()) {
                entry = member("Type", "sendres") + "," + member("Name", std::to_string(src->getCode()));
            } else {
                entry = member("Type", "sendreq") + "," + member("Name", src->getMethod());
            }
            entry += "," + member("Sent", msg->nb_sent) + "," + member("Retrans", msg->nb_sent_retrans);
            if (msg->retrans_delay) {
                entry += "," + member("Timeout", msg->nb_timeout);
            }
            if (lose_packets) {
                entry += "," + member("Lost", msg->nb_lost);
            }
        } else if (msg->recv_response || msg->recv_request) {
            if (msg->recv_response) {
                entry = member("Type", "recvres") + "," + member("Name", *msg->recv_response);
            } else {
                entry = member("Type", "recvreq") + "," + member("Name", *msg->recv_request);
            }
            entry += "," + member("Recv", msg->nb_recv) + "," + member("Retrans", msg->nb_recv_retrans) +
                     "," + member("Timeout", msg->nb_timeout) + "," + member("Unexp", msg->nb_unexp);
            if (lose_packets) {
                entry += "," + member("Lost", msg->nb_lost);
            }
        } else if (msg->pause_distribution || msg->pause_variable != -1) {
            entry = member("Type", "pause") + "," + member("Sessions", (unsigned long)msg->sessions) +
                    "," + member("Unexp", msg->nb_unexp);
        } else if (msg->M_type == MSG_TYPE_NOP) {
            entry = member("Type", "nop") + "," + member("Name", "NOP");
        } else if (msg->M_type == MSG_TYPE_RECVCMD) {
            entry = member("Type", "recvcmd") + "," + member("RecvCmd", msg->M_nbCmdRecv) +
                    "," + member("Timeout", msg->nb_timeout);
        } else if (msg->M_type == MSG_TYPE_SENDCMD) {
            entry = member("Type", "sendcmd") + "," + member("SendCmd", msg->M_nbCmdSent);
        } else {
            entry = member("Type", "invalid");
        }
        if (!first) {
            json += ',';
        }
        first = false;
        json += "{" + entry + "}";
    }
    json += "]}";
    return json;
}

/* The response codes of the unexpected messages since the last time:
 *   {"CurrentTime":..,"ElapsedTime":..,"ErrorCodes":[503,503,486]}
 * The list is emptied, unless -trace_error_codes wants it for its file. */
static std::string codes_json(scenario *scn)
{
    std::string json = "{" + time_members(scn) + ",\"ErrorCodes\":[";
    std::vector<int> &codes = scn->stats->error_codes;

    for (size_t i = 0; i < codes.size(); i++) {
        if (i) {
            json += ',';
        }
        json += std::to_string(codes[i]);
    }
    json += "]}";
    if (!useErrorCodesf) {
        codes.clear();
    }
    return json;
}

void mqtt_report_stats()
{
    if (!mqtt_active() || !main_scenario || !main_scenario->stats) {
        return;
    }
    std::string stats = MqttStats::stats(main_scenario->stats);
    if (!stats.empty()) {
        mqtt_publish(mqtt_stats_topic, stats);
    }
    mqtt_publish(mqtt_countstats_topic, counts_json(main_scenario));
    mqtt_publish(mqtt_codestats_topic, codes_json(main_scenario));
}

#ifdef GTEST
#include "gtest/gtest.h"

TEST(mqtt, JsonEscape) {
    EXPECT_EQ("plain", mqtt_json_escape("plain"));
    EXPECT_EQ("a \\\"quoted\\\" \\\\ value", mqtt_json_escape("a \"quoted\" \\ value"));
    EXPECT_EQ("SIP/2.0 200 OK\\r\\nVia: x\\r\\n", mqtt_json_escape("SIP/2.0 200 OK\r\nVia: x\r\n"));
    EXPECT_EQ("tab\\there", mqtt_json_escape("tab\there"));
    EXPECT_EQ("\\u0001\\u001f\\u007f", mqtt_json_escape(std::string("\x01\x1f\x7f")));
    EXPECT_EQ(std::string("nul\\u0000in it"), mqtt_json_escape(std::string("nul\0in it", 9)));
}

TEST(mqtt, JsonEscapeUtf8) {
    /* well-formed sequences of two, three and four bytes are kept */
    EXPECT_EQ("gr\xc3\xbc\xc3\x9f", mqtt_json_escape("gr\xc3\xbc\xc3\x9f"));
    EXPECT_EQ("\xe2\x82\xac", mqtt_json_escape("\xe2\x82\xac"));
    EXPECT_EQ("\xf0\x9f\x98\x80", mqtt_json_escape("\xf0\x9f\x98\x80"));
    /* a Latin-1 byte, a sequence cut short, an overlong form, a surrogate */
    EXPECT_EQ("gr\xef\xbf\xbd", mqtt_json_escape("gr\xfc"));
    EXPECT_EQ("\xef\xbf\xbd", mqtt_json_escape("\xe2"));
    EXPECT_EQ("\xef\xbf\xbd\xef\xbf\xbd", mqtt_json_escape("\xc0\xaf"));
    EXPECT_EQ("\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd", mqtt_json_escape("\xed\xa0\x80"));
}

TEST(mqtt, FormatTime) {
    struct timeval tv = { 86400 * 365, 42 };
    std::string s = mqtt_format_time(&tv);
    /* the date and hour depend on the time zone of the build host */
    ASSERT_EQ(26u, s.length());
    EXPECT_EQ(' ', s[10]);
    EXPECT_EQ(".000042", s.substr(19));
}

TEST(mqtt, Split) {
    EXPECT_EQ((std::vector<std::string>{"a", "b", ""}), mqtt_split("a;b;", ";"));
    EXPECT_EQ((std::vector<std::string>{"a", "", "b"}), mqtt_split("a;;b", ";"));
    EXPECT_EQ((std::vector<std::string>{""}), mqtt_split("", ";"));
    EXPECT_EQ((std::vector<std::string>{"a", "b"}), mqtt_split("a<>b", "<>"));
    EXPECT_EQ((std::vector<std::string>{"a;b"}), mqtt_split("a;b", ""));
}

TEST(mqtt, CsvToJson) {
    EXPECT_EQ("{\"CallRate(P)\":\"1.000\",\"CurrentCall\":\"15\"}",
              mqtt_csv_to_json("CallRate(P);CurrentCall;", "1.000;15;", ";"));
    /* a line shorter than its header, and a name that needs escaping */
    EXPECT_EQ("{\"a\":\"1\",\"b \\\"x\\\"\":\"\"}", mqtt_csv_to_json("a;b \"x\";", "1", ";"));
    EXPECT_EQ("{}", mqtt_csv_to_json("", "", ";"));
}
#endif /* GTEST */

#endif /* USE_MQTT */
