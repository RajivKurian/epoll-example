#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>

#include <cassert>
#include <thread>

#include "net_utils.hpp"
#include "ring_buffer.hpp"

#define MAXEVENTS 64

#define DEFAULT_BUFFER_SIZE 4096
#define DEFAULT_RING_BUFFER_SIZE 1024

struct connection_data {
  int fd;
  char* buffer;
  ssize_t capacity;
  int written;
  bool stop;

  connection_data():
  fd(-1),
  buffer(new char[DEFAULT_RING_BUFFER_SIZE]),
  capacity(DEFAULT_BUFFER_SIZE),
  written(0),
  stop(false) {

  }

  ~connection_data() {
    delete[] buffer;
  }
};

// Reverse in place.
void reverse(char* p, int length) {
  int length_before_null = length - 1;
  int i,j;

  for (i = 0, j = length_before_null; i < j; i++, j--) {
    char temp = p[i];
    p[i] = p[j];
    p[j] = temp;
  }
}

int process_messages(processor::RingBuffer<connection_data>* ring_buffer) {
  int64_t prev_sequence = -1;
  int64_t next_sequence = -1;
  int num_events_processed = 0;
  int s = -1;
  while (true) {
    // Spin till a new sequence is available.
    while (next_sequence <= prev_sequence) {
      _mm_pause();
      next_sequence = ring_buffer->getProducerSequence();
    }
    // Process everything in the batch.
    for (int64_t index = prev_sequence + 1; index <= next_sequence; index++) {
      auto c_data = ring_buffer->get(index);
      auto client_fd = c_data->fd;
      auto buffer = c_data->buffer;
      // Used to signal the server to stop.
      //printf("\nConsumer stop value %s\n", c_data->stop ? "true" : "false");
      if (c_data->stop)
        goto exit_consumer;

      auto buffer_length = c_data->written;
      assert(client_fd != -1);
      assert(buffer_length > 0);

      // Write the buffer to standard output first.
      s = write (1, buffer, buffer_length);
      if (s == -1) {
        perror ("write");
        abort ();
      }

      // Then reverse it and echo it back.
      reverse(buffer, buffer_length);
      s = write(client_fd, buffer, buffer_length);
      if (s == -1) {
        perror ("echo");
        abort ();
      }
      // We are not checking to see if all the bytes have been written.
      // In case they are not written we must use our own epoll loop, express write interest
      // and write when the client socket is ready.
      ++num_events_processed;
    }
    // Mark events consumed.
    ring_buffer->markConsumed(next_sequence);
    prev_sequence = next_sequence;
  }
exit_consumer:
  printf("Finished processing all events. Server shutting down. Num events processed = %d\n", num_events_processed);
  return 1;
}

void event_loop(int efd,
                int sfd,
                processor::RingBuffer<connection_data>* ring_buffer) {
    int n, i;
    int retval;

  struct epoll_event event;
  // Buffer where events are returned.
  struct epoll_event* events = static_cast<epoll_event*>(calloc(MAXEVENTS, sizeof event));

  while (true) {

    n = epoll_wait(efd, events, MAXEVENTS, -1);
    for (i = 0; i < n; i++) {
      epoll_event current_event = events[i];

      if ((current_event.events & EPOLLERR) ||
          (current_event.events & EPOLLHUP) ||
          (!(current_event.events & EPOLLIN))) {
        // An error has occured on this fd, or the socket is not ready for reading (why were we notified then?).
        fprintf(stderr, "epoll error\n");
        close(current_event.data.fd);
        continue;
      } else if (sfd == current_event.data.fd) {
        // We have a notification on the listening socket, which means one or more incoming connections.
        while (true) {
          struct sockaddr in_addr;
          socklen_t in_len;
          int infd;
          char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

          in_len = sizeof in_addr;
          // No need to make these sockets non blocking since accept4() takes care of it.
          infd = accept4(sfd, &in_addr, &in_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
          if (infd == -1) {
            if ((errno == EAGAIN) ||
                (errno == EWOULDBLOCK)) {
              break;  // We have processed all incoming connections.
            } else {
              perror("accept");
              break;
            }
          }

          retval = getnameinfo(&in_addr, in_len,
                          hbuf, sizeof hbuf,
                          sbuf, sizeof sbuf,
                          NI_NUMERICHOST | NI_NUMERICSERV);
          if (retval == 0) {
            printf("Accepted connection on descriptor %d (host=%s, port=%s)\n", infd, hbuf, sbuf);
          }

          event.data.fd = infd;
          event.events = EPOLLIN | EPOLLET;
          retval = epoll_ctl(efd, EPOLL_CTL_ADD, infd, &event);
          if (retval == -1) {
            perror("epoll_ctl");
            abort();
          }
        }
        continue;
      } else {
        // We have data on the fd waiting to be read. Read and  display it.
        // We must read whatever data is available completely, as we are running in edge-triggered mode
        // and won't get a notification again for the same data.
        int done = 0;

        while (true) {
          ssize_t count;
          // Get the next ring buffer entry.
          auto next_write_index = ring_buffer->nextProducerSequence();
          auto entry = ring_buffer->get(next_write_index);
          count = read(current_event.data.fd, entry->buffer, DEFAULT_BUFFER_SIZE);
          entry->fd = current_event.data.fd;
          entry->written = count;

          if (count == -1) {
            /* If errno == EAGAIN, that means we have read all
               data. So go back to the main loop. */
            if (errno != EAGAIN) {
              perror("read");
              done = 1;
            }
            break;
          } else if (count == 0) {
            /* End of file. The remote has closed the
               connection. */
            done = 1;
            break;
          }

          // Check if the client want's to exit the server.
          // This might never work out even if the client sends a exit signal because TCP might
          // split and rearrange the packets across epoll signal boundaries at the server.
          bool stop = (strncmp(entry->buffer, "exit", 4) == 0);
          entry->stop = stop;

          // Publish the ring buffer entry since all is well.
          ring_buffer->publish(next_write_index);
          if (stop)
            goto exit_loop;
        }

        if (done) {
          printf("Closed connection on descriptor %d\n", current_event.data.fd);

          // Closing the descriptor will make epoll remove it from the set of descriptors which are monitored.
          close(current_event.data.fd);
        }
      }
    }
  }
exit_loop:
  free(events);
}

int main (int argc, char *argv[]) {
  int sfd, retval;
  int efd;
  struct epoll_event event;
  // Our ring buffer.
  auto ring_buffer = new processor::RingBuffer<connection_data>(DEFAULT_RING_BUFFER_SIZE);

  if (argc != 2) {
    fprintf(stderr, "Usage: %s [port]\n", argv[0]);
    exit (EXIT_FAILURE);
  }

  sfd = create_and_bind(argv[1]);
  if (sfd == -1)
    abort ();

  retval = make_socket_non_blocking(sfd);
  if (retval == -1)
    abort ();

  retval = listen(sfd, SOMAXCONN);
  if (retval == -1) {
    perror ("listen");
    abort ();
  }

  efd = epoll_create1(0);
  if (efd == -1) {
    perror ("epoll_create");
    abort ();
  }

  event.data.fd = sfd;
  event.events = EPOLLIN | EPOLLET;
  retval = epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event);
  if (retval == -1) {
    perror ("epoll_ctl");
    abort ();
  }




  // Start the worker thread.
  std::thread t{process_messages, ring_buffer};

  // The event loop.
  event_loop(efd, sfd, ring_buffer);

  t.join();
  close(sfd);
  delete ring_buffer;
  return EXIT_SUCCESS;
}