/*
 * Common definitions
 *
 * Copyright (c) 2013 National Instruments
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

#pragma once

#include <cstring>
#include <memory>
#include <string>
#include <vector>

/**
 * Prevents unused warnings/errors without needing to declare with
 * "__attribute__((unused))" or compile with "-Wno-unused*".
 */
#define UNUSED(variable) ((void)(sizeof(variable)))

#ifdef DEBUG
   #include <cstdio> // perror
   #include <execinfo.h> // backtrace, backtrace_symbols_fd
   #define DEBUG_PRINT_ERRNO() perror(__PRETTY_FUNCTION__);
   #define DEBUG_PRINT_STACK() \
      do \
      { \
         void* stack[20]; \
         const auto stackSize = backtrace(stack, 20); \
         backtrace_symbols_fd(stack, stackSize, 2); \
      } while (false)
#else
   #define DEBUG_PRINT_ERRNO()
   #define DEBUG_PRINT_STACK()
#endif // DEBUG

namespace nirio
{
   static inline std::string normalizePath(const std::string& p)
   {
      std::unique_ptr<char, void (*)(void *)> s(strdup(p.c_str()), free);
      char *str = s.get(), *saveptr;
      std::vector<char *> pv;

      if (!str)
         throw std::bad_alloc();

      while (auto tok = strtok_r(str, "/", &saveptr))
      {
         str = NULL;
         if (!strcmp(tok, "."))
         {
            continue;
         }
         else if (!strcmp(tok, ".."))
         {
            if (!pv.empty())
               pv.pop_back();
         }
         else
         {
            pv.push_back(tok);
         }
      }

      std::string out = !p.empty() && p[0] == '/' ? "/" : "";
      for (auto it = pv.cbegin(); it != pv.cend(); ++it)
      {
         out += *it;
         if (it + 1 != pv.cend())
            out += "/";
      }

      return out;
   }

   static inline std::string joinPath(const std::string& p)
   {
      return normalizePath(p);
   }

   template <typename... Ps>
   static std::string joinPath(const std::string& p1, const std::string& p2, const Ps&... ps)
   {
      return joinPath(p1 + '/' + p2, ps...);
   }
}
