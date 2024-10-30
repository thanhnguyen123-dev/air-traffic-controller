# Report

<!-- Remember to check the output of the pdf job on gitlab to make sure everything renders correctly! -->

## Overview
- For this assignment, I have implemented an Air Traffic Control (ATC) network with concurrent processing for both the airport nodes and the controller. Both the airport nodes and the controller are implemented using a thread pool to simulate multithreading behaviour, allowing them to handle multiple requests concurrently to make the program more time-efficient. 

- The implemented extensions are:
  1. Multithreading controller node
  2. Multithreading airport nodes with fined-grained locking scheme.

- When a client sends a request (`SCHEDULE`, `PLANE_STATUS`, or `TIME_STATUS`):
  1. The controller accepts the connection and assign to a worker thread.
  2. The tgread forwards the request to the airport nodes thread pool if the request is valid.
  3. A worker thread from the airport nodes handles and parses the request, calling `process_request()`, `process_schedule()`, `process_plane_status()`, or `process_time_status()` based on the request provided.
  4. After processing the request, the airport node sends a response back to the controller.
  5. The controller receives the response, returning it to the corresponding client.

## Request Format
- I choose to forward requests from the controller to the airport nodes in their original format, which includes the airport ID. The reasons for using this approach are desribed with the following:
  1. **Simplicity**
    - No string manipulation required at the controller level, increases code readbility and shift the responsibility for request processsing to airport nodes.
    - Reduces bugs from request formatting.
  2. **Debugging**
    - Requests remain consistent throughout the network system, especially exchanging back-and-forth between nodes.
    - Log can potentially include the airport ID, which aid the debugging and tracking processs.
  3. **Trade-offs**
    - Minor processing overhead for including unused airport ID, potentially increasing network bandwith. 

## Extensions

### Overview of thread pool implementation (both controller and airport nodes)
- Both the controller node and airport nodes concurrency behaviours are achieved by using a thread pool. A data structure called `shared_queue_t` is designed as a wrapper of an array storing file descriptors as a shared resource for storing network connections, including other struct members such as: `pthread_mutex_t lock`, `pthread_cond_t slots`, `pthread_cond_t` to protect the shared resource. Meanwhile `front, rear, count, n` keep track of the head and tail position of the queue, the current number of file descriptors and the maximum size of the queue buffer.
- The client connections are stored in a global `shared_queue_t` global variable, which is protected by a `lock` mutex to prevent race conditions, ensuring only one thread can access the shared queue at a time.
- The `slots` conditional variable is used to notify the worker threads when there are new connections available in the shared queue. It is also used to make the thread wait when the queue is full and wait for free spaces to insert new file descriptors. Insertion of new file descriptors is done by calling `add_connection()`.
- The `items` conditional variable is used to notify the worker threads when there are new items available in the shared queue. It is also used to make the thread wait when the queue is empty and wait for new items to be removed. Removal of file descriptors is done by calling `get_connection()`.
- `NUM_THREADS` is defined as a constant with a value of `8`. The number of threads is chosen based on the usual number of cores of a modern CPU, which is around 8 to 16.


### Details within Controller Node
- The thread pool is created by calling `init_shared_queue()` with the `shared_queue_t` struct as an argument, which initialises the mutex lock, conditional variables and the queue buffer.
- A for loop is used to create a pool of worker threads of size `NUM_THREADS`, each of which calls `controller_thread_routine()` to handle the incoming requests.
- `controller_thread_routine()` is responsible for:
  1. Accepting a new connection from a client using `accept()`.
  2. Adding the new connection to the shared queue using `add_client_connection()`.
  3. Reading the request from the connection using `rio_readlineb()`.
  4. Check if the request is valid using `is_valid_schedule_request()`, `is_valid_plane_status_request()`, or `is_valid_time_status_request()`. If the request is valid, extract the `airport_id` from the request.
  5. Forwarding the request to the approproate airport node using the `airport_id` extracted from the request.
  6. Reading and sending the response from the airport node back to the client.


### Details within Airport Nodes
- The thread pool is created by calling `init_shared_queue()` with the `shared_queue_t` struct as an argument, which initialises the mutex lock, conditional variables and the queue buffer.
- A for loop is used to create a pool of worker threads of size `NUM_THREADS`, each of which calls `airport_thread_routine()` to handle the incoming requests.
- `airport_thread_routine()` is responsible for:
  1. Removing a connection from the shared queue using `get_connection()`.
  2. Processing the request using `process_request()`.
  3. `process_request()` is responsible for:
    - Checking if the request is valid using `is_valid_schedule_request()`, `is_valid_plane_status_request()`, or `is_valid_time_status_request()`.
    - If the request is valid, calling `process_schedule()`, `process_plane_status()`, or `process_time_status()` to process the request.
    - Create the response using `snprintf()`.
  4. Sending the response back to the file descriptor using `rio_writen()`.
  5. Adding the closed connection back to `shared_queue` using `add_connection()` if the queue is not full.
- The airport node multithreading uses a fine-grained locking scheme, where each `time_slot_t` is protected by a `pthread_mutex_t lock` to ensure that only one thread can access the time slot at a time. Locking is done before accessing and modifying the `time_slot_t` struct, in the following functions: `check_time_slots_free()`, `add_plane_to_slots()`, and `search_gate()`. This locking approach has multiple benefits:
  - **Maximise concurrency**: Only one thread can access the time slot at a time, allowing other threads to access other time slots concurrently. Therefore, multiple requests can be processed concurrently, maximising the throughput of the airport node.
  - **Minimize contention**: Only the target time slot is locked, minimizing the time spent on other non-target time slots. Locks are released as soon as read/write operations are done.
  - **Avoid deadlocks**: Since only one thread can access the time slot at a time, this ensures to prevent deadlocks from occuring.
  - **Atomic operations**: Each slot has its own lock, ensuring any modification is atomic.
  - **Error handling**: Locks are released even when an error occurs. This helps to prevent resource leaks and ensure other thread can re-acquire the lock.


## Testing
- This assignment has given me a rewarding experience in multithreading and network programming. Throughout the assignment, I have encountered a few challenges:
  - **Difficulty in processing commands**: This is one of the hardest part of the assignment. Although previous assessments have given me some experiences in processing commands, this assignment lifts the difficulty level by requiring me to retrieving request and writing response from and to a file descriptor. I constantly missed out the new line character and empty response cases, causing my program to hang for a long time when running the test cases. Additionally, I previously set the `response buffer` size to be `MAXLINE`, which is `1024` bytes. This is not enough for some test cases related to `TIME_STATUS` command, where the response format can be in multiple lines. In the worst case, it can print out from index `0` to `47`, which makes the actual size required far beyond `1024` bytes. Although this may seem trivial, it took me a long time to figure out and I decided to switch to `MAXBUF` which is `8192` bytes.
  - **Debugging multithreading program**: This is another challenging part of the assignment. In sequential program, errors can be identified by using `gdb`. But since this is related to network programming and multithreading, it is hard to identify the source of error easily. I have to add additional `LOG` statements to track the value of relevant variables throughout the program to understand the state of the program at different stages.


