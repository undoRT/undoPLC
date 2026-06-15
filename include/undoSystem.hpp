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

class UndoSys
{
public:
   UndoSys() = default;
   ~UndoSys() = default;
   bool setCpuNominalFrequency(const std::vector<int>& cores);
   bool resetCpuFrequency(const std::vector<int>& cores);
   const std::vector<int>& getIsolatedCpu();
   int getTotalCpu();
   const std::vector<int>& getSharedCpu();

private:
   // Members
   std::vector<int> _isolatedCores;   // Vector of isolated cores (empty if there are not isolated cores)
   bool _isolatedCoresChecked{false}; // True if _isolatedCores already done
   int _totNumCores{-1};              // Total number of cores
   std::vector<int> _sharedCores;     // Vector of shared cores (empty if there are not shared cores)
   bool _sharedCoresChecked{false};   // True if _sharedCores already done

   // Methods
   bool writeSysfsAttribute(const std::string& path, const std::string& value);
};