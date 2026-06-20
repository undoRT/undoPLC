/**
 * @file main.cpp
 * @brief Test application for the UndoSys class
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoSystem.hpp"
#include "undoLog.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <boost/asio/io_context.hpp>

int main(int argc, char* argv[])
{
   bool logToConsole = false;
   for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--log2console") {
         logToConsole = true;
      }
   }

   // 1. Initialize the Asio context required by our EventFd engine
   boost::asio::io_context ioc;

   // 2. Safely resolve singletons now that constructor loops are clean
   UndoSys& systemManager = UndoSys::getInstance();
   UndoLog& logger = UndoLog::getInstance();

   // Register this thread
   logger.registerThread();

   // 3. Fire up the logging backend
   logger.init(ioc, logToConsole);

   std::cout << "========================================" << std::endl;
   std::cout << "    undoPLC - System Test Validation    " << std::endl;
   std::cout << "========================================" << std::endl;

   // Log the deferred system verification safely
   systemManager.logSystemStatus();

   // 4. Test Hardware Topology Discovery
   int totalCpus = systemManager.getTotalCpu();
   logger.logRT(LogDomain::PLC, LOG_INFO, "Fetching CPU Topology. Total CPUs detected: %d", totalCpus);

   const std::vector<int>& isolatedCores = systemManager.getIsolatedCpu();
   const std::vector<int>& sharedCores = systemManager.getSharedCpu();

   // Busy wait validation test
   uint32_t sleepNs = 531000;
   uint64_t ts1 = systemManager.readTsc();
   systemManager.busyWait(sleepNs);
   ts1 = systemManager.tsc2Ns(systemManager.readTsc() - ts1);
   std::cout << "First elapsed busy-wait time in uS: " << ts1 / 1000LL << std::endl;

   ts1 = systemManager.readTsc();

   // 1. Test Hardware Topology Discovery
   totalCpus = systemManager.getTotalCpu();
   std::cout << "\n[Test] Fetching CPU Topology..." << std::endl;
   std::cout << "-> Total CPUs detected: " << totalCpus << std::endl;

   std::cout << "-> Isolated Cores count: " << isolatedCores.size() << " | Cores: [ ";
   for (int core : isolatedCores) {
      std::cout << core << " ";
   }
   std::cout << "]" << std::endl;

   std::cout << "-> Shared OS Cores count: " << sharedCores.size() << " | Cores: [ ";
   for (int core : sharedCores) {
      std::cout << core << " ";
   }
   std::cout << "]" << std::endl;

   // 2. Test Frequency Modifications (Requires Root / sudo)
   if (isolatedCores.empty()) {
      std::cout << "\n[Warning] No isolated cores found in GRUB. Skipping frequency test." << std::endl;
      std::cout << "========================================" << std::endl;
      return 0;
   }

   std::cout << "\n[Test] Attempting to lock isolated cores to nominal frequency..." << std::endl;
   std::cout << "(Note: This will fail if not running with sudo/root privileges)" << std::endl;

   bool setSuccess = systemManager.setCpuNominalFrequency(isolatedCores);
   if (setSuccess) {
      std::cout << "-> SUCCESS: Isolated cores are locked. Sleeping for 5 seconds to allow verification..." << std::endl;
      std::cout << "   (You can run 'watch -n 0.1 cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor' in another terminal)"
                << std::endl;

      std::this_thread::sleep_for(std::chrono::seconds(5));

      std::cout << "\n[Test] Restoring isolated cores back to powersave..." << std::endl;
      bool resetSuccess = systemManager.resetCpuFrequency(isolatedCores);
      if (resetSuccess) {
         std::cout << "-> SUCCESS: System power states restored correctly." << std::endl;
      } else {
         std::cerr << "-> ERROR: Failed to reset some cores back to powersave!" << std::endl;
      }
   } else {
      std::cerr << "-> ERROR: Failed to configure nominal frequency. Check root permissions." << std::endl;
   }

   std::cout << "========================================" << std::endl;

   std::cout << "Elapsed time in S " << systemManager.tsc2Ns(systemManager.readTsc() - ts1) / 1000000000LL << std::endl;

   // Since we are running a manual linear test validation without background threads,
   // we can poll the execution queue once or let ioc.poll() clear the eventfd signals.
   ioc.poll();

   return 0;
}