#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace genesis::ui {

// Shows the no-project launcher. Dropping a supported path returns it so the
// caller can resolve and launch it without restarting Genesis.
std::optional<std::filesystem::path> showProjectPrompt(const std::string& message);

}

