/*******************************************************************************
 * This file is part of Zwindstroom.
 * Copyright (c) 2021 Willem Elbers (whe@willemelbers.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <gsl/gsl_integration.h>

#include "../include/cosmology_tables.h"
#include "../include/strooklat.h"
#include "../include/units.h"
#include <gsl/gsl_errno.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_odeiv2.h>
#include <gsl/gsl_sf.h>
#include <gsl/gsl_spline.h>

double F_integrand(double x, void *params) {
  double y = *((double *)params);
  double x2 = x * x;
  double y2 = y * y;
  return x2 * sqrt(x2 + y2) / (1.0 + exp(x));
}

double G_integrand(double x, void *params) {
  double y = *((double *)params);
  double x2 = x * x;
  double y2 = y * y;
  return y * x2 / (sqrt(x2 + y2) * (1.0 + exp(x)));
}

double w_tilde(double a, double w0, double wa) {
  return (a - 1.0) * wa - (1.0 + w0 + wa) * log(a);
}

struct df_params {
  double Omega_CMB;
  double Omega_ur;
  double Omega_c;
  double Omega_b;
  double *Omega_nu_tot;
  struct strooklat *spline;
  struct model *m;
  struct units *us;
  struct physical_consts *pcs;
};

int dfunc(double a, const double y[], double f[], void *params_ptr) {
  struct df_params *lparams = (struct df_params *)params_ptr;
  struct strooklat *spline = lparams->spline;
  struct model *m = lparams->m;
  struct units *us = lparams->us;
  struct physical_consts *pcs = lparams->pcs;

  /* Pull down some constants */
  const double Omega_CMB = lparams->Omega_CMB;
  const double Omega_ur = lparams->Omega_ur;
  const double Omega_c = lparams->Omega_c;
  const double Omega_b = lparams->Omega_b;
  const double h = m->h;
  const char *model_MG = m->model_MG;

  /* Vector with neutrino densities */
  double *Omega_nu_tot = lparams->Omega_nu_tot;

  /* Define further constants */
  const double Omega_nu_a = strooklat_interp(spline, Omega_nu_tot, a);
  const double Omega_m = Omega_c + Omega_b;
  const double Omega_r = Omega_CMB + Omega_ur + Omega_nu_a;
  const double H_0 = h * 100.0 * KM_METRES / MPC_METRES * us->UnitTimeSeconds;
  const double H_0_2 = H_0 * H_0;

  /* Density parameters */
  const double a_inv = 1.0 / a;
  const double a_inv2 = a_inv * a_inv;
  const double rho_m = 3.0 * H_0_2 * Omega_m * a_inv2 * a_inv;
  const double rho_r = 3.0 * H_0_2 * Omega_r * a_inv2 * a_inv2;
  const double p_r = rho_r / 3.0;

  /* Intermediate steps */
  if (strcmp(model_MG, "fT") == 0) {
    const double T0 = -6.0 * H_0_2;
    const double b = 0.632;

    /* Intermediate steps */
    const double half_on_b = 0.5 / b;
    const double alpha =
        gsl_sf_lambert_W0(-(Omega_m + Omega_r) * 0.5 * exp(-0.5 / b) / b) +
        half_on_b;
    const double T = -6.0 * y[0] * y[0];
    const double T0_on_T_pow_b = pow(T0 / T, b);
    const double exp_T0_on_T_pow_b_alpha = exp(T0_on_T_pow_b * alpha);
    const double FT =
        -1.0 + exp_T0_on_T_pow_b_alpha * (1.0 - b * T0_on_T_pow_b * alpha);
    const double FTT = (b * exp_T0_on_T_pow_b_alpha * T0_on_T_pow_b * alpha *
                        (-1.0 + b + b * T0_on_T_pow_b * alpha)) /
                       T;

    /* Final answer, dH/da */
    f[0] = (-0.5 * (rho_m + rho_r + p_r) / (1.0 + FT + 2.0 * T * FTT)) /
           (a * y[0]);

  } else if (strcmp(model_MG, "fTT") == 0) {

    const double T0 = -6.0 * H_0_2;
    const double T = -6.0 * y[0] * y[0];
    const double b = -1.82;
    const double lambda = (1 - Omega_m - Omega_r) / (Omega_m * (1 - 2 * b));
    const double rhor0 = 3.0 * H_0_2 * Omega_r;
    const double rhom0 = 3.0 * H_0_2 * Omega_m;
    const double Hpowb = pow((-(y[0] * y[0] / T0)), b);
    const double pow6b = pow(6, b);

    f[0] = (3 * y[0] *
            (-(a * (a * a * a * p_r + rhom0)) - rhor0 -
             pow6b * lambda *
                 (a * a * a * a * p_r + a * (rhom0 - 2 * b * rhom0) + rhor0) *
                 Hpowb)) /
           (2. * a * a *
            (3 * y[0] * y[0] * a * a * a +
             pow6b * b * (-1 + 2 * b) * lambda * rhom0 * Hpowb));

  } else if (strcmp(model_MG, "LCDM") == 0) {
    f[0] = (-0.5 * (rho_m + rho_r + p_r)) / (a * y[0]);
  }

  return GSL_SUCCESS;
}

double E2(double a_input, double Omega_CMB, double Omega_ur, double Omega_nu,
          double Omega_c, double Omega_b, double Omega_lambda, double Omega_k,
          double w0, double wa, struct model *m, struct units *us,
          struct physical_consts *pcs) {

  return 0;
}

void integrate_cosmology_tables(struct model *m, struct units *us,
                                struct physical_consts *pcs,
                                struct cosmology_tables *tab, double a_start,
                                double a_final, int size) {

  /* Prepare interpolation tables of F(y) and G(y) with y > 0 */
  const int table_size = 500;
  const double y_min = 1e-4;
  const double y_max = 1e6;
  const double log_y_min = log(y_min);
  const double log_y_max = log(y_max);
  const double delta_log_y = (log_y_max - log_y_min) / table_size;

  /* Allocate the tables */
  double *y = malloc(table_size * sizeof(double));
  double *Fy = malloc(table_size * sizeof(double));
  double *Gy = malloc(table_size * sizeof(double));

  /* Prepare GSL integration workspace */
  const int gsl_workspace_size = 1000;
  const double abs_tol = 1e-10;
  const double rel_tol = 1e-10;

  /* Allocate the workspace */
  gsl_integration_workspace *workspace =
      gsl_integration_workspace_alloc(gsl_workspace_size);
  gsl_function func_F = {F_integrand};
  gsl_function func_G = {G_integrand};

  /* Perform the numerical integration */
  for (int i = 0; i < table_size; i++) {
    y[i] = exp(log_y_min + i * delta_log_y);

    /* Integration result and absolute error */
    double res, abs_err;

    /* Evaluate F(y) by integrating on [0, infinity) */
    func_F.params = &y[i];
    gsl_integration_qagiu(&func_F, 0.0, abs_tol, rel_tol, gsl_workspace_size,
                          workspace, &res, &abs_err);
    Fy[i] = res;

    /* Evaluate G(y) by integrating on [0, infinity) and dividing by F(y) */
    func_G.params = &y[i];
    gsl_integration_qagiu(&func_G, 0.0, abs_tol, rel_tol, gsl_workspace_size,
                          workspace, &res, &abs_err);
    Gy[i] = y[i] * res / Fy[i];
  }

  /* Free the workspace */
  gsl_integration_workspace_free(workspace);

  /* Prepare an interpolation spline for the y argument of F and G */
  struct strooklat spline_y = {y, table_size};
  init_strooklat_spline(&spline_y, 100);

  /* We want to interpolate the scale factor */
  const double a_min = a_start;
  const double a_max = fmax(a_final, 1.01);
  const double log_a_min = log(a_min);
  const double log_a_max = log(a_max);
  const double delta_log_a = (log_a_max - log_a_min) / size;

  tab->size = size;
  tab->avec = malloc(size * sizeof(double));
  tab->Hvec = malloc(size * sizeof(double));
  double *Ga = malloc(size * sizeof(double));
  double *E2a = malloc(size * sizeof(double));
  double *dHdloga = malloc(size * sizeof(double));

  for (int i = 0; i < size; i++) {
    tab->avec[i] = exp(log_a_min + i * delta_log_a);
    Ga[i] = sqrt(tab->avec[i] + 1);
  }

  /* Prepare a spline for the scale factor */
  struct strooklat spline = {tab->avec, size};
  init_strooklat_spline(&spline, 100);

  /* The critical density */
  const double h = m->h;
  const double H_0 = h * 100 * KM_METRES / MPC_METRES * us->UnitTimeSeconds;
  const double G_grav = pcs->GravityG;
  const double rho_crit_0 = 3.0 * H_0 * H_0 / (8.0 * M_PI * G_grav);

  /* First, calculate the present-day CMB density from the temperature */
  const double h_bar = pcs->hPlanck / (2.0 * M_PI);
  const double kT = m->T_CMB_0 * pcs->kBoltzmann;
  const double hc = h_bar * pcs->SpeedOfLight;
  const double kT4 = kT * kT * kT * kT;
  const double hc3 = hc * hc * hc;
  const double c2 = pcs->SpeedOfLight * pcs->SpeedOfLight;
  const double Omega_CMB = M_PI * M_PI / 15.0 * (kT4 / hc3) / (rho_crit_0 * c2);

  /* Other density components */
  const double Omega_c = m->Omega_c;
  const double Omega_b = m->Omega_b;
  const double Omega_cb = Omega_c + Omega_b;
  const double Omega_k = m->Omega_k;

  /* Next, calculate the ultra-relativistic density */
  const double ratio = 4. / 11.;
  const double ratio4 = ratio * ratio * ratio * ratio;
  const double Omega_ur = m->N_ur * (7. / 8.) * cbrt(ratio4) * Omega_CMB;

  /* Now, we want to evaluate the neutrino density and equation of state */
  const int N_nu = m->N_nu;
  const double kT_nu_eV_0 = m->T_nu_0 * pcs->kBoltzmann / pcs->ElectronVolt;
  const double T_on_pi = m->T_nu_0 / m->T_CMB_0 / M_PI;
  const double pre_factor =
      Omega_CMB * 15.0 * T_on_pi * T_on_pi * T_on_pi * T_on_pi;
  double *Omega_nu = malloc(N_nu * size * sizeof(double));
  double *w_nu = malloc(N_nu * size * sizeof(double));

  /* For each neutrino species */
  for (int j = 0; j < N_nu; j++) {
    const double M_nu = m->M_nu[j];
    const double deg_nu = m->deg_nu[j];

    /* For each time step, interpolate the distribution function */
    for (int i = 0; i < size; i++) {
      /* Compute the density */
      const double arg = tab->avec[i] * M_nu / kT_nu_eV_0;
      const double Farg = strooklat_interp(&spline_y, Fy, arg);
      const double Onu_ij = deg_nu * pre_factor * Farg;
      Omega_nu[j * size + i] = Onu_ij;

      /* Also compute the equation of state */
      const double Garg = strooklat_interp(&spline_y, Gy, arg);
      w_nu[j * size + i] = (1.0 - Garg) / 3.0;
    }
  }

  /* Split the neutrino densities into relativistic and non-relativistic parts
   */
  double *Omega_nu_nr = malloc(size * sizeof(double));
  double *Omega_nu_tot = malloc(size * sizeof(double));
  double *Omega_r = malloc(size * sizeof(double));
  double *Omega_m = malloc(size * sizeof(double));
  tab->f_nu_nr = malloc(size * N_nu * sizeof(double));
  tab->f_nu_nr_tot = malloc(size * sizeof(double));

  for (int i = 0; i < size; i++) {

    /* Start with constant contributions to radiation & matter */
    Omega_r[i] = Omega_CMB + Omega_ur;
    Omega_m[i] = Omega_c + Omega_b;
    Omega_nu_nr[i] = 0.0;
    Omega_nu_tot[i] = 0.0;

    /* Add the massive neutrino species */
    for (int j = 0; j < N_nu; j++) {
      const double O_nu = Omega_nu[j * size + i];
      const double w = w_nu[j * size + i];
      Omega_nu_tot[i] += O_nu;
      Omega_nu_nr[i] += (1.0 - 3.0 * w) * O_nu;
      Omega_r[i] += 3.0 * w * O_nu;
      /* We rescale by 1/a, since this is in fact Omega_m * E^2 * a^3 and
       * Omega_nu is in fact Omega_nu * E^2 * a^4 */
      Omega_m[i] += (1.0 - 3.0 * w) * O_nu / tab->avec[i];
    }

    /* Fraction of non-relativistic neutrinos in matter */
    tab->f_nu_nr_tot[i] = Omega_nu_nr[i] / Omega_m[i] / tab->avec[i];

    /* Fraction per species */
    for (int j = 0; j < N_nu; j++) {
      const double O_nu = Omega_nu[j * size + i];
      const double w = w_nu[j * size + i];
      const double O_nu_nr = (1.0 - 3.0 * w) * O_nu;
      tab->f_nu_nr[j * size + i] = O_nu_nr / Omega_m[i] / tab->avec[i];
    }
  }

  /* The total neutrino density at z = 0 */
  const double Omega_nu_tot_0 = strooklat_interp(&spline, Omega_nu_tot, 1.0);

  /* The neutrino density per species at z = 0 */
  double *Omega_nu_0 = malloc(N_nu * sizeof(double));
  for (int i = 0; i < N_nu; i++) {
    Omega_nu_0[i] = strooklat_interp(&spline, Omega_nu + i * size, 1.0);
  }

  /* Close the universe */
  const double Omega_lambda =
      1.0 - Omega_nu_tot_0 - Omega_k - Omega_ur - Omega_CMB - Omega_c - Omega_b;
  const double w0 = m->w0;
  const double wa = m->wa;

  /* If neutrinos are to be treated as non-relativistic for the purpose of
   * calculating the Hubble rate, we need to replace the previous calculation
   * of Omega_nu(a) with Omega_nu_0 now (i.e. before calculating Hvec). */
  if (m->sim_neutrino_nonrel_Hubble) {
    for (int i = 0; i < size; i++) {
      Omega_m[i] = Omega_cb + Omega_nu_tot_0;
      Omega_nu_tot[i] = Omega_nu_tot_0;
      Omega_nu_nr[i] = Omega_nu_tot_0;
      tab->f_nu_nr_tot[i] = Omega_nu_tot_0 / (Omega_cb + Omega_nu_tot_0);
      for (int j = 0; j < N_nu; j++) {
        tab->f_nu_nr[j * size + i] =
            Omega_nu_0[j] / (Omega_cb + Omega_nu_tot_0);
      }
    }
  }

  /* Now, prepare the calculation of the Hubble rate */
  struct df_params Ea_ode_params;
  int j = 0;

  gsl_odeiv2_system ode_system;
  Ea_ode_params.Omega_CMB = Omega_CMB;
  Ea_ode_params.Omega_ur = Omega_ur;
  Ea_ode_params.Omega_c = Omega_c;
  Ea_ode_params.Omega_b = Omega_b;
  Ea_ode_params.Omega_nu_tot = Omega_nu_tot;
  Ea_ode_params.spline = &spline;
  Ea_ode_params.m = m;
  Ea_ode_params.us = us;
  Ea_ode_params.pcs = pcs;

  ode_system.function = dfunc;
  ode_system.dimension = 1;
  ode_system.params = &Ea_ode_params;

  /* We can solve for y[0] by starting from H = H_0 at a = 1 */
  /* Hence, we need to solve forwards for a > 1 and backwards for a < 1 */
  const double step_size = 1e-8;
  gsl_odeiv2_driver *drv_forward = gsl_odeiv2_driver_alloc_y_new(
      &ode_system, gsl_odeiv2_step_rkf45, step_size, abs_tol, abs_tol);
  gsl_odeiv2_driver *drv_backward = gsl_odeiv2_driver_alloc_y_new(
      &ode_system, gsl_odeiv2_step_rkf45, -step_size, abs_tol, abs_tol);

  double a_integrate = 1.0;
  double H_integrate = H_0;
  int status;
  int begin_forward_index = 0;

  /* Forwards first, solving y[0] for all a > 1 */
  for (int i = 0; i < size; i++) {
    double a_next = tab->avec[i];
    if (a_next < 1) {
      begin_forward_index++;
      continue;
    }

    status = gsl_odeiv2_driver_apply(drv_forward, &a_integrate, a_next,
                                     &H_integrate);
    if (status != GSL_SUCCESS) {
      printf("Error: status = %d \n", status);
      break;
    }
    tab->Hvec[i] = H_integrate;
    // printf("%d %g %g\n", i, a_next, tab->Hvec[i]);
  }

  // printf("First a after today is %d %g\n", begin_forward_index,
  // tab->avec[begin_forward_index]);

  /* Now, reset the state variables */
  a_integrate = 1.0;
  H_integrate = H_0;

  /* And integrate backwards, solving y[0] for all a < 1 */
  for (int i = begin_forward_index - 1; i >= 0; i--) {
    double a_next = tab->avec[i];
    status = gsl_odeiv2_driver_apply(drv_backward, &a_integrate, a_next,
                                     &H_integrate);
    if (status != GSL_SUCCESS) {
      printf("Error: status = %d \n", status);
      break;
    }
    tab->Hvec[i] = H_integrate;
    // printf("%d %g %g\n", i, a_next, tab->Hvec[i]);
  }

  // exit(1);

  // for (int i=0; i<size; i++) {
  //     double a_next = tab->avec[size - i - 1];
  //     double Omega_nu_a = strooklat_interp(&spline, Omega_nu_tot,
  //     tab->avec[size - i - 1]);
  //
  //     if (a_next > 1) continue;
  //
  //     printf("%g %g\n", a_integrate, a_next);
  //
  //     Ea_ode_params.Omega_nu = Omega_nu_a;
  //     status = gsl_odeiv2_driver_apply (drv, &a_integrate, a_next,
  //     &H_integrate); if (status != GSL_SUCCESS) {
  //         printf("Error: status = %d \n", status);
  //         break;
  //     }
  //     tab->Hvec[size-i-1] = H_integrate;
  // }

  // /* Now, create a table with the Hubble rate */
  // for (int i=0; i<size; i++) {
  //     E2a[i] = E2(tab->avec[i], Omega_CMB, Omega_ur, Omega_nu_a, Omega_c,
  //                    Omega_b, Omega_lambda, Omega_k, w0, wa, m, us, pcs);
  //     tab->Hvec[i] = sqrt(E2a[i]) * H_0;
  // }

  /* Now, differentiate the Hubble rate */
  for (int i = 0; i < size; i++) {
    /* Forward at the start, five-point in the middle, backward at the end */
    if (i < 2) {
      dHdloga[i] = (tab->Hvec[i + 1] - tab->Hvec[i]) / delta_log_a;
    } else if (i < size - 2) {
      dHdloga[i] = tab->Hvec[i - 2];
      dHdloga[i] -= tab->Hvec[i - 1] * 8.0;
      dHdloga[i] += tab->Hvec[i + 1] * 8.0;
      dHdloga[i] -= tab->Hvec[i + 2];
      dHdloga[i] /= 12.0 * delta_log_a;
    } else {
      dHdloga[i] = (tab->Hvec[i] - tab->Hvec[i - 1]) / delta_log_a;
    }
  }

  /* If neutrino particle masses are not varied in the N-body simulation to
   * account for the relativistic energy density, we need to replace the
   * previous calculation of Omega_nu(a) with Omega_nu_0. However, this
   * must be done after calculating the Hubble rate, if we do take the
   * relativistic contribution into account there. */
  if (m->sim_neutrino_nonrel_masses && !m->sim_neutrino_nonrel_Hubble) {
    for (int i = 0; i < size; i++) {
      Omega_m[i] = Omega_cb + Omega_nu_tot_0;
      Omega_nu_tot[i] = Omega_nu_tot_0;
      Omega_nu_nr[i] = Omega_nu_tot_0;
      tab->f_nu_nr_tot[i] = Omega_nu_tot_0 / (Omega_cb + Omega_nu_tot_0);
      for (int j = 0; j < N_nu; j++) {
        tab->f_nu_nr[j * size + i] =
            Omega_nu_0[j] / (Omega_cb + Omega_nu_tot_0);
      }
    }
  }

  /* Now, create the A and B functions */
  tab->Avec = malloc(size * sizeof(double));
  tab->Bvec = malloc(size * sizeof(double));

  for (int i = 0; i < size; i++) {
    double a = tab->avec[i];
    tab->Avec[i] = -(2.0 + dHdloga[i] / tab->Hvec[i]);
    tab->Bvec[i] = -1.5 * Omega_m[i] / (a * a * a) / E2a[i];
  }

  free(Omega_nu_nr);
  free(Omega_r);
  free(Omega_m);
  free(Omega_nu);
  free(Omega_nu_tot);
  free(Omega_nu_0);
  free(w_nu);
  free(dHdloga);
  free(E2a);
  free(Ga);

  /* Free the interpolation tables */
  free(y);
  free(Fy);
  free(Gy);

  free_strooklat_spline(&spline);
  free_strooklat_spline(&spline_y);
}

double get_H_of_a(struct cosmology_tables *tab, double a) {
  /* Prepare a spline for the scale factor */
  struct strooklat spline = {tab->avec, tab->size};
  init_strooklat_spline(&spline, 100);

  /* Interpolate */
  double Ha = strooklat_interp(&spline, tab->Hvec, a);

  /* Free the spline */
  free_strooklat_spline(&spline);

  return Ha;
}

double get_f_nu_nr_tot_of_a(struct cosmology_tables *tab, double a) {
  /* Prepare a spline for the scale factor */
  struct strooklat spline = {tab->avec, tab->size};
  init_strooklat_spline(&spline, 100);

  /* Interpolate */
  double f_nu_nr_tot = strooklat_interp(&spline, tab->f_nu_nr_tot, a);

  /* Free the spline */
  free_strooklat_spline(&spline);

  return f_nu_nr_tot;
}

void set_neutrino_sound_speeds(struct model *m, struct units *us,
                               struct physical_consts *pcs) {

  /* Use the estimate from Blas+14 as default */
  for (int i = 0; i < m->N_nu; i++) {
    m->c_s_nu[i] = pcs->SoundSpeedNeutrinos / m->M_nu[i];
  }
}

void free_cosmology_tables(struct cosmology_tables *tab) {
  free(tab->avec);
  free(tab->Avec);
  free(tab->Bvec);
  free(tab->Hvec);
  free(tab->f_nu_nr);
  free(tab->f_nu_nr_tot);
}
