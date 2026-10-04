/**
 * @file undoLatch.hpp
 * @brief One-shot countdown latch, replacing std::latch to keep the library on C++17
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <mutex>

/**
 * @brief Blocks until a fixed number of arrivals, then opens for good.
 *
 * @details
 * Same contract as the part of std::latch this library needs (constructor,
 * count_down(), wait()), built on C++17 primitives so undoPLC does not force
 * C++20 on its consumers. std::latch was the only C++20 construct undoPLC used.
 *
 * The semantics match std::latch:
 *  - wait() blocks until the counter reaches zero and releases every waiter
 *  - wait() is idempotent and safe to call from several threads at once
 *  - once open the latch stays open
 *
 * One deliberate difference: std::latch has undefined behaviour when count_down()
 * is called more times than the initial count, which hides a double arrival
 * until it turns into a mysterious hang. Here it trips an assertion instead.
 */
class UndoLatch
{
public:
   /**
    * @brief Construct a latch expecting a given number of arrivals
    * @param expected Number of count_down() calls needed to open the latch. Must be > 0.
    */
   explicit UndoLatch(std::ptrdiff_t expected) : _remaining(expected) { assert(expected > 0 && "latch must start with a positive count"); }

   /**
    * @brief Register one arrival, opening the latch when the last one shows up
    * @param n Number of arrivals to register. Must not exceed the remaining count.
    */
   void count_down(std::ptrdiff_t n = 1)
   {
      std::lock_guard<std::mutex> lock(_mutex);
      assert(_remaining >= n && "count_down() called more times than expected");

      _remaining -= n;
      if (_remaining == 0) {
         _open = true;
         // notify_all and not notify_one: wait() releases every waiter, and with
         // a _open flag a notify_one could leave some of them blocked forever.
         _cv.notify_all();
      }
   }

   /**
    * @brief Block until the latch opens. Returns immediately if it already did.
    */
   void wait()
   {
      std::unique_lock<std::mutex> lock(_mutex);
      _cv.wait(lock, [this] { return _open; });
   }

   /**
    * @brief Check whether the latch is open, without blocking
    * @return true if the latch has already opened
    */
   bool try_wait() const
   {
      std::lock_guard<std::mutex> lock(_mutex);
      return _open;
   }

private:
   mutable std::mutex _mutex;
   std::condition_variable _cv;
   std::ptrdiff_t _remaining;
   bool _open{false};
};
