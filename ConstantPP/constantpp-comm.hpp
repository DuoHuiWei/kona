#ifndef CONSTANTPP_CONSTANTPP_COMM_HPP_
#define CONSTANTPP_CONSTANTPP_COMM_HPP_

#include <stdexcept>
#include <vector>

#include "../Networking/Player.h"
#include "constantpp-types.hpp"

namespace ConstantPP
{

inline void pack_ring_vector(const std::vector<Ring>& values, octetStream& os)
{
    for (const auto& value : values)
        value.pack(os);
}

inline void unpack_ring_vector(
        octetStream& os,
        std::size_t expected_size,
        std::vector<Ring>& values)
{
    values.resize(expected_size);
    for (std::size_t i = 0; i < expected_size; ++i)
        values[i].unpack(os);

    if (!os.done())
        throw std::runtime_error(
                "ConstantPP: unexpected trailing bytes while unpacking ring vector");
}

/*
 * Symmetric one-stage exchange used by SecED.
 *
 * Kona's existing DCF comparator already follows the same
 * send-then-receive pattern on RealTwoPartyPlayer.
 */
inline void exchange_ring_vector(
        RealTwoPartyPlayer* player,
        const std::vector<Ring>& local,
        std::vector<Ring>& peer,
        ProtocolStats* stats = nullptr)
{
    if (!player)
        throw std::invalid_argument("ConstantPP: null RealTwoPartyPlayer");

    octetStream send_os;
    octetStream receive_os;
    pack_ring_vector(local, send_os);

    if (stats)
    {
        stats->payload_bytes_sent += send_os.get_length();
        ++stats->send_calls;
    }
    player->send(send_os);

    player->receive(receive_os);
    if (stats)
        ++stats->receive_calls;

    unpack_ring_vector(receive_os, local.size(), peer);

}

inline void send_ring_vector(
        RealTwoPartyPlayer* player,
        const std::vector<Ring>& values,
        ProtocolStats* stats = nullptr)
{
    if (!player)
        throw std::invalid_argument("ConstantPP: null RealTwoPartyPlayer");

    octetStream os;
    pack_ring_vector(values, os);
    if (stats)
    {
        stats->payload_bytes_sent += os.get_length();
        ++stats->send_calls;
    }
    player->send(os);
}

inline void receive_ring_vector(
        RealTwoPartyPlayer* player,
        std::size_t expected_size,
        std::vector<Ring>& values,
        ProtocolStats* stats = nullptr)
{
    if (!player)
        throw std::invalid_argument("ConstantPP: null RealTwoPartyPlayer");

    octetStream os;
    player->receive(os);
    if (stats)
        ++stats->receive_calls;

    unpack_ring_vector(os, expected_size, values);
}

inline void pack_columns(
        const std::vector<std::vector<Ring>>& columns,
        octetStream& os)
{
    for (const auto& column : columns)
        pack_ring_vector(column, os);
}

inline void unpack_columns(
        octetStream& os,
        std::size_t num_columns,
        std::size_t rows,
        std::vector<std::vector<Ring>>& columns)
{
    columns.assign(num_columns, std::vector<Ring>(rows));
    for (std::size_t c = 0; c < num_columns; ++c)
    {
        for (std::size_t i = 0; i < rows; ++i)
            columns[c][i].unpack(os);
    }

    if (!os.done())
        throw std::runtime_error(
                "ConstantPP: unexpected trailing bytes while unpacking columns");
}

inline void send_columns(
        RealTwoPartyPlayer* player,
        const std::vector<std::vector<Ring>>& columns,
        ProtocolStats* stats = nullptr)
{
    if (!player)
        throw std::invalid_argument("ConstantPP: null RealTwoPartyPlayer");

    octetStream os;
    pack_columns(columns, os);
    if (stats)
    {
        stats->payload_bytes_sent += os.get_length();
        ++stats->send_calls;
    }
    player->send(os);
}

inline void receive_columns(
        RealTwoPartyPlayer* player,
        std::size_t num_columns,
        std::size_t rows,
        std::vector<std::vector<Ring>>& columns,
        ProtocolStats* stats = nullptr)
{
    if (!player)
        throw std::invalid_argument("ConstantPP: null RealTwoPartyPlayer");

    octetStream os;
    player->receive(os);
    if (stats)
        ++stats->receive_calls;

    unpack_columns(os, num_columns, rows, columns);
}

} // namespace ConstantPP

#endif // CONSTANTPP_CONSTANTPP_COMM_HPP_
