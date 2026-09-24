#pragma once

#include <string>

struct SpineDataVersion {
    int major = 0;
    int minor = 0;
    std::string full;
    std::string error;

    bool valid() const { return major > 0 && !full.empty(); }
    std::string family() const;
};

SpineDataVersion detectSpineDataVersion(const char* skeletonPath);

