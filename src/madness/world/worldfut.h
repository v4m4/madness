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


  $Id$
*/


#ifndef MADNESS_WORLD_WORLDFUT_H__INCLUDED
#define MADNESS_WORLD_WORLDFUT_H__INCLUDED

/// \file worldfut.h
/// \brief Implements Future
/// \ingroup futures

#include <vector>
#include <stack>
#include <new>
#include <madness/world/nodefaults.h>
#include <madness/world/worlddep.h>
#include <madness/world/array.h>
#include <madness/world/shared_ptr.h>
#include <madness/world/worldref.h>
#include <madness/world/typestuff.h>
#include <madness/world/worldfwd.h>
#include <madness/world/move.h>

namespace madness {

    //extern SharedCounter future_count; // For tracking memory leak


/*

\section gotchas Gotchas

\subsection futures Futures and STL vectors (e.g., \c vectors<Future<int>> )

 This to be turned back into documentation eventually

A common misconception is that STL containers initialize their
contents by \invoking the default constructor of each item in
the container since we are told that the items must be default
constructable.  But this is \em incorrect.  The items are initialized
by invoking the copy constructor for each element on a \em single
object made with the default constructor.   For futures this
is a very bad problem.  For instance,
\code
   vector< Future<double> > v(3);
\endcode
is equivalent to the following with an array of three elements
\code
   Future<double> junk;
   Future<double> v[3] = {junk,junk,junk};
\endcode
Since the Future copy constructor is by necessity shallow, each
element of \c v ends up referring to the future implementation that
underlies \c junk.  When you assign to an element of \c v, you'll also
be assigning to junk.  But since futures are single assignment
variables, you can only do that once.  Hence, when you assign a
second element of \c v you'll get a runtime exception.

The fix (other than using arrays) is to initialize STL vectors and
other containers from the special element returned by
\c Future<T>::default_initializer() which if passed into the copy
constructor will cause it to behave just like the default contructor.
Thus, the following code is what you actually need to use an STL
vector of futures
\code
   vector< Future<double> > v(3,Future<double>::default_initializer());
\endcode
which sucks, so we provide the factory function
\code
   template <typename T>
   vector< Future<T> > future_vector_factory(std::size_t n);
\endcode
which enables you to write
\code
   vector< Future<double> > v = future_vector_factory<double>(3);
\endcode
which merely blows instead of sucking.
*/

    template <typename T> class Future;

    /// Boost-type-trait-like testing of if a type is a future

    /// \ingroup futures
    template <typename T>
    struct is_future : public std::false_type { };

    /// Boost-type-trait-like testing of if a type is a future

    /// \ingroup futures
    template <typename T>
    struct is_future< Future<T> > : public std::true_type { };

    /// Boost-type-trait-like mapping of Future<T> to T

    /// \ingroup futures
    template <typename T>
    struct remove_future {
        typedef T type;
    };

    /// Boost-type-trait-like mapping of Future<T> to T

    /// \ingroup futures
    template <typename T>
    struct remove_future< Future<T> > {
        typedef T type;
    };

    /// Macro to determine type of future (by removing wrapping future template)

    /// \ingroup futures
#define REMFUTURE(T) typename remove_future< T >::type

    /// Human readable printing of future to stream

    /// \ingroup futures
    template <typename T>
    std::ostream& operator<<(std::ostream& out, const Future<T>& f);


    /// Implements the functionality of Futures

    /// \ingroup futures
    template <typename T>
    class FutureImpl : private Spinlock {
        friend class Future<T>;
        friend std::ostream& operator<< <T>(std::ostream& out, const Future<T>& f);

    private:
        static const int MAXCALLBACKS = 4;
        typedef std::stack<CallbackInterface*, std::vector<CallbackInterface*> > callbackT;
        typedef Stack<std::shared_ptr< FutureImpl<T> >,MAXCALLBACKS> assignmentT;
        volatile callbackT callbacks;
        volatile mutable assignmentT assignments;
        volatile bool assigned;
        RemoteReference< FutureImpl<T> > remote_ref;
        volatile T t;

        /// Private: AM handler for remote set operations
        static void set_handler(const AmArg& arg) {
            RemoteReference< FutureImpl<T> > ref;
            archive::BufferInputArchive input_arch = arg & ref;
            // The remote reference holds a copy of the shared_ptr, so no need
            // to take another.
            {
                FutureImpl<T>* pimpl = ref.get();

                ScopedMutex<Spinlock> fred(pimpl);
                if(pimpl->remote_ref) {
                    // Unarchive the value to a temporary since it is going to
                    // be forwarded to another node.
                    T value;
                    input_arch & value;

                    // Copy world and owner from remote_ref since sending remote_ref
                    // will invalidate it.
                    World& world = pimpl->remote_ref.get_world();
                    const ProcessID owner = pimpl->remote_ref.owner();
                    world.am.send(owner, FutureImpl<T>::set_handler,
                            new_am_arg(pimpl->remote_ref, value));

                    pimpl->set_assigned(value);
                } else {
                    // Unarchive the value of the future
                    input_arch & const_cast<T&>(pimpl->t);

                    pimpl->set_assigned(const_cast<const T&>(pimpl->t));
                }
            }
            ref.reset();
        }

        /// Private:  invoked locally by set routine after assignment
        inline void set_assigned(const T& value) {
            // Assume that whoever is invoking this routine is holding
            // a copy of our shared pointer on its *stack* so that
            // if this future is destroyed as a result of a callback
            // the destructor of this object is not invoked until
            // we return.
            //
            // Also assume that the caller either has the lock
            // or is sure that we are single threaded.
            MADNESS_ASSERT(!assigned);
            assigned = true;

            assignmentT& as = const_cast<assignmentT&>(assignments);
            callbackT& cb = const_cast<callbackT&>(callbacks);

            while (!as.empty()) {
                MADNESS_ASSERT(as.front());
                as.top()->set(value);
                as.pop();
            }

            while (!cb.empty()) {
                MADNESS_ASSERT(cb.top());
                cb.top()->notify();
                cb.pop();
            }
        }

        // Pass by value with implied copy to manage lifetime of f
        inline void add_to_assignments(const std::shared_ptr< FutureImpl<T> > f) {
            // ASSUME lock is already acquired
            if (assigned) {
                f->set(const_cast<T&>(t));
            }
            else {
                assignmentT* as = const_cast<assignmentT*>(&assignments);
                as->push(f);
            }
        }


    public:

        // Local unassigned value
        FutureImpl()
                : callbacks()
                , assignments()
                , assigned(false)
                , remote_ref()
                , t()
        { }


        // Wrapper for a remote future
        FutureImpl(const RemoteReference< FutureImpl<T> >& remote_ref)
                : callbacks()
                , assignments()
                , assigned(false)
                , remote_ref(remote_ref)
                , t()
        { }


        // Returns true if the value has been assigned
        inline bool probe() const { return assigned; }


        // Registers a function to be invoked when future is assigned

        // Callbacks are invoked in the order registered.  If the
        // future is already assigned the callback is immediately
        // invoked.
        inline void register_callback(CallbackInterface* callback) {
            ScopedMutex<Spinlock> fred(this);
            if (assigned) callback->notify();
            else const_cast<callbackT&>(callbacks).push(callback);
        }


        // Sets the value of the future (assignment)
        template <typename U>
        void set(const U& value) {
            ScopedMutex<Spinlock> fred(this);
            if(remote_ref) {
                // Copy world and owner from remote_ref since sending remote_ref
                // will invalidate it.
                World& world = remote_ref.get_world();
                const ProcessID owner = remote_ref.owner();
                world.am.send(owner, FutureImpl<T>::set_handler,
                        new_am_arg(remote_ref, unwrap_move(value)));
                set_assigned(value);
            } else {
                set_assigned((const_cast<T&>(t) = value));
            }
        }

        void set(const archive::BufferInputArchive& input_arch) {
            ScopedMutex<Spinlock> fred(this);
            MADNESS_ASSERT(! remote_ref);
            input_arch & const_cast<T&>(t);
            set_assigned(const_cast<T&>(t));
        }

        // Gets/forces the value, waiting if necessary (error if not local)
        T& get() {
            MADNESS_ASSERT(! remote_ref);  // Only for local futures
            World::await(bind_nullary_mem_fun(this,&FutureImpl<T>::probe));
            return *const_cast<T*>(&t);
        }

        // Gets/forces the value, waiting if necessary (error if not local)
        const T& get() const {
            MADNESS_ASSERT(! remote_ref);  // Only for local futures
            World::await(bind_nullary_mem_fun(this,&FutureImpl<T>::probe));
            return *const_cast<const T*>(&t);
        }

        bool is_local() const {
            return ! remote_ref;
        }

        bool replace_with(FutureImpl<T>* f) {
            MADNESS_EXCEPTION("IS THIS WORKING? maybe now we have the mutex", 0);
//            ScopedMutex<Spinlock> fred(this);
//             MADNESS_ASSERT(!world); // was return false;
//             MADNESS_ASSERT(!assigned || f->assigned);
//             if (f->world) {
//                 world = f->world;
//                 remote_ref = f->remote_ref;
//                 f->world = 0;
//             }
//             while(f->callbacks.size()) callbacks.push(f->callbacks.pop());
//             while(f->assignments.size()) assignments.push(f->assignments.pop());
            return true;
        }

        virtual ~FutureImpl() {
            if (const_cast<callbackT&>(callbacks).size()) {
                print("Future: uninvoked callbacks being destroyed?", assigned);
                abort();
            }
            if (const_cast<assignmentT&>(assignments).size()) {
                print("Future: uninvoked assignment being destroyed?", assigned);
                abort();
            }
        }
    }; // class FutureImpl


    /// A future is a possibly yet unevaluated value

    /// \ingroup futures
    /// Uses delegation to FutureImpl to provide desired
    /// copy/assignment semantics as well as safe reference counting
    /// for remote futures.
    ///
    /// Since we are using Futures a lot to store local values coming
    /// from containers and inside task wrappers for messages, we
    /// included in this class a value.  If a future is assigned
    /// before a copy/remote-reference is taken, the shared ptr is
    /// never made.  The point of this is to eliminate the two mallocs
    /// that must be peformed for every new shared_ptr.
    template <typename T>
    class Future {

        friend std::ostream& operator<< <T>(std::ostream& out, const Future<T>& f);

    private:

        // This future object can exist in one of three states:
        //   - f == NULL && value == NULL : Default initialized state
        //        This state occurs when the future is constructed via
        ///       Future::default_initializer().
        //   - f != NULL && value == NULL : FutureImpl object will hold the T object
        //        This state occurs when a future is constructed without a value,
        //        or from a remote reference.
        //   - f == NULL $$ value != NULL : T object is held in buffer
        //        This state occurs when a future is constructed with a value
        //        or from an input archive.

        std::shared_ptr< FutureImpl<T> > f; ///< pointer to the implementation object
        char buffer[sizeof(T)]; ///< Buffer to hold a single T object
        T* const value; ///< Pointer to buffer when it holds a T object

        class dddd {};
        explicit Future(const dddd&) : f(), value(NULL) { }

    public:
        typedef RemoteReference< FutureImpl<T> > remote_refT;

        /// Makes an unassigned future
        Future() :
            f(new FutureImpl<T>()), value(NULL)
        { }

        /// Makes an assigned future
        explicit Future(const T& t) :
            f(), value(new(static_cast<void*>(buffer)) T(t))
        { }

        /// Makes a future wrapping a remote reference
        explicit Future(const remote_refT& remote_ref) :
                f(remote_ref.is_local() ?
                        remote_ref.get_shared() :
                        std::shared_ptr<FutureImpl<T> >(new FutureImpl<T>(remote_ref))),
                value(NULL)
        { }

        /// Makes an assigned future from an input archive
        explicit Future(const archive::BufferInputArchive& input_arch) :
            f(), value(new(static_cast<void*>(buffer)) T())
        {
            input_arch & (*value);
        }

        /// Copy constructor is shallow
        Future(const Future<T>& other) :
            f(other.f),
            value(other.value ?
                new(static_cast<void*>(buffer)) T(* other.value) :
                NULL)
        {
            if(other.is_default_initialized())
                f.reset(new FutureImpl<T>()); // Other was default constructed so make a new f
        }

        ~Future() {
            if(value)
                value->~T();
        }


        /// See Gotchas on the documentation mainpage about why this exists and how to use it.
        static const Future<T> default_initializer() { return Future<T>(dddd()); }

        /// Default initialized query

        /// \return \c true if this future was constructed with
        /// \c default_initializer(), otherwise \c false.
        bool is_default_initialized() const { return ! (f || value); }

        /// Assignment future = future makes a shallow copy just like copy constructor
        Future<T>& operator=(const Future<T>& other) {
            if(this != &other) {
                MADNESS_ASSERT(!probe());
                if(f && other.value)
                    set(other);
                else
                    f = other.f;
            }
            return *this;
        }

        /// A.set(B) where A & B are futures ensures A has/will have the same value as B.

        /// An exception is thrown if A is already assigned since a
        /// Future is a single assignment variable.  We don't yet
        /// track multiple assignments from unassigned futures.
        ///
        /// If B is already assigned, this is the same as A.set(B.get())
        /// which sets A to the value of B.
        ///
        /// If B has not yet been assigned, the behavior is to ensure
        /// that when B is assigned that both A and B will be assigned
        /// and have the same value (though they may/may not refer to
        /// the same underlying copy of the data and indeed may even
        /// be in different processes).
        void set(const Future<T>& other) {
            MADNESS_ASSERT(f);
            if(f != other.f) {
                MADNESS_ASSERT(! f->probe());
                if (other.probe()) {
                    set(other.get());     // The easy case
                } else {
                    // Assignment is supposed to happen just once so
                    // safe to assume that this is not being messed
                    // with ... also other might invoke the assignment
                    // callback since it could have been assigned
                    // between the test above and now (and this does
                    // happen)
                    std::shared_ptr< FutureImpl<T> > ff = f; // manage lifetime of me
                    std::shared_ptr< FutureImpl<T> > of = other.f; // manage lifetime of other

                    { // BEGIN CRITICAL SECTION
                        ScopedMutex<Spinlock> fred(of.get());
                        of->add_to_assignments(ff); // Recheck of assigned is performed in here
                    } // END CRITICAL SECTION
                }
            }
        }

        /// Assigns the value ... it can only be set ONCE.
        inline void set(const T& value) {
            MADNESS_ASSERT(f);
            std::shared_ptr< FutureImpl<T> > ff = f; // manage life time of f
            ff->set(value);
        }

        /// Assigns the value ... it can only be set ONCE.
        inline void set(const archive::BufferInputArchive& input_arch) {
            MADNESS_ASSERT(f);
            std::shared_ptr< FutureImpl<T> > ff = f; // manage life time of f
            ff->set(input_arch);
        }


        /// Gets the value, waiting if necessary (error if not a local future)
        inline T& get() {
            MADNESS_ASSERT(f || value); // Check that future is not default initialized
            return (f ? f->get() : *value);
        }

        /// Gets the value, waiting if necessary (error if not a local future)
        inline const T& get()  const {
            MADNESS_ASSERT(f || value); // Check that future is not default initialized
            return (f ? f->get() : *value);
        }

        /// Query the whether this future has been assigned

        /// \return \c true if the future has been assigned, otherwise \c false
        inline bool probe() const { return (f ? f->probe() : bool(value)); }

        /// Same as get()
        inline operator T&() { return get(); }

        /// Same as get() const
        inline operator const T&() const { return get(); }


        /// Returns a structure used to pass references to another process.

        /// This is used for passing pointers/references to another
        /// process.  To make remote references completely safe, the
        /// RemoteReference increments the internal reference count of
        /// the Future.  The counter is decremented by either
        /// assigning to the remote Future or its destructor if it is
        /// never assigned.  The remote Future is ONLY useful for
        /// setting the future.  It will NOT be notified if the value
        /// is set elsewhere.
        ///
        /// If this is already a reference to a remote future, the
        /// actual remote reference is returned ... i.e., \em not a
        /// a reference to the local future.  Therefore, the local
        /// future will not be notified when the result is set
        /// (i.e., the communication is short circuited).
        inline remote_refT remote_ref(World& world) const {
            MADNESS_ASSERT(!probe());
            if (f->remote_ref)
                return f->remote_ref;
            else
                return RemoteReference< FutureImpl<T> >(world, f);
        }


        inline bool is_local() const { return (f && f->is_local()) || value; }

        inline bool is_remote() const { return !is_local(); }


        /// Registers an object to be called when future is assigned

        /// Callbacks are invoked in the order registered.  If the
        /// future is already assigned the callback is immediately
        /// invoked.
        inline void register_callback(CallbackInterface* callback) {
            if(probe()) {
                callback->notify();
            } else {
                MADNESS_ASSERT(f);
                f->register_callback(callback);
            }
        }
    }; // class Future


    /// A future of a future is forbidden (by private constructor)

    /// \ingroup futures
    template <typename T> class Future< Future<T> > {
        Future() {}
    };


    /// Specialization of FutureImpl<void> for internal convenience ... does nothing useful!

    /// \ingroup futures
    template <> class FutureImpl<void> {};

    /// Specialization of Future<void> for internal convenience ... does nothing useful!

    /// \ingroup futures
    template <> class Future<void> {
    public:
        typedef RemoteReference< FutureImpl<void> > remote_refT;

        static const Future<void> value; // Instantiated in world.cc

        static remote_refT remote_ref(World&) { return remote_refT(); }

        Future() {}

        Future(const RemoteReference< FutureImpl<void> >&) {}

        Future(const archive::BufferInputArchive& input_arch) {
            input_arch & *this;
        }

        inline Future<void>& operator=(const Future<void>&) { return *this; }

        static void set(const Future<void>&) { }
        static void set() { }

        static bool probe() { return true; }
    }; // class Future<void>

    /// Specialization of FutureImpl<Void> for internal convenience ... does nothing useful!

    /// \ingroup futures
    template <> class FutureImpl<Void> {};

    /// Specialization of Future<Void> for internal convenience ... does nothing useful!

    /// \ingroup futures
    template <> class Future<Void> {
    public:
        typedef RemoteReference< FutureImpl<Void> > remote_refT;

        remote_refT remote_ref(World& /*world*/) const {
            return remote_refT();
        }

        Future() {}

        Future(const RemoteReference< FutureImpl<Void> >& /*ref*/) {}

        inline void set(const Future<Void>& /*f*/) {}

        inline Future<Void>& operator=(const Future<Void>& /*f*/) {
            return *this;
        }

        inline void set(const Void& /*f*/) {}

        static bool probe() { return true; }
    }; // class Future<Void>

    /// Specialization of Future for vector of Futures

    /// \ingroup futures
    /// Enables passing a vector of futures into a task and having
    /// the dependencies correctly tracked.  Does not directly
    /// support most operations that other futures do ... that is
    /// the responsiblility of the individual futures in the vector.
    template <typename T>
    class Future< std::vector< Future<T> > > : public DependencyInterface, private NO_DEFAULTS {
    private:
        typedef typename std::vector< Future<T> > vectorT;
        vectorT v;

    public:
        Future() : v() { }

        Future(const vectorT& v) : DependencyInterface(v.size()), v(v) {
            for (int i=0; i<(int)v.size(); ++i) {
                this->v[i].register_callback(this);
            }
        }

        /// Not implemented
        explicit Future(const archive::BufferInputArchive& input_arch) {
            input_arch & v;
        }

        vectorT& get() { return v; }
        const vectorT& get() const { return v; }
        operator vectorT& () { return get(); }
        operator const vectorT& () const { return get(); }

        bool probe() const {
            for(typename std::vector< Future<T> >::const_iterator it = v.begin(); it != v.end(); ++it)
                if(! it->probe())
                    return false;
            return true;
        }
    }; // class Future< std::vector< Future<T> > >


    /// Factory for vectors of futures (see section Gotchas on the mainpage)

    /// \ingroup futures
    template <typename T>
    std::vector< Future<T> > future_vector_factory(std::size_t n) {
        return std::vector< Future<T> >(n, Future<T>::default_initializer());
    }


    namespace archive {
        /// Serialize an assigned future

        /// \ingroup futures
        template <class Archive, typename T>
        struct ArchiveStoreImpl< Archive, Future<T> > {
            static inline void store(const Archive& ar, const Future<T>& f) {
                MAD_ARCHIVE_DEBUG(std::cout << "serializing future" << std::endl);
                MADNESS_ASSERT(f.probe());
                ar & f.get();
            }
        };


        /// Deserialize a future into an unassigned future

        /// \ingroup futures
        template <class Archive, typename T>
        struct ArchiveLoadImpl< Archive, Future<T> > {
            static inline void load(const Archive& ar, Future<T>& f) {
                MAD_ARCHIVE_DEBUG(std::cout << "deserializing future" << std::endl);
                MADNESS_ASSERT(!f.probe());
                T value;
                ar & value;
                f.set(value);
            }
        };


        /// Serialize an assigned future

        /// \ingroup futures
        template <class Archive>
        struct ArchiveStoreImpl< Archive, Future<void> > {
            static inline void store(const Archive&, const Future<void>&) { }
        };


        /// Deserialize a future into an unassigned future

        /// \ingroup futures
        template <class Archive>
        struct ArchiveLoadImpl< Archive, Future<void> > {
            static inline void load(const Archive&, const Future<void>&) { }
        };

        /// Serialize an assigned future

        /// \ingroup futures
        template <class Archive>
        struct ArchiveStoreImpl< Archive, Future<Void> > {
            static inline void store(const Archive&, const Future<Void>&) { }
        };


        /// Deserialize a future into an unassigned future

        /// \ingroup futures
        template <class Archive>
        struct ArchiveLoadImpl< Archive, Future<Void> > {
            static inline void load(const Archive&, Future<Void>&) { }
        };

        /// \ingroup futures
        template <class Archive, typename T>
        struct ArchiveStoreImpl< Archive, std::vector<Future<T> > > {
            static inline void store(const Archive& ar, const std::vector<Future<T> >& v) {
                MAD_ARCHIVE_DEBUG(std::cout << "serializing vector of futures" << std::endl);
                ar & v.size();
                for(typename std::vector<Future<T> >::const_iterator it = v.begin(); it != v.end(); ++it) {
                    MADNESS_ASSERT(it->probe());
                    ar & it->get();
                }
            }
        };


        /// Deserialize a future into an unassigned future

        /// \ingroup futures
        template <class Archive, typename T>
        struct ArchiveLoadImpl< Archive, std::vector<Future<T> > > {
            static inline void load(const Archive& ar, std::vector<Future<T> >& v) {
                MAD_ARCHIVE_DEBUG(std::cout << "deserializing vector of futures" << std::endl);
                std::size_t n = 0;
                ar & n;
                if(v.size() < n)
                    v.reserve(n);
                if(v.size() > n)
                    v.resize(n);
                for(typename std::vector<Future<T> >::iterator it = v.begin(); it < v.end(); ++it, --n) {
                    MADNESS_ASSERT(! it->probe());
                    it->set(ar);
                }
                for(; n != 0; --n)
                    v.push_back(Future<T>(ar));
            }
        };
    }

    /// Friendly I/O to streams for futures

    /// \ingroup futures
    template <typename T>
    std::ostream& operator<<(std::ostream& out, const Future<T>& f) ;

    template <>
    std::ostream& operator<<(std::ostream& out, const Future<void>& f) ;

    template <>
    std::ostream& operator<<(std::ostream& out, const Future<Void>& f) ;

#ifdef WORLD_INSTANTIATE_STATIC_TEMPLATES
    /// Friendly I/O to streams for futures

    /// \ingroup futures
    template <typename T>
    std::ostream& operator<<(std::ostream& out, const Future<T>& f) {
        if (f.probe()) out << f.get();
        else if (f.is_remote()) out << f.f->remote_ref;
        else if (f.f) out << "<unassigned refcnt=" << f.f.use_count() << ">";
        else out << "<unassigned>";
        return out;
    }

#endif

}


#endif // MADNESS_WORLD_WORLDFUT_H__INCLUDED
