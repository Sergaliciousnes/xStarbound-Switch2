#include "StarDynamicLib.hpp"

namespace Star {
namespace {
class StaticExecutable final : public DynamicLib {
public:
  void* funcPtr(char const*) override { return nullptr; }
};
}
String DynamicLib::libraryExtension() { return ".nro"; }
DynamicLibUPtr DynamicLib::loadLibrary(String const&) { return {}; }
DynamicLibUPtr DynamicLib::currentExecutable() { return make_unique<StaticExecutable>(); }
}
