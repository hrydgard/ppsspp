// UWP STORAGE MANAGER
// Copyright (c) 2023 Bashar Astifan.
// Email: bashar@astifan.online
// Telegram: @basharastifan

#pragma once

#include <ppl.h>
#include <ppltasks.h>
#include <string>
#include <vector>

concurrency::task<std::string> ChooseFolder();
concurrency::task<std::string> ChooseFile(std::vector<std::string> exts);
