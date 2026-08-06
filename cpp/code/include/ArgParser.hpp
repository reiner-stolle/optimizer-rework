#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

class ArgParser {
   public:
    ArgParser(int &argc, char **argv);

    const std::string &getArgByValue(const std::string &arg) const;
    std::string getArgByValueAndRemove(const std::string &arg);

    template <class T>
    T takeParseArg(const std::string key, const std::string &on_warning, const T default_value, bool exitOnWarning) {
        const std::string &arg = getArgByValueAndRemove(key);
        T result;
        if (arg.size() == 0) {
            if (exitOnWarning) {
                throw std::runtime_error(on_warning);
            }
            std::cout << on_warning << std::endl;
            result = default_value;
        } else {
            std::stringstream(arg) >> result;
        }
        return result;
    }

    bool hasArg(const std::string &arg) const;
    std::vector<std::string> getParameterVectorCopy() const;

   private:
    const std::string empty_string{""};
    std::vector<std::string> args;
};