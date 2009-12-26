/*!
  \file he.cc
  \brief Solves the Hatree-Fock equations for the helium atom
  \defgroup examplehehf Hatree-Fock equations for the helium atom
  \ingroup examples

  The Hartree-Fock wave function is computed for the helium atom
  in three dimensions without using spherical symmetry.

  The atomic orbital is an eigenfunction of the Fock operator 
  \f{eqnarray*}{
     \hat{F} \phi(r) &=& \epsilon \phi(r) \\
     \hat{F} &=& -\frac{1}{2} \nabla^2 - \frac{2}{r} + u(r) \\
     u(r) &=& \int \frac{\rho(s)}{| r - s |} d^3s \\
     \rho(r) &=& \phi(r)^2 
  \f}
  that depends upon the orbital via the Coulomb potential (\f$ u(r) \f$)
  arising from the electronic density (\f$ \rho(r) \f$).  

  \par Implementation

  Per the usual MADNESS practice, the equation is rearranged into 
  integral form
  \f[
      \phi = - 2 G_{\mu} * ( V \phi)
  \f]
  where \f$ \mu = \sqrt{-2E} \f$ and \f$G_{\mu}\f$ is the Green's function
  for the Helmholtz equation
  \f[
      \left( - \nabla^2 + \mu^2  \right) G(r,r^{\prime}) = \delta(r,r^{\prime})
  \f]

  The initial guess is \f$ \exp(-2r) \f$, which is normalized before use.
  Each iteration proceeds by
  - computing the density by squaring the orbital,
  - computing the Coulomb potential by applying the Coulomb Green's function,
  - multiplying the orbital by the total potential, 
  - updating the orbital by applying the Helmholtz Green's function,
  - updating the energy according to a second-order accurate estimate
    (see the initial MRQC paper), and finally
  - normalizing the new wave function.

  Points of interest
  - application of the Coulomb and Helmholtz Green's functions
  - smoothing of the potential and initial guess
  - manual evaluation of the solution along a line
  
*/

#define WORLD_INSTANTIATE_STATIC_TEMPLATES  
#include <mra/mra.h>
#include <mra/operator.h>

using namespace madness;

static const double L = 32.0;   // box size
static const long k = 8;        // wavelet order
static const double thresh = 1e-6; // precision

static double guess(const coord_3d& r) {
    const double x=r[0], y=r[1], z=r[2];
    return 6.0*exp(-2.0*sqrt(x*x+y*y+z*z+1e-4));
}

static double V(const coord_3d& r) {
    const double x=r[0], y=r[1], z=r[2];
    return -2.0/(sqrt(x*x+y*y+z*z+1e-8));
}

void iterate(World& world, real_function_3d& V, real_function_3d& psi, double& eps) {
    real_function_3d Vpsi = (V*psi);
    Vpsi.scale(-2.0).truncate();
    real_convolution_3d op = BSHOperator3D<double>(world, sqrt(-2*eps), k, 0.001, 1e-6);
    real_function_3d tmp = apply(op,Vpsi).truncate();
    double norm = tmp.norm2();
    real_function_3d r = tmp-psi;
    double rnorm = r.norm2();
    double eps_new = eps - 0.5*inner(Vpsi,r)/(norm*norm);
    if (world.rank() == 0) {
        print("norm=",norm," eps=",eps," err(psi)=",rnorm," err(eps)=",eps_new-eps);
    }
    psi = tmp.scale(1.0/norm);
    eps = eps_new;
}

int main(int argc, char** argv) {
    initialize(argc, argv);
    World world(MPI::COMM_WORLD);
    startup(world,argc,argv);
    std::cout.precision(6);

    FunctionDefaults<3>::set_k(k);
    FunctionDefaults<3>::set_thresh(thresh);
    FunctionDefaults<3>::set_truncate_mode(1);  
    FunctionDefaults<3>::set_cubic_cell(-L/2,L/2);
    
    real_function_3d Vnuc = real_factory_3d(world).f(V).truncate_mode(0);
    real_function_3d psi  = real_factory_3d(world).f(guess);
    psi.scale(1.0/psi.norm2());
    real_convolution_3d op = CoulombOperator<double>(world, k, 0.001, 1e-6);

    double eps = -1.0; 
    for (int iter=0; iter<10; iter++) {
        real_function_3d rho = square(psi).truncate();
        real_function_3d potential = Vnuc + apply(op,rho).truncate();
        iterate(world, potential, psi, eps);
    }

    double kinetic_energy = 0.0;
    for (int axis=0; axis<3; axis++) {
        real_function_3d dpsi = diff(psi,axis);
        kinetic_energy += inner(dpsi,dpsi);
    }

    real_function_3d rho = square(psi).truncate();
    double two_electron_energy = inner(apply(op,rho),rho);
    double nuclear_attraction_energy = 2.0*inner(Vnuc*psi,psi);
    double total_energy = kinetic_energy + nuclear_attraction_energy + two_electron_energy;

    // Manually tabluate the orbital along a line ... probably easier
    // to use the lineplot routine
    coord_3d r(0.0);
    psi.reconstruct();
    for (int i=0; i<201; i++) {
        r[2] = -L/2 + L*i/200.0;
        print(r[2], psi(r));
    }

    if (world.rank() == 0) {
        print("            Kinetic energy ", kinetic_energy);
        print(" Nuclear attraction energy ", nuclear_attraction_energy);
        print("       Two-electron energy ", two_electron_energy);
        print("              Total energy ", total_energy);
        print("                    Virial ", (nuclear_attraction_energy + two_electron_energy) / kinetic_energy);
    }

    world.gop.fence();

    finalize();
    return 0;
}
