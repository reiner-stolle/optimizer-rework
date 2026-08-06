#include "ArgParser.hpp"

#include <iostream>
#include <stdexcept>
#include <algorithm>

ArgParser::ArgParser(int &argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        args.push_back(std::string(argv[i]));
    }
}

const std::string &ArgParser::getArgByValue(const std::string &arg) const {
    auto arg_it = std::find(args.cbegin(), args.cend(), arg);
    if (arg_it != args.cend() && ++arg_it != args.cend()) {
        return *arg_it;
    }
    return empty_string;
}

template<>
std::string ArgParser::takeParseArg<std::string>(const std::string key, const std::string &on_warning, const std::string default_value, bool exitOnWarning) {
    const std::string &arg = getArgByValueAndRemove(key);
    if (arg.size() == 0) {
        if (exitOnWarning) {
            throw std::runtime_error(on_warning);
        }
        std::cout << on_warning << std::endl;
        return default_value;
    } else {
        return arg;
    }
}

std::string ArgParser::getArgByValueAndRemove(const std::string &arg) {
    auto arg_it = std::find(args.begin(), args.end(), arg);
    if (arg_it != args.end() && (arg_it + 1) != args.end()) {
        std::string val = *(arg_it + 1);
        args.erase(arg_it + 1);
        args.erase(arg_it);
        return val;
    }
    return empty_string;
}

bool ArgParser::hasArg(const std::string &arg) const {
    return std::find(args.cbegin(), args.cend(), arg) != args.cend();
}

std::vector<std::string> ArgParser::getParameterVectorCopy() const {
    return std::vector<std::string>(args.begin(), args.end());
}