/**
 * @file undoMutex.hpp
 * @brief C++ style wrapper to use pthread_mutext_t like std::mutex, to have PTHREAD_PRIO_INHERIT
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once
#include <pthread.h>
#include <system_error>

/**
 * @brief Wrapper class for the usage of pthread_mutex with PTHREAD_PRIO_INHERIT
 * Since std::mutex is not PTHREAD_PRIO_INHERIT, just use this simple wrapper.
 */
class UndoMutex
{
public:
   UndoMutex()
   {
      pthread_mutexattr_t attr;
      pthread_mutexattr_init(&attr);
      pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);

      int rc = pthread_mutex_init(&_mutex, &attr);
      pthread_mutexattr_destroy(&attr);

      if (rc != 0) {
         throw std::system_error(rc, std::generic_category(), "pthread_mutex_init (PRIO_INHERIT) failed");
      }
   }

   ~UndoMutex() { pthread_mutex_destroy(&_mutex); }

   UndoMutex(const UndoMutex&) = delete;
   UndoMutex& operator=(const UndoMutex&) = delete;

   void lock()
   {
      int rc = pthread_mutex_lock(&_mutex);
      if (rc != 0) {
         throw std::system_error(rc, std::generic_category(), "pthread_mutex_lock failed");
      }
   }

   void unlock() { pthread_mutex_unlock(&_mutex); }

   bool try_lock() { return pthread_mutex_trylock(&_mutex) == 0; }

   using native_handle_type = pthread_mutex_t*;
   native_handle_type native_handle() { return &_mutex; }

private:
   pthread_mutex_t _mutex;
};