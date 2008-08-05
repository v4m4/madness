#define WORLD_INSTANTIATE_STATIC_TEMPLATES

#include "eigsolver.h"
#include "util.h"

using std::cout;
using std::endl;

namespace madness
{

  //***************************************************************************
  template <typename T>
  EigSolver<T>::EigSolver(World& world, std::vector<funcT> phis,
      std::vector<double> eigs, std::vector< EigSolverOp<T>* > ops,
      std::vector<kvec3dT> kpoints, double thresh)
  : _phis(phis), _eigs(eigs), _ops(ops), _kpoints(kpoints), _world(world), _thresh(thresh)
  {
    _rho = EigSolver::compute_rho(phis, world);
    _periodic = true;
  }
  //***************************************************************************

  //***************************************************************************
  template <typename T>
  EigSolver<T>::EigSolver(World& world, std::vector<funcT> phis,
      std::vector<double> eigs, std::vector< EigSolverOp<T>* > ops, double thresh)
  : _phis(phis), _eigs(eigs), _ops(ops), _world(world), _thresh(thresh)
  {
    _rho = EigSolver::compute_rho(phis, world);
    _periodic = false;
  }
  //***************************************************************************

  //***************************************************************************
  template <typename T>
  EigSolver<T>::~EigSolver()
  {
    // Eigsolver is responsible for deleting the ops
    for (typename std::vector< EigSolverOp<T>* >::iterator it = _ops.begin(); it != _ops.end();
      it++) delete (*it);
    _ops.clear();
    // Clear eigenvectors
    _phis.clear();
    // Clear eigenvalues
    _eigs.clear();
    // Clear observers
    _obs.clear();
  }
  //***************************************************************************

  //***************************************************************************
  template <typename T>
  Function<T,3> EigSolver<T>::compute_rho(typename std::vector<funcT> phis, const World& world)
  {
    // Electron density
    Function<double,3> rho = FunctionFactory<double,3>(const_cast<World&>(world));
    // Loop over all wavefunctions to compute density
    for (typename std::vector<funcT>::const_iterator pj = phis.begin();
      pj != phis.end(); ++pj)
    {
      // Get phi(j) from iterator
      const funcT& phij = (*pj);
      // Compute the j-th density
      Function<double,3> prod = square(phij);
      rho += prod;
    }
    rho.truncate();
    return rho;
  }
  //***************************************************************************

  //***************************************************************************
  template <typename T>
  void EigSolver<T>::prepare_ops()
  {
    // Loop through all of the density-dependent ops and prepare them, i.e.
    // build the rho-dependent potentials.
    for (unsigned int oi = 0; oi < _ops.size(); oi++)
    {
      EigSolverOp<T>* op = _ops[oi];
      // Prepare density-dependent operator
      if (op->is_rd()) op->prepare_op(_rho);
    }
  }
  //***************************************************************************

  //***************************************************************************
  template <typename T>
  T EigSolver<T>::matrix_element(const funcT& phii, const funcT& phij)
  {
    double value = 0.0;
    // Kinetic energy operator
    for (int axis = 0; axis < 3; axis++)
    {
      funcT dpsi_j = diff(phij, axis);
      funcT dpsi_i = diff(phii, axis);
      value += 0.5 * inner(dpsi_i, dpsi_j);
    }
    // Loop through all ops
    for (unsigned int oi = 0; oi < _ops.size(); oi++)
    {
      EigSolverOp<T>* op = _ops[oi];
      // Operate with density-dependent operator
      if (op->is_rd()) value += op->coeff() * phii.inner(op->op_r(_rho, phij));
      // Operate with orbital-dependent operator
      if (op->is_od()) value += op->coeff() * phii.inner(op->op_o(_phis, phij));
    }
    return value;
  }
  //***************************************************************************

  //***************************************************************************
  template <typename T>
  void EigSolver<T>::make_bsh_operators()
  {
    // Clear BSH vector
    _bops.clear();
    // Get defaults
    int k = FunctionDefaults<3>::get_k();
    double tol = FunctionDefaults<3>::get_thresh();
    // Loop through eigenvalues, adding a BSH operator to _bops
    // for each eigenvalue
    int sz = _phis.size();
    for (int i = 0; i < sz; i++)
    {
        double eps = _eigs[i];
        if (eps > 0)
        {
            if (_world.rank() == 0)
            {
                print("bsh: warning: positive eigenvalue", i, eps);
            }
            eps = -0.1;
        }
        _bops.push_back(poperatorT(BSHOperatorPtr<double,3>(_world, sqrt(-2.0*eps), k, 1e-4, tol)));
    }
  }
  //***************************************************************************

  //***************************************************************************
  template <typename T>
  void EigSolver<T>::print_matrix_elements(const funcT& phii, const funcT& phij)
  {
    T value = 0.0;
    // Kinetic energy operator
    for (int axis = 0; axis < 3; axis++)
    {
      funcT dpsi_j = diff(phij, axis);
      funcT dpsi_i = diff(phii, axis);
      value += 0.5 * inner(dpsi_i, dpsi_j);
    }
    if (_world.rank() == 0)
    {
      cout << "***** Evaluation of matrix elements *****" << endl;
      cout << "KineticEnergyOp:\t\t\t" << value << endl;
    }

    // Loop through all ops
    for (unsigned int oi = 0; oi < _ops.size(); oi++)
    {
      value = 0.0;
      EigSolverOp<T>* op = _ops[oi];
      // Operate with density-dependent operator
      if (op->is_rd()) value += op->coeff() * phii.inner(op->op_r(_rho, phij));
      // Operate with orbital-dependent operator
      if (op->is_od()) value += op->coeff() * phii.inner(op->op_o(_phis, phij));
      if (_world.rank() == 0)
      {
        cout << op->messsageME() << ":\t\t\t" << value << endl;
      }
    }
    if (_world.rank() == 0) printf("\n\n");
  }
  //***************************************************************************

  //***************************************************************************
  template <typename T>
  void EigSolver<T>::solve(int maxits)
  {
    for (int it = 0; it < maxits; it++)
    {
      // Since, the density has already been computed (it's fresh outta the
      // oven), go ahead and build all of the density-dependent potentials that
      // we can.
      prepare_ops();
      if (_world.rank() == 0) printf("Iteration #%d\n\n", it);
      for (unsigned int pi = 0; pi < _phis.size(); pi++)
      {
        // Get psi from collection
        funcT psi = _phis[pi];
        funcT pfunc = FunctionFactory<T,3>(_world);
        // Loop through all ops
        if (_world.rank() == 0) madness::print("Looping through the ops ...\n\n");
        for (unsigned int oi = 0; oi < _ops.size(); oi++)
        {
          EigSolverOp<T>* op = _ops[oi];
          // Operate with density-dependent operator
          if (op->is_rd()) pfunc += op->coeff() * op->op_r(_rho, psi);
          // Operate with orbital-dependent operator
          if (op->is_od()) pfunc += op->coeff() * op->op_o(_phis, psi);
        }
        if (_world.rank() == 0) madness::print("Creating BSH operator ...\n\n");
        SeparatedConvolution<T,3>* op = 0;
        if (_periodic)
        {
          // Subtract the k dot nabla part
          kvec3dT k = _kpoints[pi];
          pfunc -= k[0] * diff(psi, 0);
          pfunc -= k[1] * diff(psi, 1);
          pfunc -= k[2] * diff(psi, 2);
          pfunc.scale(-2.0).truncate(_thresh);
          op = BSHOperatorPtr<T,3>(_world, sqrt(-2.0*_eigs[pi]),
              FunctionDefaults<3>::get_k(), 1e-4, _thresh);
        }
        else
        {
          pfunc.scale(-2.0).truncate(_thresh);
          op = BSHOperatorPtr<T,3>(_world, sqrt(-2.0*_eigs[pi]),
              FunctionDefaults<3>::get_k(), 1e-4, _thresh);
        }
        // Apply the Green's function operator (stubbed)
        if (_world.rank() == 0) madness::print("Applying BSH operator ...\n\n");
        funcT tmp = apply(*op, pfunc);
        // (Not sure whether we have to do this mask thing or not!)
        // WSTHORNTON DEBUG
        double ttnorm = tmp.norm2();
        if (_world.rank() == 0) madness::print("pi = ", pi, "\tttnorm = %.5f\n\n", ttnorm);
        if (_world.rank() == 0) printf("Gram-Schmidt ...\n\n");
        for (unsigned int pj = 0; pj < pi; ++pj)
        {
//          // Make sure that pi != pj
//          MADNESS_ASSERT(pi != pj);
          // Project out the lower states
          // Get other wavefunction
          funcT psij = _phis[pj];
          double overlap = inner(tmp, psij);
          tmp -= overlap*psij;
        }
        // WSTHORNTON DEBUG
        double tttnorm = tmp.norm2();
        if (_world.rank() == 0) printf("pi = %d\tttnorm = %.5f\n\n", pi, tttnorm);
        // Update e
        if (_world.rank() == 0) printf("Updating e ...\n\n");
        funcT r = tmp - psi;
        double tnorm = tmp.norm2();
        double eps_old = _eigs[pi];
        double ecorrection = -0.5*inner(pfunc, r) / (tnorm*tnorm);
        double eps_new = eps_old + ecorrection;
        double rnorm = r.norm2();
        if (_world.rank() == 0) printf("pi = %d\trnorm = %.5f\n\n", pi, rnorm);
        if (_world.rank() == 0) printf("pi = %d\teps_new = %.5f\teps_old = %.5f\n\n", pi, eps_new, eps_old);
        // Sometimes eps_new can go positive, THIS WILL CAUSE THE ALGORITHM TO CRASH. So,
        // I bounce the new eigenvalue back into the negative side of the real axis. I
        // keep doing this until it's good or I've already done it 10 times.
        // WSTHORNTON DEBUG
//        double rnorm = r.norm2();
//        if (_world.rank() == 0) printf("pi = %d\trnorm = %.5f\ttnorm = %.5f\n\n", pi, rnorm, tnorm);
        int counter = 0;
        while (eps_new >= 0.0 && counter < 10)
        {
          // Split the difference between the new and old estimates of the
          // pi-th eigenvalue.
          eps_new = eps_old + 0.5*(eps_new - eps_old);
          counter++;
        }
        // Still no go, forget about it. (1$ to Donnie Brasco)
        if (eps_new >= 0.0)
        {
          printf("FAILURE OF WST: exiting!!\n\n");
          _exit(0);
        }
        // Update the eigenvalue estimates and wavefunctions.
        tmp.truncate(_thresh);
        _eigs[pi] = eps_new;
        _phis[pi] = tmp.scale(1.0/tmp.norm2());
      }
      // Update rho
//      if (_world.rank() == 0) printf("Computing new density for it == #%d\n\n", it);
      _rho = EigSolver::compute_rho(_phis, _world);
      // Output to observables
      for (typename std::vector<IEigSolverObserver<T>*>::iterator itr = _obs.begin(); itr
        != _obs.end(); ++itr)
      {
        (*itr)->iterateOutput(_phis, _eigs, _rho, it);
      }
    }
  }
  //***************************************************************************

  //***************************************************************************
  template <typename T>
  void EigSolver<T>::multi_solve(int maxits)
  {
    for (int it = 0; it < maxits; it++)
    {
      // Since, the density has already been computed (it's fresh outta the
      // oven), go ahead and build all of the density-dependent potentials that
      // we can.
      prepare_ops();
      if (_world.rank() == 0) printf("Iteration #%d\n\n", it);
      // Create empty functions for calculations
      vector<funcT> pfuncs(_phis.size());
      for (unsigned int pi = 0; pi < _phis.size(); pi++)
        pfuncs[pi] = FunctionFactory<T, 3>(_world);
      // Loop through all ops to work on a vector of functions
      if (_world.rank() == 0) printf("Looping through the ops ...\n\n");
      for (unsigned int oi = 0; oi < _ops.size(); oi++)
      {
        EigSolverOp<T>* op = _ops[oi];
        // Operate with density-dependent operator
        if (op->is_rd()) gaxpy(_world, 1.0, pfuncs, op->coeff(), op->multi_op_r(_rho, _phis));
        // Operate with orbital-dependent operator
        if (op->is_od()) gaxpy(_world, 1.0, pfuncs, op->coeff(), op->multi_op_o(_phis));
      }
//      // WSTHORNTON DEBUG
//      for (unsigned int pfi = 0; pfi < pfuncs.size(); pfi++)
//      {
//        double pnorm = pfuncs[pfi].norm2();
//        if (_world.rank() == 0) printf("pfi = %d\tpnorm = %.5f\n\n", pfi, pnorm);
//      }
      // Make BSH operators
      if (_world.rank() == 0) printf("Creating BSH operator ...\n\n");
      make_bsh_operators();
      // Apply the Green's function operator (stubbed)
      if (_world.rank() == 0) printf("Applying BSH operator ...\n\n");
      vector<double> sfactor(pfuncs.size());
      for (unsigned int si = 0; si < sfactor.size(); si++) sfactor[si] = -2.0;
      scale(_world, pfuncs, sfactor);
      vector<funcT> tmp = apply(_world, _bops, pfuncs);
      // WSTHORNTON DEBUG
      for (unsigned int ti = 0; ti < tmp.size(); ti++)
      {
        double ttnorm = tmp[ti].norm2();
        if (_world.rank() == 0) printf("ti = %d\tttnorm = %.5f\n\n", ti, ttnorm);
      }
      // Do Gram-Schmidt
      if (_world.rank() == 0) printf("Gram-Schmidt ...\n\n");
      for (unsigned int ti = 0; ti < tmp.size(); ++ti)
      {
        // Project out the lower states
        for (unsigned int pj = 0; pj < ti; ++pj)
        {
          double overlap = inner(tmp[ti], _phis[pj]);
          tmp[ti] -= overlap*_phis[pj];
        }
      }
      _world.gop.fence();
      // WSTHORNTON DEBUG
      for (unsigned int ti = 0; ti < tmp.size(); ti++)
      {
        double ttnorm = tmp[ti].norm2();
        if (_world.rank() == 0) printf("ti = %d\tttnorm = %.5f\n\n", ti, ttnorm);
      }
      // Update e
      if (_world.rank() == 0) printf("Updating e ...\n\n");
      for (unsigned int ei = 0; ei < _eigs.size(); ei++)
      {
        funcT r = tmp[ei] - _phis[ei];
        double tnorm = tmp[ei].norm2();
        double rnorm = r.norm2();
        if (_world.rank() == 0) printf("ei = %d\trnorm = %.5f\ttnorm = %.5f\n\n", ei, rnorm, tnorm);
        // Compute correction to the eigenvalues
        double ecorrection = -0.5*inner(pfuncs[ei], r) / (tnorm*tnorm);
        double eps_old = _eigs[ei];
        double eps_new = eps_old + ecorrection;
        // Sometimes eps_new can go positive, THIS WILL CAUSE THE ALGORITHM TO CRASH. So,
        // I bounce the new eigenvalue back into the negative side of the real axis. I
        // keep doing this until it's good or I've already done it 10 times.
        int counter = 10;
        while (eps_new >= 0.0 && counter < 20)
        {
          // Split the difference between the new and old estimates of the
          // pi-th eigenvalue.
          if (_world.rank() == 0) printf("ei = %d\teps_new = %.5f\teps_old = %.5f\n\n", ei, eps_new, eps_old);
          eps_new = eps_old + 0.5*(eps_new - eps_old);
          counter++;
        }
        // Still no go, forget about it. (1$ to Donnie Brasco)
        if (eps_new >= 0.0)
        {
          if (_world.rank() == 0) printf("FAILURE OF WST: exiting!!\n\n");
          _exit(0);
        }
        // Set new eigenvalue
        _eigs[ei] = eps_new;
      }
      // Update the eigenvalue estimates and wavefunctions.
      truncate(_world, tmp);
      for (unsigned int ti = 0; ti < tmp.size(); ti++)
      {
        _phis[ti] = tmp[ti].scale(1.0/tmp[ti].norm2());
      }
      // Update rho
      if (_world.rank() == 0) printf("Computing new density for it == #%d\n\n", it);
      _rho = EigSolver::compute_rho(_phis, _world);
      // Output to observables
      for (typename std::vector<IEigSolverObserver<T>*>::iterator itr = _obs.begin(); itr
        != _obs.end(); ++itr)
      {
        (*itr)->iterateOutput(_phis, _eigs, _rho, it);
      }
    }
  }
  //***************************************************************************

  //***************************************************************************
  template class EigSolver<double>;
  //***************************************************************************
}


