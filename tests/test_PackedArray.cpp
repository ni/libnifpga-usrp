/*
 * Copyright (c) 2020 National Instruments
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include "../src/PackedArray.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

using namespace nirio;

template <typename T> struct test_case {
  std::vector<T> native;
  std::vector<uint32_t> array;
};

// clang-format off
test_case<uint8_t> test_cases_bool[] = {
    {
        { 0x1 },
        { 0x1 }
    },
    {
        { 0x1, 0x0, 0x1 },
        { 0x5 }
    },
    {
        { 0x0, 0x1, 0x0, 0x1, 0x1, 0x0, 0x1, 0x0,
          0x0, 0x1, 0x0, 0x1, 0x1, 0x0, 0x1, 0x0,
          0x0, 0x1, 0x0, 0x1, 0x1, 0x0, 0x1, 0x0,
          0x0, 0x1, 0x0, 0x1, 0x1, 0x0, 0x1, 0x0,
          0x1, },
        { 0x5a5a5a5a, 0x80000000 }
    },
};

test_case<uint8_t> test_cases_u8[] = {
    {
        { 0xaa },
        { 0x000000aa }
    },
    {
        { 0xaa, 0xbb },
        { 0x0000aabb }
    },
    {
        { 0xaa, 0xbb, 0xcc },
        { 0x00aabbcc }
    },
    {
        { 0xaa, 0xbb, 0xcc, 0xdd },
        { 0xaabbccdd }
    },
    {
        { 0xaa, 0xbb, 0xcc, 0xdd, 0xee },
        { 0xaabbccdd, 0xee000000 }
    },
};

test_case<uint16_t> test_cases_u16[] = {
    {
        { 0xaaaa },
        { 0x0000aaaa }
    },
    {
        { 0xaaaa, 0xbbbb },
        { 0xaaaabbbb }
    },
    {
        { 0xaaaa, 0xbbbb, 0xcccc },
        { 0xaaaabbbb, 0xcccc0000 }
    },
    {
        { 0xaaaa, 0xbbbb, 0xcccc, 0xdddd },
        { 0xaaaabbbb, 0xccccdddd }
    },
};

test_case<uint32_t> test_cases_u32[] = {
    {
        { 0xaaaaaaaa },
        { 0xaaaaaaaa }
    },
    {
        { 0xaaaaaaaa, 0xbbbbbbbb },
        { 0xaaaaaaaa, 0xbbbbbbbb }
    },
};

// TODO: validate
test_case<uint64_t> test_cases_u64[] = {
    {
        { 0xaaaaaaaabbbbbbbb },
        { 0xaaaaaaaa, 0xbbbbbbbb }
    },
    {
        { 0xaaaaaaaabbbbbbbb, 0xccccccccdddddddd },
        { 0xaaaaaaaa, 0xbbbbbbbb, 0xcccccccc, 0xdddddddd }
    },
};

test_case<float> test_cases_sgl[] = {
    {
        { 13.37f },
        { 0x4155eb85 }
    },
    {
        { 13.37f, 1337.0f },
        { 0x4155eb85, 0x44a72000 }
    },
    {
        { 13.37f, 1337.0f, 1.0f/0.0f },
        { 0x4155eb85, 0x44a72000, 0x7f800000 }
    },
};

test_case<double> test_cases_dbl[] = {
    {
        { 13.37 },
        { 0x402abd70, 0xa3d70a3d }
    },
    {
        { 13.37, 0.2 },
        { 0x402abd70, 0xa3d70a3d, 0x3fc99999, 0x9999999a }
    },
};
// clang-format on

template <typename T, int type_bits>
bool run_pack_test(const struct test_case<T> &test) {
  bool pass = true;
  const size_t packedSize = packedArraySize<type_bits>(test.native.size());
  std::unique_ptr<uint32_t[]> dest;

  if (packedSize != test.array.size()) {
    printf("packed size: expected: %zu, got: %zu\n", test.array.size(),
           packedSize);
    pass = false;
  }

  dest.reset(new uint32_t[packedSize]);
  memset(dest.get(), 0, packedSize * sizeof(uint32_t));
  packArray<type_bits>(dest.get(), &test.native[0], test.native.size());

  for (size_t i = 0; i < test.array.size(); i++) {
    if (dest.get()[i] != test.array[i]) {
      printf("idx: %zu, expected: %08x, got %08x\n", i, test.array[i],
             dest.get()[i]);
      pass = false;
    }
  }

  return pass;
}

template <typename T, int type_bits>
bool run_unpack_test(const struct test_case<T> &test) {
  bool pass = true;
  std::vector<uint32_t> array = test.array;
  std::unique_ptr<T[]> dest;

  dest.reset(new T[test.native.size()]);

  unpackArray<type_bits>(&array[0], dest.get(), test.native.size());

  for (size_t i = 0; i < test.native.size(); i++) {
    if (dest.get()[i] != test.native[i]) {
      printf("idx: %zu, expected: %08x, got %08x\n", i, test.native[i],
             dest.get()[i]);
      pass = false;
    }
  }

  return pass;
}

template <typename T> struct logical_bits {
  static const int bits = std::numeric_limits<T>::digits;
};

template <> struct logical_bits<float> { static const int bits = 32; };

template <> struct logical_bits<double> { static const int bits = 64; };

template <typename T, int type_bits = logical_bits<T>::bits>
bool run_test(const char *name, int i, const struct test_case<T> &test) {
  bool pack = run_pack_test<T, type_bits>(test);
  bool unpack = run_unpack_test<T, type_bits>(test);

  printf("%s.%d: pack: %s, unpack: %s\n", name, i, pack ? "ok" : "FAIL",
         unpack ? "ok" : "FAIL");

  return pack && unpack;
}

int main() {
  bool ok = true;
  int i;

  i = 0;
  for (auto &&test : test_cases_bool)
    ok &= run_test<uint8_t, 1>("bool", i++, test);

  i = 0;
  for (auto &&test : test_cases_u8)
    ok &= run_test("u8", i++, test);

  i = 0;
  for (auto &&test : test_cases_u16)
    ok &= run_test("u16", i++, test);

  i = 0;
  for (auto &&test : test_cases_u32)
    ok &= run_test("u32", i++, test);

  i = 0;
  for (auto &&test : test_cases_u64)
    ok &= run_test("u64", i++, test);

  i = 0;
  for (auto &&test : test_cases_sgl)
    ok &= run_test("sgl", i++, test);

  i = 0;
  for (auto &&test : test_cases_dbl)
    ok &= run_test("dbl", i++, test);

  return ok ? 0 : 1;
}
