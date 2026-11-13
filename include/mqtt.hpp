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

#ifndef __SIPP_MQTT_HPP__
#define __SIPP_MQTT_HPP__

/*
 * SIPp as an MQTT client: what a run measures is published to a broker while
 * it runs, for a test that is driven and watched from somewhere else.
 *
 * The connection (mqtt.cpp)
 *
 *   One connection for the process, made before the traffic starts and
 *   closed after the last statistics went out. libmosquitto runs it in a
 *   thread of its own: it keeps the connection alive, makes it again when it
 *   is lost and writes what is queued, so the SIP main loop is never held up
 *   by the broker and has no socket of it to watch. Nothing of SIPp runs in
 *   that thread: its callbacks only set flags that the main thread reads.
 *
 *   A broker that cannot be reached at startup is a fatal error: a run
 *   nobody can see is not worth starting. Losing it later is not: SIPp goes
 *   on, says so once, and publishes again when the connection is back.
 *
 * The statistics (mqtt_stat.cpp), every -fd interval and once more at the end
 *
 *   -mqtt_stats_topic       what -trace_stat writes, one JSON object per
 *                           interval: the names of the statistics file's
 *                           columns as keys, their values as strings.
 *   -mqtt_countstats_topic  the message counts of -trace_counts, one object
 *                           per message of the scenario.
 *   -mqtt_codestats_topic   the response codes of the unexpected messages
 *                           since the last interval (-trace_error_codes).
 *
 * Remote control (-mqtt_ctrl)
 *
 *   SIPp subscribes to -mqtt_ctrl_topic and takes each message as the control
 *   socket takes a datagram: a key, or 'c' and a command ("cset rate 10").
 *   The messages arrive in libmosquitto's thread, which only queues them;
 *   the main loop takes them from the queue (mqtt_poll) and runs them.
 *
 * Errors and messages (mqtt_log.cpp), with -mqtt_stats
 *
 *   -trace_err      what the error log gets goes to -mqtt_error_topic instead,
 *                   -mqtt_error_rate messages a second at most; the others are
 *                   counted and reported with the next one.
 *   -trace_msg,     every SIP message sent and received goes to
 *   -trace_shortmsg -mqtt_message_topic too, whole or its first line.
 *
 * See docs/mqtt.rst for the options and the messages.
 */

#ifdef USE_MQTT

#include <string>
#include <vector>
#include <sys/time.h>

/* Connect to the broker and wait for it to accept us; a fatal error if it
 * does not within -mqtt_connect_timeout. Call once, from the main thread,
 * when the options are read. */
void mqtt_setup();

/* Wait for what is queued to be delivered, for -mqtt_shutdelay at most, and
 * close the connection. Safe to call when mqtt_setup() was not. */
void mqtt_shutdown();

/* Publish a payload to a topic with -mqtt_pub_qos. Returns false when it was
 * not handed to the broker connection; that is reported as a warning, at most
 * once every few seconds, unless quiet. Any thread. */
bool mqtt_publish(const char *topic, const std::string &payload, bool quiet = false);

/* True between a successful mqtt_setup() and mqtt_shutdown(). */
bool mqtt_active();

/* Run the control messages that arrived since the last call. Called by the
 * main loop on every pass; does nothing without -mqtt_ctrl. */
void mqtt_poll();

/* Publish the statistics, the message counts and the error codes of the main
 * scenario. Called by the statistics task every -fd interval and at exit. */
void mqtt_report_stats();

/* True when errors go to MQTT and not to the error log: -trace_err with
 * -mqtt_stats, while the connection is set up. */
bool mqtt_errors_enabled();

/* Publish an error or warning to -mqtt_error_topic, or count it when the
 * second's share is used up. Any thread. */
void mqtt_publish_error(const struct timeval *now, bool fatal, const std::string &text, int error);

/* Publish a SIP message that was sent ("send") or received ("recv") to
 * -mqtt_message_topic, if -trace_msg or -trace_shortmsg ask for it. */
void mqtt_trace_message(const struct timeval *now, const char *direction, const char *transport,
                        bool control, const char *buffer, size_t len);

/* ---- the pieces the messages are made of (no connection needed) ---- */

/* A string as the inside of a JSON string: quotes, backslashes and control
 * characters escaped, bytes that are not UTF-8 replaced by U+FFFD. */
std::string mqtt_json_escape(const std::string &in);

/* "2026-11-13 19:00:00.123456": local time, as every message carries it. */
std::string mqtt_format_time(const struct timeval *tv);

/* A line of a statistics file split at its delimiter. */
std::vector<std::string> mqtt_split(const std::string &line, const std::string &delimiter);

/* A header line and a data line of the statistics file as one JSON object,
 * {"<column>":"<value>",...}. Columns without a name are left out; a data
 * line shorter than the header gives empty strings. */
std::string mqtt_csv_to_json(const std::string &header, const std::string &row,
                             const std::string &delimiter);

/* An error as it is published. */
std::string mqtt_error_json(const struct timeval *now, bool fatal, const std::string &text,
                            int error, unsigned long total, unsigned long suppressed, int max_len);

/* A SIP message as it is published. */
std::string mqtt_message_json(const struct timeval *now, const std::string &call_id, const char *direction,
                              const char *transport, bool control, const std::string &message, size_t size);

/* What CStat::dumpData() wrote, a header line and a data line that each end
 * with a newline, taken apart. False if the text is not these two lines. */
bool mqtt_split_dump(const std::string &text, std::string &header, std::string &row);

#endif /* USE_MQTT */

#endif /* __SIPP_MQTT_HPP__ */
