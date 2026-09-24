#include "atmosphere/reference/model.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace atmosphere::reference;

static AtmosphereParameters Earth() {
  constexpr int kLambdaMin = 360;
  constexpr int kLambdaMax = 830;
  constexpr double kSolarIrradiance[48] = {
      1.11776,1.14259,1.01249,1.14716,1.72765,1.73054,1.6887,1.61253,
      1.91198,2.03474,2.02042,2.02212,1.93377,1.95809,1.91686,1.8298,
      1.8685,1.8931,1.85149,1.8504,1.8341,1.8345,1.8147,1.78158,
      1.7533,1.6965,1.68194,1.64654,1.6048,1.52143,1.55622,1.5113,
      1.474,1.4482,1.41018,1.36775,1.34188,1.31429,1.28303,1.26758,
      1.2367,1.2082,1.18737,1.14683,1.12362,1.1058,1.07124,1.04992};
  constexpr double kOzoneCrossSection[48] = {
      1.18e-27,2.182e-28,2.818e-28,6.636e-28,1.527e-27,2.763e-27,
      5.52e-27,8.451e-27,1.582e-26,2.316e-26,3.669e-26,4.924e-26,
      7.752e-26,9.016e-26,1.48e-25,1.602e-25,2.139e-25,2.755e-25,
      3.091e-25,3.5e-25,4.266e-25,4.672e-25,4.398e-25,4.701e-25,
      5.019e-25,4.305e-25,3.74e-25,3.215e-25,2.662e-25,2.238e-25,
      1.852e-25,1.473e-25,1.209e-25,9.423e-26,7.455e-26,6.566e-26,
      5.105e-26,4.15e-26,4.228e-26,3.237e-26,2.451e-26,2.801e-26,
      2.534e-26,1.624e-26,1.465e-26,2.078e-26,1.383e-26,7.105e-27};

  constexpr ScatteringCoefficient rayleigh0 = 1.24062e-6 / m;
  constexpr Length rayleigh_height = 8000.0 * m;
  constexpr Length mie_height = 1200.0 * m;
  constexpr double mie_beta = 5.328e-3;
  constexpr auto dobson = 2.687e20 / m2;
  constexpr NumberDensity max_ozone = 300.0 * dobson / (15.0 * km);
  std::vector<SpectralIrradiance> solar;
  std::vector<ScatteringCoefficient> rayleigh, mie_s, mie_e, ozone;
  for (int l = kLambdaMin; l <= kLambdaMax; l += 10) {
    const double lambda_um = l * 1e-3;
    const auto mie = mie_beta / mie_height;
    const int i = (l - kLambdaMin) / 10;
    solar.push_back(kSolarIrradiance[i] * watt_per_square_meter_per_nm);
    rayleigh.push_back(rayleigh0 * std::pow(lambda_um, -4.0));
    mie_s.push_back(mie * 0.9);
    mie_e.push_back(mie);
    ozone.push_back(max_ozone * kOzoneCrossSection[i] * m2);
  }

  AtmosphereParameters a{};
  a.solar_irradiance = IrradianceSpectrum(kLambdaMin * nm, kLambdaMax * nm, solar);
  a.sun_angular_radius = 0.2678 * deg;
  a.bottom_radius = 6360.0 * km;
  a.top_radius = 6420.0 * km;
  a.rayleigh_density.layers[1] = DensityProfileLayer(0.0*m, 1.0, -1.0/rayleigh_height, 0.0/m, 0.0);
  a.rayleigh_scattering = ScatteringSpectrum(kLambdaMin*nm, kLambdaMax*nm, rayleigh);
  a.mie_density.layers[1] = DensityProfileLayer(0.0*m, 1.0, -1.0/mie_height, 0.0/m, 0.0);
  a.mie_scattering = ScatteringSpectrum(kLambdaMin*nm, kLambdaMax*nm, mie_s);
  a.mie_extinction = ScatteringSpectrum(kLambdaMin*nm, kLambdaMax*nm, mie_e);
  a.mie_phase_function_g = 0.8;
  a.absorption_density.layers[0] = DensityProfileLayer(25.0*km,0.0,0.0/km,1.0/(15.0*km),-2.0/3.0);
  a.absorption_density.layers[1] = DensityProfileLayer(0.0*km,0.0,0.0/km,-1.0/(15.0*km),8.0/3.0);
  a.absorption_extinction = ScatteringSpectrum(kLambdaMin*nm, kLambdaMax*nm, ozone);
  a.ground_albedo = DimensionlessSpectrum(0.1);
  a.mu_s_min = dimensional::cos(102.0 * deg);
  return a;
}

int main(int argc, char** argv) {
  const std::filesystem::path output = argc > 1 ? argv[1] : "assets/atmosphere/bruneton";
  const std::filesystem::path cache = output / "spectral_cache";
  std::filesystem::create_directories(cache);
  std::cout << "Official Bruneton spectral LUT bake (4 scattering orders)\n";
  Model model(Earth(), cache.string() + "/");
  model.Init(4);
  model.SaveRgbTextures(output.string());
  std::cout << "Wrote real-time RGB LUTs to " << output << "\n";
  return 0;
}
