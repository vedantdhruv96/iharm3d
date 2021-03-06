/******************************************************************************
 *                                                                            *
 * PARAMETERS.H                                                               *
 *                                                                            *
 * PROBLEM-SPECIFIC CHOICES                                                   *
 *                                                                            *
 ******************************************************************************/

/* GLOBAL RESOLUTION */
#define N1TOT 256
#define N2TOT 1
#define N3TOT 1

/* MPI DECOMPOSITION */
#define N1CPU 1
#define N2CPU 1
#define N3CPU 1

/* SPACETIME METRIC
 *   MINKOWSKI, MKS
 */
#define METRIC MINKOWSKI

/* ELECTRONS AND OPTIONS
 *   SUPPRESS_MAG_HEAT - (0,1) NO ELECTRON HEATING WHEN SIGMA > 1
 *   BETA_HEAT         - (0,1) BETA-DEPENDENT HEATING
 *   TPTEMIN           - MINIMUM TP/TE
 */
#define ELECTRONS           0
#define SUPPRESS_HIGHB_HEAT 1
#define BETA_HEAT           1
#define TPTEMIN             0.01

/* RECONSTRUCTION ALGORITHM
 *   LINEAR, PPM, WENO, MP5
 */
#define RECONSTRUCTION WENO

/* BOUNDARY CONDITIONS
 *   OUTFLOW PERIODIC POLAR USER
 */
#define X1L_BOUND OUTFLOW
#define X1R_BOUND OUTFLOW
#define X2L_BOUND OUTFLOW
#define X2R_BOUND OUTFLOW
#define X3L_BOUND OUTFLOW
#define X3R_BOUND OUTFLOW

