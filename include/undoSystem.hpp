/**
 * @file undoSystem.h
 * @brief Header file for the basic undoSystem
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <string>
#include <vector>
#include <x86intrin.h>
#include <cpuid.h>
#include <cstdint>
#include "undoLog.hpp"

class UndoSys
{
public:
   // Deleted copy constructor to prevent cloning the singleton instance.
   UndoSys(const UndoSys&) = delete;
   // Deleted assignment operator to prevent copying the singleton instance.
   UndoSys& operator=(const UndoSys&) = delete;
   // Deleted move constructor.
   UndoSys(UndoSys&&) = delete;
   // Deleted move assignment operator.
   UndoSys& operator=(UndoSys&&) = delete;
   // Singleton
   static UndoSys& getInstance()
   {
      // Guaranteed to be thread-safe and initialized only once since C++11
      static UndoSys instance;
      return instance;
   }

   // Frequency Management
   bool setCpuNominalFrequency(const std::vector<int>& cores);
   bool resetCpuFrequency(const std::vector<int>& cores);
   bool initTscFrequency();
   inline uint64_t readTsc(unsigned int* coreId) const { return __rdtscp(coreId); }
   inline uint64_t readTsc() const
   {
      unsigned int dummy = 0;
      return __rdtscp(&dummy);
   }
   uint64_t tsc2Ns(uint64_t tscCycles) const;

   // Cpus Topology Discovery
   const std::vector<int>& getIsolatedCpu();
   int getTotalCpu();
   const std::vector<int>& getSharedCpu();

   // Helper methods
   void busyWait(uint64_t ns);
   void logSystemStatus();

private:
   // Private constructor to ensure instantiation only via getInstance()
   UndoSys();
   ~UndoSys() = default;

   // Members
   std::vector<int> _isolatedCores;   // Vector of isolated cores (empty if there are not isolated cores)
   bool _isolatedCoresChecked{false}; // True if _isolatedCores already done
   int _totNumCores{-1};              // Total number of cores
   std::vector<int> _sharedCores;     // Vector of shared cores (empty if there are not shared cores)
   bool _sharedCoresChecked{false};   // True if _sharedCores already done
   uint64_t _tscFrequencyHz{0};       // Global hardware constant for conversion

   // Methods
   bool writeSysfsAttribute(const std::string& path, const std::string& value);
};