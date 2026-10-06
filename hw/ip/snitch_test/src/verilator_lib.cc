// Copyright 2020 ETH Zurich and University of Bologna.
// Solderpad Hardware License, Version 0.51, see LICENSE for details.
// SPDX-License-Identifier: SHL-0.51

#include <printf.h>

#include "Vtestharness.h"
#include "Vtestharness__Dpi.h"
#include "sim.hh"
#include "tb_lib.hh"
#include "verilated.h"

#ifdef FAULT_INJECTION_ENABLE
// Faultergeist fault injection (https://github.com/antmicro/faultergeist).
// The campaign file is taken from the FI_CAMPAIGN environment variable, so
// different campaigns run on the same simulator binary.
#include <cstdlib>

#include "verilated_vpi.h"
extern "C" void FaultergeistCreate(const char *input_file);
extern "C" void FaultergeistDestroy(void);
static bool fi_enabled = false;
#endif

namespace sim {

Sim* s;

// Number of cycles between HTIF checks.
const int HTIFTimeInterval = 200;
void sim_thread_main(void *arg) { ((Sim *)arg)->main(); }

// Sim time.
int TIME = 0;

Sim::Sim(int argc, char **argv) : htif_t(argc, argv) {
    Verilated::commandArgs(argc, argv);
}

void Sim::idle() { target.switch_to(); }

/// Execute the simulation.
int Sim::run() {
    host = context_t::current();
    target.init(sim_thread_main, this);

    int exit_code = htif_t::run();
#ifdef FAULT_INJECTION_ENABLE
    if (fi_enabled) {
        VerilatedVpi::callCbs(cbEndOfSimulation);
        FaultergeistDestroy();
    }
#endif
    if (exit_code == 0)
      fprintf(stderr, "[SUCCESS] Program finished successfully\n");
    else
      fprintf(stderr, "[FAILURE] Finished with exit code %2d\n", exit_code);
    return exit_code;
}

void Sim::main() {
    // Initialize verilator environment.
    Verilated::traceEverOn(true);

    // Create a pointer to ourselves
    s = this;

    // Allocate the simulation state.
    auto top = std::make_unique<Vtestharness>();

    bool clk_i = 0, rst_ni = 0;

#ifdef FAULT_INJECTION_ENABLE
    if (const char *campaign = std::getenv("FI_CAMPAIGN")) {
        fi_enabled = true;
        FaultergeistCreate(campaign);
        VerilatedVpi::callCbs(cbStartOfSimulation);
    }
#endif

    while (!Verilated::gotFinish()) {
        clk_i = !clk_i;
        rst_ni = TIME >= 8;
        top->clk_i = clk_i;
        top->rst_ni = rst_ni;
        // Expose the time to the model (waveform timestamps, $time, VPI).
        Verilated::time(TIME);
#ifdef FAULT_INJECTION_ENABLE
        // Inject the faults due at this time, before evaluating the DUT.
        if (fi_enabled) VerilatedVpi::callTimedCbs();
#endif
        // Evaluate the DUT.
        top->eval();
#ifdef FAULT_INJECTION_ENABLE
        if (fi_enabled) VerilatedVpi::callValueCbs();
#endif
        // Increase global time.
        TIME++;
        // Switch to the HTIF interface in regular intervals.
        if (TIME % HTIFTimeInterval == 0) {
            host->switch_to();
        }
    }
}
}  // namespace sim

// Verilator callback to get the current time.
double sc_time_stamp() { return sim::TIME * 1e-9; }

// DPI calls.
void tb_memory_read(long long addr, int len, const svOpenArrayHandle data) {
    // std::cout << "[TB] Read " << std::hex << addr << std::dec << " (" << len
    //           << " bytes)\n";
    void *data_ptr = svGetArrayPtr(data);
    assert(data_ptr);
    sim::MEM.read(addr, len, (uint8_t *)data_ptr);
}

void tb_memory_write(long long addr, int len, const svOpenArrayHandle data,
                     const svOpenArrayHandle strb) {
    // std::cout << "[TB] Write " << std::hex << addr << std::dec << " (" << len
    //           << " bytes)\n";
    const void *data_ptr = svGetArrayPtr(data);
    const void *strb_ptr = svGetArrayPtr(strb);
    assert(data_ptr);
    assert(strb_ptr);
    sim::MEM.write(addr, len, (const uint8_t *)data_ptr,
                   (const uint8_t *)strb_ptr);
}

int get_entry_point() {
  return sim::s->entry_point();
}
