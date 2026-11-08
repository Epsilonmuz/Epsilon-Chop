#pragma once

#include "ChopEngine.h"
#include <functional>

namespace chop
{
void buildWaveform(Sample&) noexcept;
bool analyzeTransients(Sample&, const std::function<bool()>& cancelled);
}
