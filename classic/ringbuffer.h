#pragma once

// PURPOSE: Multi-threaded safe ring-buffer supporting 'append' for writers, and 'read' for readers
//
// CLASSES: RingBuffer::SPSC: Single producer (SP) single consumer (SC) buffer with fixed capacity k_SPSC_CAPACITY
//                            operating on objects of type 'RingBuffer::Op'

#include <atomic>
#include <thread>
#include <array>

#include <assert.h>

namespace RingBuffer {

const u_int64_t k_SPSC_CAPACITY = 16;

struct SPSC {
  // DATA
  alignas(64) std::atomic<size_t>             d_readIdx;
  alignas(64) size_t                          d_readIdxCached;
  alignas(64) std::atomic<size_t>             d_writeIdx;
  alignas(64) size_t                          d_writeIdxCached;
  alignas(64) std::array<int,k_SPSC_CAPACITY> d_data;

public:
  // CREATORS
  SPSC();
    // Construct SPSC (single-producer, single-consumer) queue operating on 'Op' objects with fixed capacity
    // k_SPSC_CAPACITY.

  SPSC(const SPSC& other) = delete;
    // Copy constructor not provided

  ~SPSC() = default;
    // Destroy this object

  // MANIPULATORS
  bool append(const int value);
    // Return true if specified 'value' was appended to queue and false. Note that false is returned when queue full.
    // Behavior is defined provided the producer thread only calls 'append'.

  bool read(int *value);
    // Return true if next object for reader was copied into specified 'value' and false othewise. Note that false
    // means queue empty or there's no current object to read e.g. reader already end of queue. Behavior is defined
    // provided the consumer thread only calls 'read'.

  SPSC& operator=(const SPSC& rhs) = delete;
    // Assignment operator not provided
};

// INLINE DEFINITIONS
// CREATORS
inline
SPSC::SPSC()
: d_readIdx(0)
, d_readIdxCached(0)
, d_writeIdx(0)
, d_writeIdxCached(0)
{
}

// MANIPULATORS
inline
bool SPSC::append(const int value) {
  auto const writeIdx = d_writeIdx.load(std::memory_order_relaxed);
  auto nextWriteIdx = d_writeIdx + 1;
  if (nextWriteIdx == k_SPSC_CAPACITY) {
    nextWriteIdx = 0;
  }
  if (nextWriteIdx == d_readIdxCached) {
    d_readIdxCached = d_readIdx.load(std::memory_order_acquire);
    if (nextWriteIdx == d_readIdxCached) {
        return false;
    }
  }
  d_data[writeIdx] = value;
  d_writeIdx.store(nextWriteIdx, std::memory_order_release);
  return true;
}

inline
bool SPSC::read(int *value) {
  auto const readIdx = d_readIdx.load(std::memory_order_relaxed);
  if (readIdx == d_writeIdxCached) {
    d_writeIdxCached = d_writeIdx.load(std::memory_order_acquire);
    if (d_readIdx == d_writeIdxCached) {
      return false;
    }
  }
  *value = d_data[readIdx];
  auto nextReadIdx = d_readIdx + 1;
  if (nextReadIdx == k_SPSC_CAPACITY) {
    nextReadIdx = 0;
  }
  d_readIdx.store(nextReadIdx, std::memory_order_release);
  return true;
}

}
