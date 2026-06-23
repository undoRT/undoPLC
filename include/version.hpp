/**
 * @file version.hpp
 * @brief Version information for undoPLC
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026 Salvatore Bamundo
 */

#pragma once

#include <string>

// Version numbers following Semantic Versioning (semver.org)
#define UNDOPLC_VERSION_MAJOR  0
#define UNDOPLC_VERSION_MINOR  1
#define UNDOPLC_VERSION_PATCH  1
#define UNDOPLC_VERSION_PREREL ""

// Helper macros for stringification (workaround for MSVC)
#define UNDOPLC_STRINGIFY_IMPL(x) #x
#define UNDOPLC_STRINGIFY(x)      UNDOPLC_STRINGIFY_IMPL(x)

// Version string for display
#define UNDOPLC_VERSION_STRING \
   UNDOPLC_STRINGIFY(UNDOPLC_VERSION_MAJOR) \
   "." UNDOPLC_STRINGIFY(UNDOPLC_VERSION_MINOR) "." UNDOPLC_STRINGIFY(UNDOPLC_VERSION_PATCH) UNDOPLC_VERSION_PREREL

// Build date (automatically updated by compiler)
#define UNDOPLC_BUILD_DATE __DATE__ " " __TIME__

// Compiler information
#ifdef __clang__
#define UNDOPLC_COMPILER "Clang " __clang_version__
#elif defined(__GNUC__)
#define UNDOPLC_COMPILER "GCC " __VERSION__
#elif defined(_MSC_VER)
// For MSVC, _MSC_VER is a number, need to stringify it
#define UNDOPLC_COMPILER "MSVC " UNDOPLC_STRINGIFY(_MSC_VER)
#else
#define UNDOPLC_COMPILER "Unknown"
#endif

/**
 * @brief Get version as a string
 * @return Version string (e.g., "1.0.0")
 */
inline std::string getVersion()
{
   return UNDOPLC_VERSION_STRING;
}

/**
 * @brief Get full version information
 * @return Detailed version string with compiler and build date
 */
inline std::string getFullVersion()
{
   return std::string("undoPLC version ") + UNDOPLC_VERSION_STRING + " (" + UNDOPLC_COMPILER + ", built " + UNDOPLC_BUILD_DATE + ")";
}

/**
 * @brief Get version numbers as tuple
 * @return Struct with major, minor, patch
 */
struct Version
{
   int major = UNDOPLC_VERSION_MAJOR;
   int minor = UNDOPLC_VERSION_MINOR;
   int patch = UNDOPLC_VERSION_PATCH;

   std::string toString() const { return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch); }
};

inline Version getVersionNumbers()
{
   return Version{};
}