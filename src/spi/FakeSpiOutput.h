#ifndef OCLOCK_SPI_FAKE_SPI_OUTPUT_H
#define OCLOCK_SPI_FAKE_SPI_OUTPUT_H

#include "spi/SpiOutput.h"

#include <mutex>
#include <vector>

class FakeSpiOutput : public SpiOutput {
public:
  FakeSpiOutput();

  bool initialize();
  void transfer(const std::uint8_t *data, std::size_t length);

  void setInitializeResult(bool result);
  bool initialized() const;
  std::vector<std::vector<std::uint8_t> > transfers() const;
  void clearTransfers();

private:
  bool initializeResult_;
  bool initialized_;
  std::vector<std::vector<std::uint8_t> > transfers_;
  mutable std::mutex mutex_;

  FakeSpiOutput(const FakeSpiOutput&) = delete;
  FakeSpiOutput& operator=(const FakeSpiOutput&) = delete;
};

#endif
