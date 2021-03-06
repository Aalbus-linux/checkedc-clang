//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++98, c++03

// <filesystem>

// class directory_entry

// file_status status() const;
// file_status status(error_code const&) const noexcept;

#include "filesystem_include.hpp"
#include <type_traits>
#include <cassert>

#include "filesystem_test_helper.hpp"
#include "rapid-cxx-test.hpp"

#include "test_macros.h"

TEST_SUITE(directory_entry_status_testsuite)

TEST_CASE(test_basic) {
  using namespace fs;
  {
    const fs::directory_entry e("foo");
    std::error_code ec;
    static_assert(std::is_same<decltype(e.status()), fs::file_status>::value, "");
    static_assert(std::is_same<decltype(e.status(ec)), fs::file_status>::value, "");
    static_assert(noexcept(e.status()) == false, "");
    static_assert(noexcept(e.status(ec)) == true, "");
  }
  path TestCases[] = {StaticEnv::File, StaticEnv::Dir, StaticEnv::SymlinkToFile,
                      StaticEnv::DNE};
  for (const auto& p : TestCases) {
    const directory_entry e(p);
    std::error_code pec = GetTestEC(), eec = GetTestEC(1);
    file_status ps = fs::status(p, pec);
    file_status es = e.status(eec);
    TEST_CHECK(ps.type() == es.type());
    TEST_CHECK(ps.permissions() == es.permissions());
    TEST_CHECK(pec == eec);
  }
  for (const auto& p : TestCases) {
    const directory_entry e(p);
    file_status ps = fs::status(p);
    file_status es = e.status();
    TEST_CHECK(ps.type() == es.type());
    TEST_CHECK(ps.permissions() == es.permissions());
  }
}

TEST_SUITE_END()
