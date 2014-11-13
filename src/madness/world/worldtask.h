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


#ifndef MADNESS_WORLD_WORLDTASK_H__INCLUDED
#define MADNESS_WORLD_WORLDTASK_H__INCLUDED

/// \file worldtask.h
/// \brief Defines TaskInterface and implements WorldTaskQueue and associated stuff.

// To do:
// a) Redo this ontop of serializable tasks which will remote much of the clutter
//    due to multiple length argument lists.
// b) Stealing which pretty much presume a) has been done

#include <iostream>
#include <madness/world/nodefaults.h>
#include <madness/world/worldrange.h>
#include <madness/world/worldtime.h>
#include <madness/world/taskfn.h>
#include <madness/world/mem_func_wrapper.h>

namespace madness {

    // Forward decls
    class World;
    class WorldTaskQueue;
    template <typename> struct TaskFunction;
    template <typename> struct TaskMemfun;

    namespace detail {

        template <typename ptrT, typename memfnT, typename resT>
        memfnT get_mem_func_ptr(const MemFuncWrapper<ptrT, memfnT, resT>&);
        template <typename, typename> class ForEachRootTask;

        /// Serialization container for sending tasks to remote nodes
        /// This is for internal use only. You should not use this class directly.
        /// \tparam refT The remote reference type for task result future
        /// \tparam functionT The task function type
        template <typename refT, typename functionT>
        struct TaskHandlerInfo {
            refT ref;               ///< Remote reference for a task result future
            functionT func;         ///< A task function
            TaskAttributes attr;    ///< Task attributes

            /// Construct task info object

            /// \param ref Remote reference to the result future
            /// \param func The task function
            /// \param attr The task attrubutes
            TaskHandlerInfo(const refT& ref, functionT func, const TaskAttributes& attr)
                    : ref(ref), func(func),attr(attr) {}
            TaskHandlerInfo() {}

            /// Serialization of object

            /// \tparam Archive The serialization archive type
            /// \param ar The serialization archive
            template <typename Archive>
            void serialize(const Archive& ar) {
                serialize_internal<functionT>(ar);
            }

        private:

            /// Identify function and member function pointers

            /// \tparam fnT The function to identify
            template <typename fnT>
            struct is_func_ptr {
                static const bool value =
                    (std::is_function<typename std::remove_pointer<fnT>::type >::value
                    || std::is_member_function_pointer<fnT>::value);
            };

            /// Serialization for function pointers and member function pointers

            /// \tparam fnT The function type
            /// \tparam Archive The serialization archive type
            /// \param ar The serialization archive
            template <typename fnT, typename Archive>
            typename enable_if<is_func_ptr<fnT> >::type
            serialize_internal(const Archive& ar) {
                ar & ref & archive::wrap_opaque(func) & attr;
            }

            /// Serialization for non- function pointers and member function pointers.
            /// \tparam fnT The function type
            /// \tparam Archive The serialization archive type
            /// \param ar The serialization archive
            template <typename fnT, typename Archive>
            typename disable_if<is_func_ptr<fnT> >::type
            serialize_internal(const Archive& ar) {
                ar & ref & func & attr;
            }
        }; // struct TaskHandlerInfo

        template <typename fnT>
        struct function_enabler : public
            lazy_enable_if_c<
                function_traits<fnT>::value || is_functor<fnT>::value,
                task_result_type<fnT> >
        { };

        template <typename memfnT>
        struct memfunc_enabler : public
            lazy_enable_if<
                std::is_member_function_pointer<memfnT>,
                task_result_type<memfnT> >
        { };

    }  // namespace detail


    /// Multi-threaded queue to manage and run tasks.
    class WorldTaskQueue : public CallbackInterface, private NO_DEFAULTS {
        friend class TaskInterface;
    private:
        World& world;              ///< The communication context
        const ProcessID me;        ///< This process
        AtomicInt nregistered;     ///< Counts pending tasks

        void notify() { nregistered--; }

        // Used in reduce kernel
        template <typename resultT, typename opT>
        static resultT sum(const resultT& left, const resultT& right, const opT& op) {
            //std::cout << " REDUCE SUM " << left << " " << right << std::endl;
            return op(left,right);
        }

        template <typename taskT>
        static void spawn_remote_task_handler(const AmArg& arg) {
            MADNESS_ASSERT(taskT::arity <= 9u);

            // Get task info and arguments form active message

            detail::TaskHandlerInfo<typename taskT::futureT::remote_refT,
                    typename taskT::functionT> info;

            archive::BufferInputArchive input_arch = arg & info;

            // Construct task
            taskT* task = new taskT(typename taskT::futureT(info.ref),
                    info.func, info.attr, input_arch);

            // Add task to queue
            arg.get_world()->taskq.add(task);
        }

        template <typename T>
        inline const T& am_arg(const Future<T>& f) {
            MADNESS_ASSERT(f.probe());
            return f.get();
        }

        template <typename T> inline const T& am_arg(const T& t) { return t; }

        typedef detail::voidT voidT;

        template <typename taskT, typename fnT, typename a1T, typename a2T, typename a3T,
                typename a4T, typename a5T, typename a6T, typename a7T,
                typename a8T, typename a9T>
        inline typename taskT::futureT
        send_task(ProcessID where, fnT fn, const a1T& a1,
                const a2T& a2, const a3T& a3, const a4T& a4, const a5T& a5,
                const a6T& a6, const a7T& a7, const a8T& a8, const a9T& a9,
                const TaskAttributes& attr)
        {
            typename taskT::futureT result;
            typedef detail::TaskHandlerInfo<typename taskT::futureT::remote_refT, typename taskT::functionT> infoT;
            world.am.send(where, & WorldTaskQueue::template spawn_remote_task_handler<taskT>,
                    new_am_arg(infoT(result.remote_ref(world), fn, attr),
                    a1, a2, a3, a4, a5, a6, a7, a8, a9));

            return result;
        }


    public:
        WorldTaskQueue(World& world);

        /// Returns the number of pending tasks
        size_t size() const { return nregistered; }


        /// Add a new local task taking ownership of the pointer

        /// The task pointer (t) is assumed to have been created with
        /// \c new and when the task is eventually run the queue
        /// will call the task's destructor using \c delete.
        ///
        /// Once the task is complete it will execute
        /// task_complete_callback to decrement the number of pending
        /// tasks and be deleted.
        void add(TaskInterface* t)  {
            nregistered++;

            t->set_info(&world, this);       // Stuff info

            if (t->ndep() == 0) {
                ThreadPool::add(t); // If no dependencies directly submit
            } else {
                // With dependencies must use the callback to avoid race condition
                t->register_submit_callback();
                //t->dec();
            }
        }

        template <typename fnT, typename a1T, typename a2T, typename a3T,
            typename a4T, typename a5T, typename a6T, typename a7T, typename a8T,
            typename a9T>
        typename TaskFn<fnT, a1T, a2T, a3T, a4T, a5T, a6T, a7T, a8T, a9T>::futureT
        add(TaskFn<fnT, a1T, a2T, a3T, a4T, a5T, a6T, a7T, a8T, a9T>* t) {
            typename TaskFn<fnT, a1T, a2T, a3T, a4T, a5T, a6T, a7T, a8T, a9T>::futureT
                res(t->result());
            add(static_cast<TaskInterface*>(t));
            return res;
        }

        /// Reduce op(item) for all items in range using op(sum,op(item))

        /// The operation must provide the following interface of
        /// which the \c operator() methods are required by reduce()
        /// and the rest by the task interface.
        /// \code
        /// struct opT {
        ///     opT();
        ///     opT(const &opT);
        ///     resultT operator()(const rangeT::iterator& it) const;
        ///     resultT operator()(const resultT& left, const resultT& right);
        ///     template <typename Archive> void serialize(const Archive& ar);
        /// }
        /// \endcode
        /// Note that the serialize method does not actually have to
        /// work unless you want to have the task be stealable.
        /// Adjust the chunksize in the range to control granularity.
        template <typename resultT, typename rangeT, typename opT>
        Future<resultT> reduce(const rangeT& range, const opT& op) {
            if (range.size() <= range.get_chunksize()) {
                resultT sum = resultT();
                for (typename rangeT::iterator it=range.begin(); it != range.end(); ++it) sum = op(sum,op(it));
                return Future<resultT>(sum);
            } else {
                rangeT left = range;
                rangeT right(left,Split());

                Future<resultT>  leftsum = add(*this, &WorldTaskQueue::reduce<resultT,rangeT,opT>, left,  op);
                Future<resultT> rightsum = add(*this, &WorldTaskQueue::reduce<resultT,rangeT,opT>, right, op);
                return add(&WorldTaskQueue::sum<resultT,opT>, leftsum, rightsum, op);
            }
        }

        /// Apply op(item) for all items in range

        /// The operation must provide the following interface of
        /// which the \c operator() method is required by for_each()
        /// and the rest by the task interface.
        /// \code
        /// struct opT {
        ///     opT();
        ///     opT(const opT&);
        ///     bool operator()(const rangeT::iterator& it) const;
        ///     template <typename Archive> void serialize(const Archive& ar);
        /// };
        /// \endcode
        /// Note that the serialize method does not actually have to
        /// work unless you want to have the task be stealable.
        ///
        /// Adjust the chunksize in the range to control granularity.
        ///
        /// Your operation should return true/false for success failure
        /// and the logical and of all results is returned as the
        /// future result.
        ///
        /// You can ignore the result if you are interested
        /// in neither synchronization nor result status.
        template <typename rangeT, typename opT>
        Future<bool> for_each(const rangeT& range, const opT& op) {
            detail::ForEachRootTask<rangeT, opT>* for_each_root =
                    new detail::ForEachRootTask<rangeT, opT>(world, range, op);
            Future<bool> result = for_each_root->result();
            add(for_each_root);
            return result;
        }


        /// Spawn a local task

        /// Spawns a task on on process.  An argument that is a future may be
        /// used to carry dependencies tasks.
        /// \tparam fnT A function pointer or functor
        /// \param fn The function to be called in the task
        /// \param attr The task attributes
        /// \return A future to the task function's results. If the task function
        /// return type is \c void , a \c Future<void> object is return that may
        /// be ignored.
        template <typename fnT>
        typename detail::function_enabler<fnT>::type
        add(fnT fn, const TaskAttributes& attr=TaskAttributes()) {
            typedef TaskFn<fnT> taskT;
            return add(new taskT(typename taskT::futureT(),
                    fn, attr));
        }

        template <typename fnT, typename a1T>
        typename detail::function_enabler<fnT>::type
        add(fnT fn, const a1T& a1, const TaskAttributes& attr=TaskAttributes()) {
            typedef TaskFn<fnT, a1T> taskT;
            return add(new taskT(typename taskT::futureT(),
                    fn, a1, attr));
        }

        template <typename fnT, typename a1T, typename a2T>
        typename detail::function_enabler<fnT>::type
        add(fnT fn, const a1T& a1, const a2T& a2, const TaskAttributes& attr=TaskAttributes()) {
            typedef TaskFn<fnT, a1T, a2T> taskT;
            return add(new taskT(typename taskT::futureT(),
                    fn, a1, a2, attr));
        }

        template <typename fnT, typename a1T, typename a2T, typename a3T>
        typename detail::function_enabler<fnT>::type
        add(fnT fn, const a1T& a1, const a2T& a2, const a3T& a3,
            const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T, a3T> taskT;
            return add(new taskT(typename taskT::futureT(),
                    fn, a1, a2, a3, attr));
        }

        template <typename fnT, typename a1T, typename a2T, typename a3T, typename a4T>
        typename detail::function_enabler<fnT>::type
        add(fnT fn, const a1T& a1, const a2T& a2, const a3T& a3, const a4T& a4,
            const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T, a3T, a4T> taskT;
            return add(new taskT(typename taskT::futureT(),
                    fn, a1, a2, a3, a4, attr));
        }

        template <typename fnT, typename a1T, typename a2T, typename a3T, typename a4T,
            typename a5T>
        typename detail::function_enabler<fnT>::type
        add(fnT fn, const a1T& a1, const a2T& a2, const a3T& a3, const a4T& a4,
            const a5T& a5, const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T, a3T, a4T, a5T> taskT;
            return add(new taskT(typename taskT::futureT(),
                    fn, a1, a2, a3, a4, a5, attr));
        }

        template <typename fnT, typename a1T, typename a2T, typename a3T, typename a4T,
            typename a5T, typename a6T>
        typename detail::function_enabler<fnT>::type
        add(fnT fn, const a1T& a1, const a2T& a2, const a3T& a3, const a4T& a4,
            const a5T& a5, const a6T& a6, const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T, a3T, a4T, a5T, a6T> taskT;
            return add(new taskT(typename taskT::futureT(),
                    fn, a1, a2, a3, a4, a5, a6, attr));
        }

        template <typename fnT, typename a1T, typename a2T, typename a3T, typename a4T,
            typename a5T, typename a6T, typename a7T>
        typename detail::function_enabler<fnT>::type
        add(fnT fn, const a1T& a1, const a2T& a2, const a3T& a3, const a4T& a4,
            const a5T& a5, const a6T& a6, const a7T& a7,
            const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T, a3T, a4T, a5T, a6T, a7T> taskT;
            return add(new taskT(typename taskT::futureT(),
                    fn, a1, a2, a3, a4, a5, a6, a7, attr));
        }

        template <typename fnT, typename a1T, typename a2T, typename a3T, typename a4T,
            typename a5T, typename a6T, typename a7T, typename a8T>
        typename detail::function_enabler<fnT>::type
        add(fnT fn, const a1T& a1, const a2T& a2, const a3T& a3, const a4T& a4,
            const a5T& a5, const a6T& a6, const a7T& a7, const a8T& a8,
            const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T, a3T, a4T, a5T, a6T, a7T, a8T> taskT;
            return add(new taskT(typename taskT::futureT(),
                    fn, a1, a2, a3, a4, a5, a6, a7, a8, attr));
        }


        template <typename fnT, typename a1T, typename a2T, typename a3T, typename a4T,
            typename a5T, typename a6T, typename a7T, typename a8T, typename a9T>
        typename detail::function_enabler<fnT>::type
        add(fnT fn, const a1T& a1, const a2T& a2, const a3T& a3, const a4T& a4,
            const a5T& a5, const a6T& a6, const a7T& a7, const a8T& a8, const a9T& a9,
            const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T, a3T, a4T, a5T, a6T, a7T, a8T, a9T> taskT;
            return add(new taskT(typename taskT::futureT(),
                    fn, a1, a2, a3, a4, a5, a6, a7, a8, a9, attr));
        }


        /// Spawn a remote task

        /// Spawns a task on process \c dest , which may or may not be this
        /// process. An argument that is a future may be used to carry
        /// dependencies for local tasks. An future that is not ready cannot be
        /// used as an argument for a remote tasks -- i.e., remote  tasks must
        /// be ready to execute (you can work around this by making a local task
        /// to submit the remote task once everything is ready).
        /// \tparam fnT A function pointer or functor type
        /// \param dest The process where the task will be spawned
        /// \param fn The function to be called in the task
        /// \param attr The task attributes
        /// \return A future to the task function's results. If the task function
        /// return type is \c void , a \c Future<void> object is return that may
        /// be ignored.
        /// \note Arguments must be (de)serializable and must of course make
        /// sense at the remote destination.  Fundamental types,
        /// simple STL containers, and pointers to World,
        /// WorldContainer, and user-defined types derived from
        /// WorldObject<> are automatically handled.  Anything else is
        /// your problem.
        template <typename fnT>
        typename detail::function_enabler<fnT>::type
        add(ProcessID dest, fnT fn, const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT> taskT;
            if(dest == me)
                return add(fn, attr);
            else
                return send_task<taskT>(dest, fn, voidT::value, voidT::value,
                        voidT::value, voidT::value, voidT::value, voidT::value,
                        voidT::value, voidT::value, voidT::value, attr);
        }

        /// Spawn a remote task

        /// Spawns a task on process \c dest , which may or may not be this
        /// process.  An argument that is a future may be used to carry
        /// dependencies for local tasks.  An unready future cannot be used as
        /// an argument for a remote tasks -- i.e., remote  tasks must be ready
        /// to execute (you can work around this by making a local task to
        /// submit the remote task once everything is ready).
        /// \param dest The process where the task will be spawned
        /// \param fn The function to be called in the task
        /// \param attr The task attributes
        /// \return A future to the task function's results. If the task function
        /// return type is \c void , a \c Future<void> object is return that may
        /// be ignored.
        /// Invoke "resultT (*fn)(a1T)" as a task, local or remote

        /// A future is returned to hold the eventual result of the
        /// task.  Future<void> is an empty class that may be ignored.
        /// \note Arguments must be (de)serializable and must of course make
        /// sense at the remote destination.  Fundamental types,
        /// simple STL containers, and pointers to World,
        /// WorldContainer, and user-defined types derived from
        /// WorldObject<> are automatically handled.  Anything else is
        /// your problem.
        template <typename fnT, typename a1T>
        typename detail::function_enabler<fnT>::type
        add(ProcessID dest, fnT fn, const a1T& a1, const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T> taskT;
            if(dest == me)
                return add(fn, a1, attr);
            else
                return send_task<taskT>(dest, fn, am_arg(a1), voidT::value,
                        voidT::value, voidT::value, voidT::value, voidT::value,
                        voidT::value, voidT::value, voidT::value, attr);
        }

        /// Invoke "resultT (*fn)(a1T,a2T)" as a task, local or remote

        /// A future is returned to hold the eventual result of the
        /// task.  Future<void> is an empty class that may be ignored.
        template <typename fnT, typename a1T, typename a2T>
        typename detail::function_enabler<fnT>::type
        add(ProcessID dest, fnT fn, const a1T& a1, const a2T& a2,
                const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T> taskT;
            if(dest == me)
                return add(fn, a1, a2, attr);
            else
                return send_task<taskT>(dest, fn, am_arg(a1), am_arg(a2),
                        voidT::value, voidT::value, voidT::value, voidT::value,
                        voidT::value, voidT::value, voidT::value, attr);
        }

        /// Invoke "resultT (*fn)(a1T,a2T,a3T)" as a task, local or remote

        /// A future is returned to hold the eventual result of the
        /// task.  Future<void> is an empty class that may be ignored.
        template <typename fnT, typename a1T, typename a2T, typename a3T>
        typename detail::function_enabler<fnT>::type
        add(ProcessID dest, fnT fn, const a1T& a1, const a2T& a2, const a3T& a3,
                const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T, a3T> taskT;
            if(dest == me)
                return add(fn, a1, a2, a3, attr);
            else
                return send_task<taskT>(dest, fn, am_arg(a1), am_arg(a2),
                        am_arg(a3), voidT::value, voidT::value, voidT::value,
                        voidT::value, voidT::value, voidT::value, attr);
        }

        /// Invoke "resultT (*fn)(a1T,a2T,a3T,a4T)" as a task, local or remote

        /// A future is returned to hold the eventual result of the
        /// task.  Future<void> is an empty class that may be ignored.
        template <typename fnT, typename a1T, typename a2T, typename a3T,
                typename a4T>
        typename detail::function_enabler<fnT>::type
        add(ProcessID dest, fnT fn, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T, a3T, a4T> taskT;
            if(dest == me)
                return add(fn, a1, a2, a3, a4, attr);
            else
                return send_task<taskT>(dest, fn, am_arg(a1), am_arg(a2),
                        am_arg(a3), am_arg(a4), voidT::value, voidT::value,
                        voidT::value, voidT::value, voidT::value, attr);
        }

        /// Invoke "resultT (*fn)(a1T,a2T,a3T,a4T,a5T)" as a task, local or remote

        /// A future is returned to hold the eventual result of the
        /// task.  Future<void> is an empty class that may be ignored.
        template <typename fnT, typename a1T, typename a2T, typename a3T,
                typename a4T, typename a5T>
        typename detail::function_enabler<fnT>::type
        add(ProcessID dest, fnT fn, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T, a3T, a4T, a5T> taskT;
            if(dest == me)
                return add(fn, a1, a2, a3, a4, a5, attr);
            else
                return send_task<taskT>(dest, fn, am_arg(a1), am_arg(a2),
                        am_arg(a3), am_arg(a4), am_arg(a5), voidT::value,
                        voidT::value, voidT::value, voidT::value, attr);
        }

        /// Invoke "resultT (*fn)(a1T,a2T,a3T,a4T,a5T,a6T)" as a task, local or remote

        /// A future is returned to hold the eventual result of the
        /// task.  Future<void> is an empty class that may be ignored.
        template <typename fnT, typename a1T, typename a2T, typename a3T,
                typename a4T, typename a5T, typename a6T>
        typename detail::function_enabler<fnT>::type
        add(ProcessID dest, fnT fn, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6,
                const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T, a3T, a4T, a5T, a6T> taskT;
            if(dest == me)
                return add(fn, a1, a2, a3, a4, a5, a6, attr);
            else
                return send_task<taskT>(dest, fn, am_arg(a1), am_arg(a2),
                        am_arg(a3), am_arg(a4), am_arg(a5), am_arg(a6),
                        voidT::value, voidT::value, voidT::value, attr);
        }

        /// Invoke "resultT (*fn)(a1T,a2T,a3T,a4T,a5T,a6T,a7T)" as a task, local or remote

        /// A future is returned to hold the eventual result of the
        /// task.  Future<void> is an empty class that may be ignored.
        template <typename fnT, typename a1T, typename a2T, typename a3T,
                typename a4T, typename a5T, typename a6T, typename a7T>
        typename detail::function_enabler<fnT>::type
        add(ProcessID dest, fnT fn, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6, const a7T& a7,
                const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T, a3T, a4T, a5T, a6T, a7T> taskT;
            if(dest == me)
                return add(fn, a1, a2, a3, a4, a5, a6, a7, attr);
            else
                return send_task<taskT>(dest, fn, am_arg(a1), am_arg(a2),
                        am_arg(a3), am_arg(a4), am_arg(a5), am_arg(a6),
                        am_arg(a7), voidT::value, voidT::value, attr);
        }

        /// Invoke "resultT (*fn)(a1T,a2T,a3T,a4T,a5T,a6T,a7T,a8T)" as a task, local or remote

        /// A future is returned to hold the eventual result of the
        /// task.  Future<void> is an empty class that may be ignored.
        template <typename fnT, typename a1T, typename a2T, typename a3T,
                typename a4T, typename a5T, typename a6T, typename a7T,
                typename a8T>
        typename detail::function_enabler<fnT>::type
        add(ProcessID dest, fnT fn, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6, const a7T& a7,
                const a8T& a8, const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T, a3T, a4T, a5T, a6T, a7T, a8T> taskT;
            if(dest == me)
                return add(fn, a1, a2, a3, a4, a5, a6, a7, a8, attr);
            else
                return send_task<taskT>(dest, fn, am_arg(a1), am_arg(a2),
                        am_arg(a3), am_arg(a4), am_arg(a5), am_arg(a6),
                        am_arg(a7), am_arg(a8), voidT::value, attr);
        }

        /// Invoke "resultT (*fn)(a1T,a2T,a3T,a4T,a5T,a6T,a7T,a8T,a9T)" as a task, local or remote

        /// A future is returned to hold the eventual result of the
        /// task.  Future<void> is an empty class that may be ignored.
        template <typename fnT, typename a1T, typename a2T, typename a3T,
                typename a4T, typename a5T, typename a6T, typename a7T,
                typename a8T, typename a9T>
        typename detail::function_enabler<fnT>::type
        add(ProcessID dest, fnT fn, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6, const a7T& a7,
                const a8T& a8, const a9T& a9, const TaskAttributes& attr=TaskAttributes())
        {
            typedef TaskFn<fnT, a1T, a2T, a3T, a4T, a5T, a6T, a7T, a8T, a9T> taskT;
            if(dest == me)
                return add(fn, a1, a2, a3, a4, a5, a6, a7, a8, a9, attr);
            else
                return send_task<taskT>(dest, fn, am_arg(a1), am_arg(a2),
                        am_arg(a3), am_arg(a4), am_arg(a5), am_arg(a6),
                        am_arg(a7), am_arg(a8), am_arg(a9), attr);
        }

        /// Invoke "resultT (obj.*memfun)()" as a local task
        template <typename objT, typename memfnT>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT& obj, memfnT memfun, const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),attr); }

        /// Invoke "resultT (obj.*memfun)(a1T)" as a local task
        template <typename objT, typename memfnT, typename a1T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT& obj, memfnT memfun, const a1T& a1,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT& obj, memfnT memfun, const a1T& a1, const a2T& a2,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT& obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT& obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT& obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const TaskAttributes attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT& obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6,a7)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T, typename a7T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT& obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6, const a7T& a7,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,a7,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6,a7,a8)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T, typename a7T,
            typename a8T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT& obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6, const a7T& a7,
                const a8T& a8, const TaskAttributes attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,a7,a8,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6,a7,a8,a9)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T, typename a7T,
            typename a8T, typename a9T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT& obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6, const a7T& a7,
                const a8T& a8, const a9T& a9, const TaskAttributes attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,a7,a8,a9,attr); }


        /// Invoke "resultT (obj.*memfun)()" as a local task
        template <typename objT, typename memfnT>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT& obj, memfnT memfun, const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),attr); }

        /// Invoke "resultT (obj.*memfun)(a1T)" as a local task
        template <typename objT, typename memfnT, typename a1T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT& obj, memfnT memfun, const a1T& a1,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT& obj, memfnT memfun, const a1T& a1, const a2T& a2,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT& obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT& obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT& obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const TaskAttributes attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT& obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6,a7)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T, typename a7T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT& obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6, const a7T& a7,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,a7,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6,a7,a8)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T, typename a7T,
            typename a8T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT& obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6, const a7T& a7,
                const a8T& a8, const TaskAttributes attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,a7,a8,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6,a7,a8,a9)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T, typename a7T,
            typename a8T, typename a9T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT& obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6, const a7T& a7,
                const a8T& a8, const a9T& a9, const TaskAttributes attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,a7,a8,a9,attr); }

        /// Invoke "resultT (obj.*memfun)()" as a local task
        template <typename objT, typename memfnT>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT* obj, memfnT memfun, const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),attr); }

        /// Invoke "resultT (obj.*memfun)(a1T)" as a local task
        template <typename objT, typename memfnT, typename a1T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT* obj, memfnT memfun, const a1T& a1,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT* obj, memfnT memfun, const a1T& a1, const a2T& a2,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT* obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT* obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,attr); }


        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT* obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const TaskAttributes attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,attr); }


        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT* obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6,a7)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T, typename a7T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT* obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6, const a7T& a7,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,a7,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6,a7,a8)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T, typename a7T,
            typename a8T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT* obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6, const a7T& a7,
                const a8T& a8, const TaskAttributes attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,a7,a8,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6,a7,a8,a9)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T, typename a7T,
            typename a8T, typename a9T>
        typename detail::memfunc_enabler<memfnT>::type
        add(objT* obj, memfnT memfun, const a1T& a1, const a2T& a2,
                const a3T& a3, const a4T& a4, const a5T& a5, const a6T& a6,
                const a7T& a7, const a8T& a8, const a9T& a9,
                const TaskAttributes attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,a7,a8,a9,attr); }


        /// Invoke "resultT (obj.*memfun)()" as a local task
        template <typename objT, typename memfnT>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT* obj, memfnT memfun, const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),attr); }

        /// Invoke "resultT (obj.*memfun)(a1T)" as a local task
        template <typename objT, typename memfnT, typename a1T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT* obj, memfnT memfun, const a1T& a1,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT* obj, memfnT memfun, const a1T& a1, const a2T& a2,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT* obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT* obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,attr); }


        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT* obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const TaskAttributes attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,attr); }


        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT* obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6,a7)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T, typename a7T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT* obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6, const a7T& a7,
                const TaskAttributes& attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,a7,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6,a7,a8)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T, typename a7T,
            typename a8T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT* obj, memfnT memfun, const a1T& a1, const a2T& a2, const a3T& a3,
                const a4T& a4, const a5T& a5, const a6T& a6, const a7T& a7,
                const a8T& a8, const TaskAttributes attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,a7,a8,attr); }

        /// Invoke "resultT (obj.*memfun)(a1T,a2T,a3,a4,a5,a6,a7,a8,a9)" as a local task
        template <typename objT, typename memfnT, typename a1T, typename a2T,
            typename a3T, typename a4T, typename a5T, typename a6T, typename a7T,
            typename a8T, typename a9T>
        typename detail::memfunc_enabler<memfnT>::type
        add(const objT* obj, memfnT memfun, const a1T& a1, const a2T& a2,
                const a3T& a3, const a4T& a4, const a5T& a5, const a6T& a6,
                const a7T& a7, const a8T& a8, const a9T& a9,
                const TaskAttributes attr=TaskAttributes())
        { return add(detail::wrap_mem_fn(obj,memfun),a1,a2,a3,a4,a5,a6,a7,a8,a9,attr); }

    private:

        struct ProbeAllDone {
            WorldTaskQueue* tq;
            ProbeAllDone(WorldTaskQueue* tq) : tq(tq) {}
            bool operator()() const { return (tq->nregistered == 0); }
        };

    public:

        /// Returns after all local tasks have completed

        /// While waiting the calling thread will run tasks.
        void fence()  {
            try {
                ThreadPool::await(ProbeAllDone(this), true);
            } catch(...) {
                printf("!!MADNESS ERROR: Exception thrown in WorldTaskQueue::fence() with %i pending task(s)\n", int(nregistered));
                throw;
            }
        }
    };

    namespace detail {

        /// Apply an operation to a range of iterators

        /// \tparam rangeT The range of iterators type
        /// \tparam opT The operation type
        /// This task will progressively split range, spawning leaf for each
        /// tasks, until the range of iterators is smaller than the range chunck
        /// size.
        template <typename rangeT, typename opT>
        class ForEachTask : public TaskInterface {
        private:
            rangeT range_; ///< The range of iterators
            opT op_; ///< The operation to apply to range
            ForEachRootTask<rangeT, opT>& root_; ///< The root task that signals completion and status

            // not allowed
            ForEachTask(const ForEachTask<rangeT, opT>&);
            ForEachTask& operator=(const ForEachTask<rangeT, opT>&);

        public:

            /// Constructor
            ForEachTask(const rangeT range, const opT& op, ForEachRootTask<rangeT, opT>& root) :
                TaskInterface(0, TaskAttributes::hipri()), range_(range), op_(op), root_(root)
            {
                // Increment the master task dependency counter for this task
                root_.inc();
            }

            /// Virtual destructor
            virtual ~ForEachTask() { }

            /// Run the task
            virtual void run(const TaskThreadEnv&) {
                // Spawn leaf tasks and split range until it is less than chuncksize
                while(range_.size() > range_.get_chunksize()) {
                    rangeT right(range_,Split());
                    ForEachTask<rangeT,opT>* leaf = new ForEachTask<rangeT,opT>(right, op_, root_);
                    root_.world().taskq.add(leaf);
                }

                // Iterate over the remaining chunck of range and call op_ for each element
                int status = 0;
                for(typename rangeT::iterator it = range_.begin(); it != range_.end();  ++it)
                    if(op_(it))
                        ++status;

                // Notify the root task that this task is done give the status
                root_.complete(status);
            }

        private:
            /// Get the task id

            /// \param id The id to set for this task
            virtual void get_id(std::pair<void*,unsigned short>& id) const {
                PoolTaskInterface::make_id(id, *this);
            }
        }; // class ForEachTask


        /// Apply an operation to a range of iterators

        /// \tparam rangeT The range of iterators type
        /// \tparam opT The operation type
        /// This task spawns for each tasks and collects information on the
        /// results of those tasks. Once all tasks have completed it will set
        /// the result future.
        template <typename rangeT, typename opT>
        class ForEachRootTask : public TaskInterface {
        private:
            World& world_; ///< The world where this task will run
            AtomicInt status_; ///< Accumulates the status of each iteration
            Future<bool> completion_status_; ///< The result of this set of tasks

        public:

            /// Constructor

            /// \param world The world where the tasks will run
            /// \param range The range of iterators
            /// \param op The oeration that will be applied to the range of iterators
            ForEachRootTask(World& world, const rangeT range, const opT& op) :
                TaskInterface(0, TaskAttributes::hipri()), world_(world)
            {
                status_ = - (range.size());
                // Create the first for each task.
                world_.taskq.add(new ForEachTask<rangeT,opT>(range, op, *this));
            }

            /// Virtual destructor
            virtual ~ForEachRootTask() { }

            /// World accessor

            /// \return A reference to the world
            World& world() const { return world_; }

            /// Result accessor

            /// \return A const reference to the result future
            const Future<bool>& result() const { return completion_status_; }

            /// Called by child tasks when they are complete

            /// \param status The number of iterations that returned true
            void complete(const int status) {
                status_ += status;
                DependencyInterface::notify();
            }

            /// Task run work

            /// Sets the result future based on the status of all iterations
            virtual void run(const TaskThreadEnv&) { completion_status_.set(status_ == 0); }

        private:
            /// Get the task id

            /// \param id The id to set for this task
            virtual void get_id(std::pair<void*,unsigned short>& id) const {
                PoolTaskInterface::make_id(id, *this);
            }
        }; // class ForEachRootTask


    }  // namespace detail

}


#endif // MADNESS_WORLD_WORLDTASK_H__INCLUDED
