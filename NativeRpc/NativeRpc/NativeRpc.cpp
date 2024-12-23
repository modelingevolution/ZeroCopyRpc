#include <boost/interprocess/ipc/message_queue.hpp>
#include <iostream>
#include <vector>
#include "NativeRpc.h"

#if defined(_WIN32) || defined(_WIN64)
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
typedef CyclicBuffer<sizeof(int) * 2, 2> Buffer;

random_generator Random::Shared;

int ParseInt(int index, int argc, char* argv[]);
void AppendInt(Buffer& b, int value, int type = 999);

int message_queue_srv()
{
    try {
        //Erase previous message queue
        message_queue::remove("message_queue");

        //Create a message_queue.
        message_queue mq
        (create_only               //only create
            , "message_queue"           //name
            , 100                       //max message number
            , sizeof(int)               //max message size
        );

        //Send 100 numbers
        for (int i = 0; i < 100; ++i) {
            mq.send(&i, sizeof(i), 0);
        }
    }
    catch (interprocess_exception& ex) {
        std::cout << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
int message_queue_client()
{
    try {
        //Open a message queue.
        message_queue mq
        (open_only        //only open
            , "message_queue"  //name
        );

        unsigned int priority;
        message_queue::size_type recvd_size;

        //Receive 100 numbers
        for (int i = 0; i < 100; ++i) {
            int number;
            mq.receive(&number, sizeof(number), recvd_size, priority);
            if (number != i || recvd_size != sizeof(number))
                return 1;
        }
    }
    catch (interprocess_exception& ex) {
        message_queue::remove("message_queue");
        std::cout << ex.what() << std::endl;
        return 1;
    }
    message_queue::remove("message_queue");
    return 0;
}

void test_server();
void test_client();
void test_cyclic_buffer2();

int main(int argc, char* argv[])
{
    while (!isDebuggerAttached()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "Waiting for attach..." << endl;
    }
    try
    {
        int arg1 = ParseInt(1, argc, argv); // Call ParseInt with index 1
        switch(arg1)
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
    catch(const boost::interprocess::interprocess_exception &e)
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
    auto buffer = new CyclicBuffer<1024 * 1024 * 8, 256>();
    long size = sizeof(CyclicBuffer<1024 * 1024 * 8, 256>);
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
    while (true) {
        string line;
        cin >> line;
        {
            auto prep = topic->Prepare(sizeof(long), 2);
            long* ptr = (long*)prep.Span().Start;
            *ptr = 123;
            prep.Span().Commit(sizeof(long));
        }
    }
    
}
void test_client()
{
    message_queue::remove(("hot." + std::to_string(GetCurrentProcessId())).c_str());
    SharedMemoryClient client("hot");
    client.Connect();
    auto cursor = client.Subscribe("chick");
    cout << "Client subscribed" << endl;
    cout << "Waiting for commands..." << endl;
    string line;
    cin >> line;
    while (true)
    {
        auto accessor = cursor->Read();
        long* ptr = (long*) accessor.Get();
        std::cout << "Received message, type: " << accessor.Item->Type << " size: " << accessor.Item->Size << " value: " << *ptr << std::endl;;
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
            std::cerr << "Invalid argument: Not a valid integer." << std::endl;
            throw;
        }
        catch (const std::out_of_range& e)
        {
            std::cerr << "Invalid argument: Integer out of range." << std::endl;
            throw;
        }
    }
    else
    {
        std::cerr << "Argument index out of range." << std::endl;
        throw std::out_of_range("Argument index out of range.");
    }
}
void test_cyclic_buffer()
{
    CyclicBuffer<sizeof(int) * 2, 2> buffer;
    auto cursor = buffer.ReadNext();
    cout << "Can read: " << cursor.TryRead() << endl;

    AppendInt(buffer, 123);

    cout << "Can read, expected 1 " << cursor.TryRead() << endl;
    auto data = cursor.Data();
    cout << "Read, size expected 4: " << data.Item->Size << " type, expected 999: " << data.Item->Type << " value, expected: 123 " << *data.As<int>() << endl;
    cout << "Can read, expected 0:" << cursor.TryRead() << endl;

    AppendInt(buffer, 333, 666);

    cout << "Remaining: " << cursor.Remaining() << " can read, expected 1 " << cursor.TryRead() << endl;
    auto data2 = cursor.Data();
    cout << "Read, size expected 4: " << data2.Item->Size << " type, expected 666: " << data2.Item->Type << " value, expected: 333" << *data2.As<int>() << endl;
    cout << "Remaining: " << cursor.Remaining() << " can read, expected 0:" << cursor.TryRead() << endl;


    AppendInt(buffer, 444, 555);

    cout << "Remaining: " << cursor.Remaining() << " can read, expected 1 " << cursor.TryRead() << endl;
    auto data3 = cursor.Data();
    cout << "Read, size expected 4: " << data3.Item->Size << " type, expected 555: " << data3.Item->Type << " value, expected: 444" << *data3.As<int>() << endl;
    cout << "Remaining: " << cursor.Remaining() << " can read, expected 0:" << cursor.TryRead() << endl;

}
void AppendInt(Buffer &b, int value, int type)
{
    auto scope = b.WriteScope(sizeof(int), type);
    auto data = (int*)scope.Span.Start;
    *data = value;
    scope.Span.Commit(sizeof(int));
}
