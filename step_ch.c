
/**
 *
 * this contains the generic piece of code for advancing
 * the primitive variables 
 *
**/

/*

added high order emf reconstruction

cfg 12.29.13

*/

#include "decs.h"
#include <mpi.h>

/** algorithmic choices **/

/* use local lax-friedrichs or HLL flux:  these are relative weights on each numerical flux */
#define HLLF  (0.0)
#define LAXF  (1.0)

/** end algorithmic choices **/


/* local functions for step_ch */
double advance(grid_prim_type pi, grid_prim_type pb,
	       double Dt, grid_prim_type pf);
double fluxcalc(grid_prim_type pr, grid_prim_type F,
		int dir);
void flux_ct(grid_prim_type F1, grid_prim_type F2, grid_prim_type F3);

/***********************************************************************************************/
/***********************************************************************************************
  step_ch():
  ---------
     -- handles the sequence of making the time step, the fixup of unphysical values, 
        and the setting of boundary conditions;

     -- also sets the dynamically changing time step size;

***********************************************************************************************/
void step_ch()
{
	double ndt;
	int i, j, k;

	//fprintf(stderr, "h");
	ndt = advance(p, p, 0.5 * dt, ph);	/* time step primitive variables to the half step */
	fixup(ph);		/* Set updated densities to floor, set limit for gamma */
	fixup_utoprim(ph);	/* Fix the failure points using interpolation and updated ghost zone values */
	bound_prim(ph);		/* Set boundary conditions for primitive variables, flag bad ghost zones */
	//fixup_utoprim(ph);	/* Fix the failure points using interpolation and updated ghost zone values */
	//bound_prim(ph);		/* Reset boundary conditions with fixed up points */

	/* step Lagrangian tracer particles forward */
	if(NPTOT > 0) advance_particles(ph, dt);

	/* Repeat and rinse for the full time (aka corrector) step:  */
	//fprintf(stderr, "f");
	ZSLOOP(-NG,N1-1+NG,-NG,N2-1+NG,-NG,N3-1+NG) PLOOP psave[i][j][k][ip] = p[i][j][k][ip];

	ndt = advance(p, ph, dt, p);

	dtsave = dt ;

#if ( ALLOW_COOL )
	/* apply cooling function directly to internal energy */
	cool_down(p, dt);
#endif

	fixup(p);
	fixup_utoprim(p);
	bound_prim(p);
	//bound_prim(p);

	/* Determine next time increment based on current characteristic speeds: */
	if (dt < 1.e-9) {
		fprintf(stderr, "timestep too small\n");
		exit(11);
	}

	/* increment time */
	t += dt;

	/* set next timestep */
	if (ndt > SAFE * dt) ndt = SAFE * dt;
	dt = ndt;
	dt = mpi_min(dt);
	if (t + dt > tf) dt = tf - t;	/* but don't step beyond end of run */

	/* done! */
}

/***********************************************************************************************
  advance():
  ---------
     -- responsible for what happens during a time step update, including the flux calculation, 
         the constrained transport calculation (aka flux_ct()), the finite difference 
         form of the time integral, and the calculation of the primitive variables from the 
         update conserved variables;
     -- also handles the "fix_flux()" call that sets the boundary condition on the fluxes;

***********************************************************************************************/
double advance(grid_prim_type pi,
	       grid_prim_type pb,
	       double Dt, 
	       grid_prim_type pf)
{
	int i, j, k;
	double ndt, ndt1, ndt2, ndt3, U[NPR], dU[NPR];
	struct of_state q;

	ZLOOP PLOOP pf[i][j][k][ip] = pi[i][j][k][ip];	/* needed for Utoprim */

	//fprintf(stderr, "0");

	ndt1 = fluxcalc(pb, F1, 1);
	ndt2 = fluxcalc(pb, F2, 2);
	ndt3 = fluxcalc(pb, F3, 3);

	fix_flux(F1, F2, F3);

	flux_ct(F1, F2, F3);

	/* evaluate diagnostics based on fluxes */
	diag_flux(F1, F2, F3);

	//fprintf(stderr, "1");
	/** now update pi to pf **/
#pragma omp parallel \
 shared ( pi, pb, pf, F1, F2, F3, ggeom, pflag, dx, Dt, fail_save ) \
 private ( i, j, k, dU, q, U )
{
#pragma omp for
	ZLOOP {

		source(pb[i][j][k], &(ggeom[i][j][CENT]), i, j, dU, Dt);
		get_state(pi[i][j][k], &(ggeom[i][j][CENT]), &q);
		primtoU(pi[i][j][k], &q, &(ggeom[i][j][CENT]), U);

		PLOOP {
			U[ip] +=
			    Dt * (- (F1[i + 1][j][k][ip] - F1[i][j][k][ip]) / dx[1]
				      - (F2[i][j + 1][k][ip] - F2[i][j][k][ip]) / dx[2]
				      - (F3[i][j][k + 1][ip] - F3[i][j][k][ip]) / dx[3]
				  + dU[ip]
			    );
		}

		pflag[i][j][k] = Utoprim_mm(U, &(ggeom[i][j][CENT]), pf[i][j][k]);
		if(pflag[i][j][k]) fail_save[i][j][k] = 1;
	}
}

	ndt = defcon * 1. / (1. / ndt1 + 1. / ndt2 + 1. / ndt3);
	//fprintf(stderr,"\nndt: %g %g %g\n",ndt,ndt1,ndt2) ;
	//fprintf(stderr, "2");

	return (ndt);
}


/***********************************************************************************************/
/***********************************************************************************************
  fluxcalc():
  ---------
     -- sets the numerical fluxes, avaluated at the cell boundaries using the slope limiter
        slope_lim();

     -- only has HLL and Lax-Friedrichs  approximate Riemann solvers implemented;
        
***********************************************************************************************/
#define OFFSET	1
double fluxcalc(grid_prim_type pr, grid_prim_type F, int dir)
{
	int i, j, k ;
	double p_l[NMAX+2*NG][NPR], p_r[NMAX+2*NG][NPR] ;
	double ndt;
	double cij,cmax;	//[N1+2*NG][N2+2*NG][N3+2*NG] ;
	double ptmp[NMAX+2*NG][NPR] ;

	ndt = 1.e3 ;
	cmax = 0.;

	if(dir == 1) {
		/* loop over other direction */

#pragma omp parallel for \
 shared ( pr, ggeom, F, dir ) \
 private ( ptmp, p_l, p_r, i, j, k ) \
 reduction(max:cmax)
		JSLOOP(-1,N2) 
		KSLOOP(-1,N3) {

			/* copy out variables and operate on those */
			ISLOOP(-NG,N1-1+NG) PLOOP ptmp[i][ip] = pr[i][j][k][ip] ;

			/* reconstruct left & right states */
#if (RECON_LIN)
			reconstruct_lr_lin(ptmp, N1, p_l, p_r) ;
#elif (RECON_PARA)
			reconstruct_lr_par(ptmp, N1, p_l, p_r) ;
#elif (RECON_WENO)
			reconstruct_lr_weno(ptmp, N1, p_l, p_r) ;
#endif

			ISLOOP(0,N1) {
				lr_to_flux(p_r[i-1],p_l[i], &(ggeom[i][j][FACE1]), dir, F[i][j][k], &cij) ;
				cmax = (cij > cmax ? cij : cmax);
			}
		}
	}
	else if(dir == 2) {
		/* loop over other direction */
#pragma omp parallel for \
 shared ( pr, ggeom, F, dir ) \
 private ( ptmp, p_l, p_r, i, j, k ) \
 reduction(max:cmax)
		ISLOOP(-1,N1)
		KSLOOP(-1,N3) {

			/* copy out variables and operate on those */
			JSLOOP(-NG,N2-1+NG) PLOOP ptmp[j][ip] = pr[i][j][k][ip] ;

			/* reconstruct left & right states */
#if (RECON_LIN)
			reconstruct_lr_lin(ptmp, N2, p_l, p_r) ;
#elif (RECON_PARA)
			reconstruct_lr_par(ptmp, N2, p_l, p_r) ;
#elif (RECON_WENO) 
			reconstruct_lr_weno(ptmp, N2, p_l, p_r) ;
#endif

			JSLOOP(0,N2) {
				lr_to_flux(p_r[j-1],p_l[j], &(ggeom[i][j][FACE2]), dir, F[i][j][k], &cij);
				cmax = (cij > cmax ? cij : cmax);
			}

		}
	}
	else if(dir == 3) {
		/* or the other direction */
#pragma omp parallel for \
 shared ( pr, ggeom, F, dir ) \
 private ( ptmp, p_l, p_r, i, j, k ) \
 reduction(max:cmax)
		ISLOOP(-1,N1)
		JSLOOP(-1,N2) {

			/* copy out variables and operate on those */
			KSLOOP(-NG,N3-1+NG) PLOOP ptmp[k][ip] = pr[i][j][k][ip];

			/* reconstruct left & right states */
#if (RECON_LIN)
			reconstruct_lr_lin(ptmp, N3, p_l, p_r) ;
#elif (RECON_PARA)
			reconstruct_lr_par(ptmp, N3, p_l, p_r) ;
#elif (RECON_WENO) 
			reconstruct_lr_weno(ptmp, N3, p_l, p_r) ;
#endif

			KSLOOP(0,N3) {
				lr_to_flux(p_r[k-1],p_l[k], &(ggeom[i][j][FACE3]), dir, F[i][j][k], &cij);
				cmax = (cij > cmax ? cij : cmax);
			}
				
		}
	}
		

	ndt = cour * dx[dir] / cmax;
	//ZLOOP {
	//	dtij = cour * dx[dir] / cmax[i][j][k];
	//	if (dtij < ndt) ndt = dtij;
	//}

	return (ndt);

}

/***********************************************************************************************/
/***********************************************************************************************
  flux_ct():
  ---------
     -- performs the flux-averaging used to preserve the del.B = 0 constraint (see Toth 2000);
        
***********************************************************************************************/
void flux_ct(grid_prim_type F1, grid_prim_type F2, grid_prim_type F3)
{
	int i, j, k;
	static double emf1[N1 + 2*NG][N2 + 2*NG][N3 + 2*NG];
	static double emf2[N1 + 2*NG][N2 + 2*NG][N3 + 2*NG];
	static double emf3[N1 + 2*NG][N2 + 2*NG][N3 + 2*NG];

	/* calculate EMFs */
	/* Toth approach: just average to corners */
	/* best done in scalar mode! */
	ZSLOOP(0, N1, 0, N2, 0, N3) {
		emf3[i][j][k] = 0.25 * (F1[i][j][k][B2] + F1[i][j - 1][k][B2]
		                               - F2[i][j][k][B1] - F2[i - 1][j][k][B1]);
		emf2[i][j][k] = -0.25 * (F1[i][j][k][B3] + F1[i][j][k-1][B3]
                                       - F3[i][j][k][B1] - F3[i-1][j][k][B1]);
		emf1[i][j][k] = 0.25 * (F2[i][j][k][B3] + F2[i][j][k-1][B3]
                                       - F3[i][j][k][B2] - F3[i][j-1][k][B2]);
	}

	/* rewrite EMFs as fluxes, after Toth */
	ZSLOOP(0, N1, 0, N2 - 1, 0, N3 - 1) {
		F1[i][j][k][B1] = 0.;
		F1[i][j][k][B2] = 0.5 * (emf3[i][j][k] + emf3[i][j + 1][k]);
		F1[i][j][k][B3] = -0.5 * (emf2[i][j][k] + emf2[i][j][k + 1]);
	}
	ZSLOOP(0, N1 - 1, 0, N2, 0, N3 - 1) {
		F2[i][j][k][B1] = -0.5 * (emf3[i][j][k] + emf3[i + 1][j][k]);
		F2[i][j][k][B2] = 0.;
		F2[i][j][k][B3] = 0.5 * (emf1[i][j][k] + emf1[i][j][k + 1]);
	}
	ZSLOOP(0, N1 - 1, 0, N2 - 1, 0, N3) {
		F3[i][j][k][B1] = 0.5 * (emf2[i][j][k] + emf2[i + 1][j][k]);
		F3[i][j][k][B2] = -0.5 * (emf1[i][j][k] + emf1[i][j + 1][k]);
		F3[i][j][k][B3] = 0.;
	}

}

void lr_to_flux(double p_l[NPR], double p_r[NPR], struct of_geom *geom, 
		int dir, double Flux[NPR], double *max_speed)
{
	struct of_state state_l, state_r ;
	double F_l[NPR],F_r[NPR],U_l[NPR],U_r[NPR] ;
	double cmax_l,cmax_r,cmin_l,cmin_r,cmax,cmin,ctop ;
	int k ;

	if(geom->g < SMALL) {
		for(k=0;k<8;k++) Flux[k] = 0.;
		*max_speed = 0.;
		return;
	}

	get_state(p_l, geom, &state_l);
	get_state(p_r, geom, &state_r);

	primtoflux(p_l, &state_l, dir, geom, F_l);
	primtoflux(p_r, &state_r, dir, geom, F_r);

	primtoflux(p_l, &state_l, TT, geom, U_l);
	primtoflux(p_r, &state_r, TT, geom, U_r);

	mhd_vchar(p_l, &state_l, geom, dir, &cmax_l, &cmin_l);
	mhd_vchar(p_r, &state_r, geom, dir, &cmax_r, &cmin_r);

	cmax = fabs(MY_MAX(MY_MAX(0., cmax_l), cmax_r));
	cmin = fabs(MY_MAX(MY_MAX(0., -cmin_l), -cmin_r));
	ctop = MY_MAX(cmax, cmin);

	for(k=0;k<8;k++) Flux[k] = 
		HLLF*((cmax*F_l[k] + cmin*F_r[k] - cmax*cmin*(U_r[k]-U_l[k]))/(cmax+cmin+SMALL)) 
		+ LAXF*(0.5*(F_l[k] + F_r[k] - ctop*(U_r[k] - U_l[k])));

	*max_speed = ctop ;

	return ;
}


