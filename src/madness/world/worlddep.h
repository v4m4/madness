/*
  This file is part of MADNESS.

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
*/


#ifndef MADNESS_WORLD_WORLDDEP_H__INCLUDED
#define MADNESS_WORLD_WORLDDEP_H__INCLUDED

/// \file worlddep.h
/// \brief Defines DependencyInterface and CallbackInterface

#include <madness/world/array.h>
#include <madness/world/worldmutex.h>
#include <madness/world/atomicint.h>
#include <madness/world/world.h>
#include <typeinfo>

namespace madness {

    /// This class used for callbacks (e.g., for dependency tracking)
    class CallbackInterface {
    public:
        virtual void notify() = 0;

        virtual ~CallbackInterface() {}
    };


    /// Provides interface for tracking dependencies
    class DependencyInterface : public CallbackInterface, private Spinlock {
    private:
        // Replaced atomic counter with critical section so that all
        // data (callbacks and counter) were being consistently managed.
        volatile int ndepend;   ///< Counts dependencies

        static const int MAXCALLBACKS = 8;
        typedef Stack<CallbackInterface*,MAXCALLBACKS> callbackT;
        mutable volatile callbackT callbacks; ///< Called ONCE by dec() when ndepend==0

        // Main design point is that since a callback might destroy
        // this object, when callbacks are invoked we cannot be
        // holding the lock and all necessary data must be on the
        // stack (i.e., not from the object state).
        void do_callbacks(callbackT& cb) const {
            while (!cb.empty()) {
                cb.front()->notify();
                cb.pop();
            }
        }

    public:
        DependencyInterface(int ndep = 0) : ndepend(ndep) {}

        /// Returns the number of unsatisfied dependencies
        int ndep() const {return ndepend;}

        /// Returns true if ndepend == 0
        bool probe() const {return ndep() == 0;}

        /// Invoked by callbacks to notifiy of dependencies being satisfied
        void notify() {dec();}

        /// Registers a callback for when \c ndepend==0 , immediately invoked if \c ndepend==0
        void register_callback(CallbackInterface* callback) {
            callbackT cb;
            {
                ScopedMutex<Spinlock> obolus(this);
                const_cast<callbackT&>(this->callbacks).push(callback);
                if (probe()) {
                    cb = const_cast<callbackT&>(callbacks);
                    const_cast<callbackT&>(callbacks).clear();
                }
            }
            do_callbacks(cb);
        }

        /// Increment the number of dependencies
        void inc() {
            ScopedMutex<Spinlock> obolus(this);
            ndepend++;
        }

        /// Decrement the number of dependencies and invoke callback if ndepend=0
        void dec() {
            callbackT cb;
            {
                ScopedMutex<Spinlock> obolus(this);
                if (--ndepend == 0) {
                    cb = const_cast<callbackT&>(callbacks);
                    const_cast<callbackT&>(callbacks).clear();
                }
            }
            do_callbacks(cb);
        }

        virtual ~DependencyInterface() {
#ifdef MADNESS_ASSERTIONS_THROW
            if(ndepend != 0)
                error("DependencyInterface::~DependencyInterface(): ndepend =", ndepend);
#else
            MADNESS_ASSERT(ndepend == 0);
#endif
        }

    };
}
#endif // MADNESS_WORLD_WORLDDEP_H__INCLUDED
