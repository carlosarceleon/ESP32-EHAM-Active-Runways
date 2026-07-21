#pragma once

#include <cstdint>
#include <ctime>

enum class RunwayActivity : uint8_t {
  Inactive = 0,
  Landing = 1,
  Departure = 2,
  LandingAndDeparture = 3,
};

struct RunwayEndUse {
  bool landing = false;
  bool departure = false;
};

struct PhysicalRunwayState {
  RunwayEndUse end_a;
  RunwayEndUse end_b;
};

struct EhamWeather {
  bool available = false;
  bool variable_wind = false;
  bool calm_wind = false;
  uint16_t wind_direction_deg = 0;
  uint16_t wind_speed_kt = 0;
  bool has_gust = false;
  uint16_t gust_speed_kt = 0;
  int8_t temperature_c = 0;
  std::time_t observed_at_utc = 0;
};

struct EhamOperationalState {
  PhysicalRunwayState runways[6];

  bool runway_data_available = false;
  bool runway_data_stale = false;
  std::time_t runway_observed_at_utc = 0;
  std::time_t runway_fetched_at_utc = 0;

  EhamWeather weather;
};
