/**
 * @file undoTasks.hpp
 * @brief Core real-time task framework for Master and Worker synchronization.
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "undoLog.hpp"
#include "undoSystem.hpp"
#include "undoMutex.hpp"
#include <latch>
#include <condition_variable>
#include <undoCore/ioBus.hpp>
#include <undoCore/processImage.hpp>

class UndoWorkerTaskBase;

// Synchronization variables
struct SyncVars
{
   std::vector<UndoWorkerTaskBase*> workers;          // Vector containing all the worker' pointers
   alignas(64) std::atomic<int> activeWorkers{0};     // Counter of active workers
   std::condition_variable_any masterCv;              // Condition variable for the Master thread
   std::condition_variable_any workerCv;              // Condition variable for Worker threads
   alignas(64) std::atomic<uint64_t> cycleCounter{0}; // Shared cycle token to avoid spurious wakeups
   UndoMutex syncMutex;
};

/**
 * @brief Basic struct to make some diag
 */
struct DiagVars
{
   uint32_t jitterMax{0};
   uint32_t jitterMin{0xFFFFFFFF};
   uint32_t execMax{0};
   uint32_t execMin{0xFFFFFFFF};
};

// clang-format off
/**
 * @class UndoMasterTaskBase
 * @brief Base class responsible for fieldbus orchestration and worker synchronization.
 */
class UndoMasterTaskBase
{
public:
   UndoMasterTaskBase(uint64_t cycleTimeNs);
   virtual ~UndoMasterTaskBase();

   bool start(uint16_t prio = 99);
   void stop();
   void registerWorker(UndoWorkerTaskBase* worker);
   inline SyncVars& getSyncVars() { return _syncVars; }
   inline const DiagVars& getDiagVars() { return _diagVars; }
   inline void resetDiagVars() { _diagVars = DiagVars(); }
   inline uint16_t getCpuId() { return _cpuId; }
   inline uint16_t getPrio() { return _prio; }
   inline const std::string& getName() { return _taskName; }
   inline uint16_t getCycleUs() { return _cycleTimeNs / 1000; }
   inline uint16_t getCycleNs() { return _cycleTimeNs; }
   inline uint64_t getCurrentCycleTimeNs() const { return _currentCycleTimeNs.load(std::memory_order_acquire); }
   inline uint64_t getCurrentCycleTimeUs() const { return _currentCycleTimeNs.load(std::memory_order_acquire) / 1000ULL; }
   inline uint64_t getCurrentCycleTimeMs() const { return _currentCycleTimeNs.load(std::memory_order_acquire) / 1000000ULL; }
   void waitAllRegistered() { _registrationLatch->wait(); }
   void countDownRegistration() { _registrationLatch->count_down(); }
   void setIoBus(undoCore::IoBus* ioBus) { _ioBus = ioBus; }

protected:
   int waitCycle(timespec& nextWakeup);
   virtual void readInputBus();
   virtual void writeOutputBus();
   virtual void safeStopHandler() = 0;
   virtual void onCycleTimeout();
   virtual bool runStartup() { return true; }
   virtual void runFinish() { return; }
   void shutdownAndJoin();
   std::string _taskName{""};
   uint16_t _cpuId, _prio;
private:
   void run();

   uint64_t _cycleTimeNs;
   undoCore::IoBus *_ioBus{nullptr};
   std::atomic<bool> _running{false};
   SyncVars _syncVars;
   DiagVars _diagVars;
   std::thread _thread;
   std::atomic<uint64_t> _currentCycleTimeNs{0};   // Absolute time aligned with the cycle
   static constexpr int _STARTUP_DELAY_CYCLES = 5; // Number of cycle to wait before starting
   std::unique_ptr<std::latch> _registrationLatch; // A C++20 safe thread counter with wait method
};

/**
 * @class UndoWorkerTaskBase
 * @brief Base class for concurrent execution units (PRGs).
 */
class UndoWorkerTaskBase
{
   friend class UndoMasterTaskBase;

public:
   UndoWorkerTaskBase(UndoMasterTaskBase* master, uint16_t cpuId, uint16_t prio);
   virtual ~UndoWorkerTaskBase();

   bool start();
   void stop();
   inline const DiagVars& getDiagVars() { return _diagVars; }
   inline void resetDiagVars() { _diagVars = DiagVars(); }
   inline uint16_t getCpuId() { return _cpuId; }
   inline uint16_t getPrio() { return _prio; }
   inline const std::string& getName() { return _taskName; }
   inline void setRunning(bool running) { _running.store(running, std::memory_order_release); }
   inline uint16_t getCycleUs() { return _cycleTimeNs / 1000; }
   inline uint16_t getCycleNs() { return _cycleTimeNs; }
   inline uint64_t getCurrentCycleTimeNs() const { return _currentCycleTimeNs; }
   inline uint64_t getCurrentCycleTimeUs() const { return _currentCycleTimeNs / 1000ULL; }
   inline uint64_t getCurrentCycleTimeMs() const { return _currentCycleTimeNs / 1000000ULL; }

protected:
   virtual bool runStartup() { return true; }
   virtual bool runWork() { return true; }
   virtual void runFinish() { return; }
   void shutdownAndJoin();
   std::string _taskName{""};
   uint16_t _cpuId, _prio;
   bool _masterIsPresent{false};

private:
   void run();

   UndoMasterTaskBase* _master{nullptr};
   std::atomic<bool> _running{false};
   uint64_t _lastProcessedCycle{0}; // Tracks the last processed cycle token
   uint64_t _currentCycleTimeNs{0}; // Absolute time aligned with the cycle
   uint64_t _cycleTimeNs;
   DiagVars _diagVars;
   std::thread _thread;
};
// clang-format on