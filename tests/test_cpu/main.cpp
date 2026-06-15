/**
 * @file main.cpp
 * @brief Test application for the UndoSys class
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoSystem.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main()
{
   UndoSys systemManager;

   std::cout << "========================================" << std::endl;
   std::cout << "    undoPLC - System Test Validation    " << std::endl;
   std::cout << "========================================" << std::endl;

   // 1. Test Hardware Topology Discovery
   int totalCpus = systemManager.getTotalCpu();
   std::cout << "\n[Test] Fetching CPU Topology..." << std::endl;
   std::cout << "-> Total CPUs detected: " << totalCpus << std::endl;

   const std::vector<int>& isolatedCores = systemManager.getIsolatedCpu();
   std::cout << "-> Isolated Cores count: " << isolatedCores.size() << " | Cores: [ ";
   for (int core : isolatedCores) {
      std::cout << core << " ";
   }
   std::cout << "]" << std::endl;

   const std::vector<int>& sharedCores = systemManager.getSharedCpu();
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
   return 0;
}