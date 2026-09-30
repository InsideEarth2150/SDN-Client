#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <chrono>

#define TOML_HEADER_ONLY 1
#include "toml.hpp"

struct PortRange {
    uint16_t min;
    uint16_t max;
};

struct ClientConfig {
    std::string remote_proxy_host;
    uint16_t remote_proxy_port;
    int connect_timeout_ms;
    uint16_t game_server_port;

    std::vector<uint16_t> single_tcp;
    std::vector<uint16_t> single_udp;
    std::vector<PortRange> tcp_ranges;
    std::vector<PortRange> udp_ranges;
    
    bool stun_enabled;
    std::vector<std::string> stun_servers;
};

std::vector<uint16_t> parse_single_ports(toml::array* arr, const std::string& name) {
    std::vector<uint16_t> ports;
    if (!arr) return ports;
    for (auto& elem : *arr) {
        if (auto val = elem.value<int64_t>()) {
            ports.push_back(static_cast<uint16_t>(*val));
        } else {
            throw std::runtime_error("Invalid value in port array: " + name);
        }
    }
    return ports;
}

std::vector<PortRange> parse_port_ranges(toml::array* arr, const std::string& name) {
    std::vector<PortRange> ranges;
    if (!arr) return ranges;
    
    for (auto& elem : *arr) {
        if (auto range_arr = elem.as_array()) {
            if (range_arr->size() >= 2) {
                PortRange pr;
                pr.min = static_cast<uint16_t>(range_arr->get(0)->value<int64_t>().value());
                pr.max = static_cast<uint16_t>(range_arr->get(1)->value<int64_t>().value());
                ranges.push_back(pr);
            } else {
                throw std::runtime_error("Range inside " + name + " must contain [min, max]");
            }
        } else {
            throw std::runtime_error("Expected array of arrays for " + name);
        }
    }
    return ranges;
}

ClientConfig load_client_config(const std::string& config_path) {
    auto tbl = toml::parse_file(config_path);
    ClientConfig config;

    // Load Upstream Proxy Target
    auto host_val = tbl["client_proxy"]["remote_proxy_host"].value<std::string>();
    if (!host_val) throw std::runtime_error("Missing [client_proxy].remote_proxy_host in TOML");
    config.remote_proxy_host = *host_val;

    auto port_val = tbl["client_proxy"]["remote_proxy_port"].value<int64_t>();
    if (!port_val) throw std::runtime_error("Missing [client_proxy].remote_proxy_port in TOML");
    config.remote_proxy_port = static_cast<uint16_t>(*port_val);

    config.connect_timeout_ms = static_cast<int>(tbl["client_proxy"]["connect_timeout_ms"].value<int64_t>().value_or(5000));

    // Load Game Server Port
    auto gs_port = tbl["client_proxy"]["game_server_port"].value<int64_t>();
    if (!gs_port) throw std::runtime_error("Missing [client_proxy].game_server_port in TOML");
    config.game_server_port = static_cast<uint16_t>(*gs_port);

    // Load Port Arrays and Ranges (Multiple TCP/UDP support)
    config.single_tcp = parse_single_ports(tbl["client_proxy"]["ports"]["single_tcp"].as_array(), "single_tcp");
    config.single_udp = parse_single_ports(tbl["client_proxy"]["ports"]["single_udp"].as_array(), "single_udp");
    config.tcp_ranges = parse_port_ranges(tbl["client_proxy"]["ports"]["tcp_ranges"].as_array(), "tcp_ranges");
    config.udp_ranges = parse_port_ranges(tbl["client_proxy"]["ports"]["udp_ranges"].as_array(), "udp_ranges");

    // Load STUN Configuration
    config.stun_enabled = tbl["p2p"]["stun"]["enabled"].value<bool>().value_or(true);
    if (auto stun_arr = tbl["p2p"]["stun"]["servers"].as_array()) {
        for (auto& elem : *stun_arr) {
            if (auto srv = elem.value<std::string>()) {
                config.stun_servers.push_back(*srv);
            }
        }
    }

    std::cout << "[Config] Successfully loaded client configuration from " << config_path << std::endl;
    return config;
}

int main(int argc, char* argv[]) {
    std::string config_file = "app/client_config.toml";
    if (argc > 1) {
        config_file = argv[1];
    }

    try {
        ClientConfig config = load_client_config(config_file);

        std::cout << "========================================" << std::endl;
        std::cout << " Starting GNS Client Proxy (C++)        " << std::endl;
        std::cout << " Upstream Target : " << config.remote_proxy_host << ":" << config.remote_proxy_port << std::endl;
        std::cout << " Game Server Port: " << config.game_server_port << std::endl;
        
        std::cout << " Single TCP      : ";
        for (auto p : config.single_tcp) std::cout << p << " ";
        std::cout << std::endl;

        std::cout << " Single UDP      : ";
        for (auto p : config.single_udp) std::cout << p << " ";
        std::cout << std::endl;

        std::cout << " TCP Ranges      : ";
        for (auto r : config.tcp_ranges) std::cout << "[" << r.min << "-" << r.max << "] ";
        std::cout << std::endl;

        std::cout << " UDP Ranges      : ";
        for (auto r : config.udp_ranges) std::cout << "[" << r.min << "-" << r.max << "] ";
        std::cout << std::endl;

        std::cout << " STUN Servers    : " << config.stun_servers.size() << " configured" << std::endl;
        std::cout << "========================================" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[Configuration Fatal Error] " << e.what() << std::endl;
        return 1;
    }

    // Keep the client proxy active
    std::cout << "[Info] Client proxy active. Press Ctrl+C to exit." << std::endl;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    return 0;
}