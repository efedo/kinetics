#pragma once

#include <span>

namespace app {

// Help topics, in the order of the Help menu and the help window's list.
enum class HelpId {
    GettingStarted,
    Equation,
    Data,
    FileData,
    Fitting,
    Plotting,
    Residuals,
    StandardCurve,
    FittingOptions,
    Reports,
    Changes,
};

// A topic's body is paragraphs separated by blank lines. A paragraph that
// starts with "## " is a heading, one whose lines start with four spaces is
// shown in a fixed-width font, and lines starting with "- " are a list.
struct HelpTopic {
    const char* title;
    const char* body;
};

std::span<const HelpTopic> help_topics();

}  // namespace app
