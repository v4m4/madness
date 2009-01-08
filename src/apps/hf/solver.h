#include <mra/mra.h>
#include <world/world.h>
#include <vector>
#include <fortran_ctypes.h>
#include <cmath>

#include "poperator.h"
#include "libxc.h"
#include "electronicstructureparams.h"

#ifndef SOLVER_H_

namespace madness
{

  //***************************************************************************
  double abs(double x) {return x;}
  //***************************************************************************

  //***************************************************************************
  double real(double x) {return x;}
  //***************************************************************************

  //***************************************************************************
  template <typename Q, int NDIM>
  struct abs_square_op
  {
    typedef typename TensorTypeData<Q>::scalar_type resultT;
    Tensor<resultT> operator()(const Key<NDIM>& key, const Tensor<Q>& t) const
    {
      Tensor<resultT> result(t.ndim, t.dim);
      BINARY_OPTIMIZED_ITERATOR(Q, t, resultT, result, resultT d = abs(*_p0); *_p1 = d*d);
      return result;
    }
    template <typename Archive>
    void serialize(Archive& ar) {}
  };
  //***************************************************************************

  //***************************************************************************
  template<typename Q, int NDIM>
  Function<typename TensorTypeData<Q>::scalar_type,NDIM> abs_square(const Function<Q,NDIM>& func)
  {
    return unary_op(func, abs_square_op<Q,NDIM>());
  }
  //***************************************************************************

  //***************************************************************************
  template <typename T, typename valueT, int NDIM>
  class Solver
  {
    // Typedef's
    typedef Function<T,NDIM> real_funcT;
    typedef FunctionFactory<T,NDIM> real_factoryT;
    typedef Function<valueT,NDIM> funcT;
    typedef FunctionFactory<valueT,NDIM> factoryT;
    typedef Vector<double,NDIM> kvecT;
    typedef SeparatedConvolution<T,3> operatorT;
    typedef SharedPtr<operatorT> poperatorT;

    //*************************************************************************
    World _world;
    //*************************************************************************

    //*************************************************************************
    // This variable could either be a nuclear potiential or a nuclear charge
    // density depending on the "ispotential" variable in the
    // ElectronicStructureParams class.
    real_funcT _vnucrhon;
    //*************************************************************************

    //*************************************************************************
    std::vector<funcT> _phisa;
    //*************************************************************************

    //*************************************************************************
    std::vector<funcT> _phisb;
    //*************************************************************************

    //*************************************************************************
    std::vector<T> _eigsa;
    //*************************************************************************

    //*************************************************************************
    std::vector<T> _eigsb;
    //*************************************************************************

    //*************************************************************************
    ElectronicStructureParams _params;
    //*************************************************************************

    //*************************************************************************
    real_funcT _rhoa;
    //*************************************************************************

    //*************************************************************************
    real_funcT _rhob;
    //*************************************************************************

    //*************************************************************************
    real_funcT _rho;
    //*************************************************************************

    //*************************************************************************
    real_funcT _vnuc;
    //*************************************************************************

    //*************************************************************************
    SeparatedConvolution<T,NDIM>* _cop;
    //*************************************************************************


  public:
    //*************************************************************************
    // Constructor
    Solver(World& world, real_funcT vnucrhon, std::vector<funcT> phisa,
      std::vector<funcT> phisb, std::vector<T> eigsa, std::vector<T> eigsb,
      ElectronicStructureParams params)
       : _world(world), _vnucrhon(vnucrhon), _phisa(phisa), _phisb(phisb),
       _eigsa(eigsa), _eigsb(eigsb), _params(params)
    {
      if (params.periodic)
      {
        Tensor<double> box = FunctionDefaults<NDIM>::get_cell_width();
        _cop = PeriodicCoulombOpPtr<T,NDIM>(const_cast<World&>(world),
            FunctionDefaults<NDIM>::get_k(), params.lo, params.thresh * 0.1, box);
      }
      else
      {
        _cop = CoulombOperatorPtr<T,NDIM>(const_cast<World&>(world),
            FunctionDefaults<NDIM>::get_k(), params.lo, params.thresh * 0.1);
      }

      if (params.ispotential)
      {
        _vnuc = copy(_vnucrhon);
      }
      else
      {
        _vnuc = apply(*_cop, _vnucrhon);
      }
    }
    //*************************************************************************

      //*************************************************************************
      // Constructor
      Solver(World& world, real_funcT vnucrhon, std::vector<funcT> phis,
          std::vector<T> eigs, ElectronicStructureParams params)
         : _world(world), _vnucrhon(vnucrhon), _phisa(phis), _phisb(phis),
         _eigsa(eigs), _eigsb(eigs), _params(params)
      {
        if (params.periodic)
        {
          Tensor<double> box = FunctionDefaults<NDIM>::get_cell_width();
          _cop = PeriodicCoulombOpPtr<T,NDIM>(const_cast<World&>(world),
              FunctionDefaults<NDIM>::get_k(), params.lo, params.thresh * 0.1, box);
        }
        else
        {
          _cop = CoulombOperatorPtr<T,NDIM>(const_cast<World&>(world),
              FunctionDefaults<NDIM>::get_k(), params.lo, params.thresh * 0.1);
        }

        if (params.ispotential)
        {
          _vnuc = copy(_vnucrhon);
        }
        else
        {
          _vnuc = apply(*_cop, _vnucrhon);
        }
      }
      //*************************************************************************

    //***************************************************************************
    real_funcT compute_rho(std::vector<funcT> phis)
    {
      // Square op

      // Electron density
      real_funcT rho = real_factoryT(_world);
      // Loop over all wavefunctions to compute density
      for (unsigned int j = 0; j < phis.size(); j++)
      {
        // Get phi(j) from iterator
        const funcT& phij = phis[j];
        // Compute the j-th density
        //funcT prod = square(phij);
        real_funcT prod = abs_square(phij);
        //rho += occs[j]*prod;
        rho += prod;
      }
      rho.truncate();
      return rho;
    }
    //***************************************************************************

    //***************************************************************************
    std::vector<poperatorT> make_bsh_operators(const std::vector<T>& eigs)
    {
      // Make BSH vector
      std::vector<poperatorT> bops;
      // Get defaults
      int k = FunctionDefaults<NDIM>::get_k();
      double tol = FunctionDefaults<NDIM>::get_thresh();
      // Loop through eigenvalues, adding a BSH operator to bops
      // for each eigenvalue
      int sz = eigs.size();
      for (int i = 0; i < sz; i++)
      {
          T eps = eigs[i];
          if (eps > 0)
          {
              if (_world.rank() == 0)
              {
                  std::cout << "bsh: warning: positive eigenvalue" << i << eps << std::endl;
              }
              eps = -0.1;
          }
          bops.push_back(poperatorT(BSHOperatorPtr<T,NDIM>(_world, sqrt(-2.0*eps), k, _params.lo, tol * 0.1)));
      }
      return bops;
    }
    //*************************************************************************

    //*************************************************************************
    double calculate_kinetic_energy()
    {
      double ke = 0.0;
      if (!_params.periodic)
      {
        for (unsigned int i = 0; i < _phisa.size(); i++)
        {
          for (int axis = 0; axis < 3; axis++)
          {
            funcT dpsi = diff(_phisa[i], axis);
            ke += 0.5 * real(inner(dpsi, dpsi));
          }
        }
        if (_params.spinpol)
        {
          for (unsigned int i = 0; i < _phisb.size(); i++)
          {
            for (int axis = 0; axis < 3; axis++)
            {
              funcT dpsi = diff(_phisb[i], axis);
              ke += 0.5 * real(inner(dpsi, dpsi));
            }
          }
        }
        else
        {
          ke *= 2.0;
        }
      }
      return ke;
    }
    //*************************************************************************

    //*************************************************************************
    void apply_potential(std::vector<funcT>& pfuncsa,
        std::vector<funcT>& pfuncsb, const std::vector<funcT>& phisa,
        const std::vector<funcT>& phisb, const real_funcT& rhoa, const real_funcT& rhob,
        const real_funcT& rho)
    {
      // Nuclear and coulomb potentials
      real_funcT vc = apply(*_cop, rho);
      real_funcT vlocal = _vnuc + vc;
      // Calculate energies for Coulomb and nuclear
      double ce = 0.5*inner(vc,rho);
      double pe = inner(_vnuc,rho);
      double xc = 0.0;
      double ke = calculate_kinetic_energy();
      // Exchange
      if (_params.functional == 1)
      {
        // LDA, is calculation spin-polarized?
        if (_params.spinpol)
        {
          // potential
          real_funcT vxca = binary_op(rhoa, rhob, &::libxc_ldaop_sp);
          real_funcT vxcb = binary_op(rhob, rhoa, &::libxc_ldaop_sp);
          pfuncsa = mul_sparse(_world, vlocal + vxca, phisa, _params.thresh * 0.1);
          pfuncsb = mul_sparse(_world, vlocal + vxcb, phisb, _params.thresh * 0.1);
          // energy
          real_funcT fca = binary_op(rhoa, rhob, &::libxc_ldaeop_sp);
          real_funcT fcb = binary_op(rhob, rhoa, &::libxc_ldaeop_sp);
          xc = fca.trace() + fcb.trace();
        }
        else
        {
          // potential
          real_funcT vxc = copy(rhoa);
          vxc.unaryop(&::libxc_ldaop);
          real_funcT vxc2 = binary_op(rhoa, rhoa, &::libxc_ldaop_sp);
          pfuncsa = mul_sparse(_world, vlocal + vxc2, phisa, _params.thresh * 0.1);
          // energy
          real_funcT fc = copy(rhoa);
          fc.unaryop(&::ldaeop);
          xc = fc.trace();
        }
      }
      std::cout.precision(8);
      print("Energies:");
      print("Kinetic energy:\t\t ", ke);
      print("Potential energy:\t ", pe);
      print("Coulomb energy:\t\t ", ce);
      print("Exchage energy:\t\t ", xc, "\n");
      print("Total energy:\t\t ", ke + pe + ce + xc, "\n\n");
    }
    //*************************************************************************

    //*************************************************************************
    virtual ~Solver() {}
    //*************************************************************************

    //*************************************************************************
    void solve()
    {
      for (int it = 0; it < _params.maxits; it++)
      {
        // Compute density
        _rhoa = compute_rho(_phisa);
        _rhob = (_params.spinpol) ? compute_rho(_phisb) : _rhoa;
        _rho = _rhoa + _rhob;

        vector<funcT> pfuncsa(_phisa.size()), pfuncsb(_phisb.size());
        for (unsigned int pi = 0; pi < _phisa.size(); pi++)
          pfuncsa[pi] = factoryT(_world);
        for (unsigned int pi = 0; pi < _phisb.size(); pi++)
          pfuncsb[pi] = factoryT(_world);

        // Apply the potentials to the orbitals
        apply_potential(pfuncsa, pfuncsb, _phisa, _phisb, _rhoa, _rhob, _rho);

        // Make BSH Green's function
        std::vector<poperatorT> bopsa = make_bsh_operators(_eigsa);
        vector<T> sfactor(pfuncsa.size());
        for (unsigned int si = 0; si < sfactor.size(); si++) sfactor[si] = -2.0;
        scale(_world, pfuncsa, sfactor);

        // Apply Green's function to orbitals
        vector<funcT> tmpa = apply(_world, bopsa, pfuncsa);

//        {
//          if (_world.rank() == 0) printf("\n");
//          Tensor<double> boxsize = FunctionDefaults<NDIM>::get_cell_width();
//          double L = boxsize[0];
//          double bstep = L / 100.0;
//          _phisa[0].reconstruct();
//          pfuncsa[0].reconstruct();
//          tmpa[0].reconstruct();
//          for (int i = 0; i < 101; i++)
//          {
//           Vector<T,NDIM> p(-L / 2 + i * bstep);
//           if (_world.rank() == 0)
//             printf("%.2f\t\t%.8f\t%.8f\t%.8f\n", p[0], _phisa[0](p), pfuncsa[0](p), tmpa[0](p));
//          }
//          if (_world.rank() == 0) printf("\n");
//        }

        // Gram-Schmidt
        gram_schmidt(tmpa, _phisa);

        // Update eigenvalues
        update_eigenvalues(tmpa, pfuncsa, _phisa, _eigsa);

        // Update orbitals
        truncate(_world, tmpa);
        for (unsigned int ti = 0; ti < tmpa.size(); ti++)
        {
          _phisa[ti] = tmpa[ti].scale(1.0/tmpa[ti].norm2());
        }

        // Do other spin
        if (_params.spinpol)
        {
          std::vector<poperatorT> bopsb = make_bsh_operators(_eigsb);
          scale(_world, pfuncsb, sfactor);
          vector<funcT> tmpb = apply(_world, bopsb, pfuncsb);
          gram_schmidt(tmpb, _phisb);
          // Update orbitals
          truncate(_world, tmpb);
          update_eigenvalues(tmpb, pfuncsb, _phisb, _eigsb);
          for (unsigned int ti = 0; ti < tmpb.size(); ti++)
          {
            _phisb[ti] = tmpb[ti].scale(1.0/tmpb[ti].norm2());
          }
        }

        std::cout.precision(8);
        if (_world.rank() == 0)
        {
          print("Iteration: ", it, "\nEigenvalues for alpha spin: \n");
          for (unsigned int i = 0; i < _eigsa.size(); i++)
          {
            print(_eigsa[i]);
          }
          print("\n\n");
        }
        if (_params.spinpol)
        {
          if (_world.rank() == 0)
          {
            print("Eigenvalues for beta spin: \n");
            for (unsigned int i = 0; i < _eigsb.size(); i++)
            {
              print(_eigsb[i]);
            }
            print("\n\n");
          }
        }
      }
    }
    //*************************************************************************

    //*************************************************************************
    void gram_schmidt(std::vector<funcT>& a, const std::vector<funcT>& b)
    {
      // Do Gram-Schmidt
      if (_world.rank() == 0) printf("Gram-Schmidt ...\n\n");
      for (unsigned int ai = 0; ai < a.size(); ++ai)
      {
        // Project out the lower states
        for (unsigned int bj = 0; bj < ai; ++bj)
        {
          valueT overlap = inner(a[ai], b[bj]);
          a[ai] -= overlap*b[bj];
        }
      }
    }
    //*************************************************************************

    //*************************************************************************
    void update_eigenvalues(const std::vector<funcT>& tmp,
        const std::vector<funcT>& pfuncs, const std::vector<funcT>& phis,
        std::vector<T>& eigs)
    {
      // Update e
      if (_world.rank() == 0) printf("Updating e ...\n\n");
      for (unsigned int ei = 0; ei < eigs.size(); ei++)
      {
        funcT r = tmp[ei] - phis[ei];
        double tnorm = tmp[ei].norm2();
        // Compute correction to the eigenvalues
        T ecorrection = -0.5*real(inner(pfuncs[ei], r)) / (tnorm*tnorm);
        T eps_old = eigs[ei];
        T eps_new = eps_old + ecorrection;
//        if (_world.rank() == 0) printf("ecorrection = %.8f\n\n", ecorrection);
//        if (_world.rank() == 0) printf("eps_old = %.8f eps_new = %.8f\n\n", eps_old, eps_new);
        // Sometimes eps_new can go positive, THIS WILL CAUSE THE ALGORITHM TO CRASH. So,
        // I bounce the new eigenvalue back into the negative side of the real axis. I
        // keep doing this until it's good or I've already done it 10 times.
        int counter = 10;
        while (eps_new >= 0.0 && counter < 20)
        {
          // Split the difference between the new and old estimates of the
          // pi-th eigenvalue.
          eps_new = eps_old + 0.5 * (eps_new - eps_old);
          counter++;
        }
        // Still no go, forget about it. (1$ to Donnie Brasco)
        if (eps_new >= 0.0)
        {
          if (_world.rank() == 0) printf("FAILURE OF WST: exiting!!\n\n");
          _exit(0);
        }
        // Set new eigenvalue
        eigs[ei] = eps_new;
      }
    }
    //*************************************************************************

//    //*************************************************************************
//    double get_eig(int indx)
//    {
//      return _solver->get_eig(indx);
//    }
//    //*************************************************************************
//
//    //*************************************************************************
//    funcT get_phi(int indx)
//    {
//      return _solver->get_phi(indx);
//    }
//    //*************************************************************************
//
//    //*************************************************************************
//    const std::vector<double>& eigs()
//    {
//      return _solver->eigs();
//    }
//    //*************************************************************************
//
//    //*************************************************************************
//    const std::vector<funcT>& phis()
//    {
//      return _solver->phis();
//    }
//    //*************************************************************************

  };
  //***************************************************************************

}
#define SOLVER_H_


#endif /* SOLVER_H_ */