#pragma once

#include <iostream>
#include <string>

namespace logger {

    inline void Info(const std::string& message)
    {
        std::cout << "[INFO]  " << message << std::endl;
    }

    inline void Error(const std::string& message)
    {
        std::cerr << "[ERROR] " << message << std::endl;
    }

} 