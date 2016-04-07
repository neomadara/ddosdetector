#ifndef IP_HPP
#define IP_HPP

#include <iostream>
#include <vector>

#include <boost/program_options.hpp>
#include <boost/asio/ip/address.hpp>

#include "../action.hpp"
#include "../parser.hpp"
#include "baserule.hpp"

struct Ipv4Rule
{
    uint8_t ver_ihl;
    uint8_t tos;
    uint16_t length;
    uint16_t identification;
    uint16_t flag_offset;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    NumRange<uint32_t> ip_src;
    NumRange<uint32_t> ip_dst;
    explicit Ipv4Rule(uint8_t proto);
    void ip_header_parse(boost::program_options::variables_map& vm);
};

#endif // end IP_HPP