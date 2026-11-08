#pragma once

#include <iostream>
#include <string>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace logger {

    inline void Info(const std::string& message)
    {
        std::cout << "[INFO]  " << message << std::endl;
    }

    inline void Error(const std::string& message)
    {
        std::cerr << "[ERROR] " << message << std::endl;
    }

   
    inline void EnableUtf8Console()
    {
#ifdef _WIN32
        if (!::SetConsoleOutputCP(CP_UTF8))
        {
            Error("SetConsoleOutputCP() failed, code " + std::to_string(::GetLastError()));
        }
#endif
    }

} // namespace logger