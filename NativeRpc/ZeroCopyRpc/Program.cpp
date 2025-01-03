// main.cpp
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include "ShmReplicator.h"
#include <iostream>
#include <vector>

namespace po = boost::program_options;

// Helper to split comma-separated topics into vector
std::vector<std::string> parse_topics(const std::string& topics_str) {
    std::vector<std::string> topics;
    boost::split(topics, topics_str, boost::is_any_of(","));
    return topics;
}

int handle_publish(const po::variables_map& vm) {
    try {
        boost::asio::io_context io;

        auto channel = vm["channel"].as<std::string>();
        auto host = vm["host"].as<std::string>();
        auto port = vm["port"].as<uint16_t>();

        std::cout << "Starting publisher for channel: " << channel
            << " on " << host << ":" << port << std::endl;
        std::cout << "Waiting for subscription requests..." << std::endl;

        ShmReplicationSource source(io, channel, port);

        // Run the IO context in the main thread
        io.run();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error in publisher: " << e.what() << std::endl;
        return 1;
    }
}

int handle_subscribe(const po::variables_map& vm) {
    try {
        boost::asio::io_context io;

        auto channel = vm["channel"].as<std::string>();
        auto host = vm["host"].as<std::string>();
        auto port = vm["port"].as<uint16_t>();
        auto topics = parse_topics(vm["topics"].as<std::string>());

        std::cout << "Starting subscriber for channel: " << channel
            << " connecting to " << host << ":" << port << std::endl;

        auto server = std::make_shared<SharedMemoryServer>(channel);
        auto target = std::make_shared<ShmReplicationTarget>(
            io, server, host, port);

        // Subscribe to all requested topics
        for (const auto& topic : topics) {
            std::cout << "Subscribing to topic: " << topic << std::endl;
            target->ReplicateTopic(topic);
        }

        // Run the IO context in the main thread
        io.run();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error in subscriber: " << e.what() << std::endl;
        return 1;
    }
}

int main(int argc, char* argv[]) {
    try {
        // Command line options - must be first to get the command
        po::options_description cmd_opts("Command options");
        cmd_opts.add_options()
            ("command", po::value<std::string>()->required(), "Command (publish or subscribe)");

        po::positional_options_description pos;
        pos.add("command", 1);

        // First, parse just the command
        po::variables_map cmd_vm;
        po::store(po::command_line_parser(argc, argv)
            .options(cmd_opts)
            .positional(pos)
            .allow_unregistered()
            .run(), cmd_vm);

        // Get the command without calling notify() yet
        std::string command;
        if (cmd_vm.count("command")) {
            command = cmd_vm["command"].as<std::string>();
        }

        // Common options required for both commands
        po::options_description common_opts("Common options");
        common_opts.add_options()
            ("help", "Print help message")
            ("channel", po::value<std::string>()->required(), "Channel name")
            ("host", po::value<std::string>()->required(), "Host address")
            ("port", po::value<uint16_t>()->required(), "Port number");

        // Subscribe-specific options
        po::options_description subscribe_opts("Subscribe options");
        subscribe_opts.add_options()
            ("topics", po::value<std::string>()->required(),
                "Comma-separated list of topics to subscribe to");

        // Create the appropriate options description based on the command
        po::options_description opts("Allowed options");
        opts.add(cmd_opts).add(common_opts);
        if (command == "subscribe") {
            opts.add(subscribe_opts);
        }

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv)
            .options(opts)
            .positional(pos)
            .run(), vm);

        // Handle help first
        if (vm.count("help")) {
            std::cout << "Usage: " << argv[0] << " <command> [options]\n"
                << "Commands:\n"
                << "  publish   - Start a publisher\n"
                << "             Required: --channel, --host, --port\n"
                << "  subscribe - Start a subscriber\n"
                << "             Required: --channel, --host, --port, --topics\n\n"
                << opts << std::endl;
            return 0;
        }

        // Now validate the options
        po::notify(vm);

        if (command == "publish") {
            return handle_publish(vm);
        }
        else if (command == "subscribe") {
            return handle_subscribe(vm);
        }
        else {
            std::cerr << "Unknown command: " << command << "\n"
                << "Use --help for usage information" << std::endl;
            return 1;
        }
    }
    catch (const po::error& e) {
        std::cerr << "Error parsing command line: " << e.what() << "\n"
            << "Use --help for usage information" << std::endl;
        return 1;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}