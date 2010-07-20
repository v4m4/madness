/*
  this file is part of MADNESS.
  
  Copyright (C) 2007,2010 Oak Ridge National Laboratory
  
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
  
  $Id$
*/
#ifndef WAVEF_H
#define WAVEF_H
//\file wavef.h
//\brief The hydrogenic bound and continuum states
/************************************************************************
 * Here is a madness representation of the hydrogenic wave functions.
 * The bound states come from the Gnu Scientific Library. The unbound
 * states are generated with the confluent hypergeometric function which
 * uses gmp and mpfr for extended precision
 * 
 * Using: Gnu Scientific Library          http://www.gnu.org/software/gsl/
 *        GNU Multiple Precision library  http://gmplib.org/
 *        mpfr                            http://www.mpfr.org/
 * By:    Nick Vence
 ************************************************************************/
#define WORLD_INSTANTIATE_STATIC_TEMPLATES
#include "hyp.h"
#include <mra/mra.h>
#include "interp.h"
#include <complex>
#include <iostream>
#include <stdio.h>
#include <complex>

const int NDIM  = 3;
typedef madness::SharedPtr< madness::FunctionFunctorInterface<complexd,NDIM> > functorT;
typedef std::complex<double> complexd;
typedef madness::Vector<double,NDIM> vector3D;

class baseWF : public madness::FunctionFunctorInterface<complexd,NDIM> {
public:
    typedef std::complex<double> complexd;
    typedef madness::Vector<double,NDIM> vector3D;
    virtual complexd operator()(const vector3D& x) const = 0;
    static const complexd I;
    static const double PI;
};

/******************************************
 * Scattering WaveFunction
 ******************************************/
class ScatteringWF : public baseWF { 
public:
    ScatteringWF(madness::World& world, const double Z, double cutoff);
    ScatteringWF(const double Z, double cutoff);
    void init(madness::World& world);
    virtual complexd f11(const double r) const = 0;
    virtual double getk() const = 0;
    virtual complexd setAA() = 0;
    virtual complexd setBB() = 0;
    complexd aForm(complexd ZZ) const;
    complexd gamma(double re, double im);
    complexd gamma(complexd AA);
    CubicInterpolationTable<complexd > fit1F1;
    const double Z_;
    const double cutoff_;
    double   k_;
    complexd AA;
    complexd BB;
    double   domain;
    complexd one;
    double   dx;
    int      n;
    complexd mAA;
    complexd AAmBB;
    complexd expPIAAXgammaBBmAAr;
    complexd expPIZ_2kXgamma1pIZ_k_;
    complexd gammaAAr;
protected:
    struct MemberFuncPtr {
        ScatteringWF* obj;
        MemberFuncPtr(ScatteringWF* obj) : obj(obj) {}
        complexd operator()(double x) {return obj->f11(x);}
    };
};

class PhiK : public ScatteringWF {
public:
    PhiK(madness::World& world, const double Z, const vector3D& kVec, double cutoff);
    PhiK(const double Z, const vector3D& kVec, double cutoff);
    complexd operator()(const vector3D& x) const;
    complexd f11(const double r) const ;
    complexd setAA();
    complexd setBB();
    double   getk() const;
private:
    const vector3D kVec_;
}; 

class Phikl : public ScatteringWF {
public:
    Phikl(const double Z, const double k, const int l, double cutoff);
    Phikl(madness::World& world, const double Z, const double k, const int l, double cutoff);
    complexd operator()(const vector3D& x) const;
    complexd f11(const double r) const ;    
    complexd setAA();
    complexd setBB();
    double   getk() const ;
private:
    const int l_;
};    


/******************************************
 * Bound WaveFunction
 ******************************************/
class BoundWF : public baseWF {
public:
    BoundWF(double Z, int nn, int ll, int mm );
    complexd operator()(const vector3D& x) const;
private:
    double Z;
    int n;
    int l;
    int m;
};

/******************************************
 *Exp[ I*(k.r) ]
 ******************************************/
class Expikr : public baseWF
{
public:
    Expikr(const vector3D& kVec);
    complexd operator()(const vector3D& r) const;
private:
    vector3D kVec;
    double k;
    double costhK;
};
#endif
