#include "spi/FakeSpiOutput.h"

#include <stdexcept>

FakeSpiOutput::FakeSpiOutput()
    : initializeResult_(true), initialized_(false) {
}

bool FakeSpiOutput::initialize() {
  std::lock_guard<std::mutex> guard(mutex_);
  initialized_ = initializeResult_;
  return initialized_;
}

void FakeSpiOutput::transfer(const std::uint8_t *data, std::size_t length) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!initialized_)
    throw std::runtime_error("fake SPI output is not initialized");
  if (data == NULL && length != 0)
    throw std::invalid_argument("fake SPI output data is missing");
  std::vector<std::uint8_t> payload;
  if (length != 0) payload.assign(data, data + length);
  transfers_.push_back(payload);
}

void FakeSpiOutput::setInitializeResult(bool result) {
  std::lock_guard<std::mutex> guard(mutex_);
  initializeResult_ = result;
}

bool FakeSpiOutput::initialized() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return initialized_;
}

std::vector<std::vector<std::uint8_t> > FakeSpiOutput::transfers() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return transfers_;
}

void FakeSpiOutput::clearTransfers() {
  std::lock_guard<std::mutex> guard(mutex_);
  transfers_.clear();
}
