// mayhem: an in-memory UART bus for the fuzz harnesses. The components under test talk to
// their device through uart::UARTComponent (esphome/components/uart/uart_component.h); this
// is a test double of that bus, like the fake UARTs in upstream's tests/components/*/common.h,
// that serves the fuzz input as the bytes the device sent and discards what the component writes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "esphome/components/uart/uart_component.h"

namespace mayhem_fuzz {

class FuzzUART : public esphome::uart::UARTComponent {
 public:
  void feed(const uint8_t *data, size_t size) {
    data_ = data;
    size_ = size;
    pos_ = 0;
  }
  size_t remaining() const { return size_ - pos_; }
  bool drained() const { return pos_ >= size_; }

  // Request/response mode: the input is a sequence of replies, each prefixed with a 2-byte
  // big-endian length. A reply only becomes readable once the component starts writing its next
  // command, so drivers that flush stale RX bytes before each command still see the reply.
  void feed_replies(const uint8_t *data, size_t size) {
    feed(data, 0);
    rr_data_ = data;
    rr_size_ = size;
    rr_pos_ = 0;
    rr_mode_ = true;
    rr_armed_ = false;
  }
  bool replies_drained() const { return !rr_mode_ || (rr_pos_ >= rr_size_ && drained()); }

  void write_array(const uint8_t *, size_t) override {
    if (!rr_mode_ || rr_armed_)
      return;
    rr_armed_ = true;
    size_t len = 0;
    if (rr_size_ - rr_pos_ >= 2) {
      len = (size_t(rr_data_[rr_pos_]) << 8) | rr_data_[rr_pos_ + 1];
      rr_pos_ += 2;
    } else {
      rr_pos_ = rr_size_;
    }
    if (len > rr_size_ - rr_pos_)
      len = rr_size_ - rr_pos_;
    data_ = rr_data_ + rr_pos_;
    size_ = len;
    pos_ = 0;
    rr_pos_ += len;
  }
  bool peek_byte(uint8_t *data) override {
    rr_armed_ = false;
    if (pos_ < size_) {
      *data = data_[pos_];
      return true;
    }
    return false;
  }
  bool read_array(uint8_t *data, size_t len) override {
    rr_armed_ = false;
    if (len > size_ - pos_)
      return false;
    if (len)
      std::memcpy(data, data_ + pos_, len);
    pos_ += len;
    return true;
  }
  size_t available() override {
    rr_armed_ = false;
    return size_ - pos_;
  }
  esphome::uart::UARTFlushResult flush() override { return esphome::uart::UARTFlushResult::UART_FLUSH_RESULT_SUCCESS; }
  void check_logger_conflict() override {}

 private:
  const uint8_t *data_{nullptr};
  size_t size_{0};
  size_t pos_{0};
  const uint8_t *rr_data_{nullptr};
  size_t rr_size_{0};
  size_t rr_pos_{0};
  bool rr_mode_{false};
  bool rr_armed_{false};
};

}  // namespace mayhem_fuzz
