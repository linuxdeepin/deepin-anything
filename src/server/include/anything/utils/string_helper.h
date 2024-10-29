#ifndef ANYTHING_STRING_HELPER_H_
#define ANYTHING_STRING_HELPER_H_

#include <string>
#include <vector>

#include "anything/common/anything_fwd.hpp"


ANYTHING_NAMESPACE_BEGIN

inline std::vector<std::string> split(std::string s, const std::string& delimiter) {
    std::vector<std::string> tokens;
    size_t pos = 0;
    std::string token;
    while ((pos = s.find(delimiter)) != std::string::npos) {
        token = s.substr(0, pos);
        tokens.push_back(token);
        s.erase(0, pos + delimiter.length());
    }
    tokens.push_back(s);

    return tokens;
}

inline bool ends_with(const std::string& path, const std::string& ending) {
    if (path.length() >= ending.length())
        return 0 == path.compare(path.length() - ending.length(), ending.length(), ending);
    
    return false;
}

inline bool contains(const std::string& str, const std::string& substr) {
    return str.find(substr) != std::string::npos;
}

ANYTHING_NAMESPACE_END

#endif // ANYTHING_STRING_HELPER_H_