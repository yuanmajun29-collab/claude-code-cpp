#pragma once

#include "app/config.h"
#include <CLI/CLI.hpp>

namespace claude {

// Set up CLI argument parsing
void setup_cli(CLI::App& app, AppConfig& config);

// Parse CLI arguments and return the config
AppConfig parse_cli(int argc, char** argv);

}  // namespace claude
