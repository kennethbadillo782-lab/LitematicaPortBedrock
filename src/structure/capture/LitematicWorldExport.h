#pragma once
#include "structure/capture/StructureCapture.h"
class BlockSource;
namespace lholo::structure::capture {
bool exportLitematicWorld(BlockSource&,Draft const&,std::filesystem::path const&,std::string& error);
}
