//--------------------------------------------------------------------------
// Copyright (C) 2019-2021 Cisco and/or its affiliates. All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------
// http2_settings_frame.cc author Deepak Ramadass <deramada@cisco.com>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "http2_settings_frame.h"

#include "service_inspectors/http_inspect/http_test_manager.h"

#include "http2_enum.h"
#include "http2_flow_data.h"

using namespace Http2Enums;

static const uint8_t SfSize = 6;

static uint16_t get_parameter_id(const uint8_t* data_buffer)
{
    return (data_buffer[0] << 8) + data_buffer[1];
}

static uint32_t get_parameter_value(const uint8_t* data_buffer)
{
    static const uint8_t frame_value_index = 2;
    return (data_buffer[frame_value_index]  << 24) +
        (data_buffer[frame_value_index + 1] << 16) +
        (data_buffer[frame_value_index + 2] << 8) +
        data_buffer[frame_value_index + 3];
}

Http2SettingsFrame::Http2SettingsFrame(const uint8_t* header_buffer, const uint32_t header_len,
    const uint8_t* data_buffer, const uint32_t data_len, Http2FlowData* ssn_data,
    HttpCommon::SourceId src_id, Http2Stream* stream_) : Http2Frame(header_buffer, header_len,
    data_buffer, data_len, ssn_data, src_id, stream_)
{
    if (!sanity_check())
    {
        session_data->events[source_id]->create_event(EVENT_SETTINGS_FRAME_ERROR);
        *session_data->infractions[source_id] += INF_INVALID_SETTINGS_FRAME;
        return;
    }

    if (ACK & get_flags())
        return;

    if (src_id == HttpCommon::SRC_SERVER && !ssn_data->was_server_settings_received())
        ssn_data->set_server_settings_received();

    parse_settings_frame();
}

void Http2SettingsFrame::parse_settings_frame()
{
    int32_t data_pos = 0;

    while (data_pos < data.length())
    {
        uint16_t parameter_id = get_parameter_id(data.start() + data_pos);
        uint32_t parameter_value = get_parameter_value(data.start() + data_pos);

        data_pos += SfSize;

        if (parameter_id < HEADER_TABLE_SIZE or parameter_id > MAX_HEADER_LIST_SIZE)
        {
            session_data->events[source_id]->create_event(EVENT_SETTINGS_FRAME_UNKN_PARAM);
            *session_data->infractions[source_id] += INF_SETTINGS_FRAME_UNKN_PARAM;
            continue;
        }

        if (handle_update(parameter_id, parameter_value))
            session_data->connection_settings[source_id].set_param(parameter_id, parameter_value);
    }
}

bool Http2SettingsFrame::sanity_check()
{
    const bool ack = ACK & get_flags();

    // FIXIT-E this next check should possibly be moved to valid_sequence()
    if (get_stream_id() != 0)
        bad_frame = true;
    else if (((data.length() % 6) != 0) or (ack and data.length() != 0))
        bad_frame = true;

    return !(bad_frame);
}

bool Http2SettingsFrame::handle_update(uint16_t id, uint32_t value)
{
    switch (id)
    {
        case HEADER_TABLE_SIZE:
            // Sending a table size parameter informs the receiver the maximum hpack dynamic
            // table size they may use.
            session_data->get_hpack_decoder((HttpCommon::SourceId) (1 - source_id))->
                get_decode_table()->settings_table_size_update(value);
            break;
        case ENABLE_PUSH:
            // Only values of 0 or 1 are allowed
            if (!(value == 0 or value == 1))
            {
                session_data->events[source_id]->create_event(EVENT_BAD_SETTINGS_VALUE);
                *session_data->infractions[source_id] += INF_BAD_SETTINGS_PUSH_VALUE;
                return false;
            }
            break;
        default:
            break;
    }
    return true;
}

#ifdef REG_TEST
void Http2SettingsFrame::print_frame(FILE* output)
{
    fprintf(output, "\nSettings frame:");

    if (bad_frame)
        fprintf(output, " Error in settings frame.");
    else if (ACK & get_flags())
        fprintf(output, " ACK");
    else
        fprintf(output, " Parameters in current frame - %d.", (data.length()/6)) ;

    fprintf(output, "\n");
    Http2Frame::print_frame(output);
}
#endif

uint32_t Http2ConnectionSettings::get_param(uint16_t id)
{
    assert(id >= HEADER_TABLE_SIZE);
    assert(id <= MAX_HEADER_LIST_SIZE);

    return parameters[id - 1];
}

void Http2ConnectionSettings::set_param(uint16_t id, uint32_t value)
{
    assert(id >= HEADER_TABLE_SIZE);
    assert(id <= MAX_HEADER_LIST_SIZE);

    parameters[id - 1] = value;
}
