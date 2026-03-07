#pragma once
#include <stdint.h>

/* Usage
FIFO<uint8_t, 64>  cpu_rx_fifo;
FIFO<uint8_t, 64>  cpu_tx_fifo;

FIFO<uint8_t, 32>  keyboard_fifo;

FIFO<uint8_t, 256> vdu_fifo;

cpu_rx_fifo.push(data);
*/

/// <summary>
/// fifo circular buffer implementation for single producer/single consumer use case
/// </summary>
/// <typeparam name="T"></typeparam>
/// <typeparam name="SIZE"></typeparam>

template<typename T, size_t SIZE>
class FIFO
{
  static_assert((SIZE& (SIZE - 1)) == 0, "SIZE must be power of two");

private:

  volatile uint32_t head = 0;
  volatile uint32_t tail = 0;

  T buffer[SIZE];

  static constexpr uint32_t MASK = SIZE - 1;

public:

  enum StatusBits : uint8_t {
    EMPTY = 1 << 0,
    FULL = 1 << 1,
    DATA = 1 << 2,   // data available
    SPACE = 1 << 3    // space available
  };

  static constexpr uint32_t capacity = SIZE - 1;

  inline bool isEmpty() const {
    return head == tail;
  }

  inline bool isFull() const {
    return ((head + 1) & MASK) == tail;
  }

  inline uint32_t count() const {
    return (head - tail) & MASK;
  }

  inline uint8_t status() const {
    uint8_t s = 0;

    if (head == tail)
      s |= EMPTY;
    else
      s |= DATA;

    if (((head + 1) & MASK) == tail)
      s |= FULL;
    else
      s |= SPACE;

    return s;
  }

  inline bool push(T value) {
    uint32_t h = head;
    uint32_t next = (h + 1) & MASK;

    if (next == tail)
      return false;

    buffer[h] = value;
    head = next;

    return true;
  }

  inline bool pop(T& value) {
    uint32_t t = tail;

    if (t == head)
      return false;

    value = buffer[t];
    tail = (t + 1) & MASK;

    return true;
  }

  inline void clear() {
    head = tail = 0;
  }
};
