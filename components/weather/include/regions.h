#pragma once
#include <cstddef>
namespace weather {
struct Region {
    int code;
    const char* name;
};
struct City {
    int code;
    int province;
    const char* name;
};
extern const Region provinces[];
extern const size_t provinceCount;
extern const City cities[];
extern const size_t cityCount;
}  // namespace weather
