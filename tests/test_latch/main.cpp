/**
 * @file main.cpp
 * @brief Test application for UndoLatch, the C++17 stand-in for std::latch.
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoLatch.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
   std::cout << "  [" << (ok ? " OK " : "FAIL") << "] " << what << std::endl;
   if (!ok) {
      ++failures;
   }
}

// wait() releases every waiter once the latch opens.
void testOpensAndReleasesEveryWaiter()
{
   UndoLatch latch(3);
   std::atomic<int> passed{0};

   std::vector<std::thread> waiters;
   for (int i = 0; i < 3; ++i) {
      waiters.emplace_back(
         [&latch, &passed]() {
            latch.wait();
            passed.fetch_add(1);
         });
   }

   for (int i = 0; i < 3; ++i) {
      latch.count_down();
   }

   for (auto& waiter : waiters) {
      waiter.join();
   }

   check(passed.load() == 3, "3 waiters on latch(3), 3 count_down -> all released");
}

// The latch must not open early: if it did, the master task would start cycling
// before its workers registered, which is exactly what it exists to prevent.
void testDoesNotOpenEarly()
{
   UndoLatch latch(2);

   std::thread waiter([&latch]() { latch.wait(); });

   latch.count_down();
   std::this_thread::sleep_for(std::chrono::milliseconds(150));

   check(!latch.try_wait(), "latch(2) with a single count_down stays closed");

   std::thread late([&latch]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      latch.count_down();
   });

   late.join();
   waiter.join();

   check(latch.try_wait(), "latch is open once the last arrival shows up");
}

// wait() is idempotent and safe from several threads at the same time.
void testWaitIsIdempotentAndThreadSafe()
{
   UndoLatch latch(1);
   latch.count_down();

   std::atomic<int> returned{0};
   std::vector<std::thread> waiters;
   for (int i = 0; i < 8; ++i) {
      waiters.emplace_back(
         [&latch, &returned]() {
            latch.wait();
            latch.wait();  // second wait, on an already open latch
            returned.fetch_add(1);
         });
   }

   for (auto& waiter : waiters) {
      waiter.join();
   }

   check(returned.load() == 8, "repeated wait() on an open latch returns immediately");
}

// The registration handshake as UndoMasterTaskBase uses it: the master waits for
// every worker plus itself, and must observe all registrations once released.
void testRegistrationPattern()
{
   constexpr int kWorkers = 4;
   const int totalThreads = 1 + kWorkers;

   UndoLatch registration(totalThreads);
   std::atomic<int> registered{0};
   std::atomic<bool> sawEveryThread{false};

   std::thread master([&registration, &registered, &sawEveryThread]() {
      registration.wait();
      sawEveryThread.store(registered.load() == totalThreads);
   });

   for (int i = 0; i < kWorkers; ++i) {
      std::thread worker([&registration, &registered]() {
         registered.fetch_add(1);
         registration.count_down();
      });
      worker.join();
   }

   // The master registers last, as it does after setting its own affinity.
   registered.fetch_add(1);
   registration.count_down();

   master.join();

   check(sawEveryThread.load(), "waitAllRegistered() returns only after every thread registered");
}

// Concurrent arrivals must not lose a count_down.
void testConcurrentArrivals()
{
   constexpr int kThreads = 16;
   UndoLatch latch(kThreads);
   std::atomic<int> arrived{0};

   std::vector<std::thread> threads;
   for (int i = 0; i < kThreads; ++i) {
      threads.emplace_back(
         [&latch, &arrived]() {
            arrived.fetch_add(1);
            latch.count_down();
         });
   }

   latch.wait();
   for (auto& thread : threads) {
      thread.join();
   }

   check(arrived.load() == kThreads, "16 concurrent arrivals: no count_down lost");
}

}  // namespace

int main()
{
   std::cout << "UndoLatch test suite" << std::endl;

   testOpensAndReleasesEveryWaiter();
   testDoesNotOpenEarly();
   testWaitIsIdempotentAndThreadSafe();
   testRegistrationPattern();
   testConcurrentArrivals();

   if (failures == 0) {
      std::cout << "All UndoLatch tests passed." << std::endl;
      return 0;
   }

   std::cout << failures << " UndoLatch test(s) failed." << std::endl;
   return 1;
}
