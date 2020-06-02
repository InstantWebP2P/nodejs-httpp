// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/stack.h"

#include <memory>
#include <ostream>

#include "include/v8config.h"
#include "src/base/platform/platform.h"
#include "testing/gtest/include/gtest/gtest.h"

#if V8_OS_LINUX && (V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64)
#include <xmmintrin.h>
#endif

namespace cppgc {
namespace internal {

namespace {

class GCStackTest : public ::testing::Test {
 protected:
  void SetUp() override {
    stack_.reset(new Stack(v8::base::Stack::GetStackStart()));
  }

  void TearDown() override { stack_.reset(); }

  Stack* GetStack() const { return stack_.get(); }

 private:
  std::unique_ptr<Stack> stack_;
};

}  // namespace

TEST_F(GCStackTest, IsOnStackForStackValue) {
  void* dummy;
  EXPECT_TRUE(GetStack()->IsOnStack(&dummy));
}

TEST_F(GCStackTest, IsOnStackForHeapValue) {
  auto dummy = std::make_unique<int>();
  EXPECT_FALSE(GetStack()->IsOnStack(dummy.get()));
}

#ifdef CPPGC_SUPPORTS_CONSERVATIVE_STACK_SCAN

namespace {

class StackScanner final : public StackVisitor {
 public:
  struct Container {
    std::unique_ptr<int> value;
  };

  StackScanner() : container_(new Container{}) {
    container_->value = std::make_unique<int>();
  }

  void VisitPointer(const void* address) final {
    if (address == container_->value.get()) found_ = true;
  }

  void Reset() { found_ = false; }
  bool found() const { return found_; }
  int* needle() const { return container_->value.get(); }

 private:
  std::unique_ptr<Container> container_;
  bool found_ = false;
};

}  // namespace

TEST_F(GCStackTest, IteratePointersFindsOnStackValue) {
  auto scanner = std::make_unique<StackScanner>();

  // No check that the needle is initially not found as on some platforms it
  // may be part of the redzone or temporaries after setting it up throuhg
  // StackScanner.
  {
    int* volatile tmp = scanner->needle();
    USE(tmp);
    GetStack()->IteratePointers(scanner.get());
    EXPECT_TRUE(scanner->found());
  }
}

TEST_F(GCStackTest, IteratePointersFindsOnStackValuePotentiallyUnaligned) {
  auto scanner = std::make_unique<StackScanner>();

  // No check that the needle is initially not found as on some platforms it
  // may be part of the redzone or temporaries after setting it up throuhg
  // StackScanner.
  {
    char a = 'c';
    USE(a);
    int* volatile tmp = scanner->needle();
    USE(tmp);
    GetStack()->IteratePointers(scanner.get());
    EXPECT_TRUE(scanner->found());
  }
}

namespace {

void RecursivelyPassOnParameter(int* volatile p1, int* volatile p2,
                                int* volatile p3, int* volatile p4,
                                int* volatile p5, int* volatile p6,
                                int* volatile p7, int* volatile p8,
                                Stack* stack, StackVisitor* visitor) {
  if (p1) {
    RecursivelyPassOnParameter(nullptr, p1, nullptr, nullptr, nullptr, nullptr,
                               nullptr, nullptr, stack, visitor);
  } else if (p2) {
    RecursivelyPassOnParameter(nullptr, nullptr, p2, nullptr, nullptr, nullptr,
                               nullptr, nullptr, stack, visitor);
  } else if (p3) {
    RecursivelyPassOnParameter(nullptr, nullptr, nullptr, p3, nullptr, nullptr,
                               nullptr, nullptr, stack, visitor);
  } else if (p4) {
    RecursivelyPassOnParameter(nullptr, nullptr, nullptr, nullptr, p4, nullptr,
                               nullptr, nullptr, stack, visitor);
  } else if (p5) {
    RecursivelyPassOnParameter(nullptr, nullptr, nullptr, nullptr, nullptr, p5,
                               nullptr, nullptr, stack, visitor);
  } else if (p6) {
    RecursivelyPassOnParameter(nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, p6, nullptr, stack, visitor);
  } else if (p7) {
    RecursivelyPassOnParameter(nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, nullptr, p7, stack, visitor);
  } else if (p8) {
    stack->IteratePointers(visitor);
  }
}

}  // namespace

TEST_F(GCStackTest, IteratePointersFindsParameter) {
  auto scanner = std::make_unique<StackScanner>();
  // No check that the needle is initially not found as on some platforms it
  // may be part of the redzone or temporaries after setting it up throuhg
  // StackScanner.
  RecursivelyPassOnParameter(nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr, nullptr, scanner->needle(), GetStack(),
                             scanner.get());
  EXPECT_TRUE(scanner->found());
}

TEST_F(GCStackTest, IteratePointersFindsParameterInNestedFunction) {
  auto scanner = std::make_unique<StackScanner>();
  // No check that the needle is initially not found as on some platforms it
  // may be part of the redzone or temporaries after setting it up throuhg
  // StackScanner.
  RecursivelyPassOnParameter(scanner->needle(), nullptr, nullptr, nullptr,
                             nullptr, nullptr, nullptr, nullptr, GetStack(),
                             scanner.get());
  EXPECT_TRUE(scanner->found());
}

// The following test uses inline assembly and has been checked to work on clang
// to verify that the stack-scanning trampoline pushes callee-saved registers.
//
// The test uses a macro loop as asm() can only be passed string literals.
#ifdef __clang__
#ifdef V8_TARGET_ARCH_X64
#ifdef V8_OS_WIN

// Excluded from test: rbp
#define FOR_ALL_CALLEE_SAVED_REGS(V) \
  V("rdi")                           \
  V("rsi")                           \
  V("rbx")                           \
  V("r12")                           \
  V("r13")                           \
  V("r14")                           \
  V("r15")

#else  // !V8_OS_WIN

// Excluded from test: rbp
#define FOR_ALL_CALLEE_SAVED_REGS(V) \
  V("rbx")                           \
  V("r12")                           \
  V("r13")                           \
  V("r14")                           \
  V("r15")

#endif  // !V8_OS_WIN
#endif  // V8_TARGET_ARCH_X64
#endif  // __clang__

#ifdef FOR_ALL_CALLEE_SAVED_REGS

TEST_F(GCStackTest, IteratePointersFindsCalleeSavedRegisters) {
  auto scanner = std::make_unique<StackScanner>();

  // No check that the needle is initially not found as on some platforms it
  // may be part of the redzone or temporaries after setting it up throuhg
  // StackScanner.

// First, clear all callee-saved registers.
#define CLEAR_REGISTER(reg) asm("mov $0, %%" reg : : : reg);

  FOR_ALL_CALLEE_SAVED_REGS(CLEAR_REGISTER)
#undef CLEAR_REGISTER

  // Keep local raw pointers to keep instruction sequences small below.
  auto* local_stack = GetStack();
  auto* local_scanner = scanner.get();

// Moves |local_scanner->needle()| into a callee-saved register, leaving the
// callee-saved register as the only register referencing the needle.
// (Ignoring implementation-dependent dirty registers/stack.)
#define KEEP_ALIVE_FROM_CALLEE_SAVED(reg)                                \
  local_scanner->Reset();                                                \
  /* This moves the temporary into the calee-saved register. */          \
  asm("mov %0, %%" reg : : "r"(local_scanner->needle()) : reg);          \
  /* Register is unprotected from here till the actual invocation. */    \
  local_stack->IteratePointers(local_scanner);                           \
  EXPECT_TRUE(local_scanner->found())                                    \
      << "pointer in callee-saved register not found. register: " << reg \
      << std::endl;                                                      \
  /* Clear out the register again */                                     \
  asm("mov $0, %%" reg : : : reg);

  FOR_ALL_CALLEE_SAVED_REGS(KEEP_ALIVE_FROM_CALLEE_SAVED)
#undef KEEP_ALIVE_FROM_CALLEE_SAVED
#undef FOR_ALL_CALLEE_SAVED_REGS
}
#endif  // FOR_ALL_CALLEE_SAVED_REGS

#if V8_OS_LINUX && (V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64)
class CheckStackAlignmentVisitor final : public StackVisitor {
 public:
  void VisitPointer(const void*) final {
    float f[4] = {0.};
    volatile auto xmm = ::_mm_load_ps(f);
    USE(xmm);
  }
};

TEST_F(GCStackTest, StackAlignment) {
  auto checker = std::make_unique<CheckStackAlignmentVisitor>();
  GetStack()->IteratePointers(checker.get());
}
#endif  // V8_OS_LINUX && (V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64)

#endif  // CPPGC_SUPPORTS_CONSERVATIVE_STACK_SCAN

}  // namespace internal
}  // namespace cppgc
