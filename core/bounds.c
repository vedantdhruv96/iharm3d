/******************************************************************************
 *                                                                            *
 * BOUNDS.C                                                                   *
 *                                                                            *
 * PHYSICAL BOUNDARY CONDITIONS                                               *
 *                                                                            *
 ******************************************************************************/

#include "decs.h"

void inflow_check(struct GridGeom *geom, struct FluidState *state, int i, int j,
  int k, int type);

void set_bounds(struct GridGeom *geom, struct FluidState *state)
{
  timer_start(TIMER_BOUND);

  // Sanity checks against grid dimensions
  #if N2 > 1 && N2 < NG
  fprintf(stderr, "N2 must be >= NG\n");
  exit(-1);
  #elif N3 > 1 && N3 < NG
  fprintf(stderr, "N3 must be >= NG\n");
  exit(-1);
  #endif

printf("a\n");
  sync_mpi_boundaries(state);
printf("a\n");

  if(global_start[0] == 0) {
    #pragma omp parallel for collapse(2)
    KSLOOP(0, N3 - 1) {
      JSLOOP(0, N2 - 1) {
        ISLOOP(-NG, -1) {
          #if X1L_BOUND == OUTFLOW
          int iz = 0 + NG;
          PLOOP state->P[ip][k][j][i] = state->P[ip][k][j][iz];
          pflag[k][j][i] = pflag[k][j][iz];

          double rescale = geom->gdet[CENT][j][iz]/geom->gdet[CENT][j][i];
          state->P[B1][k][j][i] *= rescale;
          state->P[B2][k][j][i] *= rescale;
          state->P[B3][k][j][i] *= rescale;
          //state->P[i][j][k][B1] *= rescale;
          //state->P[i][j][k][B2] *= rescale;
          //state->P[i][j][k][B3] *= rescale;
          //if (j == 4 && i < 7) {
          //  printf("B: 
          //}
          #elif X1L_BOUND == PERIODIC
          int iz = N1 + i;
          PLOOP state->P[ip][k][j][i] = state->P[ip][k][j][iz];
          pflag[k][j][i] = pflag[k][j][iz];
          #elif X1L_BOUND == POLAR
          printf("X1L_BOUND choice POLAR not supported\n", X1L_BOUND);
          exit(-1);
          #elif X1L_BOUND == USER
          printf("X1L_BOUND choice USER not supported\n", X1L_BOUND);
          exit(-1);
          #else
          printf("X1L_BOUND choice %i not supported\n", X1L_BOUND);
          exit(-1);
          #endif
        }
      }
    }
  } // global_start[0] == 0

  if(global_stop[0] == N1TOT) {
    #pragma omp parallel for collapse(2)
    KSLOOP(0, N3 - 1) {
      JSLOOP(0, N2 - 1) {
        ISLOOP(N1, N1 - 1 + NG) {
          #if X1R_BOUND == OUTFLOW
          int iz = N1 - 1 + NG;
          PLOOP state->P[ip][k][j][i] = state->P[ip][k][j][iz];
          pflag[k][j][i] = pflag[k][j][iz];

          double rescale = geom->gdet[CENT][j][iz]/geom->gdet[CENT][j][i];
          state->P[B1][k][j][i] *= rescale;
          state->P[B2][k][j][i] *= rescale;
          state->P[B3][k][j][i] *= rescale;
          #elif X1R_BOUND == PERIODIC
          int iz = i - N1;
          PLOOP state->P[ip][k][j][i] = state->P[ip][k][j][iz];
          pflag[k][j][i] = pflag[k][j][iz];
          #elif X1R_BOUND == POLAR
          printf("X1R_BOUND choice POLAR not supported\n");
          exit(-1);
          #elif X1R_BOUND == USER
          printf("X1R_BOUND choice USER not supported\n");
          exit(-1);
          #else
          printf("X1R_BOUND choice %i not supported\n", X1R_BOUND);
          exit(-1);
          #endif
        }
      }
    }
  } // global_stop[0] == N1TOT

  #if N2 == 1
  #pragma omp parallel for collapse(2)
  KSLOOP(-NG, N3 -1 + NG) {
    JSLOOP(-NG, -1) {
      ISLOOP(-NG, N1 - 1 + NG) {
        PLOOP state->P[ip][k][j][i] = state->P[ip][k][NG][i];
      }
    }
  }
  #pragma omp parallel for collapse(2)
  KSLOOP(-NG, N3 -1 + NG) {
    JSLOOP(N2, N2 - 1 + NG) {
      ISLOOP(-NG, N1 - 1 + NG) {
        PLOOP state->P[ip][k][j][i] = state->P[ip][k][NG][i];
      }
    }
  }
  #else
  if(global_start[1] == 0) {
    #pragma omp parallel for collapse(2)
    KSLOOP(-NG, N3 - 1 + NG) {
      JSLOOP(-NG, -1) {
        ISLOOP(-NG, N1 - 1 + NG) {
          #if X2L_BOUND == OUTFLOW
          int jz = 0 + NG ;
          PLOOP state->P[ip][k][j][i] = state->P[ip][k][jz][i];
          pflag[k][j][i] = pflag[k][jz][i];
          #elif X2L_BOUND == POLAR
          int jrefl = -j + 2*NG - 1;
          PLOOP state->P[ip][k][j][i] = state->P[ip][k][jrefl][i];
          pflag[k][j][i] = pflag[k][jrefl][i];
          state->P[U2][k][j][i] *= -1.;
          state->P[B2][k][j][i] *= -1.;
          #elif X2L_BOUND == USER
          printf("X2L_BOUND choice USER not supported\n");
          exit(-1);
          #elif X1L_BOUND != PERIODIC
          printf("X2L_BOUND choice %i not supported\n", X2L_BOUND);
          exit(-1);
          #endif
        }
      }
    }

    // Treat periodic bounds separately in presence of MPI
    #if X2L_BOUND == PERIODIC && X2R_BOUND == PERIODIC
    if (global_stop[1] == N2TOT) {
      #pragma omp parallel for collapse(2)
      KSLOOP(-NG, N3 - 1 + NG) {
        JSLOOP(-NG, -1) {
          ISLOOP(-NG, N1 - 1 + NG) {
            int jz = N2 + j;
            PLOOP state->P[ip][k][j][i] = state->P[ip][k][jz][i];
            pflag[k][j][i] = pflag[k][jz][i];
          }
        }
      }
    }
    #endif
  } // global_start[1] == 0

  if(global_stop[1] == N2TOT) {
    #pragma omp parallel for collapse(2)
    KSLOOP(-NG, N3 - 1 + NG) {
      JSLOOP(N2, N2 - 1 + NG) {
        ISLOOP(-NG, N1 - 1 + NG) {
          #if X2R_BOUND == OUTFLOW
          int jz = N2 - 1 + NG;
          PLOOP state->P[ip][k][j][i] = state->P[ip][k][jz][i];
          pflag[k][j][i] = pflag[k][jz][i];
          #elif X2R_BOUND == POLAR
          int jrefl = -j + 2*(N2 + NG) - 1;
          PLOOP state->P[ip][k][j][i] = state->P[ip][k][jrefl][i];
          pflag[k][j][i] = pflag[k][jrefl][i];
          state->P[U2][k][j][i] *= -1.;
          state->P[B2][k][j][i] *= -1.;
          #elif X2R_BOUND == USER
          printf("X2R_BOUND choice USER not supported\n");
          exit(-1);
          #elif X2R_BOUND != PERIODIC
          printf("X2R_BOUND choice %i not supported\n", X2R_BOUND);
          exit(-1);
          #endif
        }
      }
    }
  } // global_stop[1] == N2TOT
  #endif // N2 == 1

printf("AOSIDJHIAOSUJDIASUD\n");
  #if N3 == 1
  #pragma omp parallel for collapse(2)
  KSLOOP(-NG, -1) {
    JSLOOP(-NG, N2 - 1 + NG) {
      ISLOOP(-NG, N1 - 1 + NG) {
        PLOOP state->P[ip][k][j][i] = state->P[ip][NG][j][i];
      }
    }
  }
printf("AOSIDJHIAOSUJDIASUD\n");
  #pragma omp parallel for collapse(2)
  KSLOOP(N3, N3 - 1 + NG) {
    JSLOOP(-NG, N2 - 1 + NG) {
      ISLOOP(-NG, N1 - 1 + NG) {
    printf("k = %i %i %i\n", i,j,k);
        PLOOP {printf("%i", ip); state->P[ip][k][j][i] = state->P[ip][NG][j][i];
        }
      }
    }
  }
printf("AOSIDJHIAOSUJDIASUD\n");
  #else
  if (global_start[2] == 0) {
    #pragma omp parallel for collapse(2)
    KSLOOP(-NG, -1) {
      JSLOOP(-NG, N2 - 1 + NG) {
        ISLOOP(-NG, N1 - 1 + NG) {
          #if X3L_BOUND == OUTFLOW
          int kz = 0 + NG ;
          PLOOP state->P[ip][k][j][i] = state->P[ip][kz][j][i];
          pflag[k][j][i] = pflag[kz][j][i];
          #elif X3L_BOUND == POLAR
          printf("X3L_BOUND choice POLAR not supported\n");
          exit(-1);
          #elif X3L_BOUND == USER
          printf("X3L_BOUND choice USER not supported\n");
          exit(-1);
          #elif X3L_BOUND != PERIODIC
          printf("X3L_BOUND choice %i not supported\n", X3L_BOUND);
          exit(-1);
          #endif
        }
      }
    }

    #if X3L_BOUND == PERIODIC && X3R_BOUND == PERIODIC
    if (global_stop[2] == 0) {
      #pragma omp parallel for collapse(2)
      KSLOOP(-NG, -1) {
        JSLOOP(-NG, N2 - 1 + NG) {
          ISLOOP(-NG, N1 - 1 + NG) {
            int kz = N3 + k;
            PLOOP state->P[ip][k][j][i] = state->P[ip][kz][j][i];
            pflag[k][j][i] = pflag[kz][j][i];
          }
        }
      }
      KSLOOP(N3, N3 - 1 + NG) {
        JSLOOP(-NG, N2 - 1 + NG) {
          ISLOOP(-NG, N1 - 1 + NG) {
            int kz = k - N3;
            PLOOP state->P[ip][k][j][i] = state->P[ip][kz][j][i];
            pflag[k][j][i] = pflag[kz][j][i];
          }
        }
      }
    }
    #endif
  } // global_start[2] == 0
printf("AOSIDJHIAOSUJDIASUD\n");

  if(global_stop[2] == N3TOT) {
    #pragma omp parallel for collapse(2)
    KSLOOP(N3, N3 - 1 + NG) {
      JSLOOP(-NG, N2 - 1 + NG) {
        ISLOOP(-NG, N1 - 1 + NG) {
          #if X3R_BOUND == OUTFLOW
          int kz = N3 - 1 + NG;
          PLOOP state->P[ip][k][j][i] = state->P[ip][kz][j][i];
          pflag[k][j][i] = pflag[kz][j][i];
          #elif X3R_BOUND == POLAR
          printf("X3R_BOUND choice POLAR not supported\n");
          exit(-1);
          #elif X3R_BOUND == USER
          printf("X3R_BOUND choice USER not supported\n");
          exit(-1);
          #elif X3R_BOUND != PERIODIC
          printf("X3R_BOUND choice %i not supported\n", X3R_BOUND);
          exit(-1);
          #endif
        }
      }
    }
  } // global_stop[2] == N3TOT
  #endif // N3
printf("AOSIDJHIAOSUJDIASUD\n");
  
  #if METRIC == MKS
  //ucon_calc(geom, state);
  if(global_start[0] == 0) {
    // Make sure there is no inflow at the inner boundary
    #pragma omp parallel for collapse(2)
    KSLOOP(-NG, N3 - 1 + NG) {
      JSLOOP(-NG, N2 - 1 + NG) {
        ISLOOP(-NG, -1) {
          inflow_check(geom, state, i, j, k, 0);
        }
      }
    }
  }
  if(global_stop[0] == N1TOT) {
    // Make sure there is no inflow at the outer boundary
    #pragma omp parallel for collapse(2)
    KSLOOP(-NG, N3 - 1 + NG) {
      JSLOOP(-NG, N2 - 1 + NG) {
        ISLOOP(N1, N1 - 1 + NG) {
          inflow_check(geom, state, i, j, k, 1);
        }
      }
    }
  }
  #endif
printf("AOSIDJHIAOSUJDIASUD\n");
  
  timer_stop(TIMER_BOUND);
}

#if METRIC == MKS
void inflow_check(struct GridGeom *G, struct FluidState *S, int i, int j, int k,
  int type)
{
  //double ucon[NDIM];
  double alpha, beta1, gamma, vsq;

  //geom = get_geometry(ii, jj, CENT);
  //ucon_calc(Pr, geom, ucon);
  ucon_calc(G, S, i, j, k, CENT);

  if (((S->ucon[1][k][j][i] > 0.) && (type == 0)) || 
      ((S->ucon[1][k][j][i] < 0.) && (type == 1)))
  {
    //double gamma = get_mhd_gamma(G, S, i, j, k, CENT);
    // Find gamma and remove it from state->Pitives
    if (mhd_gamma_calc(G, S, i, j, k, CENT, &gamma)) {
      fflush(stderr);
      fprintf(stderr, "\ninflow_check(): gamma failure\n");
      fflush(stderr);
      fail(FAIL_GAMMA, i, j, k);
    }
    S->P[U1][k][j][i] /= gamma;
    S->P[U2][k][j][i] /= gamma;
    S->P[U3][k][j][i] /= gamma;
    alpha = G->lapse[CENT][j][i];
    beta1 = G->gcon[CENT][0][1][j][i]*alpha*alpha;

    // Reset radial velocity so radial 4-velocity is zero
    S->P[U1][k][j][i] = beta1/alpha;

    // Now find new gamma and put it back in
    vsq = 0.;
    for (int mu = 1; mu < NDIM; mu++) {
      for (int nu = 1; nu < NDIM; nu++) {
        vsq += G->gcov[CENT][mu][nu][j][i]*S->P[U1+mu-1][k][j][i]*S->P[U1+nu-1][k][j][i];
      }
    }
    if (fabs(vsq) < 1.e-13)
      vsq = 1.e-13;
    if (vsq >= 1.) {
      vsq = 1. - 1./(GAMMAMAX*GAMMAMAX);
    }
    gamma = 1./sqrt(1. - vsq);
    S->P[U1][k][j][i] *= gamma;
    S->P[U2][k][j][i] *= gamma;
    S->P[U3][k][j][i] *= gamma;
  }
}

void fix_flux(struct FluidFlux *F)
{
  if (global_start[0] == 0) {
    #pragma omp parallel for collapse(2)
    KSLOOP(0, N3) { 
      JSLOOP(0, N2) {
        F->X1[RHO][k][j][0+NG] = MY_MIN(F->X1[RHO][k][j][0+NG], 0.);
      }
    }
  }

  if (global_stop[0] == N1TOT) {
    #pragma omp parallel for collapse(2)
    KSLOOP(0, N3+NG-1) {
      JSLOOP(0, N2+NG-1) {
        F->X1[RHO][k][j][N1+NG] = MY_MAX(F->X1[RHO][k][j][N1+NG], 0.);
      }
    }
  }

  if (global_start[1] == 0) {
    #pragma omp parallel for collapse(2)
    KSLOOP(0, N3) {
      ISLOOP(0, N1) {
        F->X1[B2][k][-1+NG][i] = -F->X1[B2][k][0+NG][i];
        F->X3[B2][k][-1+NG][i] = -F->X3[B2][k][0+NG][i];
        PLOOP F->X2[ip][k][0+NG][i] = 0.;
      }
    }
  }

  if (global_stop[1] == N2TOT) {
    #pragma omp parallel for collapse(2)
    KSLOOP(0, N3) {
      ISLOOP(0, N1) {
        F->X1[B2][k][N2+NG][i] = -F->X1[B2][k][N2-1+NG][i];
        F->X3[B2][k][N2+NG][i] = -F->X3[B2][k][N2-1+NG][i];
        PLOOP F->X2[ip][k][N2+NG][i] = 0.;
      }
    }
  }
}
#endif // METRIC
