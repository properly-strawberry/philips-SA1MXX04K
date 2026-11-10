#pragma once
#include "interface.h"
namespace player {
std::vector<Station> readRadioConfig(const std::filesystem::path& file,std::wstring& error);
}
