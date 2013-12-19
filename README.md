Epoll Sample project
====================

A multi-threaded server which accepts bytes from the client and reverses them at arbitrary boundaries and sends it back to the client.

1.  Most of the code for the epoll part of the tutorial is from this [Banu Systems Blog](https://banu.com/blog/2/how-to-use-epoll-a-complete-example-in-c/). The extension was making it multi threaded.
2.  The code is a mix of C style (since it borrows from the blog) with some C++ thrown in.
3.  The code is very simplistic and does not handle a lot of cases. There are comments in such places. Examples include but are not limited to:
  1. We check for the string "exit" string from the client to stop the server. The "exit" string might be split across two different epoll events and we will not stop it then. This is handled by a proper protocol design.
  2.  The worker thread writes the reversed string to the client. It checks for an error but does not check if all bytes were written. The worker thread can also have it's own epoll loop where it waits for write interest when all bytes don't go through. One should still first attempt a write and only make an epoll call if all the bytes did not go through. The trick here is balancing between polling the ring buffer (for more events to process) and making epoll calls.
4.  There are two threads connected by a ring buffer:
  1.  Thread 1: Accepts client connections and reads bytes from them in an event loop using epoll. For every read event it reads the bytes into a ring buffer entry and publishes it with the client FD.
  2.  Thread 2: It reads events of the ring buffer. For each entry it prints the bytes, reverses them and sends the reversed bytes back to the client.

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




