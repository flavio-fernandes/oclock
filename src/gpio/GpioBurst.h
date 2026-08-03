#ifndef OCLOCK_GPIO_GPIO_BURST_H
#define OCLOCK_GPIO_GPIO_BURST_H

#include <cstddef>
#include <cstdint>

#include "Gpio.h"

// A pre-resolved, low-overhead value path for a small fixed set of lines that
// are already configured as outputs by the ordinary GPIO backend.
//
// This exists for exactly one caller: the HT1632 matrix, whose select chain and
// mixed 3/7/4-bit fields do not map onto any kernel subsystem. Every other
// device either uses kernel SPI or is slow enough not to care. Do not widen
// this into a general replacement for the Gpio interface or for kernel SPI.
//
// The ordinary backend keeps chip discovery, line validation, direction,
// initial values, ownership, and cleanup. Only the repeated value stores in the
// matrix bit loop take this path.
//
// Ordering: the BCM2835 guarantees that reads and writes to the *same*
// peripheral complete in order, so a full barrier is not required between
// individual GPIO stores. Barriers are needed when switching peripherals, so
// this class places them at burst boundaries through begin() and finish()
// rather than after every edge. That is the single largest saving relative to
// the per-write path.
#ifdef OCLOCK_GPIO_BURST_TEST_HOOK
// Defined by the burst equivalence test only.
void oclockGpioBurstTestHook(std::uint32_t mask, GpioValue value);
#endif

class GpioBurstPins {
public:
  GpioBurstPins() : setRegister_(0), clearRegister_(0) {}

  GpioBurstPins(volatile std::uint32_t *setRegister,
                volatile std::uint32_t *clearRegister)
      : setRegister_(setRegister), clearRegister_(clearRegister) {}

  bool valid() const {
    return setRegister_ != 0 && clearRegister_ != 0;
  }

  // Enter and leave a run of same-peripheral stores.
  void begin() const { __sync_synchronize(); }
  void finish() const { __sync_synchronize(); }

  inline void write(std::uint32_t mask, GpioValue value) const {
#ifdef OCLOCK_GPIO_BURST_TEST_HOOK
    // Test builds only. Each store would otherwise overwrite the same word,
    // so the emitted order could not be compared against the gpio.write()
    // path. Never defined for hardware or sandbox builds.
    oclockGpioBurstTestHook(mask, value);
#else
    if (value == GpioValue::high) {
      *setRegister_ = mask;
    } else {
      *clearRegister_ = mask;
    }
#endif
  }

private:
  volatile std::uint32_t *setRegister_;
  volatile std::uint32_t *clearRegister_;
};

// The HT1632 requires at least 50 ns between a data change and the rising edge
// of WR. The old per-write path satisfied that incidentally, because each write
// cost microseconds. A direct register store does not, so the burst path must
// hold that setup time explicitly.
//
// A GPIO peripheral store on this target already costs tens of nanoseconds, and
// these nops add roughly one core cycle each at 1 GHz. The default is
// deliberately generous rather than minimal: this gate is about removing
// milliseconds, so spending a few nanoseconds on a documented setup guarantee
// is the right trade.
//
// If a hardware run ever shows garbled or unstable matrix content, raise this
// first. Lowering it below the datasheet requirement is not an optimization.
#ifndef OCLOCK_GPIO_BURST_SETUP_NOPS
#define OCLOCK_GPIO_BURST_SETUP_NOPS 32
#endif

inline void gpioBurstSetupDelay() {
  for (int i = 0; i < OCLOCK_GPIO_BURST_SETUP_NOPS; ++i) {
    // asm volatile is not foldable, unlike an empty loop body.
    __asm__ __volatile__("nop");
  }
}

#endif
