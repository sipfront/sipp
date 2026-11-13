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

/* Errors and SIP messages over MQTT: see mqtt.hpp.
 *
 * An error is one message, and a load test that fails has one per call. So
 * the errors are held to -mqtt_error_rate a second: the others are counted
 * and their number goes out as "Suppressed" with the next one that does. A
 * fatal error is the last thing SIPp says and always goes out.
 *
 * WARNING() and ERROR() are called from any thread (the RTP threads too), so
 * what is kept here between calls is behind a mutex, and nothing here calls
 * them back. */

#ifdef USE_MQTT

#include <mutex>

#include "sipp.hpp"
#include "mqtt.hpp"

static std::mutex error_mutex;
static MqttErrorLimit error_limit;

bool MqttErrorLimit::admit(time_t second, bool fatal, int rate)
{
    total_++;
    if (fatal || rate <= 0) {
        return true;
    }
    if (second != second_) {
        second_ = second;
        second_count_ = 0;
    }
    if (second_count_ >= rate) {
        suppressed_++;
        return false;
    }
    second_count_++;
    return true;
}

void MqttErrorLimit::published(bool taken)
{
    if (taken) {
        suppressed_ = 0;
    } else {
        suppressed_++;
    }
}

static std::string member(const char *name, const std::string &value, bool last = false)
{
    return std::string("\"") + name + "\":\"" + mqtt_json_escape(value) + (last ? "\"" : "\",");
}

std::string mqtt_error_json(const struct timeval *now, bool fatal, const std::string &text, int error,
                            unsigned long total, unsigned long suppressed, int max_len)
{
    std::string content = text;
    if (max_len > 0 && content.length() > (size_t)max_len) {
        /* The start says what went wrong; an unexpected message that
         * follows is cut, not dropped. Not in the middle of a UTF-8
         * sequence, which the escaping would turn into U+FFFD. */
        size_t cut = max_len;
        while (cut > 0 && ((unsigned char)content[cut] & 0xc0) == 0x80) {
            cut--;
        }
        content.resize(cut);
        content += "...";
    }
    return "{" + member("CurrentTime", mqtt_format_time(now)) + member("Content", content) +
           member("Fatal", fatal ? "1" : "0") + member("Total", std::to_string(total)) +
           member("Suppressed", std::to_string(suppressed)) + member("ErrnoString", error ? strerror(error) : "") +
           member("Errno", std::to_string(error), true) + "}";
}

bool mqtt_errors_enabled()
{
    return print_all_responses && mqtt_stats && mqtt_active();
}

void mqtt_publish_error(const struct timeval *now, bool fatal, const std::string &text, int error)
{
    std::lock_guard<std::mutex> lock(error_mutex);
    /* The second of the limit is read here, behind the mutex: the caller's
     * time was taken before it, and a caller that waited would bring an
     * old second back and its allowance with it. */
    struct timeval limit_clock;
    GET_TIME(&limit_clock);
    if (!error_limit.admit(limit_clock.tv_sec, fatal, mqtt_error_rate)) {
        return;
    }
    /* Quiet: a publish that fails would be reported with a WARNING, which
     * comes back here. Behind the mutex, so that the count of what was held
     * back goes out once, and is kept when the message is not taken: it,
     * and this error, are reported with the next one that is. */
    error_limit.published(mqtt_publish(
        mqtt_error_topic,
        mqtt_error_json(now, fatal, text, error, error_limit.total(), error_limit.suppressed(), mqtt_error_max_len),
        true));
}

std::string mqtt_message_json(const struct timeval *now, const std::string &call_id, const char *direction,
                              const char *transport, bool control, const std::string &message, size_t size)
{
    return "{" + member("CurrentTime", mqtt_format_time(now)) + member("CallId", call_id) +
           member("Direction", direction) + member("Transport", transport) + member("Type", control ? "control" : "") +
           member("Message", message) + member("Size", std::to_string(size), true) + "}";
}

void mqtt_trace_message(const struct timeval *now, const char *direction, const char *transport, bool control,
                        const char *buffer, size_t len)
{
    if (!(useMessagef == 1 || useShortMessagef == 1) || !mqtt_stats || !mqtt_active()) {
        return;
    }
    /* A buffered message is not null terminated. */
    std::string message(buffer, len);
    std::string call_id(get_trimmed_call_id(message.c_str()));
    if (useMessagef != 1) {
        /* -trace_shortmsg alone: the first line says which message it was. */
        message = std::string(get_first_line(message.c_str()));
    }
    mqtt_publish(mqtt_message_topic, mqtt_message_json(now, call_id, direction, transport, control, message, len));
}

#ifdef GTEST
#include "gtest/gtest.h"

TEST(mqtt, ErrorJson)
{
    struct timeval tv = {0, 0};
    std::string json = mqtt_error_json(
        &tv, false, "Aborting call on unexpected message for Call-Id 'a@b': received 'SIP/2.0 503 x\r\n'", 0, 7, 3,
        4096);
    EXPECT_NE(
        std::string::npos,
        json.find(
            "\"Content\":\"Aborting call on unexpected message for Call-Id 'a@b': received 'SIP/2.0 503 x\\r\\n'\""));
    EXPECT_NE(std::string::npos,
              json.find("\"Fatal\":\"0\",\"Total\":\"7\",\"Suppressed\":\"3\",\"ErrnoString\":\"\",\"Errno\":\"0\"}"));

    json = mqtt_error_json(&tv, true, "Unable to bind main socket", EADDRINUSE, 1, 0, 4096);
    EXPECT_NE(std::string::npos, json.find("\"Fatal\":\"1\""));
    EXPECT_NE(std::string::npos, json.find(std::string("\"ErrnoString\":\"") + strerror(EADDRINUSE) + "\""));
}

TEST(mqtt, ErrorLimit)
{
    MqttErrorLimit limit;
    /* two a second: the third and fourth of second 100 are held back */
    EXPECT_TRUE(limit.admit(100, false, 2));
    limit.published(true);
    EXPECT_TRUE(limit.admit(100, false, 2));
    limit.published(true);
    EXPECT_FALSE(limit.admit(100, false, 2));
    EXPECT_FALSE(limit.admit(100, false, 2));
    EXPECT_EQ(2UL, limit.suppressed());
    /* a fatal one goes out in a second that is used up, and reports them */
    EXPECT_TRUE(limit.admit(100, true, 2));
    EXPECT_EQ(5UL, limit.total());
    EXPECT_EQ(2UL, limit.suppressed());
    limit.published(true);
    EXPECT_EQ(0UL, limit.suppressed());
    /* the next second has its own two */
    EXPECT_FALSE(limit.admit(100, false, 2));
    EXPECT_TRUE(limit.admit(101, false, 2));
    EXPECT_EQ(1UL, limit.suppressed());
    limit.published(true);
    EXPECT_TRUE(limit.admit(101, false, 2));
    limit.published(true);
    EXPECT_FALSE(limit.admit(101, false, 2));
    EXPECT_EQ(9UL, limit.total());
}

TEST(mqtt, ErrorLimitKeepsTheCountOfAMessageNotTaken)
{
    MqttErrorLimit limit;
    EXPECT_TRUE(limit.admit(100, false, 1));
    limit.published(true);
    EXPECT_FALSE(limit.admit(100, false, 1));
    /* the message that would have said "1 suppressed" is not taken */
    EXPECT_TRUE(limit.admit(101, false, 1));
    EXPECT_EQ(1UL, limit.suppressed());
    limit.published(false);
    EXPECT_EQ(2UL, limit.suppressed());
    /* the next one says both, and then they are forgotten */
    EXPECT_TRUE(limit.admit(102, false, 1));
    EXPECT_EQ(2UL, limit.suppressed());
    limit.published(true);
    EXPECT_EQ(0UL, limit.suppressed());
}

TEST(mqtt, ErrorLimitOff)
{
    MqttErrorLimit limit;
    for (int i = 0; i < 1000; i++) {
        EXPECT_TRUE(limit.admit(100, false, 0));
        limit.published(true);
    }
    EXPECT_EQ(1000UL, limit.total());
    EXPECT_EQ(0UL, limit.suppressed());
}

TEST(mqtt, ErrorJsonCutsLongText)
{
    struct timeval tv = {0, 0};
    EXPECT_NE(std::string::npos, mqtt_error_json(&tv, false, "0123456789", 0, 1, 0, 4).find("\"Content\":\"0123...\""));
    EXPECT_NE(std::string::npos,
              mqtt_error_json(&tv, false, "0123456789", 0, 1, 0, 0).find("\"Content\":\"0123456789\""));
    /* not inside the two bytes of the u-umlaut */
    EXPECT_NE(std::string::npos, mqtt_error_json(&tv, false,
                                                 "ab\xc3\xbc"
                                                 "cd",
                                                 0, 1, 0, 3)
                                     .find("\"Content\":\"ab...\""));
}

TEST(mqtt, MessageJson)
{
    struct timeval tv = {0, 0};
    std::string json = mqtt_message_json(&tv, "1-2@127.0.0.1", "recv", "UDP", false, "SIP/2.0 200 OK\r\n\r\n", 18);
    EXPECT_NE(std::string::npos,
              json.find("\"CallId\":\"1-2@127.0.0.1\",\"Direction\":\"recv\",\"Transport\":\"UDP\",\"Type\":\"\","
                        "\"Message\":\"SIP/2.0 200 OK\\r\\n\\r\\n\",\"Size\":\"18\"}"));
}
#endif /* GTEST */

#endif /* USE_MQTT */
