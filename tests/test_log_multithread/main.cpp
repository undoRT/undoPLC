/**
 * @file main.cpp
 * @brief Multithreaded validation test for lock-free multi-queue logging.
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoSystem.hpp"
#include "undoLog.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <boost/asio/io_context.hpp>

// Atomic flag to control the lifecycle of real-time simulation threads
std::atomic<bool> keepRunning{true};

/**
 * @brief Simulated worker function mimicking a deterministic real-time thread cycle.
 * @param domain The logging target domain (BUS, PLC, PRG).
 * @param threadName Identification string used inside the log messages.
 */
void simulatedRtThread(LogDomain domain, const std::string& threadName)
{
   UndoLog& logger = UndoLog::getInstance();
   UndoSys& sys = UndoSys::getInstance();

   // 1. Mandatory registration sequence (Executed during thread boot, safe from RT path)
   logger.registerThread();

   logger.logRT(domain, LOG_INFO, "Thread [%s] successfully initialized and isolated.", threadName.c_str());

   // Set the cyclic interval to 5 seconds expressed in nanoseconds
   uint64_t standardIntervalNs = 5000000000ULL;

   while (keepRunning) {
      uint64_t startTsc = sys.readTsc();

      // Dispatch a real-time log record simulating mission-critical activity
      logger.logRT(domain, LOG_INFO, "Cyclic tick notification from [%s]. Start TSC: %lu", threadName.c_str(), startTsc);

      // Deterministic TSC-based hardware busy wait block
      sys.busyWait(standardIntervalNs);
   }

   logger.logRT(domain, LOG_WARNING, "Thread [%s] received stop request. Terminating loop.", threadName.c_str());
}

int main(int argc, char* argv[])
{
   bool logToConsole = false;
   for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--log2console") {
         logToConsole = true;
      }
   }

   boost::asio::io_context ioc;
   UndoLog& logger = UndoLog::getInstance();
   UndoSys& sys = UndoSys::getInstance();

   // Initialize the backend logging engine attached to the Asio context
   logger.init(ioc, logToConsole);

   // Register the main execution thread to allow isolated logging operations
   logger.registerThread();

   std::cout << "=========================================================" << std::endl;
   std::cout << "   undoRT - Multithreaded Lock-Free Queue Validation    " << std::endl;
   std::cout << "=========================================================" << std::endl;

   // Print the resolved CPU invariant TSC frequency info
   sys.logSystemStatus();

   // 2. Launch the concurrent real-time simulation thread pool
   std::vector<std::thread> pool;

   // Thread 1: Fieldbus Management Simulator (Domain: BUS)
   pool.emplace_back(simulatedRtThread, LogDomain::BUS, "UndoMasterBus");

   // Thread 2: Core PLC Framework Logic Simulator (Domain: PLC)
   pool.emplace_back(simulatedRtThread, LogDomain::PLC, "UndoTask_Logic0");

   // Thread 3: Compiled User Application Program Simulator (Domain: PRG)
   pool.emplace_back(simulatedRtThread, LogDomain::PRG, "UndoTask_UserPrg");

   // Spin up an asynchronous dedicated thread to run the Asio Event Loop background context
   std::thread asioThread([&ioc]() { ioc.run(); });

   // Let the pipeline run for 16 seconds to monitor 3 continuous execution waves (at 0s, 5s, 10s, 15s)
   std::this_thread::sleep_for(std::chrono::seconds(16));

   std::cout << "\n[Main] Initiating thread pool shutdown sequence..." << std::endl;
   keepRunning = false;

   // Join active execution workers back to the primary context safely
   for (auto& th : pool) {
      if (th.joinable()) {
         th.join();
      }
   }

   // Halt the Asio execution context and close the reactive driver worker
   ioc.stop();
   if (asioThread.joinable()) {
      asioThread.join();
   }

   std::cout << "[Main] System test sequence completed successfully. Environment clean." << std::endl;
   return 0;
}