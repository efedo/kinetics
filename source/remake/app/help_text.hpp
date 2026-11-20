#pragma once

#include <span>

namespace app {

struct HelpTopic {
    const char* title;
    const char* body;
};

std::span<const HelpTopic> help_topics();

}  // namespace app
