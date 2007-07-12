/*
  This file is part of MADNESS.
  
  Copyright (C) <2007> <Oak Ridge National Laboratory>
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
  
  For more information please contact:

  Robert J. Harrison
  Oak Ridge National Laboratory
  One Bethel Valley Road
  P.O. Box 2008, MS-6367

  email: harrisonrj@ornl.gov 
  tel:   865-241-3937
  fax:   865-572-0680

  
  $Id: tests-hqi.cc 257 2007-06-25 19:09:38Z HartmanBaker $
*/

/// \file tests-hqi.cc
/// \brief The loadbal test suite

  
#include <mra/mra.h>

const double PI = 3.1415926535897932384;

using namespace madness;

template <typename T, int NDIM>
class GaussianFunctor : public FunctionFunctorInterface<T,NDIM> {
private:
    typedef Vector<double,NDIM> coordT;
    const std::vector<coordT> center;
    const std::vector<double> exponent;
    const std::vector<T> coefficient;
    
public:
    GaussianFunctor(const std::vector<coordT>& center, std::vector<double> exponent, std::vector<T> coefficient) 
        : center(center), exponent(exponent), coefficient(coefficient) {};

    GaussianFunctor(const coordT& center, double exponent, T coefficient) 
        : center(std::vector<coordT>(1,center)), exponent(std::vector<double>(1,exponent)), coefficient(std::vector<T>(1,coefficient)) {};

    T operator()(const coordT& x) const {
	T retval = 0;
	for (int j=0; j<center.size(); j++) {
	    double sum = 0.0;
	    for (int i=0; i<NDIM; i++) {
		double xx = center[j][i]-x[i];
		sum += xx*xx;
	    }
	    retval += coefficient[j]*exp(-exponent[j]*sum);
	}
        return retval;
    };

    GaussianFunctor operator+ (const GaussianFunctor& other) const {
	std::vector<coordT> newcenter = this->center;
	std::vector<double> newexponent = this->exponent;
	std::vector<T> newcoefficient = this->coefficient;
	newcenter.insert(newcenter.end(), other.center.begin(), other.center.end());
	newexponent.insert(newexponent.end(), other.exponent.begin(), other.exponent.end());
	newcoefficient.insert(newcoefficient.end(), other.coefficient.begin(), other.coefficient.end());
	return (GaussianFunctor(newcenter, newexponent, newcoefficient));
    };
};

template <typename T, int NDIM>
void test_basic(World& world) {
    typedef Vector<double,NDIM> coordT;
    typedef SharedPtr< FunctionFunctorInterface<T,NDIM> > functorT;

    if (world.rank() == 0) 
        print("Test compression of a normalized gaussian at origin, type =",archive::get_type_name<T>(),", ndim =",NDIM);

    for (int i=0; i<NDIM; i++) {
        FunctionDefaults<NDIM>::cell(i,0) = -11.0-2*i;  // Deliberately assymetric bounding box
        FunctionDefaults<NDIM>::cell(i,1) =  10.0+i;
    }
    FunctionDefaults<NDIM>::k = 7;
    FunctionDefaults<NDIM>::thresh = 1e-5;
    FunctionDefaults<NDIM>::compress = false;
    FunctionDefaults<NDIM>::refine = true;
    FunctionDefaults<NDIM>::initial_level = 2;
    
    double used;
    const coordT origin1(0.0);
    const coordT origin2(0.0);
    coordT point;
    const double expnt1 = 1.0;
    const double coeff1 = pow(2.0/PI,0.25*NDIM);
    const double expnt2 = 2.0;
    const double coeff2 = pow(4.0/PI,0.25*NDIM);

//    functorT functor1(new GaussianFunctor<T,NDIM>(origin1, expnt1, coeff1));
//    functorT functor2(new GaussianFunctor<T,NDIM>(origin2, expnt2, coeff2));
    functorT functor(new GaussianFunctor<T,NDIM>(GaussianFunctor<T,NDIM>(origin1, expnt1, coeff1) + GaussianFunctor<T,NDIM>(origin2, expnt2, coeff2)));
//    functorT functor(new GaussianFunctor<T,NDIM>(*(functor1.get()) + *(functor2.get())));


    for (int i=0; i<NDIM; i++) point[i] = 0.1*i;

    used = -wall_time();
    Function<T,NDIM> f = FunctionFactory<double,NDIM>(world).functor(functor);
    used += wall_time();
    double norm = f.norm2();
    double err = f.err(*functor);
    if (world.rank() == 0) {
        print("project+refine used",used);
        print("               norm", norm);
        print("     sampling point", point);
        print("          numerical", f(point));
        print("           analytic", (*functor)(point));
        print("       global error", err);
        print("");
    }

    used = -wall_time();
    f.compress();
    used += wall_time();
    double new_norm = f.norm2();
    
    if (world.rank() == 0) {
        print("   compression used", used);
        print("               norm", new_norm, norm-new_norm);
        print("");
    }
    MADNESS_ASSERT(abs(norm-new_norm) < 1e-14*norm);
    
    used = -wall_time();
    f.reconstruct();
    used += wall_time();
    new_norm = f.norm2();
    err = f.err(*functor);
    
    if (world.rank() == 0) {
        print("reconstruction used", used);
        print("               norm", new_norm, norm-new_norm);
        print("       global error", err);
    }
    MADNESS_ASSERT(abs(norm-new_norm) < 1e-14*norm);
    
    used = -wall_time();
    f.compress();
    used += wall_time();
    new_norm = f.norm2();
    
    if (world.rank() == 0) {
        print("   compression used", used);
        print("               norm", new_norm, norm-new_norm);
        print("");
    }
    MADNESS_ASSERT(abs(norm-new_norm) < 1e-14*norm);

    used = -wall_time();
    f.truncate();
    used += wall_time();
    new_norm = f.norm2();
    err = f.err(*functor);
    if (world.rank() == 0) {
        print("    truncation used", used);
        print("               norm", new_norm, norm-new_norm);
        print("       global error", err);
    }
    
    if (world.rank() == 0) print("projection, compression, reconstruction, truncation OK\n\n");
}

template <typename T, int NDIM>
void test_conv(World& world) {
    typedef Vector<double,NDIM> coordT;
    typedef SharedPtr< FunctionFunctorInterface<T,NDIM> > functorT;

    if (world.rank() == 0)
        print("Test convergence - log(err)/(n*k) should be roughly const, a least for each value of k\n");
    const coordT origin(0.0);
    const double expnt = 1.0;
    const double coeff = pow(2.0/PI,0.25*NDIM);
    functorT functor(new GaussianFunctor<T,NDIM>(origin, expnt, coeff));

    for (int i=0; i<NDIM; i++) {
        FunctionDefaults<NDIM>::cell(i,0) = -10.0;
        FunctionDefaults<NDIM>::cell(i,1) =  10.0;
    }

    for (int k=1; k<=15; k+=2) {
	if (world.rank() == 0) printf("k=%d\n", k);
	if (NDIM > 2 && k>5) ntop = 4;
	for (int n=1; n<=ntop; n++) {
	int n = ntop;
	    Function<T,NDIM> f = FunctionFactory<T,NDIM>(world).functor(functor).nocompress().norefine().initial_level(n).k(k);
	    double err2 = f.err(*functor);
            std::size_t size = f.size();
	    std::size_t tree_size = f.tree_size();
            if (world.rank() == 0) 
                printf("   n=%d err=%.2e #coeff=%.2e tree_size=%.2e log(err)/(n*k)=%.2e\n", 
                       n, err2, double(size), double(tree_size), abs(log(err2)/n/k));
	}
    }

    if (world.rank() == 0) print("test conv OK\n\n");
}


int main(int argc, char**argv) {
    MPI::Init(argc, argv);
    World world(MPI::COMM_WORLD);

    try {
        startup(world,argc,argv);
/*
        test_basic<double,1>(world);
        test_conv<double,1>(world);

        test_basic<double,2>(world);
        test_conv<double,2>(world);
*/

        test_basic<double,3>(world);
        test_conv<double,3>(world);
    } catch (const MPI::Exception& e) {
        print(e);
        error("caught an MPI exception");
    } catch (const madness::MadnessException& e) {
        print(e);
        error("caught a MADNESS exception");
    } catch (const madness::TensorException& e) {
        print(e);
        error("caught a Tensor exception");
    } catch (const char* s) {
        print(s);
        error("caught a string exception");
    } catch (const std::string& s) {
        print(s);
        error("caught a string (class) exception");
    } catch (const std::exception& e) {
        print(e.what());
        error("caught an STL exception");
    } catch (...) {
        error("caught unhandled exception");
    }

    print("entering final fence");
    world.gop.fence();
    print("done with final fence");
    MPI::Finalize();

    return 0;
}
