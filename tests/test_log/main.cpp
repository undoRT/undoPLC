/**
 * @file main.cpp
 * @brief Test application for multi-domain deferred logging and system architecture.
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoSystem.hpp"
#include "undoLog.hpp"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

int main(int argc, char* argv[])
{
   bool logToConsole = false;
   for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--log2console") {
         logToConsole = true;
      }
   }

   boost::asio::io_context ioc;

   UndoSys& sys = UndoSys::getInstance();
   UndoLog& logger = UndoLog::getInstance();

   // Register this thread
   logger.registerThread();

   // Pass the io_context reference to the logger subsystem
   logger.init(ioc, logToConsole);

   // Register OS Signals asynchronously using Boost.Asio
   boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
   signals.async_wait([&ioc, &logger](const boost::system::error_code& error, int signal_number) {
      if (!error) {
         logger.logRT(LogDomain::PLC, LOG_WARNING, "Termination signal (%d) intercepted. Stopping engine...", signal_number);
         // Stop the io_context loop. This causes ioc.run() to return instantly.
         ioc.stop();
      }
   });

   std::cout << "====================================================" << std::endl;
   std::cout << "                Initialize Engine undoRT            " << std::endl;
   std::cout << "====================================================" << std::endl;

   // Log (PLC) - system start
   logger.logRT(LogDomain::PLC, LOG_INFO, "undoPLC infrastructure started. Total CPU cores detected in the system: %d", sys.getTotalCpu());

   const auto& isolatedCores = sys.getIsolatedCpu();
   logger.logRT(LogDomain::PLC, LOG_INFO, "Number of isolated cores detected via sysfs: %zu", isolatedCores.size());

   // Log fieldbus (BUS) - simulate ethercat scan
   logger.logRT(LogDomain::BUS, LOG_INFO, "Initializing EtherCAT master on interface eth0...");
   logger.logRT(LogDomain::BUS, LOG_INFO, "Bus scan completed: 3 slaves detected (EK1100 / EL2008)");
   logger.logRT(LogDomain::BUS, LOG_WARNING, "Slave ID 2 responded with a coupling delay (Init -> Pre-OP)");

   // Log User Program Logic (PRG) - Simulate state machine startup (Structured Text)
   logger.logRT(LogDomain::PRG, LOG_INFO, "Loading of compiled executable from st2cpp completed.");
   logger.logRT(LogDomain::PRG, LOG_INFO, "Main task 'PLC_PRG' registered with a cycle time of 1000 uS.");
   logger.logRT(LogDomain::PRG, LOG_INFO, "User machine state changed to: RUNNING");

   // --- Runtime Anomalies Simulation ---
   logger.logRT(LogDomain::PRG, LOG_WARNING, "User variable 'EngineTemperature' rose to %d degrees", 72);
   logger.logRT(LogDomain::BUS, LOG_ERR, "Critical Frame Loss error (incorrect CRC) on slave node %d", 1);
   logger.logRT(LogDomain::PLC, LOG_ERR, "Overrun detected on Core %d! Cycle exceeded time budget by %llu ns", 12, 14200ULL);

   // 4. Deferred processing handled by the Main thread
   std::cout << "\n[Main Thread] Records received in lock-free queue. Starting flushing..." << std::endl;
   if (logToConsole) {
      std::cout << "----------------------------------------------------" << std::endl;
   }

   // Blocks the main thread with 0% CPU overhead, handling triggers reactively.
   ioc.run();

   if (logToConsole) {
      std::cout << "----------------------------------------------------" << std::endl;
   }
   std::cout << "[Main Thread] Flushing completed successfully. System in IDLE.\n" << std::endl;

   return 0;
}