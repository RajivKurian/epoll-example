#ifndef RING_BUFFER_H_
#define RING_BUFFER_H_

#include <x86intrin.h>

#include <atomic>
#include <cinttypes>
#include <cstdio>

namespace processor {

#define CACHE_LINE_SIZE 64

// A simple ring buffer for single producers and single consumers.
// Does not support parallel consumers for now.
template<typename T>
class RingBuffer {

public:
  // Events_size must be a power of two.
  explicit RingBuffer(uint64_t event_size) :
    event_size_(event_size),
    publisher_sequence_(-1),
    cached_consumer_sequence_(-1),
    events_(new T[event_size]),
    consumer_sequence_(-1) {
  }

  // No copy constructor.
  RingBuffer(const RingBuffer&) = delete;
  RingBuffer& operator = (const RingBuffer &) = delete;

  // Used to get an event for a given sequence.
  //Can be called by both the producer and consumer.
  inline T* get(int64_t sequence) {
    return &events_[sequence & (event_size_ - 1)];  // size - 1 is the mask here.
  }

  // Can be called by either producer or consumer.
  inline uint64_t getBufferSize() const {
    return event_size_;
  }

  // Called by the producer to get the next publish slot.
  // Will block till there is a slot to claim.
  int64_t nextProducerSequence() {
    int64_t current_producer_sequence = publisher_sequence_.load(std::memory_order::memory_order_relaxed);
    int64_t next_producer_sequence = current_producer_sequence + 1;
    int64_t wrap_point = next_producer_sequence - event_size_;
    //printf("\nCurrent seq: %" PRId64 ", next seq: %" PRId64 ", wrap_point: %" PRId64 "\n", current_producer_sequence, next_producer_sequence, wrap_point);
    // TODO(Rajiv): Combine pausing with backoff + sleep.
    if (cached_consumer_sequence_ > wrap_point) {
      return next_producer_sequence;
    }
    cached_consumer_sequence_ = getConsumerSequence();
    while (cached_consumer_sequence_ <= wrap_point) {
      _mm_pause();
      cached_consumer_sequence_ = getConsumerSequence();
    }
    return next_producer_sequence;
  }

  // Called by the producer to see what entries the consumer is done with.
  inline int64_t getConsumerSequence() const {
    return consumer_sequence_.load(std::memory_order::memory_order_acquire);
  }

  // Called by the producer after it has set the correct event entries.
  inline void publish(int64_t sequence) {
    publisher_sequence_.store(sequence, std::memory_order::memory_order_release);
  }

  // Called by the consumer to see where the producer is at.
  inline int64_t getProducerSequence() const {
    return publisher_sequence_.load(std::memory_order::memory_order_acquire);
  }

  // Called by the consumer to set the latest consumed sequence.
  inline void markConsumed(int64_t sequence) {
    // Assert if sequence is higher than the previous one?
    consumer_sequence_.store(sequence, std::memory_order::memory_order_release);
  }

  ~RingBuffer() {
    printf("Deleted Ring Buffer\n");
    delete[] events_;
  }

private:

  int64_t event_size_;
  std::atomic<int64_t> publisher_sequence_;
  int64_t cached_consumer_sequence_;
  T* events_;
  // Ensure that the consumer sequence is on it's own cache line to prevent false sharing.
  std::atomic<int64_t> consumer_sequence_ __attribute__ ((aligned (CACHE_LINE_SIZE)));

} __attribute__ ((aligned(CACHE_LINE_SIZE)));

}  // processor

#endif