#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include "ShmReplicator.h"
#include <iostream>
#include <vector>
#include <conio.h>
#include "PeriodicTimer.h"
#include "TestFrame.h"
#include <iostream>
#include <csignal>
namespace po = boost::program_options;

// Helper to split comma-separated topics into vector
std::vector<std::string> parse_topics(const std::string& topics_str) {
    std::vector<std::string> topics;
    boost::split(topics, topics_str, boost::is_any_of(","));
    return topics;
}
void init_logging() {
   
    // Add logging to the console
    boost::log::add_console_log(
        std::cout,
        boost::log::keywords::format = "[%TimeStamp%] [%Severity%]: %Message%"
    );
    // Add common attributes like timestamps
    boost::log::add_common_attributes();
}
boost::asio::io_context* global_io_context = nullptr;
// Signal handler for Ctrl+C
void signalHandler(int signal) {
    if (signal == SIGINT) {
        std::cout << "Ctrl+C pressed. Stopping io_context...\n";
        if (global_io_context) {
            global_io_context->stop(); // Stop the io_context
        }
    }
}
int handle_replication_publish(const po::variables_map& vm) {
    try {
        boost::asio::io_context io;
        global_io_context = &io;
        std::signal(SIGINT, signalHandler);
        auto channel = vm["channel"].as<std::string>();
        auto host = vm["host"].as<std::string>();
        auto port = vm["port"].as<uint16_t>();

        BOOST_LOG_TRIVIAL(info) << "Starting publisher for channel: " << channel
            << " on " << host << ":" << port ;
        BOOST_LOG_TRIVIAL(info) << "Waiting for subscription requests..." ;

        ShmReplicationSource source(io, channel, port);
        executor_work_guard<io_context::executor_type> work_guard(io.get_executor());
        io.run();
        return 0;
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Error in publisher: " << e.what() ;
        return 1;
    }
}

int handle_replication_subscribe(const po::variables_map& vm) {
    try {
        boost::asio::io_context io;
        global_io_context = &io;
        std::signal(SIGINT, signalHandler);

        auto channel = vm["channel"].as<std::string>();
        auto host = vm["host"].as<std::string>();
        auto port = vm["port"].as<uint16_t>();
        auto topics = parse_topics(vm["topics"].as<std::string>());

        BOOST_LOG_TRIVIAL(info) << "Starting subscriber for channel: " << channel
            << " connecting to " << host << ":" << port;

        auto server = std::make_shared<SharedMemoryServer>(channel);
        auto target = std::make_shared<ShmReplicationTarget>(
            io, server, host, port);

        for (const auto& topic : topics) {
            BOOST_LOG_TRIVIAL(info) << "Subscribing to topic: " << topic;
            target->ReplicateTopic(topic);
        }
        executor_work_guard<io_context::executor_type> work_guard(io.get_executor());
        io.run();
        return 0;
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Error in subscriber: " << e.what();
        return 1;
    }
}

int handle_clear(const po::variables_map& vm) {
    try {
        auto channel = vm["channel"].as<std::string>();

        BOOST_LOG_TRIVIAL(info) << "Clearing shared memory channel: " << channel;

        // Add implementation for clearing the shared memory channel
        // This would depend on your SharedMemoryServer implementation
        if (SharedMemoryServer::RemoveChannel(channel))
            BOOST_LOG_TRIVIAL(info) << "Channel: " << channel << " was removed successfully.";
        else
            BOOST_LOG_TRIVIAL(info) << "Channel: " << channel << " does not exists or was not removed.";

        if(vm.contains("topic"))
        {
            auto topic = vm["topic"].as<std::string>();
            if (TopicService::TryRemove(channel, topic))
                BOOST_LOG_TRIVIAL(info) << "Topic: " << topic << " data removed successfully.";
            else 
                BOOST_LOG_TRIVIAL(info) << "Topic " << topic << " does not exists or was not removed.";
        }

        return 0;
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Error clearing channel: " << e.what() ;
        return 1;
    }
}
std::string toLower(const std::string& str) {
    std::string result = str;
    std::ranges::transform(result, result.begin(),
                           [](unsigned char c) { return std::tolower(c); });
    return result;
}
int handle_test_write(const po::variables_map& vm) {
    try {
        auto count = vm["count"].as<uint32_t>();
        auto frequency = vm["frequency"].as<uint32_t>();
        auto messageSize = vm["message-size"].as<uint32_t>();
        auto channelName = vm["channel"].as<std::string>();
        auto topicName = vm["topic"].as<std::string>();
        auto interactiveStr = vm["interactive"].as<std::string>();
        auto interactive = toLower(interactiveStr) == "true" || interactiveStr == "";
        
        BOOST_LOG_TRIVIAL(info) << "Starting write test with parameters:";
        BOOST_LOG_TRIVIAL(info) << "  Channel: " << channelName;
        BOOST_LOG_TRIVIAL(info) << "  Topic: " << topicName;
        BOOST_LOG_TRIVIAL(info) << "  Count: " << count;
        BOOST_LOG_TRIVIAL(info) << "  Frequency: " << frequency << " messages/second";
        BOOST_LOG_TRIVIAL(info) << "  Message payload size: " << messageSize << " bytes, actual: " << messageSize + sizeof(TestFrame) << " bytes";
        BOOST_LOG_TRIVIAL(info) << "  Interactive: " << (interactive ? "true" : "false");
        
        // Create server and topic
        auto server = std::make_shared<SharedMemoryServer>(channelName);
        auto topic = server->CreateTopic(topicName);

        if (!topic) {
            BOOST_LOG_TRIVIAL(error) << "Failed to create topic";
            return 1;
        }
        PeriodicTimer pt = PeriodicTimer::CreateFromFrequency(frequency);

        if (interactive)
        {
            std::cout << "Press space or enter to write a message. 'q' or Ctrl+C for exit." << std::endl;
        }
        
        // Write messages
        for (uint32_t i = 0; i < count; i++) {
            if(interactive)
            {
	            // we need to wait for enter or space key stroke.
                char key;
                std::cin >> std::noskipws >> key; // Wait for keypress
                if (key == 'q') {
                    BOOST_LOG_TRIVIAL(info) << "Exiting interactive mode.";
                    return 0;
                }
                if (key != ' ' && key != '\n') {
                    --i; // Ignore invalid keypress and retry
                    continue;
                }
            }
            auto msgSize = TestFrame::SizeOf(messageSize);
            {
                auto scope = topic->Prepare(msgSize, 69); // Type 69 for test messages
                auto& span = scope.Span();

                TestFrame frame(span.Start, messageSize);

                span.Commit(msgSize);

                BOOST_LOG_TRIVIAL(info) << "Published message " << (i + 1) << "/" << count << " " << frame;
            }
            if(!interactive)
				pt.WaitForNext();
        }
        
        BOOST_LOG_TRIVIAL(info) << "Write test completed successfully";
        if (interactive)
        {
            std::cout << "Press a key to exit." << std::endl;
        }
        return 0;
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Error in write test: " << e.what();
        return 1;
    }
}
int handle_test_read(const po::variables_map& vm) {
    try {
        auto channelName = vm["channel"].as<std::string>();
        auto topicName = vm["topic"].as<std::string>();
        auto interactiveStr = vm["interactive"].as<std::string>();
        auto interactive = toLower(interactiveStr) == "true" || interactiveStr == "";

        BOOST_LOG_TRIVIAL(info) << "Starting read test";
        BOOST_LOG_TRIVIAL(info) << "  Channel: " << channelName;
        BOOST_LOG_TRIVIAL(info) << "  Topic: " << topicName;
        BOOST_LOG_TRIVIAL(info) << "  Interactive: " << (interactive ? "true" : "false");

        auto client = std::make_shared<SharedMemoryClient>(channelName);

        client->Connect();
        
        auto cursor = client->Subscribe(topicName);
        CyclicBuffer::Accessor accessor;
        bool read;
        while((read=cursor->TryReadFor(accessor, chrono::seconds(10))) || interactive)
        {
            if (interactive && !read)
            {
                // TODO:
                // We need to check if someone hasn't press q.
#ifdef WIN32
                if (_kbhit()) { // Check if a key has been pressed
                    char key = _getch(); // Get the pressed key
                    if (key == 'q') {
                        BOOST_LOG_TRIVIAL(info) << "Exiting interactive mode.";
                        return 0;
                    }
                }
#endif
                continue;
            }
			if(accessor.Type() == 69)
			{
				auto ptr = accessor.Get();
				// lets check the message integrity.
                auto* header= (TestFrameHeader*) ptr;
                auto* data = ptr + sizeof(TestFrameHeader);
                TestFrame frame(header, data);
                
                auto age = frame.Age();
                if(accessor.Item->Size != TestFrame::SizeOf(frame.Header->Size))
                {
                    BOOST_LOG_TRIVIAL(error) << "Test frame message size is incorrect.";
                    return 1;
                }
                auto hash = frame.ComputeHash();
                if(hash != frame.Header->Hash)
                {
                    BOOST_LOG_TRIVIAL(error) << "Test frame integrity is corrupted.";
                }
                BOOST_LOG_TRIVIAL(info) << frame << "[" << frame.Age() << "]";
			}
        }
        BOOST_LOG_TRIVIAL(info) << "No more messages has arrived in last 10 seconds. Exiting.";

        return 0;
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Error in read test: " << e.what();
        return 1;
    }
}

int main(int argc, char* argv[]) {
    try {
        init_logging();
        // Main command options
        po::options_description main_opts("Main options");
        main_opts.add_options()
            ("help", "Print help message")
            ("command", po::value<std::string>(), "Command (replication, test, clear)");

        // Replication subcommand options
        po::options_description repl_opts("Replication options");
        repl_opts.add_options()
            ("sub-command", po::value<std::string>(), "Subcommand (publish or subscribe)");

        // Test subcommand options
        po::options_description test_opts("Test options");
        test_opts.add_options()
            ("test-command", po::value<std::string>(), "Subcommand (write or read)");
            
        // Common options for replication commands
        po::options_description common_opts("Common options");
        common_opts.add_options()
            ("channel", po::value<std::string>()->required(), "Channel name")
            ("host", po::value<std::string>()->required(), "Host address")
            ("port", po::value<uint16_t>()->required(), "Port number");

        // Subscribe-specific options
        po::options_description subscribe_opts("Subscribe options");
        subscribe_opts.add_options()
            ("topics", po::value<std::string>()->required(),
                "Comma-separated list of topics to subscribe to");

        // Clear command options
        po::options_description clear_opts("Clear options");
        clear_opts.add_options()
            ("channel", po::value<std::string>()->required(), "Channel name to clear")
            ("topic", po::value<std::string>(), "Topic to be removed");

        // Positional options
        po::positional_options_description pos;
        pos.add("command", 1)
            .add("sub-command", 1);

        // Parse command and subcommand first
        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv)
            .options(main_opts.add(repl_opts).add(test_opts))
            .positional(pos)
            .allow_unregistered()
            .run(), vm);

        if (vm.count("help") || argc == 1) {
            std::cout << "Usage: zq <command> [options]\n\n"
                << "Commands:\n"
                << "  replication|-r <subcommand> [options]\n"
                << "    Subcommands:\n"
                << "      publish   - Start a publisher\n"
                << "                  Required: --channel, --host, --port\n"
                << "      subscribe - Start a subscriber\n"
                << "                  Required: --channel, --host, --port, --topics\n"
                << "  test <subcommand> [options]\n"
                << "    Subcommands:\n"
                << "      write     - Run write test\n"
                << "                  Required: --channel, --topic\n"
                << "                  Options: --count=N, --frequency=N, --message-size=N, --interactive=[true|false]\n"
                << "      read      - Run read test\n"
                << "                  Required: --channel, --topic\n"
                << "  clear         - Clear a shared memory channel\n"
                << "                  Required: --channel\n"
        	    << "                  Options: --topic, --interactive=[true|false]\n\n"
                << main_opts << "\n"
                << common_opts << "\n"
                << subscribe_opts << "\n"
                << test_opts << std::endl;
            return 0;
        }

        // Process commands
        if (vm.count("command")) {
            std::string command = vm["command"].as<std::string>();

            if (command == "replication" || command == "r") {
                if (vm.count("sub-command")) {
                    std::string repl_command = vm["sub-command"].as<std::string>();

                    if (repl_command == "publish") {
                        // Use common_opts for "publish"
                        po::options_description publish_opts;
                        publish_opts.add_options()
                            ("channel", po::value<std::string>()->required(), "Channel name")
                            ("host", po::value<std::string>()->required(), "Host address")
                            ("port", po::value<uint16_t>()->required(), "Port number");

                        po::store(po::command_line_parser(argc, argv)
                            .options(publish_opts)
                            .allow_unregistered()
                            .run(), vm);
                        po::notify(vm);

                        return handle_replication_publish(vm);
                    }
                    else if (repl_command == "subscribe") {
                        // Use common_opts + subscribe_opts for "subscribe"
                        po::options_description subscribe_opts;
                        subscribe_opts.add_options()
                            ("channel", po::value<std::string>()->required(), "Channel name")
                            ("host", po::value<std::string>()->required(), "Host address")
                            ("port", po::value<uint16_t>()->required(), "Port number")
                            ("topics", po::value<std::string>()->required(),
                                "Comma-separated list of topics to subscribe to");

                        po::store(po::command_line_parser(argc, argv)
                            .options(subscribe_opts)
                            .allow_unregistered()
                            .run(), vm);
                        po::notify(vm);

                        return handle_replication_subscribe(vm);
                    }
                }
            }
            else if (command == "test") {
                if (vm.count("sub-command") == 0) {
                    BOOST_LOG_TRIVIAL(error) << "Error: test command requires a subcommand (write or read)";
                    return 1;
                }
                std::string subcommand = vm["sub-command"].as<std::string>();
               

                if (subcommand == "write") {
                    po::options_description test_ops;
                    test_ops.add_options()
                        ("channel", po::value<std::string>()->required(), "Channel name")
                        ("topic", po::value<std::string>()->required(), "Topic name")
                        ("count", po::value<uint32_t>()->default_value(10u), "Number of messages to write")
                        ("interactive", po::value<std::string>()->default_value("false"), "Interactive mode")
                        ("frequency", po::value<uint32_t>()->default_value(1u), "Messages per second [Hz]")
                        ("message-size", po::value<uint32_t>()->default_value(8u), "Size of each message in bytes");
                    po::store(po::command_line_parser(argc, argv)
                        .options(test_ops)
                        .allow_unregistered()
                        .run(), vm);
                    po::notify(vm);  // This will throw if required options are missing

                    return handle_test_write(vm);
                }
                else if (subcommand == "read") {

                    po::options_description test_ops;
                    test_ops.add_options()
                        ("channel", po::value<std::string>()->required(), "Channel name")
                        ("interactive", po::value<std::string>()->default_value("false"), "Interactive mode")
                        ("topic", po::value<std::string>()->required(), "Topic name");

                    po::store(po::command_line_parser(argc, argv)
                        .options(test_ops)
                        .allow_unregistered()
                        .run(), vm);
                    po::notify(vm);  // This will throw if required options are missing

                    return handle_test_read(vm);
                }
                else {
                    BOOST_LOG_TRIVIAL(error) << "Unknown subcommand: " << subcommand;
                    return 1;
                }
            }
            else if (command == "clear") {
                po::store(po::command_line_parser(argc, argv)
                    .options(clear_opts)  // Include main_opts
                    .allow_unregistered()
                    .run(), vm);
                po::notify(vm);

                return handle_clear(vm);
            }
            else {
                BOOST_LOG_TRIVIAL(error) << "Unknown command: " << command;
                return 1;
            }
        }

        return 0;
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