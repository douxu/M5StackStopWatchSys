#include "weather.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
int main(int argc, char** argv)
{
    weather::Snapshot s;
    std::string location;
    const std::string good =
        R"({"current_condition":[{"temp_C":"-3","weatherCode":"113"}],"nearest_area":[{"areaName":[{"value":"Beijing"}],"latitude":"39.9","longitude":"116.4"}]})";
    assert(weather::parseResponse(good, s, location));
    assert(s.valid && s.temperature == -3 && s.city == "Beijing" && location == "39.9,116.4");
    assert(weather::parseResponse("{\"data\":" + good + "}", s, location));
    for (auto bad : {"Unknown location", "{}", "{\"current_condition\":[]}", "<html>503</html>",
                     "{\"current_condition\":[{\"temp_C\":\"nan\",\"weatherCode\":\"113\"}]}",
                     "{\"current_condition\":[{\"temp_C\":\"999\",\"weatherCode\":\"113\"}]}",
                     "{\"current_condition\":[{\"temp_C\":\"3bad\",\"weatherCode\":\"113\"}]}",
                     "{\"current_condition\":[{\"temp_C\":\"3\",\"weatherCode\":\"999\"}]}"}) {
        assert(!weather::parseResponse(bad, s, location));
        assert(s.temperature == -3);
    }
    assert(weather::encodeLocation("北京,China") == "%E5%8C%97%E4%BA%AC%2CChina");
    if (argc > 1) {
        std::ifstream f(argv[1]);
        std::string body((std::istreambuf_iterator<char>(f)), {});
        assert(weather::parseResponse(body, s, location));
    }
    std::cout << "PASS: JSON schemas, invalid payloads, temperature bounds, weather codes, URL encoding\n";
}
