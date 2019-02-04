################################################################################
#                                                                              # 
#  CALCULATE TIME-DEPENDENT AND TIME-AVERAGED QUANTITIES                       #
#                                                                              # 
################################################################################

from __future__ import print_function, division

from analysis_fns import *
import hdf5_to_dict as io
import util

import sys
import multiprocessing
import psutil
import pickle
import itertools

import numpy as np

# Option to calculate fluxes at (just inside) r = 5
# This reduces interference from floors
floor_workaround_flux = False
# Option to ignore accretion at high magnetization (funnel)
# This also reduces interference from floors
floor_workaround_funnel = False

# Whether to calculate a couple of the more expensive variables
calc_lumproxy = False
calc_etot = False

if len(sys.argv) < 2:
  util.warn('Format: python eht_analysis.py /path/to/dumps [start time] [start radial averages] [stop radial averages] [stop time]')
  sys.exit()

# This doesn't seem like the _right_ way to do optional args
# Skips everything before tstart, averages between tavg_start and tavg_end
tstart = None
tavg_start = None
tavg_end = None
tend = None
if sys.argv[1] == "-d":
  debug = True
  path = sys.argv[2]
  if len(sys.argv) > 3:
    tstart = float(sys.argv[3])
  if len(sys.argv) > 4:
    tavg_start = float(sys.argv[4])
  if len(sys.argv) > 5:
    tavg_end = float(sys.argv[5])
  if len(sys.argv) > 6:
    tend = float(sys.argv[6])
else:
  debug = False
  path = sys.argv[1]
  if len(sys.argv) > 2:
    tstart = float(sys.argv[2])
  if len(sys.argv) > 3:
    tavg_start = float(sys.argv[3])
  if len(sys.argv) > 4:
    tavg_end = float(sys.argv[4])
  if len(sys.argv) > 5:
    tend = float(sys.argv[5])

if tstart > 0 or tend < 10000:
  outfname = "eht_out_{}_{}.p".format(tstart,tend)
else:
  outfname = "eht_out.p"

# This file is deleted or moved if intended to be replaced
if os.path.exists(outfname):
  exit(0)

dumps = io.get_dumps_list(path)
ND = len(dumps)

hdr = io.load_hdr(dumps[0])
geom = io.load_geom(hdr, path)

# If the time after which to average wasn't given, just use the back half of dumps
if tavg_start is None:
  tavg_start = io.load_dump(dumps[ND//2], hdr, geom, derived_vars=False, extras=False)['t'] - 0.1
# Sometimes we don't know times but want averages
# This is always when we've converted dumps given only over quiescence, and want averages over all of them
if tavg_start < 0.:
  tavg_start = 0.

if tavg_end is None:
  tavg_end = io.load_dump(dumps[ND-1], hdr, geom, derived_vars=False, extras=False)['t']
if tavg_end == 0.:
  tavg_end = float(ND)

if tstart is None:
  tstart = 0.

if tend is None:
  tend = io.get_dump_time(dumps[-1])+1

# Decide where to measure fluxes
def i_of(rcoord):
  i = 0
  while geom['r'][i,hdr['n2']//2,0] < rcoord:
    i += 1
  i -= 1
  return i

# Leave several extra zones if using MKS3 coordinates
if geom['metric'] == "MKS3":
  iEH = i_of(hdr['r_eh'])+4
else:
  iEH = i_of(hdr['r_eh'])

if floor_workaround_flux:
  iF = i_of(5) # Measure fluxes at r=5M
else:
  iF = iEH

# Max radius when computing "total" energy
iEmax = i_of(40)

# BZ luminosity
# 100M seems like the standard measuring spot (or at least, BHAC does it that way)
# L_BZ seems constant* after that, but much higher within ~50M
if geom['r_out'] < 100 or geom['r'][-1,geom['n2']//2,0] < 100: # If in theory or pracice the sim is small...
  iBZ = i_of(40) # most SANEs
else:
  iBZ = i_of(100) # most MADs

jmin, jmax = get_j_vals(geom)

print("Using EH at zone {}, Fluxes at zone {}, Emax within zone {}, L_BZ at zone {}".format(iEH, iF, iEmax, iBZ))

def avg_dump(n):
  out = {}

  out['t'] = io.get_dump_time(dumps[n])
  # When we don't know times, fudge
  if out['t'] == 0 and n != 0:
    out['t'] = n

  if out['t'] < tstart or out['t'] > tend:
    #print("Loaded {} / {}: {} (SKIPPED)".format((n+1), len(dumps), out['t']))
    return out
  else:
    print("Loaded {} / {}: {}".format((n+1), len(dumps), out['t']))
    dump = io.load_dump(dumps[n], hdr, geom, extras=False)

  # Variables that will be referenced a bunch
  tem_rt = TEM_mixed(dump, 1, 0)
  tem_rphi = TEM_mixed(dump, 1, 3)
  tfl_rt = TFl_mixed(dump, 1, 0)
  tfl_rphi = TFl_mixed(dump, 1, 3)
  tfull_rt = T_mixed(dump, 1, 0)
  tfull_rphi = T_mixed(dump, 1, 3)
  be_b = bernoulli(dump, with_B=True)
  be_nob = bernoulli(dump, with_B=False)
  
  Pg = (hdr['gam']-1.)*dump['UU']
  B = np.sqrt(dump['bsq'])

  rho_ur = dump['RHO']*dump['ucon'][:,:,:,1]
  sigma = dump['bsq']/dump['RHO']

  # These only twice; eliminate soon
  rur = geom['r']*dump['ucon'][:,:,:,1]
  gamma = get_gamma(geom, dump)

  # SHELL AVERAGES (only for t >= tavg_start usu. tmax/2 to end)
  if out['t'] >= tavg_start and out['t'] <= tavg_end:

    out['rho_r'] = eht_profile(geom, dump['RHO'], jmin, jmax)
    out['Theta_r'] = eht_profile(geom, Pg/dump['RHO'], jmin, jmax)
    out['B_r'] = eht_profile(geom, B, jmin, jmax)

    Pb = dump['bsq']/2
    out['Pg_r'] = eht_profile(geom, Pg, jmin, jmax)
    out['Ptot_r'] = eht_profile(geom, Pg + Pb, jmin, jmax)
    out['betainv_r'] = eht_profile(geom, Pb/Pg, jmin, jmax)

    out['uphi_r'] = eht_profile(geom, dump['ucon'][:,:,:,3], jmin, jmax)

    # THETA AVERAGES
    # TODO TODO Actually result in X2 profiles.  Multiply by dth/dX2 to complete the theta profiles
    Fcov01, Fcov13 = Fcov(geom, dump, 0, 1), Fcov(geom, dump, 1, 3)
    out['omega_hth'] = theta_av(geom, Fcov01, iEH, 1) / theta_av(geom, Fcov13, iEH, 1)
    out['omega_av_hth'] = theta_av(geom, Fcov01, iEH, 5) / theta_av(geom, Fcov13, iEH, 5)

    # This produces much worse results
    #out['omega_alt_th'] = theta_av(Fcov(dump, 0, 2), iEH, 1) / theta_av(Fcov(dump, 2, 3), iEH, 1)
    #out['omega_alt_av_th'] = theta_av(Fcov(dump, 0, 2), iEH-2, 5) / theta_av(Fcov(dump, 2, 3), iEH-2, 5)

  # Flux profiles for jet power
  profiles = {'RHO': dump['RHO'], 'bsq' : dump['bsq'], 'U' : dump['UU'], 'ut' : dump['ucov'][:,:,:,0], 'uphi' : dump['ucov'][:,:,:,3],
              'FM' : rho_ur, 'FE' : -tfull_rt, 'FE_EM' : -tem_rt, 'FE_Fl' : -tfl_rt, 'FL' : tfull_rphi, 'FL_EM' : tem_rphi, 'FL_Fl' : tfl_rphi,
              'sigma' : sigma, 'Be_b' : be_b, 'Be_nob' : be_nob, 'rur' : rur, 'gamma' : gamma}
  
  zones_av=5

  for key in profiles.keys():
    out[key+'_5_tht'] = np.sum(profiles[key][i_of(5)], axis=-1)
    out[key+'_100_tht'] = np.sum(profiles[key][iBZ], axis=-1)
    if out['t'] >= tavg_start and out['t'] <= tavg_end:
      out[key+'_5_th'] = out[key+'_5_tht']
      out[key+'_5_thphi'] = profiles[key][i_of(5),:,:]
      out[key+'_100_th'] = out[key+'_100_tht']
      out[key+'_100_thphi'] = profiles[key][iBZ,:,:]
      out[key+'_rth'] = profiles[key].mean(axis=-1)

  # The HARM B_unit is sqrt(4pi)*c*sqrt(rho) which has caused issues:
  #norm = np.sqrt(4*np.pi) # This is what I believe matches T,N,M '11 and Narayan '12
  norm = 1 # This is what the EHT comparison uses?  
  if geom['mixed_metrics']:
    # B1 will be in the _vector_ coordinates.  Must perform the integral in those instead of zone coords
    # Some gymnastics were done to keep in-memory size small
    dxEH = np.einsum("i,...ij->...j", np.array([0, geom['dx1'], geom['dx2'], geom['dx3']]), np.linalg.inv(geom['vec_to_grid'][iEH,:,:,:]))
    out['Phi_b'] = 0.5*norm * np.sum( np.fabs(dump['B1'][iEH,:,:]) * geom['gdet_vec'][iEH,:,None]*dxEH[:,None,2]*dxEH[:,None,3], axis=(0,1) )
  else:
    out['Phi_b'] = 0.5*norm*sum_shell(geom, np.fabs(dump['B1']), at_zone=iEH)

  # FLUXES
  # Radial profiles of Mdot and Edot, and their particular values
  # EHT normalization has both these values positive
  if out['t'] >= tavg_start and out['t'] <= tavg_end:
    out['FE_r'] = sum_shell(geom, -tfull_rt)
    out['Edot'] = out['FE_r'][iF]
  else:
    out['Edot'] = sum_shell(geom, -tfull_rt, at_zone=iF)

  if floor_workaround_funnel:
    # TODO implement all of this with 'mask='?
    mdot_full = rho_ur[iF,:,:]*geom['gdet'][iF,:,None]*hdr['dx2']*hdr['dx3']
    sigma_shaped = dump['bsq'][iF,:,:]/dump['RHO'][iF,:,:]
    out['Mdot'] = (mdot_full[np.where(sigma_shaped < 10)]).sum()
    if out['t'] >= tavg_start and out['t'] <= tavg_end:
      out['FM_r'] = -sum_shell(geom, rho_ur, mask=(sigma_shaped < 10))
  else:
    if out['t'] >= tavg_start and out['t'] <= tavg_end:
      out['FM_r'] = sum_shell(geom, rho_ur)
      out['Mdot'] = -out['FM_r'][iF]
    else:
      out['Mdot'] = -sum_shell(geom, rho_ur, at_zone=iF)

  out['Ldot'] = sum_shell(geom, tfull_rphi, at_zone=iF)

  out['sigma_max'] = np.max(sigma)

  # Blandford-Znajek Luminosity L_BZ
  if debug:
    # A bunch of radial profiles to test consistency
    out['LBZ_r'] = sum_shell(geom, -tem_rt, mask=(sigma > 1))
    out['LBZ'] = out['LBZ_r'][iBZ]
    
    mu = (-tfull_rt + rho_ur) / rho_ur
    out['LBZ_mu2_r'] = sum_shell(geom, -tem_rt, mask=np.logical_or(sigma > 1, mu > 2))
    out['LBZ_mu3_r'] = sum_shell(geom, -tem_rt, mask=np.logical_or(sigma > 1, mu > 3))
    out['LBZ_mu4_r'] = sum_shell(geom, -tem_rt, mask=np.logical_or(sigma > 1, mu > 4))
  else:
    # This is a lot of luminosities!
    #ucon_mean = np.mean(dump['ucon'][:,:,:,1], axis=-1)
    #out['Ltot'] = sum_shell(geom, tfull_rt + rho_ur, at_zone=iBZ, mask=( (ucon_mean > 1)[:,:,None] ))
    lbz = -tem_rt
    ltot = -tfull_rt - rho_ur

    out['LBZ_sigma1_rt'] = sum_shell(geom, lbz, mask=(sigma > 1.0))
    out['Ltot_sigma1_rt'] = sum_shell(geom, ltot, mask=(sigma > 1.0))
    out['LBZ_sigma10_rt'] = sum_shell(geom, lbz, mask=(sigma > 10.0))
    out['Ltot_sigma10_rt'] = sum_shell(geom, ltot, mask=(sigma > 10.0))
    
    out['LBZ_be_b0_rt'] = sum_shell(geom, lbz, mask=(be_b > 0.02))
    out['Ltot_be_b0_rt'] = sum_shell(geom, ltot, mask=(be_b > 0.02))
    out['LBZ_be_b1_rt'] = sum_shell(geom, lbz, mask=(be_b > 1.0))
    out['Ltot_be_b1_rt'] = sum_shell(geom, ltot, mask=(be_b > 1.0))
    
    out['LBZ_be_nob0_rt'] = sum_shell(geom, lbz, mask=(be_nob > 0.02))
    out['Ltot_be_nob0_rt'] = sum_shell(geom, ltot, mask=(be_nob > 0.02))
    out['LBZ_be_nob1_rt'] = sum_shell(geom, lbz, mask=(be_nob > 1.0))
    out['Ltot_be_nob1_rt'] = sum_shell(geom, ltot, mask=(be_nob > 1.0))
    
    out['LBZ_rur_rt'] = sum_shell(geom, lbz, mask=(rur > 1.0))
    out['Ltot_rur_rt'] = sum_shell(geom, ltot, mask=(rur > 1.0))
    
    out['LBZ_gamma_rt'] = sum_shell(geom, lbz, mask=(gamma > 1.5))
    out['Ltot_gamma_rt'] = sum_shell(geom, ltot, mask=(gamma > 1.5))
    
    out['LBZ_allp_rt'] = sum_shell(geom, lbz, mask=(ltot > 0.0))
    out['Ltot_allp_rt'] = sum_shell(geom, ltot, mask=(ltot > 0.0))
    
    if out['t'] >= tavg_start and out['t'] <= tavg_end:
      for tag in ['sigma1', 'sigma10', 'be_b0', 'be_b1', 'be_nob0', 'be_nob1', 'rur', 'gamma', 'allp']:
        out['LBZ_'+tag+'_r'] = out['LBZ_'+tag+'_rt']
        out['LBZ_'+tag] = out['LBZ_'+tag+'_r'][iBZ]
        out['Ltot_'+tag+'_r'] = out['Ltot_'+tag+'_rt']
        out['Ltot_'+tag] = out['Ltot_'+tag+'_r'][iBZ]
    else:
      for tag in ['sigma1', 'sigma10', 'be_b0', 'be_b1', 'be_nob0', 'be_nob1', 'rur', 'gamma', 'allp']:
        out['LBZ_'+tag] = out['LBZ_'+tag+'_rt'][iBZ]
        out['Ltot_'+tag] = out['Ltot_'+tag+'_rt'][iBZ]

  if calc_lumproxy:
    rho = dump['RHO']
    C = 0.2
    j = rho**3 * Pg**(-2) * np.exp(-C*(rho**2 / (B*Pg**2))**(1./3.))
    out['Lum'] = eht_vol(geom, j, jmin, jmax, outside=iEH)

  if calc_etot:
    tfull_tt = T_mixed(dump, 0,0)
    out['Etot'] = sum_vol(geom, tfull_tt, within=iEmax)
    #print "Energy on grid: ",out['Etot']

    # For an averaged energy profile
    #out['E_r'] = radial_sum(geom, tfull_tt)

  return out

if debug:
  # SERIAL (very slow)
  out_list = [avg_dump(n) for n in range(len(dumps))]
else:
  # PARALLEL: TODO put in util.py
  nthreads = util.calc_nthreads(hdr, pad=0.2)
  pool = multiprocessing.Pool(nthreads)
  try:
    # Map the above function to the dump numbers, returning a list of 'out' dicts
    out_list = pool.map_async(avg_dump, list(range(len(dumps)))).get(99999999)
  except KeyboardInterrupt:
    pool.terminate()
    pool.join()
  else:
    pool.close()
    pool.join()

# TODO this properly some other day
nstart, nmin, nmax, nend = int(tstart)//5, int(tavg_start)//5, int(tavg_end)//5, int(tend)//5

full_avg_range = nmax - nmin

if nmin < nstart: nmin = nstart
if nmin > nend: nmin = nend
if nmax < nstart: nmax = nstart
if nmax > nend: nmax = nend

my_avg_range = nmax - nmin

print("nstart = {}, nmin = {}, nmax = {} nend = {}".format(nstart,nmin,nmax,nend))

# Make a dict for merged variables
out_full = {}
out_full['a'] = hdr['a']
# Toss in the common geom lists and our weight in the overall average
out_full['r'] = geom['r'][:,hdr['n2']//2,0]

# For quick angular plots. Note most will need geometry to convert from dX2 to dth
out_full['th_eh'] = geom['th'][iEH,:hdr['n2']//2,0]
out_full['th_5'] = geom['th'][i_of(5),:,0]
out_full['th_100'] = geom['th'][iBZ,:,0]

out_full['phi'] = geom['phi'][0,hdr['n2']//2,:]

out_full['avg_start'] = tavg_start
out_full['avg_end'] = tavg_end
out_full['avg_w'] = my_avg_range / full_avg_range
print("Will weight averages by {}".format(out_full['avg_w']))

# Merge the output dicts
#print(out_list[nmin].keys())
for key in list(out_list[nmin].keys()):
  if key[-2:] == '_r' or key[-3:] == '_rt':
    out_full[key] = np.zeros((ND, hdr['n1']))
    for n in range(ND):
      if key in out_list[n]:
        out_full[key][n,:] = out_list[n][key]
  elif key[-4:] == '_hth' or key[-5:] == '_htht':
    out_full[key] = np.zeros((ND, hdr['n2']//2))
    for n in range(ND):
      if key in out_list[n]:
        out_full[key][n,:] = out_list[n][key]
  elif key[-3:] == '_th' or key[-4:] == '_tht':
    out_full[key] = np.zeros((ND, hdr['n2']))
    for n in range(ND):
      if key in out_list[n]:
        out_full[key][n,:] = out_list[n][key]
  elif key[-4:] == '_rth' or key[-5:] == '_rtht':
    out_full[key] = np.zeros((ND, hdr['n1'], hdr['n2']))
    for n in range(ND):
      if key in out_list[n]:
        out_full[key][n,:,:] = out_list[n][key]
  elif key[-6:] == '_thphi' or key[-7:] == '_thphit':
    out_full[key] = np.zeros((ND, hdr['n2'], hdr['n3']))
    for n in range(ND):
      if key in out_list[n]:
        out_full[key][n,:,:] = out_list[n][key]
  else:
    out_full[key] = np.zeros(ND)
    for n in range(ND):
      if key in out_list[n]:
        out_full[key][n] = out_list[n][key]

# Average items with certain names.  Note suffixing 't' keeps the full NDxN1 or NDxN2/2 array
for key in out_full:
  if key[-2:] == '_r' or key[-3:] == '_th':
    out_full[key] = (out_full[key][nmin:nmax,:]).mean(axis=0)
  elif key[-4:] == '_rth' or key[-6:] == '_thphi':
    out_full[key] = (out_full[key][nmin:nmax,:,:]).mean(axis=0)

# Compat/completeness stuff
out_full['mdot'] = out_full['Mdot']
out_full['phi_b'] = out_full['Phi_b']/np.sqrt(out_full['Mdot'])

#print(out_full.keys())

# OUTPUT
with open(outfname, "wb") as outf:
  print("Writing {}".format(outfname))
  pickle.dump(out_full, outf)
