# Experimental Turbulence Proxy Index

Experimental C++ engine for extracting localized motion irregularity from
rolling aircraft-state observations and auxiliary weather data.

Aircraft dynamics are represented through robust dispersion of vertical rate,
vertical acceleration, turn rate and horizontal acceleration. Outlier influence
is reduced using 5–95% Winsorized standard deviation.

For each aircraft:

$$
B_a =
\mathrm{clamp}
\left(
0.45f_1 + 0.35f_2 + 0.15f_3 + 0.05f_4
\right)
$$

Aircraft contributions are weighted by sample density and observation age:

$$
w_i =
\min(n_i,n_{\max})
e^{-a_i/\tau}
$$

and aggregated as:

$$
B_T =
\frac{\sum_i w_i B_{a,i}}
{\sum_i w_i}
$$

Aircraft and weather evidence are blended according to available aircraft
sample density:

$$
\lambda =
1-e^{-N_{\mathrm{eff}}/N_0}
$$

$$
TPI =
\mathrm{clamp}
\left(
\lambda B_T + (1-\lambda)W_T
\right)
$$

The architecture is motivated by established aircraft-based turbulence
observation methods, where vertical aircraft motion is used as an indicator of
atmospheric turbulence.

> **Scientific scope:** TPI is an experimental proxy and not an EDR
> implementation. Its fusion weights, normalization scales and thresholds are
> heuristic parameters requiring empirical calibration against validated
> turbulence observations.

## References

- Sharman et al. (2014), *Journal of Applied Meteorology and Climatology* —
  automated in-situ EDR estimation from aircraft observations.
- Kim et al. (2017), *Journal of Applied Meteorology and Climatology* —
  comparison of aircraft-derived turbulence indicators.
- WMO Aircraft-Based Observations Programme — EDR and aircraft-based
  turbulence observations.
- NIST — Winsorization and Winsorized Standard Deviation for robust statistics.
