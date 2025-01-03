#include <boost/interprocess/ipc/message_queue.hpp>
#include <iostream>
#include <vector>
#include "SharedMemoryServer.h"
#include "SharedMemoryClient.h"

#if defined(_WIN32) || defined(_WIN64)
#include <WinSock2.h> 
#include <windows.h>
bool isDebuggerAttached() {
    return IsDebuggerPresent();
}
#else
#include <sys/ptrace.h>
bool isDebuggerAttached() {
    return ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1;
}
#endif


using namespace boost::interprocess;
using namespace std;



int ParseInt(int index, int argc, char* argv[]);


void test_server();
void test_client();
void test_cyclic_buffer2();

int main(int argc, char* argv[])
{
    /*while (!isDebuggerAttached()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "Waiting for attach..." << endl;
    }*/
    try
    {
        int arg1 = ParseInt(1, argc, argv); // Call ParseInt with index 1
        switch (arg1)
        {
        case 1:
            test_server();
            //test_cyclic_buffer2();
            break;
        case 2:
            test_client();
            break;
        case 3:

            break;
        }

    }
    catch (const boost::interprocess::interprocess_exception& e)
    {
        std::cerr << "Interprocess exception: " << e.what() << " code: " << e.get_error_code() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Standard exception: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Failed to parse the argument." << std::endl;
    }
    return 0;
}
void test_cyclic_buffer2()
{
    auto buffer = new CyclicBuffer(256,8*1024*1024);
    long size = sizeof(CyclicBuffer);
    unsigned char* ptr = new unsigned char[size];
    memset(ptr, 0, size);

    int result = memcmp(buffer, ptr, size);
    std::cout << "Is memory same: " << (result == 0 ? "true" : "false") << std::endl;
}
void test_server()
{
    shared_memory_object::remove("hot.chick.buffer");

    message_queue::remove("hot");
    SharedMemoryServer srv("hot");
    auto topic = srv.CreateTopic("chick");
    cout << "Server ready.";
    cout << "Waiting for commands..." << endl;

    long long i = 0;
    cout << "Let's run like crazy....";
    while (true) {

        {
            string line;
            cin >> line;
            //std::this_thread::sleep_for(std::chrono::milliseconds(23));
            if (line == "q") break;
            {
                auto prep = topic->Prepare(sizeof(long long) * 2, 2);
                long long* ptr = (long long*)prep.Span().Start;
                auto n = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()
                ).count();
                ptr[0] = n;
                ptr[1] = ++i;
                prep.Span().Commit(sizeof(long long) * 2);
            }
        }

    }

}
void test_client()
{
    message_queue::remove(("hot." + std::to_string(getCurrentProcessId())).c_str());
    SharedMemoryClient client("hot");
    client.Connect();
    auto cursor = client.Subscribe("chick");
    cout << "Client subscribed" << endl;
    cout << "Waiting for commands..." << endl;

    while (true)
    {
        string line;
        cin >> line;
        if (line == "d")
        {
            delete cursor.release();
        }
        auto accessor = cursor->Read();
        long long* ptr = (long long*)accessor.Get();
        auto n = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
        auto d = n - ptr[0];
        auto i = ptr[1];
        std::cout << "Received message, type: " << accessor.Item->Type << " size: " << accessor.Item->Size << " value: " << i << " delay: " << d << std::endl;;
    }

}

int ParseInt(int index, int argc, char* argv[])
{
    if (index < argc) // Check if the index is within the range of arguments
    {
        try
        {
            return std::stoi(argv[index]); // Parse the argument at the specified index to an integer
        }
        catch (const std::invalid_argument& e)
        {
            std::cerr << "Invalid argument: Not a valid integer." << e.what() << std::endl;
            throw;
        }
        catch (const std::out_of_range& e)
        {
            std::cerr << "Invalid argument: Integer out of range." << e.what() << std::endl;
            throw;
        }
    }
    else
    {
        std::cerr << "Argument index out of range." << std::endl;
        throw std::out_of_range("Argument index out of range.");
    }
}


