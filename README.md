Epoll Sample project
====================

A multi-threaded server which accepts bytes from the client and reverses them at arbitrary boundaries and sends it back to the client.

1.  Most of the code for the epoll part of the tutorial is from a [Banu Systems Blog](https://banu.com/blog/2/how-to-use-epoll-a-complete-example-in-c/). The extension was making it multi threaded.
2.  The code is a mix of C style (since it borrows from the blog) with some C++ thrown in.
3.  The code is very simplistic and does not handle a lot of cases. There are comments in such places. Examples include but are not limited to:
  1. We check for the string "exit" from the client to stop the server. The "exit" string might be split across two different epoll events and we will not detect this. This is handled by a proper protocol design.
  2.  The worker thread writes the reversed string to the client. It checks for an error but does not check if all bytes were written. The worker thread can also have it's own epoll loop where it waits for a write event when all bytes don't go through. One should still first attempt a write and only make an epoll call if all the bytes did not go through. There are a few tricky things here:
    1.  Balancing between polling the ring buffer (for more events to process) and making epoll calls is tricky.
    2.  The worker thread must copy any of the ring buffer entry data that it still needs before marking the entry processed. In our case that would be the FD for the socket and the data not sent yet. This is important because the producer thread could overwrite the entry before we manage to send the bytes to the client.
    3.  If a particular socket is waiting for a writable signal, the worker thread should not write data on that socket after a separate event is processed till the first pending data is successfully written. We can process the event and just append the data to our already pending data. This can be done by having a linked list of buffers or even a realloc + copy.
4.  There are two threads connected by a ring buffer:
  1.  Thread 1: Accepts client connections and reads bytes from them in an event loop using epoll. For every read event it reads the bytes into a ring buffer entry and publishes it with the client FD.
  2.  Thread 2: It reads events of the ring buffer. For each entry it prints the bytes, reverses them in place and sends the reversed bytes back to the client.
5.  An important benefit of using a ring buffer is that there are no malloc/free calls in steady state. All the memory is pre-allocated and just re-used. We use static 4096k buffers to read client data. Even if multiple sizes of buffers are needed this design can be altered for close to zero allocations in steady state. This is easily achieved by using a length prefixed protocol:
  1.   Once the first 4/8 bytes (32bit/64 bit length) of a message are read(this can be done on the stack), the length of the buffer needed to hold the entire message is known.
  2.  We can pick an appropriately sized buffer from a pool of buffers and use it for the rest of the message. Once the entire message is read we can put a pointer to this buffer on the ring buffer and publish it. Beware of slowloris attacks with such an approach though. Malicious or slow/faulty clients could cause buffer exhaustion on the server. For eg: A bunch of clients could connect and say they have a 1MB message to send. Once you allocate they never send any data but your buffers get used up. Using timers to reclaim such buffers and disconnect misbehaving clients can help with these problems.
  3.  Whenever we are short on buffers we could check all the ring buffer entries that have been processed in this cycle. We can claim all those buffers at such a point and put them in our pool. With this technique we must claim buffers at least once per cycle of the ring buffer otherwise we will end up reclaiming buffers that are still in use. This accounting can be a bit tricky. There are other ways of exchanging buffer info without any sharing (false or otherwise).
  4. Using a memory allocator like jemalloc can free us from maintaining our own pool of buffers. Jemalloc already uses slab allocation + buddy memory allocation to reuse buffers. We could use Jemalloc and just call malloc and free. Note: We still always call malloc and free on the same thread instead of across thread boundaries.

  To build and test
-------------------

  Make sure you have cmake. This will only work on a Linux OS with epoll and accept4 support.

    git clone https://github.com/RajivKurian/epoll-example.git
    cd epoll-example
    mkdir build
    cd build
    cmake ../src
    make
    // To start the server at port 9090
    ./epoll 9090
    // You can test by using netcat in another terminal
    nc localhost 9090





