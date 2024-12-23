# ZeroCopy-Rpc

This C++ header library provides ultra fast inter-process communication:

1. Communication is done using shared memory, named semaphores and atomic operations.
2. It uses boost because of cross-patform reasons.
3. The library uses shared memory and zero-copy concept. This indicates, that messages
can be be seen untouched for only certain time.

Server can base started with:
```cpp

SharedMemoryServer srv("my-channel"); 
srv.CreateTopic("my-topic");

```

Client can connect, subscribe or invoke request-response:
```cpp
void Callback(unsigned int eventType, unsigned char *ptr, unsigned long size);

void main() 
{
	SharedMemoryClient client("my-channel");

	// sub
	client.Subscribe("my-topic", Callback);

	// request: fire & forget
	int *payload = client.Allocate<int>(sizeof(int));
	*payload = 999;
	unsigned actionId = 123;
	client.Invoke(actionId, payload, sizeof(int));

	// request & response
	int *payload = client.Allocate<int>(sizeof(int));
	*payload = 999;
	unsigned actionId = 123;
	int *response = client.Invoke<int>(actionId, payload, sizeof(int));
}
```

The invoke mechanisms: fire and forget or fire and response, under the hood are essentially 
subscribing to internal session topic: session-in, and when response is required also session-out;

## The subscription process.

The client is responsible for ensuring it processes events promptly. Failure to do so may result in reading stale or overwritten data. Configure the minimum buffer read lag to provide a safety margin, but excessive lag increases the risk of data loss

Let's start with configuration of the server.

```cpp
void main() 
{
	SharedMemoryServer srv("my-channel"); 
	auto topic = srv.CreateTopic("my-topic");
	// ...
}
```

Under the hood, this creates a:
1) boost::lockfree:queue with the fixed capacity of 128, for incoming requests for subscription, placed in shared memory called my-channel.my-topic.shm
2) named semaphore called "my-channel.my-topic.subscription.sem");

This makes name of the channel act like a namespace. 

Now lets consider subscribing:

```cpp
void Callback(unsigned int eventType, unsigned char *ptr, unsigned long size);

void main() 
{
	SharedMemoryClient client("my-channel");

	// sub
	client.Subscribe("my-topic", Callback, 11);  // 11 is minimum difference of free spaces between cursors read and write positions.

	// ..
}
```
The client opens shared memory called "my-channel.my-topic.shm" and the named semaphore as well. 
It:
1. registeres a subscription slot for watch-dog to remove the entry if the process (pid) doest not exists; 
This will be a small entry in shared memory prefixed with pid:
```
pid.my-channel.my-topic.shm 
```
Under the hood, there shall be saved a structure called **SubscriptionCursor**, that hold current position in the subscription
Also a named semaphore is created.
```
pid.my-channel.my-topic.0.sem // for the first subscription for this process...
```

2. pushes a SubscribeTopic command on the queue and raises the semaphore. 

The server then:
1. Can read the command, because the semaphore had been rised. 
2. The command only carries the information about PID and id of the subscription, for instance 0.
3. The server, registeres a slot in Subscription instance, opens shared memory for the curor, and in atomic operation saves current event index in the SubscriptionCursor structure.
4. When a new event is to be published, the server will increment atomicly Cursor's current index and rise the associated semaphore.

The client then:
1. Whenever the semaphore 'pid.my-channel.my-topic.0.sem' can be waited, it reads SubscriptionCursor current index. 
This is to ensure, that the next operation can be executed, because the server could override the data. 
The structure should have a capability, that if the server is too close to last read position, the exception shall be thrown. (from Client's prespective)
2. Then the client reads the next EventSlot structure from cyclic buffer from shared memory. The EventSlot contains generic information about the event:
```cpp
structure EventSlot 
{
	unsigned int Type; // this is the event-type, values uint.max - 255...uint.max, are reseved for internal usage.
	unsigned long Offset; // this is the offset in shared-memory (from it's beginning) to the object that is described by Type
	unsigned long Size; // Size of data in shared memory.
}
```
3. Then the callback at the client-side can be called. Once it's done, the cursor read-position is advanded by 1.

So how to publish an event?

On the server:
```cpp

// arrange
auto topic = srv.CreateTopic("my-topic");

int *payload = srv.Allocate<int>(sizeof(int));
*payload = 999;
unsigned eventId = 123;
topic.Publish(eventId, payload, sizeof(int));

```

under the hood:
1. The server creates EventSlot, calculate EventSlot Offset, saves it in cyclic-buffer (shared with all the clients). The cyclic-buffer structure holds current write index (atomic), that can be accesed by all subscribes.
2. The server goes through Channel's TopicSubscribesTable and raises all semaphore once. 
3. When the server is about to override new data in cyclic buffer, a signal to the topic is executed. This is for debug.

Shared memory structures:
We have thus 3 structures placed in one shared-memory, for a single channel-topic:
1. queue for subsctiotion events.
2. cyclic-buffer with const size of an item for EventSlot. 
3. cyclic-buffer with variable lengh size of an item for real payload data.

On the server we also have internal structure (not in shm):
TopicSubscribesTable.

## Request: Fire & Forget

// TBD: but most likely one lockfree:queue, named semaphore and shm will be enough.

## Request & Reponse

// TBD: but most likely this will be constructed of ouf two SingleChannel.

## The watchdog service