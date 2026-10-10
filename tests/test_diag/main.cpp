/**
 * @file main.cpp
 * @brief Tests for undoDiag::Hub and the UndoMasterTaskBase::onCycleEnd() hook.
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 *
 * @details
 * The tests drive the hub exactly as the RT master will: they push Commands on
 * the observer side and call the hub through the overridden onCycleEnd(), which
 * is the same virtual the cycle loop calls after writeOutputBus(). The cycle is
 * advanced by hand instead of by the timer, so the test runs anywhere, without
 * isolated CPUs or real-time privileges.
 */

#include "undoDiag.hpp"
#include "undoTasks.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <undoCore/types.hpp>

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
   std::cout << "  [" << (ok ? " OK " : "FAIL") << "] " << what << std::endl;
   if (!ok) {
      ++failures;
   }
}

using undoDiag::Node;
using undoDiag::NodeFlags;
using undoDiag::NodeKind;
using undoDiag::Op;
using undoDiag::Quality;
using undoDiag::SampleFlags;

// ============================================================================
//  Synthetic generated layout. It mirrors what st2cpp emits: POD structs, and
//  STArray for IEC arrays, so the offsets below are the real generated offsets.
// ============================================================================

struct Inner
{
   int16_t a;
   int32_t b;
};

struct Point
{
   int16_t x;
   int16_t y;
};

struct Program
{
   int32_t speed;
   Point point;
   undoCore::STArray<Inner, 0, 2> arr;
   undoCore::STArray<undoCore::STArray<int16_t, 0, 2>, 0, 1> grid;
   undoCore::STArray<int32_t, 0, 39> big;
};

constexpr uint8_t readable = static_cast<uint8_t>(NodeFlags::Readable);
constexpr uint8_t writable = static_cast<uint8_t>(NodeFlags::Writable);
constexpr uint8_t forceable = static_cast<uint8_t>(NodeFlags::Forceable);

// clang-format off
static const Node kNodes[] = {
   // 0 root
   { "MAIN", "PLC/MAIN", undoDiag::NO_PARENT, 1, 5, static_cast<uint8_t>(NodeKind::Root), 0, 0, 0,
     offsetof(Program, speed), sizeof(Program), 0, {0, 0, 0, 0}, {0, 0, 0, 0} },
   // 1 speed
   { "speed", "PLC/MAIN/speed", 0, 0, 0, static_cast<uint8_t>(NodeKind::Scalar), static_cast<uint8_t>(readable | writable | forceable), 0, 0,
     offsetof(Program, speed), sizeof(int32_t), 0, {0, 0, 0, 0}, {0, 0, 0, 0} },
   // 2 point
   { "point", "PLC/MAIN/point", 0, 3, 2, static_cast<uint8_t>(NodeKind::Struct), readable, 0, 0,
     offsetof(Program, point), sizeof(Point), 0, {0, 0, 0, 0}, {0, 0, 0, 0} },
   // 3 point.x (read-only on purpose)
   { "x", "PLC/MAIN/point.x", 2, 0, 0, static_cast<uint8_t>(NodeKind::Scalar), readable, 0, 0,
     offsetof(Point, x), sizeof(int16_t), 0, {0, 0, 0, 0}, {0, 0, 0, 0} },
   // 4 point.y
   { "y", "PLC/MAIN/point.y", 2, 0, 0, static_cast<uint8_t>(NodeKind::Scalar), static_cast<uint8_t>(readable | writable), 0, 0,
     offsetof(Point, y), sizeof(int16_t), 0, {0, 0, 0, 0}, {0, 0, 0, 0} },
   // 5 arr  ARRAY[0..2] OF Inner
   { "arr", "PLC/MAIN/arr", 0, 6, 2, static_cast<uint8_t>(NodeKind::Array), readable, 1, 0,
     offsetof(Program, arr), sizeof(Program::arr), sizeof(Inner), {0, 0, 0, 0}, {2, 0, 0, 0} },
   // 6 arr.a
   { "a", "PLC/MAIN/arr.a", 5, 0, 0, static_cast<uint8_t>(NodeKind::Scalar), static_cast<uint8_t>(readable | writable | forceable), 0, 0,
     offsetof(Inner, a), sizeof(int16_t), 0, {0, 0, 0, 0}, {0, 0, 0, 0} },
   // 7 arr.b
   { "b", "PLC/MAIN/arr.b", 5, 0, 0, static_cast<uint8_t>(NodeKind::Scalar), static_cast<uint8_t>(readable | writable | forceable), 0, 0,
     offsetof(Inner, b), sizeof(int32_t), 0, {0, 0, 0, 0}, {0, 0, 0, 0} },
   // 8 grid ARRAY[0..1, 0..2] OF INT
   { "grid", "PLC/MAIN/grid", 0, 0, 0, static_cast<uint8_t>(NodeKind::Array), readable, 2, 0,
     offsetof(Program, grid), sizeof(Program::grid), sizeof(int16_t), {0, 0, 0, 0}, {1, 2, 0, 0} },
   // 9 big ARRAY[0..39] OF DINT
   { "big", "PLC/MAIN/big", 0, 0, 0, static_cast<uint8_t>(NodeKind::Array), readable, 1, 0,
     offsetof(Program, big), sizeof(Program::big), sizeof(int32_t), {0, 0, 0, 0}, {39, 0, 0, 0} },
};
// clang-format on

constexpr uint32_t kNodeCount = sizeof(kNodes) / sizeof(kNodes[0]);

// The hook under test: onCycleEnd() is what UndoMasterTaskBase::run() calls at
// the end of every cycle, so driving it here exercises the real path.
class HookMaster : public UndoMasterTaskBase
{
public:
   explicit HookMaster(undoDiag::Hub* hub) : UndoMasterTaskBase(1'000'000), _hub(hub) {}

   ~HookMaster() override { shutdownAndJoin(); }

   void trigger() { onCycleEnd(); }
   void triggerInputsRead() { onInputsRead(); }
   void triggerBeforeOutputsWrite() { onBeforeOutputsWrite(); }

   uint64_t cycles{0};

protected:
   // What the generated Master wires: the two force points plus process().
   void onInputsRead() override { _hub->applyForces(); }
   void onBeforeOutputsWrite() override { _hub->applyForces(); }
   void onCycleEnd() override { _hub->process(cycles++, 42); }
   void safeStopHandler() override {}
   void readInputBus() override {}
   void writeOutputBus() override {}

private:
   undoDiag::Hub* _hub;
};

undoDiag::Command makeCommand(Op op, uint32_t node)
{
   undoDiag::Command command{};
   command.op = static_cast<uint8_t>(op);
   command.node = node;
   return command;
}

template<typename T>
void setValue(undoDiag::Command& command, const T& value)
{
   command.len = sizeof(T);
   std::memcpy(command.payload, &value, sizeof(T));
}

void setIndex(undoDiag::Command& command, std::initializer_list<int32_t> indices)
{
   command.nIdx = static_cast<uint8_t>(indices.size());
   uint32_t i = 0;
   for (int32_t value : indices) {
      command.idx[i++] = value;
   }
}

template<typename T>
T sampleValue(const undoDiag::Sample& sample)
{
   T value{};
   std::memcpy(&value, sample.payload, sizeof(T));
   return value;
}

void testScalarRead()
{
   Program program{};
   undoDiag::CommandQueue commands;
   undoDiag::SampleQueue samples;
   undoDiag::Hub hub(kNodes, kNodeCount, commands, samples);
   hub.bindRoot(0, &program);
   HookMaster master(&hub);

   program.speed = 1234;
   commands.push(makeCommand(Op::Read, 1));
   master.trigger();

   undoDiag::Sample sample;
   bool got = samples.pop(sample);
   check(got && sample.node == 1 && sample.len == sizeof(int32_t), "read scalar: one 4-byte sample");
   check(sample.quality == static_cast<uint8_t>(Quality::Good), "read scalar: quality Good");
   check(sampleValue<int32_t>(sample) == 1234, "read scalar: value matches the program");
   check((sample.flags & static_cast<uint8_t>(SampleFlags::LastChunk)) != 0, "read scalar: single chunk is flagged last");
}

void testNestedStructMember()
{
   Program program{};
   undoDiag::CommandQueue commands;
   undoDiag::SampleQueue samples;
   undoDiag::Hub hub(kNodes, kNodeCount, commands, samples);
   hub.bindRoot(0, &program);
   HookMaster master(&hub);

   program.point.x = -7;
   program.point.y = 9;

   commands.push(makeCommand(Op::Read, 3));
   commands.push(makeCommand(Op::Read, 4));
   master.trigger();

   undoDiag::Sample sample;
   samples.pop(sample);
   check(sampleValue<int16_t>(sample) == -7, "read nested struct member point.x");
   samples.pop(sample);
   check(sampleValue<int16_t>(sample) == 9, "read nested struct member point.y");
}

void testArrayElementAndWholeArray()
{
   Program program{};
   for (int i = 0; i < 3; ++i) {
      program.arr[static_cast<int>(i)].a = static_cast<int16_t>(i + 1);
      program.arr[static_cast<int>(i)].b = static_cast<int32_t>(i * 10);
   }
   undoDiag::CommandQueue commands;
   undoDiag::SampleQueue samples;
   undoDiag::Hub hub(kNodes, kNodeCount, commands, samples);
   hub.bindRoot(0, &program);
   HookMaster master(&hub);

   undoDiag::Command element = makeCommand(Op::Read, 7); // arr.b
   setIndex(element, {1});
   commands.push(element);

   undoDiag::Command whole = makeCommand(Op::Read, 5); // arr
   commands.push(whole);

   master.trigger();

   undoDiag::Sample sample;
   samples.pop(sample);
   check(sampleValue<int32_t>(sample) == 10, "read array element arr[1].b");

   // The whole arr is 3 * sizeof(Inner) = 24 bytes, still one chunk.
   samples.pop(sample);
   check(sample.len == sizeof(Program::arr), "read whole array: 24 bytes in one chunk");
   check(std::memcmp(sample.payload, program.arr.data.data(), sizeof(Program::arr)) == 0, "read whole array: bytes match memory");
}

void testMultidimensionalArray()
{
   Program program{};
   program.grid[1][2] = 99;
   undoDiag::CommandQueue commands;
   undoDiag::SampleQueue samples;
   undoDiag::Hub hub(kNodes, kNodeCount, commands, samples);
   hub.bindRoot(0, &program);
   HookMaster master(&hub);

   undoDiag::Command command = makeCommand(Op::Read, 8);
   setIndex(command, {1, 2});
   commands.push(command);
   master.trigger();

   undoDiag::Sample sample;
   samples.pop(sample);
   check(sampleValue<int16_t>(sample) == 99, "read 2D element grid[1][2]");
}

void testOutOfRangeIndexRejected()
{
   Program program{};
   undoDiag::CommandQueue commands;
   undoDiag::SampleQueue samples;
   undoDiag::Hub hub(kNodes, kNodeCount, commands, samples);
   hub.bindRoot(0, &program);
   HookMaster master(&hub);

   undoDiag::Command command = makeCommand(Op::Read, 7);
   setIndex(command, {3}); // valid range is 0..2
   commands.push(command);
   master.trigger();

   undoDiag::Sample sample;
   samples.pop(sample);
   check(sample.quality == static_cast<uint8_t>(Quality::Bad), "out-of-range index is rejected");
   check((sample.flags & static_cast<uint8_t>(SampleFlags::Ack)) != 0, "rejection is reported as an Ack");
}

void testWriteAllowedAndRejected()
{
   Program program{};
   undoDiag::CommandQueue commands;
   undoDiag::SampleQueue samples;
   undoDiag::Hub hub(kNodes, kNodeCount, commands, samples);
   hub.bindRoot(0, &program);
   HookMaster master(&hub);

   undoDiag::Command allowed = makeCommand(Op::Write, 4); // point.y is writable
   setValue<int16_t>(allowed, 55);
   commands.push(allowed);

   undoDiag::Command denied = makeCommand(Op::Write, 3); // point.x is read-only
   setValue<int16_t>(denied, 1);
   commands.push(denied);

   undoDiag::Command bounds = makeCommand(Op::Write, 4);
   setValue<int16_t>(bounds, 2);
   bounds.offset = sizeof(int16_t); // one byte past the end
   commands.push(bounds);

   master.trigger();

   undoDiag::Sample sample;
   samples.pop(sample);
   check(sample.quality == static_cast<uint8_t>(Quality::Good), "write to a writable node is accepted");
   check(program.point.y == 55, "write actually stored the value");

   samples.pop(sample);
   check(sample.quality == static_cast<uint8_t>(Quality::Bad), "write to a read-only node is rejected");
   check(program.point.x == 0, "rejected write did not touch memory");

   samples.pop(sample);
   check(sample.quality == static_cast<uint8_t>(Quality::Bad), "write past the node end is rejected");
}

void testForcePersistsAcrossCycles()
{
   Program program{};
   undoDiag::CommandQueue commands;
   undoDiag::SampleQueue samples;
   undoDiag::Hub hub(kNodes, kNodeCount, commands, samples);
   hub.bindRoot(0, &program);
   HookMaster master(&hub);

   undoDiag::Command force = makeCommand(Op::Force, 1);
   setValue<int32_t>(force, 777);
   commands.push(force);
   master.trigger();

   undoDiag::Sample sample;
   samples.pop(sample);
   check(sample.quality == static_cast<uint8_t>(Quality::Forced), "force is acknowledged as Forced");
   check(program.speed == 777, "force stored the value");

   // The program overwrites the variable, as it would every cycle.
   program.speed = 0;
   master.trigger();

   check(program.speed == 777, "force is re-applied after the program overwrites it");

   commands.push(makeCommand(Op::Read, 1));
   master.trigger();
   samples.pop(sample);
   check(sampleValue<int32_t>(sample) == 777, "read reports the forced value");
   check(sample.quality == static_cast<uint8_t>(Quality::Forced), "read quality is Forced while forced");

   commands.push(makeCommand(Op::Release, 1));
   master.trigger();
   samples.pop(sample);

   program.speed = 5;
   master.trigger();
   check(program.speed == 5, "after release the program value survives");
}

// The two Master hooks are what make a force *physical*: applied after the input
// image (so the logic sees it this cycle) and restored before the bus copy (so
// the fieldbus takes it instead of the value the logic just computed). This test
// walks the exact cycle order UndoMasterTaskBase::run() executes.
void testPhysicalForceAcrossFullCycle()
{
   Program program{};
   undoDiag::CommandQueue commands;
   undoDiag::SampleQueue samples;
   undoDiag::Hub hub(kNodes, kNodeCount, commands, samples);
   hub.bindRoot(0, &program);
   HookMaster master(&hub);

   // The observer forces speed while the PLC is running.
   undoDiag::Command force = makeCommand(Op::Force, 1);
   setValue<int32_t>(force, 777);
   commands.push(force);
   master.trigger(); // process(): the Force is stored (and applied immediately)

   undoDiag::Sample sample;

   // Cycle N+1, in run() order:
   // 1. the input image is read and overwrites the variable (simulated),
   program.speed = 0;
   // 2. onInputsRead() restores the force: the logic computes with 777.
   master.triggerInputsRead();
   check(program.speed == 777, "forced input is visible to the logic this cycle");

   // 3. the logic runs and would overwrite the variable,
   program.speed = 1;
   // 4. onBeforeOutputsWrite() restores the force before the bus copy: a
   //    writeOutputBus() right now would carry 777 to the process, not 1.
   master.triggerBeforeOutputsWrite();
   check(program.speed == 777, "forced output is restored before the bus copy");

   // 5. process() serves observers: a read reports the forced value. Drain the
   //    force ack first, so the read sample is the one popped below.
   undoDiag::Sample junk;
   while (samples.pop(junk)) {
   }
   commands.push(makeCommand(Op::Read, 1));
   master.trigger();
   samples.pop(sample);
   check(sampleValue<int32_t>(sample) == 777, "end-of-cycle read reports the forced value");
   check(sample.quality == static_cast<uint8_t>(Quality::Forced), "end-of-cycle read quality is Forced");

   // 6. release drops the force and the program value survives the cycle.
   commands.push(makeCommand(Op::Release, 1));
   master.trigger();
   samples.pop(sample);
   program.speed = 9;
   master.triggerInputsRead();
   check(program.speed == 9, "after release the input image is no longer overridden");
}

void testForceElement()
{
   Program program{};
   undoDiag::CommandQueue commands;
   undoDiag::SampleQueue samples;
   undoDiag::Hub hub(kNodes, kNodeCount, commands, samples);
   hub.bindRoot(0, &program);
   HookMaster master(&hub);

   undoDiag::Command first = makeCommand(Op::Force, 7); // arr[2].b
   setValue<int32_t>(first, 31337);
   setIndex(first, {2});
   commands.push(first);

   // A second element of the same array must not collide with the first.
   undoDiag::Command second = makeCommand(Op::Force, 7); // arr[0].b
   setValue<int32_t>(second, 11);
   setIndex(second, {0});
   commands.push(second);
   master.trigger();

   program.arr[2].b = 0;
   program.arr[0].b = 0;
   program.arr[1].b = 5;
   master.trigger();

   check(program.arr[2].b == 31337, "force on one array element is re-applied");
   check(program.arr[0].b == 11, "a second element of the same array is forced independently");
   check(program.arr[1].b == 5, "unforced elements are left alone");

   commands.push(makeCommand(Op::Release, 7)); // release by node drops every element
   master.trigger();
   program.arr[0].b = 5;
   program.arr[2].b = 5;
   master.trigger();
   check(program.arr[0].b == 5 && program.arr[2].b == 5, "release by node drops every forced element");
}

void testSubscriptionCycle()
{
   Program program{};
   undoDiag::CommandQueue commands;
   undoDiag::SampleQueue samples;
   undoDiag::Hub hub(kNodes, kNodeCount, commands, samples);
   hub.bindRoot(0, &program);
   HookMaster master(&hub);

   commands.push(makeCommand(Op::Subscribe, 1));
   master.trigger();

   undoDiag::Sample sample;
   samples.pop(sample); // subscribe ack
   check(sample.flags == static_cast<uint8_t>(SampleFlags::Ack), "subscribe is acknowledged");

   // A fresh subscription publishes immediately, in the same cycle as its ack.
   samples.pop(sample);
   check(sampleValue<int32_t>(sample) == 0, "subscribe publishes the current value right away");

   program.speed = 8;
   master.trigger();
   bool got = samples.pop(sample);
   check(got && sample.seq == 0 && sampleValue<int32_t>(sample) == 8, "subscription publishes every cycle");

   commands.push(makeCommand(Op::Unsubscribe, 1));
   master.trigger();
   samples.pop(sample); // unsubscribe ack
   check(!samples.pop(sample), "the unsubscribe cycle does not publish again");

   master.trigger();
   check(!samples.pop(sample), "no more publishes after unsubscribe");
}

void testChunking()
{
   Program program{};
   for (int i = 0; i < 40; ++i) {
      program.big[static_cast<int>(i)] = i * 3;
   }
   undoDiag::CommandQueue commands;
   undoDiag::SampleQueue samples;
   undoDiag::Hub hub(kNodes, kNodeCount, commands, samples);
   hub.bindRoot(0, &program);
   HookMaster master(&hub);

   commands.push(makeCommand(Op::Read, 9)); // big: 160 bytes
   master.trigger();

   uint8_t rebuilt[sizeof(Program::big)];
   undoDiag::Sample sample;
   uint32_t produced = 0;
   uint32_t chunks = 0;
   while (samples.pop(sample)) {
      check(sample.offset == produced, "chunk offset is contiguous");
      std::memcpy(rebuilt + sample.offset, sample.payload, sample.len);
      produced += sample.len;
      ++chunks;
      if ((sample.flags & static_cast<uint8_t>(SampleFlags::LastChunk)) != 0) {
         break;
      }
   }

   check(chunks == 3, "160 bytes are split into 3 chunks of 64/64/32");
   check(produced == sizeof(Program::big), "reassembled length matches");
   check(std::memcmp(rebuilt, program.big.data.data(), sizeof(Program::big)) == 0, "reassembled bytes match memory");
}

void testPathLookupAndBinding()
{
   Program program{};
   undoDiag::CommandQueue commands;
   undoDiag::SampleQueue samples;
   undoDiag::Hub hub(kNodes, kNodeCount, commands, samples);

   check(hub.findByPath("PLC/MAIN/point.y") == 4, "findByPath resolves a nested path");
   check(hub.findByPath("PLC/MAIN/nope") == undoDiag::NO_NODE, "findByPath misses cleanly");
   check(!hub.bindRoot(1, &program), "bindRoot refuses a non-root node");

   // Resolving before binding must fail cleanly, not dereference null.
   HookMaster master(&hub);
   commands.push(makeCommand(Op::Read, 1));
   master.trigger();
   undoDiag::Sample sample;
   samples.pop(sample);
   check(sample.quality == static_cast<uint8_t>(Quality::Bad), "read on an unbound root is rejected");
}

void testForceTableFull()
{
   Program program{};
   undoDiag::CommandQueue commands;
   undoDiag::SampleQueue samples;
   undoDiag::Hub hub(kNodes, kNodeCount, commands, samples);
   hub.bindRoot(0, &program);
   HookMaster master(&hub);

   // Force the same node more than MAX_FORCES times: upsert must reuse the slot.
   for (uint32_t i = 0; i < undoDiag::MAX_FORCES + 5; ++i) {
      undoDiag::Command force = makeCommand(Op::Force, 1);
      setValue<int32_t>(force, static_cast<int32_t>(i));
      commands.push(force);
      master.trigger();
   }

   undoDiag::Sample sample;
   while (samples.pop(sample)) {
   }
   commands.push(makeCommand(Op::Read, 1));
   master.trigger();
   samples.pop(sample);
   check(sample.quality == static_cast<uint8_t>(Quality::Forced), "repeated force on one node keeps it forced");
   check(sampleValue<int32_t>(sample) == static_cast<int32_t>(undoDiag::MAX_FORCES + 4), "last force value wins");
}

} // namespace

int main()
{
   std::cout << "undoDiag test suite" << std::endl;

   testScalarRead();
   testNestedStructMember();
   testArrayElementAndWholeArray();
   testMultidimensionalArray();
   testOutOfRangeIndexRejected();
   testWriteAllowedAndRejected();
   testForcePersistsAcrossCycles();
   testPhysicalForceAcrossFullCycle();
   testForceElement();
   testSubscriptionCycle();
   testChunking();
   testPathLookupAndBinding();
   testForceTableFull();

   if (failures == 0) {
      std::cout << "All undoDiag tests passed." << std::endl;
      return 0;
   }

   std::cout << failures << " undoDiag test(s) failed." << std::endl;
   return 1;
}
