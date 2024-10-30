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

### Overview of thread pool implementation
Both the controller node and airport nodes concurrency behaviours are achieved by using a thread pool. A data structure called `shared_queue_t` is designed as a wrapper of an array storing file descriptors as a shared resource for storing network connections, including other struct members such as: `pthread_mutex_t lock`, `pthread_cond_t slots`, `pthread_cond_t` to protect the shared resource. Meanwhile `front, rear, count, n` keep track of the head and tail position of the queue, the current number of file descriptors and the maximum size of the queue buffer.

### 1. Multithreading within Controller Node
- The thread pool is created by calling `init_shared_queue()` with the `shared_queue_t` struct as an argument, which initialises the mutex lock, conditional variables and the queue buffer.
- A for loop is used to create a pool of worker threads, each of which calls `controller_thread_routine()` to handle the incoming requests.
- `controller_thread_routine()` is responsible for:
  1. Accepting a new connection from a client using `accept()`.
  2. Adding the new connection to the shared queue using `add_client_connection()`.
  3. Reading the request from the connection using `rio_readlineb()`.
  4. Check if the request is valid using `is_valid_schedule_request()`, `is_valid_plane_status_request()`, or `is_valid_time_status_request()`. If the request is valid, extract the `airport_id` from the request.
  5. Forwarding the request to the approproate airport node using the `airport_id` extracted from the request.
  6. Reading and sending the response from the airport node back to the client.
- `NUM_THREADS` is defined as a constant with a value of `16`. The number of threads is chosen based on the usual number of cores of a modern CPU, which is 16.
- The worker threads are detached from the main thread to ensure that the program does not wait for them to finish before cleaning up and terminating.
- The client connections are stored in a global shared queue, which is protected by a `lock` mutex to prevent race conditions.



### 2. Multithreading within Airport Nodes




## Testing

