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

/// \file SRConf.h
/// \brief handles the low-level details of a separated representation tensor

#ifndef SRCONF_H_
#define SRCONF_H_

#include "tensor/tensor.h"
#include "mra/funcdefaults.h"
#include <linalg/clapack.h>
#include <linalg/tensor_lapack.h>

namespace madness {

	/// return the number of vectors (i.e. dim_eff) according to the TensorType
	static unsigned int compute_nvec(const TensorType& tt) {
		if (tt==TT_FULL) return 1;
		if (tt==TT_2D) return 2;
		if (tt==TT_3D) return 3;
		print("unknown TensorType",tt);
		MADNESS_ASSERT(0);
	}

	enum orthoMethod {ortho3_, ortho5_, ortho6_, reconstruct_, sequential_};


	struct OrthoMethod {

		const static orthoMethod om=ortho3_;

	};

	static
	inline
    std::ostream& operator<<(std::ostream& s, const OrthoMethod& om) {
    	std::string str="confused orthogonalization method";
    	if (om.om==ortho3_) str="ortho3";
    	if (om.om==ortho5_) str="ortho5";
    	if (om.om==ortho6_) str="ortho6";
    	if (om.om==sequential_) str="sequential";
    	if (om.om==reconstruct_) str="reconstruct";
    	s << str.c_str();
    	return s;
    }

	/// return the index of the last singular vector/value to meet the threshold

	/// given a matrix A in SVD form, truncate the singular values such that the
	/// accuracy threshold is still met.
	/// @param[in]	thresh	the threshold eps: || A(svd) - A(truncated) || < eps
	/// @param[in] 	rank	the number of singular values in w
	/// @paran[in]	w		the weights/singular values of A(svd)
	/// @return		i		the index of s_max to contribute: w(Slice(0,i)); i.e. inclusive!
	static int max_sigma(const double& thresh, const int& rank, const Tensor<double>& w) {

		// find the maximal singular value that's supposed to contribute
		// singular values are ordered (largest first)
		double residual=0.0;
		long i;
		for (i=rank-1; i>=0; i--) {
			residual+=w(i)*w(i);
			if (residual>thresh*thresh) break;
		}
		return i;
	}



	/**
	 * A SRConf handles all the configurations in a Separated Representation.
	 */

	template <typename T>
	class SRConf {

	public:

		typedef Tensor<T> tensorT;

		/// check orthonormality at low rank additions
		static const bool check_orthonormality=false;

		/// the number of dimensions (the order of the tensor)
		unsigned int dim_;

		/// for each configuration the weight; length should be r
		Tensor<double>  weights_;

		/// for each (physical) dimension one Tensor of (logical) dimension (r,k)
		/// for vectors or (r,kprime,k) for operators
		std::vector<tensorT> vector_;

		/// for SVD updates these matrices diagonalize the new singular value matrix
		/// cf eq. (11) of Brand2006: U', V'
		std::vector<tensorT> subspace_vec_;

		/// what is the rank of this
		long rank_;

		/// the number of underlying basis functions
		/// the dimensions of vector_ will be
		/// vector_(rank,maxk),
		/// vector_(rank,maxk,maxk), etc
		unsigned int maxk_;

		/// Slice containing the actual data in each vector, ignoring "empty" configurations;
		/// will maintain contiguity of the data.
		std::vector<Slice> s_;

		/// how will this be represented
		TensorType tensortype_;

		/// flag if we are in updating mode

		/// if so,there will be additional subspace rotating matrices (cf Brand, eq. (11)),
		/// that we have to incorporate to vector_ before doing anything else
		bool updating_;

	public:

		/// default ctor
		SRConf() : dim_(0), rank_(0), maxk_(0), s_(), tensortype_(TT_NONE), updating_(false) {
		};

		/// ctor with dimensions for a vector configuration (tested)
		SRConf(const unsigned int& dim, const unsigned int& k, const TensorType& tt)
			: dim_(dim)
			, rank_(0)
			, maxk_(k)
			, s_()
			, tensortype_(tt)
			, updating_(false) {

			// make sure dim is integer multiple of requested TT
			const unsigned int nvec=compute_nvec(tt);
			MADNESS_ASSERT(dim%nvec==0);

			// construct empty vector
			weights_=Tensor<double>(int(0));
			vector_=std::vector<Tensor<T> > (nvec);

			if (tt==TT_FULL) {
				vector_[0]=tensorT(std::vector<long>(dim,k));
			} else {
				for (unsigned int idim=0; idim<nvec; idim++) vector_[idim]=Tensor<T>(0,this->kVec());
			}
			make_structure();
			MADNESS_ASSERT(has_structure());
		}

		/// copy ctor (tested); shallow copy
		SRConf(const SRConf& rhs)  {
			*this=rhs;
            MADNESS_ASSERT(has_structure());
		}

		/// ctor with provided weights and effective vectors; shallow copy
		SRConf(const Tensor<double>& weights, const std::vector<Tensor<T> >& vectors,
				const unsigned int& dim, const unsigned int maxk, const TensorType& tt)
			: dim_(dim)
			, maxk_(maxk)
			, tensortype_(tt)
			, updating_(false) {

			// consistency check
			MADNESS_ASSERT(vectors.size()>0);
			MADNESS_ASSERT(weights.ndim()==1 and weights.dim(0)==vectors[0].dim(0));

			// compute dimension
			unsigned int nvec=compute_nvec(tt);
			MADNESS_ASSERT(vectors.size()==nvec);
			MADNESS_ASSERT(dim%nvec==0);

			rank_=weights.dim(0);
			weights_=weights;
			vector_=std::vector<Tensor<T> > (vectors.size());
			for (unsigned int idim=0; idim<vectors.size(); idim++) {
				vector_[idim]=vectors[idim];
			}
			make_slices();
            MADNESS_ASSERT(has_structure());
		}

		/// explicit ctor with one vector (aka full representation), shallow
		SRConf(const tensorT& vector1)
			: dim_(vector1.ndim())
			, weights_(Tensor<double>())
			, rank_(-1)
			, maxk_(vector1.dim(0))
			, tensortype_(TT_FULL)
			, updating_(false) {

			vector_.resize(1);
			vector_[0]=vector1;
			MADNESS_ASSERT(has_structure());
		}

		/// explicit ctor with two vectors (aka SVD), shallow
		SRConf(const Tensor<double>& weights, const tensorT& vector1, const tensorT& vector2,
				const unsigned int& dim, const unsigned int maxk)
			: dim_(dim)
			, maxk_(maxk)
			, tensortype_(TT_2D)
			, updating_(false) {

			MADNESS_ASSERT(weights.ndim()==1);
			MADNESS_ASSERT(vector1.ndim()==2);
			MADNESS_ASSERT(vector2.ndim()==2);
			MADNESS_ASSERT(weights.dim(0)==vector1.dim(0));
			MADNESS_ASSERT(vector2.dim(0)==vector1.dim(0));
			vector_.resize(2);
			vector_[0]=vector1;
			vector_[1]=vector2;
			weights_=weights;
			rank_=weights.dim(0);
			make_structure();
			make_slices();
            MADNESS_ASSERT(has_structure());
		}

		/// assignment operator (tested), shallow copy of vectors
		SRConf& operator=(const SRConf& rhs)  {

			// check for self-assignment
			if (&rhs==this) return *this;

			MADNESS_ASSERT(not rhs.updating_);
			// these always hold
			dim_=rhs.dim_;
			tensortype_=rhs.tensortype_;
			updating_=rhs.updating_;
			maxk_=rhs.maxk_;
			s_=rhs.s_;

			if (rhs.has_no_data()) {
				// construct empty vector
				weights_=Tensor<double>(0);
				vector_=std::vector<Tensor<T> > (rhs.dim_eff());
				rank_=0;
				for (unsigned int idim=0; idim<dim_eff(); idim++) vector_[idim]=Tensor<T>(0,this->kVec());
				make_structure();


			} else if (rhs.type()==TT_FULL) {
				weights_=Tensor<double>();
				rank_=-1;
				vector_.resize(1);
				vector_[0]=rhs.ref_vector(0);

			} else {
				// assign vectors; shallow copy
				vector_.resize(rhs.vector_.size());
				for (unsigned int i=0; i<rhs.vector_.size(); i++) {
					vector_[i]=rhs.vector_[i];
				}

				// shallow copy
				weights_=(rhs.weights_);
				rank_=rhs.rank();

				// consistency check
				for (unsigned int idim=0; idim<dim_eff(); idim++) {
					MADNESS_ASSERT(weights_.dim(0)==vector_[idim].dim(0));
				}
			}
            MADNESS_ASSERT(has_structure());
			return *this;
		}

		/// return some of the terms of the SRConf (start,..,end), inclusively
		/// shallow copy
		const SRConf get_configs(const int& start, const int& end) const {

			MADNESS_ASSERT((start>=0) and (end<=rank()));
			MADNESS_ASSERT(s_.size()>1);
			const long nvec=dim_eff();
			const long dim_pv_eff=s_.size()-1;	// #dim per vector minus rank-dim

			Slice s(start,end);
			std::vector<tensorT> v(nvec);

			// slice vectors
			if (dim_pv_eff==1) {
				for (long i=0; i<nvec; i++) v[i]=ref_vector(i)(s,_);
			} else if (dim_pv_eff==2) {
				for (long i=0; i<nvec; i++) v[i]=ref_vector(i)(s,_,_);
			} else if (dim_pv_eff==3) {
				for (long i=0; i<nvec; i++) v[i]=ref_vector(i)(s,_,_,_);
			} else {
				MADNESS_EXCEPTION("faulty dim_pv in SRConf::get_configs",0);
			}

			SRConf<T> result(weights_(s),v,dim(),get_k(),type());
            MADNESS_ASSERT(result.has_structure());
			return result;
		}

		/// dtor
		virtual ~SRConf() {
			vector_.clear();
			subspace_vec_.clear();
			weights_.clear();
			s_.clear();
		}

        template <typename Archive>
        void serialize(Archive& ar) {
              	int i=int(tensortype_);
              	ar & dim_ & weights_ & vector_ & subspace_vec_ & rank_ & maxk_ & i & updating_;
              	tensortype_=TensorType(i);
              	make_slices();
                MADNESS_ASSERT(has_structure());
        }


		/// does this have any data?
		bool has_data() const {
			if (tensortype_==TT_FULL) return (vector_.size()>0 and vector_[0].has_data());
			return rank()>0;
		}

		/// does this have any data?
		bool has_no_data() const {return !has_data();}

		/// reserve enough space to hold at least r configurations
		void reserve(long r) {

			// this should at least hold the current information
			MADNESS_ASSERT(r>=this->rank());
			MADNESS_ASSERT(has_data() or vector_.size()>0);

			// fast return if possible
			// nothing to be done
			if (r==0) return;
			// already large enuff?
			if (this->vector_[0].dim(0)>=r) return;

			// to avoid incremental increase of the rank
			r+=3;

			// for convenience
			const long rank=this->rank();
			const long kvec=this->kVec();
			const bool had_structure=this->has_structure();
			if (had_structure) this->undo_structure();

			// transfer weights
			Tensor<double> newWeights(r);
			if (rank>0) newWeights(Slice(0,rank-1))=weights_(Slice(0,rank-1));
			std::swap(weights_,newWeights);

			// transfer vectors
			for (unsigned int idim=0; idim<this->dim_eff(); idim++) {

				tensorT newVector(r,kvec);
				if (rank>0) newVector(this->c0())=vector_[idim](this->c0());
				std::swap(vector_[idim],newVector);

			}
			MADNESS_ASSERT(weights_.dim(0)==vector_[0].dim(0));
			if (had_structure) this->make_structure(true);
            MADNESS_ASSERT(has_structure());

		}

		/// return a Slice that corresponds the that part of vector_ that holds coefficients
		const std::vector<Slice>& c0() const {
			MADNESS_ASSERT(s_.size()>0);
			return s_;
		}

		/// rank-n update updating the whole chunk
//		void rank_n_update_chunkwise(const SRConf<T>& rhs) {
		void rank_n_update_chunkwise(const tensorT& aa, const tensorT& b, const Tensor<double>& alpha) {

			// works only for SVD
			MADNESS_ASSERT(this->dim_eff()==2);

			const long rank=this->rank();
			const long rhs_rank=alpha.dim(0);

			tensorT a=copy(aa);

			// include weights
			for (unsigned int r=0; r<rhs_rank; r++) {
				a(r,Slice(_)).scale(alpha(r));
			}


			if (not updating_) init_accumulate();
			MADNESS_ASSERT(updating_);


			// use the language of the article
			const tensorT UT=this->ref_vector(0)(this->c0());
			const tensorT VT=this->ref_vector(1)(this->c0());

			/*
			 * formal dimensions 	 				computational dimensions in use:
			 * 	a(k,r)								a(r,k)
			 * 	U(k,R)								UT(R,k)
			 * 	mm(R,r) = UT(R,k) a(k,r)			mm(R,r)	= UT(R,k) a(r,k)
			 * 	m(R,r)	= U'(R,R) UT(R,k) a(k,r)	m(R,r)	= U'(R,R) mm(R,r)
			 * 	p(k,r)	= a(k,r) - U(k,R) mm(R,r)	p(r,k)	= a(r,k) = mm(R,r) UT(R,k)
			 * 	Ra(r,r)
			 */
			// eq (6)
			const tensorT mm=inner(UT,a,1,1);
			const tensorT m=inner(subspace_vec_[0],mm,0,0);
			const tensorT p=a-inner(mm,UT,0,0);
			tensorT Ra(rhs_rank,rhs_rank);
			for (unsigned int r=0; r<rhs_rank; r++) {
				double pnorm=p(r,Slice(_)).normf();
				Ra(r,r)=pnorm;
			}

			// eq (7)
			const tensorT nn=inner(VT,b,1,1);
			const tensorT n=inner(subspace_vec_[1],nn,0,0);
			const tensorT q=b-inner(nn,VT,0,0);
			tensorT Rb(rhs_rank,rhs_rank);
			for (unsigned int r=0; r<rhs_rank; r++) {
				double qnorm=q(r,Slice(_)).normf();
				Rb(r,r)=qnorm;
			}


			// eq (8)
			tensorT K(rank+rhs_rank,rank+rhs_rank);
			K(Slice(0,rank-1),Slice(0,rank-1))				= inner(m,n,1,1);
			K(Slice(0,rank-1),Slice(rank,rank+rhs_rank-1))	= inner(m,Rb,1,1);
			K(Slice(rank,rank+rhs_rank-1),Slice(0,rank-1))	= inner(Ra,n,1,1);
			K(Slice(rank,rank+rhs_rank-1),Slice(rank,rank+rhs_rank-1))	= inner(Ra,Rb);

			// include overlap of existing matrix
			for (long i=0; i<rank; i++) {
				K(i,i)+=this->weights(i);
			}															// 0.3 s

			// diagonalize K, sec. (4.1)
			tensorT Up,VTp;
			Tensor<double> Sp;
			svd(K,Up,Sp,VTp);											// 1.3 s
			tensorT Vp=transpose(VTp);									// 0.1 s

			// note there are only left subspaces

			// rank-increasing update
			if (Sp(rank)>1.e-10 and rank<11) {
				tensorT P=p;
				tensorT Q=q;
				for (unsigned int r=0; r<rhs_rank; r++) {
					P(r,Slice(_)).scale(1.0/Ra(r,r));
					Q(r,Slice(_)).scale(1.0/Rb(r,r));
				}
				update_left_subspace(Up,P,0);
				update_left_subspace(Vp,Q,1);

				rank_+=rhs_rank;
				weights_=Sp(Slice(0,rank_-1));
				make_slices();

			// non-rank-increasing update
			} else {
//				if (rank>=10) print("truncating at rank 10");
				Up=Up(Slice(0,rank-1),Slice(0,rank-1));
				Vp=Vp(Slice(0,rank-1),Slice(0,rank-1));
				update_left_subspace(Up,tensorT(),0);
				update_left_subspace(Vp,tensorT(),1);
				weights_=Sp(Slice(0,rank-1));

			}
            MADNESS_ASSERT(has_structure());

		}

		/// rank-n update updating one at a time
		void rank_n_update_sequential(const SRConf<T>& rhs2) {

			// works only for SVD
			MADNESS_ASSERT(this->dim_eff()==2);
			MADNESS_ASSERT(rhs2.dim_eff()==2);


			SRConf<T> rhs=rhs2;
			rhs.undo_structure();										// 0.3 s

			for (long r=0; r<rhs.rank(); r++) {
				const tensorT a=(rhs.ref_vector(0)(r,Slice(_)));
				const tensorT b=(rhs.ref_vector(1)(r,Slice(_)));
				this->rank1_update_slow(a,b,rhs.weights(r));
			}
            MADNESS_ASSERT(has_structure());

		}


		/// rank-1 update of this as in:	 *this += alpha * rhs

		/// M. Brand, Linear Algebra Appl 2006 vol. 415 (1) pp. 20-30
		void rank1_update_slow(const tensorT& a, const tensorT& b, const double& alpha) {

			// works only for SVD
			MADNESS_ASSERT(this->dim_eff()==2);
			// for now

			if (not updating_) init_accumulate();
			MADNESS_ASSERT(updating_);

			const long rank=this->rank();

			// use the language of the article
			const tensorT UT=this->ref_vector(0)(this->c0());
			const tensorT VT=this->ref_vector(1)(this->c0());

			// eq (6)
			const tensorT mm=inner(UT,a);
			const tensorT m=inner(subspace_vec_[0],mm,0,0);
			const tensorT p=a-inner(UT,mm,0,0);
			const double Ra=p.normf();

			// eq (7)
			const tensorT nn=inner(VT,b);
			const tensorT n=inner(subspace_vec_[1],nn,0,0);
			const tensorT q=b-inner(VT,nn,0,0);
			const double Rb=q.normf();									// 1.4 s


			// eq (8)
			tensorT mp(rank+1);
			mp(Slice(0,rank-1))=m;
			mp(rank)=Ra;
			tensorT nq(rank+1);
			nq(Slice(0,rank-1))=n;
			nq(rank)=Rb;
			tensorT K=outer(mp,nq)*alpha;		// include weights only here
			for (long i=0; i<rank; i++) {
				K(i,i)+=this->weights(i);
			}															// 0.3 s

			// diagonalize K, sec. (4.1)
			tensorT Up,VTp;
			Tensor<double> Sp;
			svd(K,Up,Sp,VTp);											// 1.3 s
			tensorT Vp=transpose(VTp);									// 0.1 s

			// note there are only left subspaces

			// rank-increasing update
			if (Sp(rank)>1.e-10 and rank<11) {
				const tensorT P=p*(1.0/Ra);
				const tensorT Q=q*(1.0/Rb);
				update_left_subspace(Up,P,0);
				update_left_subspace(Vp,Q,1);

				rank_++;
				weights_=Sp(Slice(0,rank_-1));
				make_slices();

			// non-rank-increasing update
			} else {
//				if (rank>=10) print("truncating at rank 10");
				Up=Up(Slice(0,rank-1),Slice(0,rank-1));
				Vp=Vp(Slice(0,rank-1),Slice(0,rank-1));
				update_left_subspace(Up,tensorT(),0);
				update_left_subspace(Vp,tensorT(),1);
				weights_=Sp(Slice(0,rank-1));

			}															// 0.3 s
            MADNESS_ASSERT(has_structure());
		}


		/// update left subspace as in section 4.1, Brand2006

		/// note that in the language of the article there are only left subspaces
		/// note that we have U^T (r,k) instead of U (k,r)
		/// @param[in] C	the C matrix
		/// @param[in] p	the p vector (pass only in if rank is increasing)
		/// @param[in] idim	dimension to be updated
		void update_left_subspace(const tensorT& C, const tensorT& p, const int idim) {

			MADNESS_ASSERT(this->is_flat());
			MADNESS_ASSERT(idim==0 or idim==1);

			// rank increasing
			if (p.has_data()) {

				const long rank=this->rank();
				long pdim=p.dim(0);	// the number of additional configurations
				if (p.ndim()==1) pdim=1;
				this->reserve(rank+pdim);

				// U
				vector_[idim](Slice(rank,rank+pdim-1),Slice(_))=p;

				// U'
				tensorT scr(rank+pdim,rank+pdim);
				scr(Slice(0,rank-1),Slice(0,rank-1))=subspace_vec_[idim];
				for (unsigned int r=rank; r<rank+pdim; r++) {
					scr(r,r)=1.0;
				}
//				scr(rank,rank)=1.0;
				subspace_vec_[idim]=inner(scr,C);

			// not rank increasing
			} else {

				// U
				// nothing to be done

				// U'
				subspace_vec_[idim]=inner(subspace_vec_[idim],C);
			}
            MADNESS_ASSERT(has_structure());
		}

		/// initialize accumulation
		void init_accumulate() {
			// works only for SVD
			MADNESS_ASSERT(dim_eff()==2);
			subspace_vec_.resize(2);
			for (int idim=0; idim<2; idim++) {
				subspace_vec_[idim]=tensorT(this->rank(),this->rank());
				for (unsigned int r=0; r<this->rank(); r++) {
					subspace_vec_[idim](r,r)=1.0;
				}
			}
			undo_structure();
			updating_=true;
            MADNESS_ASSERT(has_structure());
		}

		/// finalize accumulation: incorporate V', U' into vector_
		void finalize_accumulate() {
			updating_=false;
			if (subspace_vec_.size()==0) return;

			MADNESS_ASSERT(subspace_vec_.size()==2);
			vector_[0]=inner(subspace_vec_[0],vector_[0](c0()),0,0);
			vector_[1]=inner(subspace_vec_[1],vector_[1](c0()),0,0);
			subspace_vec_.clear();
            MADNESS_ASSERT(has_structure());
		}

		/// reduce the rank using a divide-and-conquer approach
		void divide_and_conquer_reduce(const double& thresh) {

			if (has_no_data()) return;
			if (rank()==1) {
				normalize();
				return;
			}

			// divide the SRConf into two
			const long chunksize=8;
			if (rank()>chunksize) {
        		SRConf<T> chunk1=this->get_configs(0,rank()/2);
        		SRConf<T> chunk2=this->get_configs(rank()/2+1,rank()-1);
        		chunk1.divide_and_conquer_reduce(thresh*0.5);
        		chunk2.divide_and_conquer_reduce(thresh*0.5);

        		// collect the two SRConfs
        		*this=chunk1;
        		if (OrthoMethod::om==ortho3_) {
        			this->add_SVD(chunk2,thresh);
        		} else if (OrthoMethod::om==ortho6_) {
        			this->append(chunk2);
        			this->right_orthonormalize(thresh);
        		} else {
        			MADNESS_EXCEPTION("confused ortho method in SRConf::divide_and_conquer_reduce",0);
        		}


			} else {

				// and reduce the rank
				if (OrthoMethod::om==ortho3_) this->orthonormalize(thresh);
				else if (OrthoMethod::om==ortho6_) this->right_orthonormalize(thresh);
				else {
					MADNESS_EXCEPTION("confused ortho method in SRConf::divide_and_conquer_reduce",0);
				}
			}
            MADNESS_ASSERT(has_structure());
		}


		/// orthonormalize this, normalize y and shift weights to x
		void right_orthonormalize(const double& thresh) {

		    MADNESS_EXCEPTION("no right_orthonormalize",0);

			if (type()==TT_FULL) return;
			if (has_no_data()) return;
			if (rank()==1) {
				normalize_and_shift_weights_to_x();
				return;
			}

			MADNESS_ASSERT(is_flat());
			vector_[0]=vector_[0](c0());
			vector_[1]=vector_[1](c0());
			weights_=weights_(Slice(0,rank()-1));

			normalize();
//			if (OrthoMethod::om==ortho5_) {
//				ortho5(vector_[1],vector_[0],weights_,thresh);
//				rank_=weights_.size();
//				if (not rank_==0) {
//					normalize();
//					ortho5(vector_[0],vector_[1],weights_,thresh);
//				}
//			} else if (OrthoMethod::om==ortho6_) {
				ortho6(vector_[1],vector_[0],weights_,thresh);
				rank_=weights_.size();
				if (not rank()==0) {
					normalize();
					ortho6(vector_[0],vector_[1],weights_,thresh);
				}
//			} else {
//				MADNESS_EXCEPTION("confused orthogonalization method in SRConf::right_orthonormalize",0);
//			}
			rank_=weights_.size();
			make_slices();
            MADNESS_ASSERT(has_structure());
		}

		/// orthonormalize this
		void orthonormalize(const double& thresh) {

			if (type()==TT_FULL) return;
			if (has_no_data()) return;
			if (rank()==1) {
				normalize();
				return;
			}
			vector_[0]=vector_[0](c0());
			vector_[1]=vector_[1](c0());
			weights_=weights_(Slice(0,rank()-1));
            normalize();

            this->undo_structure();
			ortho3(vector_[0],vector_[1],weights_,thresh);
			rank_=weights_.size();
			MADNESS_ASSERT(rank_>=0);
			this->make_structure();
			make_slices();
            MADNESS_ASSERT(has_structure());

		}

		/// project and add rhs on this, subtract it from rhs

		/// !!! requires this and rhs to have orthonormalized right subspaces !!!
		/// x1 y1 += x2 y2
		void project_and_orthogonalize(SRConf<T>& rhs) {

			if (rhs.has_no_data()) return;

			// aliasing for clarity
			tensorT x1=vector_[0](c0());
			tensorT y1=vector_[1](c0());
			tensorT x2=rhs.vector_[0](rhs.c0());
			tensorT y2=rhs.vector_[1](rhs.c0());

			/// projector of right subspace of rhs on right subspace of lhs
			tensorT U=inner(y2,y1,1,1);

//			x1+=inner(U,x2,0,0);
//			y2-=inner(U,y1,1,0);
			inner_result(U,x2,0,0,x1);
			U.scale(-1.0);
			inner_result(U,y1,1,0,y2);
            MADNESS_ASSERT(has_structure());

		}

		/// invert lower triangular matrix in by backsubstitution

		/// @param[in]	in	matrix to be reverted (upper triangle is ignored)
		/// @param[out]	out	inverse of in
		static void invert_lower_triangular_matrix(const tensorT& in, tensorT& out) {

			MADNESS_ASSERT(in.ndim()==2);
			MADNESS_ASSERT(in.dim(0)==in.dim(1));
			const long rank=in.dim(0);
			out=tensorT(rank,rank);

			// work on line r
			// subtract line s from r
			// work thru columns i
			for (int r=0; r<rank; r++) {
				out(r,r)=1.0;
				double norm=1.0/in(r,r);
				for (int s=0; s<r; s++) {
					double fac=in(r,s)*norm;
					for (int i=0; i<r; i++) {
						out(r,i) -= out(s,i)*fac;
					}
				}
			}
			for (int r=0; r<rank; r++) {
				for (int i=0; i<=r; i++) {
					out(r,i)=out(r,i)/in(i,i);
				}
			}
		}

		/// append configurations of rhs to this

		/// simplified version of inplace_add for flattened configurations
		/// *this += fac*rhs
		void append(const SRConf<T>& rhs, const double fac=1.0) {

			// fast return if possible
			if (rhs.has_no_data()) return;
			if (this->has_no_data()) {
				*this=copy(rhs);
				this->scale(fac);
				return;
			}

			const long newRank=this->rank()+rhs.rank();
			const long lhsRank=this->rank();
			const long rhsRank=rhs.rank();
			reserve(newRank);

			// assign weights
			this->weights_(Slice(lhsRank,newRank-1))=rhs.weights_(Slice(0,rhsRank-1))*fac;
			std::vector<Slice> s(dim_per_vector()+1,_);
			s[0]=Slice(lhsRank,newRank-1);

			// assign vectors
			for (unsigned int idim=0; idim<this->dim_eff(); idim++) {
//              vector_[idim](Slice(lhsRank,newRank-1),_)=rhs.vector_[idim](rhs.c0());
				vector_[idim](s)=rhs.vector_[idim](rhs.c0());
			}

			rank_=newRank;
			make_slices();
            MADNESS_ASSERT(has_structure());

		}

		/// add rhs to this

		/// this and rhs2 must be orthonormalized
		void low_rank_add(const SRConf<T>& rhs2, const double& thresh) {

			if (rhs2.has_no_data()) return;
			SRConf<T> rhs=copy(rhs2);
			rhs.undo_structure();
			this->undo_structure();

			if (check_orthonormality) check_right_orthonormality();
			if (check_orthonormality) rhs2.check_right_orthonormality();

//			rhs.right_orthonormalize(thresh);
//			this->right_orthonormalize(thresh);

			this->project_and_orthogonalize(rhs);
			rhs.right_orthonormalize(thresh);
			this->append(rhs);
            MADNESS_ASSERT(has_structure());

		}

		/// add rhs to this
		void low_rank_add_sequential(const SRConf<T>& rhs2, const double& thresh, const double& fac) {

			if (rhs2.has_no_data()) return;

			if (this->has_no_data()) {
				*this=copy(rhs2);
				this->scale(fac);
				right_orthonormalize(thresh);
				return;
			}

			if (check_orthonormality) check_right_orthonormality();

			SRConf<T> rhs=copy(rhs2);
			rhs.undo_structure();
			rhs.normalize_and_shift_weights_to_x();
			// should always be 1.0
			Tensor<double> weight(1);
			weight(long(0))=rhs.weights(0);

			for (unsigned int i=0; i<rhs.rank(); i++) {

				SRConf<T> one_term(weight,
						(rhs.vector_[0](i,Slice(_)).reshape(1,rhs.kVec()))*fac,
						(rhs.vector_[1](i,Slice(_)).reshape(1,rhs.kVec())),
						rhs.dim(),rhs.get_k());

				one_term.make_slices();
				this->project_and_orthogonalize(one_term);
				one_term.normalize_and_shift_weights_to_x();

				if (one_term.normf()>thresh) {
					this->append(one_term);
				}
			}
            MADNESS_ASSERT(has_structure());
		}

		/// right-orthonormalize this using low_rank_add_sequential
		void sequential_orthogonalization(const double& thresh) {

			if (has_no_data()) return;

			normalize_and_shift_weights_to_x();
			if (rank()==1) return;

			Tensor<double> weight(long(1));
			weight(long(0))=1.0;

			SRConf<T> rhs=copy(*this);
			SRConf<T> first_term(copy(weight),
					copy(rhs.vector_[0](0,Slice(_)).reshape(1,rhs.kVec())),
					copy(rhs.vector_[1](0,Slice(_)).reshape(1,rhs.kVec())),
					rhs.dim(),rhs.get_k());


			*this=copy(first_term);

			// use first term as initial basis to project on, so start loop with second term
			for (unsigned int i=1; i<rhs.rank(); i++) {

				SRConf<T> one_term(copy(weight),
						copy(rhs.vector_[0](i,Slice(_)).reshape(1,rhs.kVec())),
						copy(rhs.vector_[1](i,Slice(_)).reshape(1,rhs.kVec())),
						rhs.dim(),rhs.get_k());

				this->project_and_orthogonalize(one_term);
				one_term.normalize_and_shift_weights_to_x();
				if (one_term.normf()>thresh) {
					this->append(one_term);
				}
			}
            MADNESS_ASSERT(has_structure());
		}

		/// right-orthonormalize this
		void rank_revealing_modified_gram_schmidt2(const double& thresh) {

		    MADNESS_EXCEPTION("no RR/MGS2",0);

			if (has_no_data()) return;
			if (type()==TT_FULL or type()==TT_NONE) return;
			MADNESS_ASSERT(type()==TT_2D);

			normalize();
			if (rank()==1) return;

			MADNESS_ASSERT(is_flat());
			vector_[0]=vector_[0](c0());
			vector_[1]=vector_[1](c0());
			weights_=weights_(Slice(0,rank()-1));

			normalize();
			ortho6(vector_[0],vector_[1],weights_,thresh);
			rank_=weights_.size();
			if (rank_>0) {
				normalize();
				ortho6(vector_[1],vector_[0],weights_,thresh);
				rank_=weights_.dim(0);
				normalize();
			}
            MADNESS_ASSERT(has_structure());

//			print(weights_);

		}

		/// add two orthonormal configurations, yielding an optimal SVD decomposition
		void add_SVD(const SRConf<T>& rhs, const double& thresh) {

			if (rhs.has_no_data()) return;
			if (has_no_data()) {
				*this=rhs;
				return;
			}

			if (check_orthonormality) check_right_orthonormality();
            if (check_orthonormality) rhs.check_right_orthonormality();

            this->undo_structure();
            ortho4(ref_vector(0),ref_vector(1),weights_,
					rhs.flat_vector(0),rhs.flat_vector(1),rhs.weights_,thresh);
			rank_=weights_.size();
			make_structure();
			make_slices();
            MADNESS_ASSERT(has_structure());
		}

		/// alpha * this(lhs_s) + beta * rhs(rhs_s)

		/// bounds checking should have been performed by caller
		/// s denotes where in lhs the new contribution from rhs will be inserted
		void inplace_add(const SRConf<T>& rhs2, std::vector<Slice> lhs_s,
				std::vector<Slice> rhs_s, const double alpha, const double beta) {

			// fast return if possible; no fast return for this.rank()==0
			// since we might work with slices!
			if (rhs2.has_no_data()) return;

			// fast return for full rank tensors
			if (type()==TT_FULL) {
				vector_[0](lhs_s)+=rhs2.vector_[0](rhs_s);
				return;
			}

			// unflatten this and rhs; shallow wrt vector_
			SRConf<T>& lhs=*this;
			const SRConf<T>& rhs=rhs2;
			if (lhs.has_no_data()) lhs.make_structure(true);
			MADNESS_ASSERT(lhs.has_structure() or (lhs.has_no_data()));
			MADNESS_ASSERT(rhs.has_structure());
			MADNESS_ASSERT(not updating_ or rhs2.updating_);

			// conflicts with lhs_s ??
			MADNESS_ASSERT(alpha==1.0);

			// for convenience
			const long lhsRank=lhs.rank();
			const long rhsRank=rhs.rank();
			const long newRank=lhs.rank()+rhs.rank();

			const long rhs_k=rhs.get_k();
			const long lhs_k=lhs.get_k();

			const long dim_pv=lhs.dim_per_vector();

			// adapt slices for use
			for (unsigned int idim=0; idim<lhs.dim(); idim++) {
				if (lhs_s[idim].end<0) lhs_s[idim].end+=lhs_k;
				if (rhs_s[idim].end<0) rhs_s[idim].end+=rhs_k;
				// make sure slices conform
				MADNESS_ASSERT((lhs_s[idim].end-lhs_s[idim].start) == (rhs_s[idim].end-rhs_s[idim].start));
				// make sure lhs can actually hold rhs(s)
				MADNESS_ASSERT(lhs_k>=(rhs_s[idim].end-rhs_s[idim].start+1));
			}

			lhs.reserve(newRank);

			// assign weights, and include factors alpha and beta
			if (alpha!=1.0) lhs.scale(alpha);
			lhs.weights_(Slice(lhsRank,newRank-1))=rhs.weights_(Slice(0,rhsRank-1))*beta;


			// assign vectors
			for (unsigned int idim=0; idim<lhs.dim_eff(); idim++) {

				// insert rhs at the right place
				if (dim_pv==1) {
					lhs.ref_vector(idim)(Slice(lhsRank,newRank-1),lhs_s[idim])=
							rhs.ref_vector(idim)(Slice(0,rhsRank-1),rhs_s[idim]);

				} else if (dim_pv==2) {
					lhs.ref_vector(idim)(Slice(lhsRank,newRank-1),lhs_s[2*idim],lhs_s[2*idim+1])=
							rhs.ref_vector(idim)(Slice(0,rhsRank-1),rhs_s[2*idim],rhs_s[2*idim+1]);

				} else if (dim_pv==3) {
					lhs.ref_vector(idim)(Slice(lhsRank,newRank-1),lhs_s[3*idim],lhs_s[3*idim+1],lhs_s[3*idim+2])=
							rhs.ref_vector(idim)(Slice(0,rhsRank-1),rhs_s[3*idim],rhs_s[3*idim+1],rhs_s[3*idim+2]);

				} else {
					MADNESS_EXCEPTION("extend dim_pv in srconf::inplace_add",0);
				}
			}

			lhs.rank_=newRank;
			lhs.make_slices();
            MADNESS_ASSERT(has_structure());
		}

		/// deep copy of rhs, shrink
		friend SRConf<T> copy(const SRConf<T>& rhs) {

			// if rhs is non-existent simply construct a new SRConf
			if (rhs.has_no_data()) return SRConf<T>(rhs.dim(),rhs.get_k(),rhs.type());

			MADNESS_ASSERT(not rhs.updating_);

			if (rhs.type()==TT_FULL) return SRConf<T>(copy(rhs.ref_vector(0)));

			// pass a copy of the weights and vectors of rhs to ctor
			std::vector<tensorT> vector(rhs.dim_eff());
			for (unsigned int idim=0; idim<rhs.dim_eff(); idim++)
				vector[idim]=copy(rhs.ref_vector(idim)(rhs.c0()));

			return SRConf<T>(copy(rhs.weights_(Slice(0,rhs.rank()-1))),vector,rhs.dim(),rhs.get_k(),rhs.type());
		}

		/// reassign weight and data for a specific SRConf only
		void reassign(const unsigned int& idim, const unsigned int& r,
				const double& weight, const Tensor<T> & data, const unsigned int& maxk) {

			// some checks
	        MADNESS_ASSERT(idim<this->dim_eff());
	        MADNESS_ASSERT(r<this->rank());
	        MADNESS_ASSERT(this->is_flat());

	        // assign weight
	        weights_(r)=weight;

	        // assign data
	        MADNESS_ASSERT(data.size()==maxk);
	        for (unsigned int k=0; k<maxk; k++) {
	        	ref_vector(idim)(r,k)=data(k);
	        }

	        // make sure the ranks comply for all dimensions
	        for (unsigned int idim=0; idim<dim_eff(); idim++) {
                MADNESS_ASSERT(weights_.dim(0)==vector_[idim].dim(0));
	        }
            MADNESS_ASSERT(has_structure());
		}

		/// redo the Slices for getting direct access to the configurations
		void make_slices() {
			if (type()==TT_FULL) return;
			if (this->has_no_data()) {
				s_.clear();
			} else {
				// first dim is the rank
				if (vector_[0].ndim()>TENSOR_MAXDIM) {
					print(*this);
					MADNESS_EXCEPTION("serializing failed",0);
				}
				s_.resize(vector_[0].ndim());
				s_[0]=Slice(0,this->rank()-1);
				for (int i=1; i<vector_[0].ndim(); i++) {
					s_[i] = Slice(_);
				}
			}
		}


		void make_structure(bool force=false) {

			// fast return if rank is zero
			if ((not force) and this->has_no_data()) return;
			if (type()==TT_FULL) return;

			const int dim_pv=this->dim_per_vector();
			MADNESS_ASSERT(dim_pv>0 and dim_pv<=3);
			int rr=weights_.dim(0);	// not the rank!
			if (weights_.size()==0) rr=0;
			const int k=this->get_k();

			// reshape the vectors and adapt the Slices
			for (unsigned int idim=0; idim<this->dim_eff(); idim++) {
				if (dim_pv==2) this->vector_[idim]=vector_[idim].reshape(rr,k,k);
				if (dim_pv==3) this->vector_[idim]=vector_[idim].reshape(rr,k,k,k);
			}

			this->make_slices();

		}

		void undo_structure(bool force=false) {

			// fast return if rank is zero
			if ((not force) and this->has_no_data()) return;
			if (type()==TT_FULL) return;

			const int dim_pv=this->dim_per_vector();
			MADNESS_ASSERT(dim_pv>0 and dim_pv<=3);
			int rr=weights_.dim(0);	// not the rank!
			if (weights_.size()==0) rr=0;
			const int kvec=this->kVec();

			for (unsigned int idim=0; idim<this->dim_eff(); idim++) {
				this->vector_[idim]=this->vector_[idim].reshape(rr,kvec);
			}

			this->make_slices();
		}

		/// return reference to one of the vectors F
		Tensor<T>& ref_vector(const unsigned int& idim) {
			return vector_[idim];
		}

		/// return reference to one of the vectors F
		const Tensor<T>& ref_vector(const unsigned int& idim) const {
			return vector_[idim];
		}

		/// return shallow copy of a slice of one of the vectors, flattened to (r,kVec)
		const Tensor<T> flat_vector(const unsigned int& idim) const {
		    MADNESS_ASSERT(rank()>0);
		    return vector_[idim](c0()).reshape(rank(),kVec());
		}


		/// fill this SRConf with 1 flattened random configurations (tested)
		void fillWithRandom(const unsigned int& rank=1) {

			rank_=rank;

			// assign; note that Slice(0,_) is inclusive
			weights_=Tensor<double>(rank);
			weights_=1.0;

			for (unsigned int idim=0; idim<this->dim_eff(); idim++) {
				vector_[idim]=Tensor<T>(rank_,this->kVec());
				vector_[idim].fillrandom();
			}

			this->normalize();
			for (unsigned int idim=0; idim<this->dim_eff(); idim++) {
				vector_[idim].scale(madness::RandomValue<T>()*10.0);
			}
			weights_(Slice(0,this->rank()-1)).fillrandom().scale(10.0);
			make_slices();
            MADNESS_ASSERT(has_structure());
		}

		/// normalize the vectors (tested)
		void normalize() {

			if (type()==TT_FULL) return;
			if (rank()==0) return;
            MADNESS_ASSERT(has_structure());

	        // for convenience
	        const unsigned int rank=this->rank();
	        std::vector<Slice> s(dim_per_vector()+1,_);

	        // we calculate the norm sum_i < F^r_i | F^r_i > for each dimension for each r

	        // loop over all configurations
	        for (unsigned int r=0; r<rank; r++) {
	            s[0]=Slice(r,r);
	        	// loop over all dimensions
	        	for (unsigned int idim=0; idim<dim_eff(); idim++) {

	        		Tensor<T> config=this->ref_vector(idim)(s);

	        		const double norm=config.normf();
	        		const double fac=norm;
	        		double oofac=1.0/fac;
	        		if (fac<1.e-13) oofac=0.0;

	        		weights_(r)*=fac;
	        		config.scale(oofac);
	        	}
	        }
            MADNESS_ASSERT(has_structure());
		}

		/// does what you think it does
		void normalize_and_shift_weights_to_x() {
			MADNESS_ASSERT(has_no_data() or dim_eff()==2);
			for (unsigned int i=0; i<rank(); i++) {
				double norm=vector_[1](i,Slice(_)).normf();
				double fac=1.0/norm;
				if (norm<1.e-14) fac=0.0;
				vector_[0](i,Slice(_)).scale(norm*weights(i));
				vector_[1](i,Slice(_)).scale(fac);
				weights_[i]=1.0;
			}
			MADNESS_ASSERT(has_structure());
		}

		/// check if the terms are orthogonal
		bool check_right_orthonormality() const {

			// fast return if possible
			if (rank()==0) return true;

			MADNESS_ASSERT(type()==TT_2D);

			const tensorT t1=ref_vector(1)(c0()).reshape(rank(),kVec());
			tensorT S=inner(t1,t1,1,1);
			for (int i=0; i<S.dim(0); i++) S(i,i)-=1.0;

			// error per matrix element
			double norm=S.normf();
			double small=sqrt(norm*norm/S.size());
			return (small<1.e-13);
		}

		/// return if this has only one additional dimension (apart from rank)
		bool is_flat() const {
			return (vector_[0].ndim()==2);
		}

		/// return if this has a tensor structure (has not been flattened)
		bool has_structure() const {
            return (type()==TT_FULL or has_no_data() or vector_[0].dim(1)==this->get_k());
		}

		/// return the dimension of this
		unsigned int dim() const {return dim_;}

		/// return the number of vectors
		unsigned int dim_eff() const {return vector_.size();}

		/// return the logicalrank
		long rank() const {return rank_;};

		/// return the number of physical matrix elements per dimension
		unsigned int get_k() const {return maxk_;};

		/// return the length of the vector (dim_pv*maxk)
		unsigned int kVec() const {return pow(this->get_k(),this->dim_per_vector());};

		/// return the tensor type
		TensorType type() const {return tensortype_;};

		/// return the number of physical dimensions
		int dim_per_vector() const {
			const int nvec=vector_.size();
			const int dim=this->dim();
			MADNESS_ASSERT(dim%nvec==0);
			return dim/nvec;
		}

		/// return the number of coefficients
		unsigned int nCoeff() const {
			if (type()==TT_FULL) return ref_vector(0).size();
			return this->dim_eff()*this->kVec()*this->rank();
		};

		/// calculate the Frobenius inner product (tested)
		template<typename Q>
		friend TENSOR_RESULT_TYPE(T,Q) overlap(const SRConf<T>& rhs, const SRConf<Q>& lhs) {

			// fast return if either rank is 0
			if ((lhs.has_no_data()) or (rhs.has_no_data())) return 0.0;

			/*
			 * the structure of an SRConf is (r,k) or (r,k',k), with
			 * r the slowest index; the overlap is therefore simply calculated
			 * as the matrix multiplication rhs*lhs^T
			 */

			// some checks
			MADNESS_ASSERT(rhs.dim()==lhs.dim());
			MADNESS_ASSERT(rhs.dim()>0);

			typedef TENSOR_RESULT_TYPE(T,Q) resultT;

			if (rhs.type()==TT_FULL) {
				return rhs.ref_vector(0).trace(lhs.ref_vector(0));
			}

			const unsigned int dim_eff=rhs.dim_eff();

			// get the weight matrix
			Tensor<resultT> weightMatrix=outer(lhs.weights_(Slice(0,lhs.rank()-1)),
					rhs.weights_(Slice(0,rhs.rank()-1)));

			// calculate the overlap matrices for each dimension at a time
			for (unsigned int idim=0; idim<dim_eff; idim++) {
				const Tensor<T> lhs2=lhs.flat_vector(idim);
				const Tensor<Q> rhs2=rhs.flat_vector(idim);
				Tensor<resultT> ovlp(lhs.rank(),rhs.rank());
				inner_result(lhs2,rhs2,-1,-1,ovlp);

			    // multiply all overlap matrices with the weight matrix
				weightMatrix.emul(ovlp);
			}

			//	return weightMatrix;
			const TENSOR_RESULT_TYPE(T,Q) overlap=weightMatrix.sum();
			return overlap;
		}

		/// calculate the Frobenius norm, if this is in SVD form
        typename TensorTypeData<T>::float_scalar_type svd_normf() const {
            if (has_no_data()) return 0.0;
            MADNESS_ASSERT(type()==TT_2D);
            return weights_(Slice(0,rank()-1)).normf();
        }


		/// calculate the Frobenius norm
		typename TensorTypeData<T>::float_scalar_type normf() const {

			// fast return if possible
			if (has_no_data()) return 0.0;
			if (type()==TT_FULL) return ref_vector(0).normf();

			// some checks
			MADNESS_ASSERT(dim()>0);
			MADNESS_ASSERT(not TensorTypeData<T>::iscomplex);

			// get the weight matrix
			Tensor<T> weightMatrix=outer(weights_(Slice(0,rank()-1)),weights_(Slice(0,rank()-1)));

			// calculate the overlap matrices for each dimension at a time
			for (unsigned int idim=0; idim<dim_eff(); idim++) {
				const Tensor<T> vec=flat_vector(idim);
				Tensor<T> ovlp(rank(),rank());
				inner_result(vec,vec,-1,-1,ovlp);

			    // multiply all overlap matrices with the weight matrix
				weightMatrix.emul(ovlp);
			}

			typedef typename TensorTypeData<T>::float_scalar_type resultT;
			const resultT overlap=std::abs(weightMatrix.sum());
			return sqrt(overlap);
		}

		/// scale this by a number
		void scale(const double& fac) {weights_.scale(fac);};

		/// return the weight
		double weights(const unsigned int& i) const {return weights_(i);};

		/// return the maximum weight
		double maxWeight() const {return weights_(Slice(0,this->rank()-1)).max();};

		/// check compatibility
		friend bool compatible(const SRConf& lhs, const SRConf& rhs) {
			return ((lhs.dim()==rhs.dim()) and (lhs.dim_per_vector()==rhs.dim_per_vector()));
		}

		/// make one of the terms in the B matrix (BM2005)
		void friend makeB(Tensor<T> & B, const unsigned int& idim, const SRConf<T>& lhs, const SRConf<T>& rhs) {
			// some checks
			MADNESS_ASSERT(compatible(rhs,lhs));
			MADNESS_ASSERT(lhs.rank()==B.dim(0));
			MADNESS_ASSERT(rhs.rank()==B.dim(1));
			MADNESS_ASSERT(idim<rhs.dim_eff());

//			dgemm_NT(lhs.refVector(idim),rhs.refVector(idim),B,lhs.rank(),rhs.rank());
			Tensor<T> lhs2=lhs.ref_vector(idim)(lhs.c0());
			Tensor<T> rhs2=rhs.ref_vector(idim)(rhs.c0());
			B=0.0;
			inner_result(lhs2,rhs2,-1,-1,B);
		}

	    /// \code
		///     result(i,j,k,...) <-- sum(i',j', k',...) t(i',j',k',...)  c(i',i) c(j',j) c(k',k) ...
		/// \endcode
		///
		/// The input dimensions of \c t must all be the same .
		SRConf<T> transform(const Tensor<T>& c) const {

			// fast return if possible
			if (this->has_no_data()) {
				return copy(*this);
			}

			// fast return for full rank tensor
			if (type()==TT_FULL) {
				return SRConf<T> (madness::transform(this->vector_[0],c));
			}

			// copying shrinks the vectors to (r,k,k,..)
			SRConf<T> result=copy(*this);

			// make sure this is not flattened
			MADNESS_ASSERT(this->has_structure());

			// these two loops go over all physical dimensions (dim = dim_eff * merged_dim)
			for (unsigned int idim=0; idim<this->dim_eff(); idim++) {
				for (unsigned int jdim=1; jdim<this->ref_vector(idim).ndim(); jdim++) {

					// note: tricky ordering (jdim is missing): this is actually correct!
					// note: no slicing necessary, since we've copied this to result (incl shrinking)
//					result.refVector_struct(idim)=madness::inner(result.refVector_struct(idim),c,1,0);
					result.ref_vector(idim)=madness::inner(result.ref_vector(idim),c,1,0);

				}
			}
            MADNESS_ASSERT(result.has_structure());
			return result;
		}

	    /// \code
		///     result(i,j,k,...) <-- sum(i',j', k',...) t(i',j',k',...)  c(i',i) c(j',j) c(k',k) ...
		/// \endcode
		///
		/// The input dimensions of \c t must all be the same .
		template<typename Q>
		SRConf<TENSOR_RESULT_TYPE(T,Q) > general_transform(const Tensor<Q> c[]) const {

			// fast return if possible
			if (this->has_no_data()) return SRConf<T>(copy(*this));
			if (type()==TT_FULL) {
				return SRConf<T> (madness::general_transform(this->vector_[0],c));
			}

			// copying shrinks the vectors to (r,k,k,..)
			SRConf<T> result=copy(*this);

			// make sure this is not flattened
			if (not this->has_structure()) {
				print("no structure!");
			}
			MADNESS_ASSERT(this->has_structure());

			long i=0;
			// these two loops go over all physical dimensions (dim = dim_eff * merged_dim)
			for (unsigned int idim=0; idim<this->dim_eff(); idim++) {
				for (unsigned int jdim=1; jdim<this->ref_vector(idim).ndim(); jdim++) {

					// note tricky ordering (jdim is missing): this is actually correct!
					// note: no slicing necessary, since we've copied this to result (incl shrinking)
					result.ref_vector(idim)=madness::inner(result.ref_vector(idim),c[i],1,0);
					i++;

				}
			}
            MADNESS_ASSERT(result.has_structure());
			return result;
		}


		SRConf<T> transform_dir(const Tensor<T>& c, const int& axis) const {

			if (this->has_no_data()) {
				return SRConf<T>(copy(*this));
			}

			// fast return for full rank tensor
			if (type()==TT_FULL) {
				return SRConf<T> (madness::transform_dir(this->vector_[0],c,axis));
			}

			// copying shrinks the vectors to (r,k,k,..)
			SRConf<T> result=copy(*this);

			// only a matrix is allowed for c
			MADNESS_ASSERT(c.ndim()==2);

			// make sure this is not flattened
			MADNESS_ASSERT(this->has_structure());

			// compute idim for accessing the vector_, and the dimension inside vector_
			// the +1 on jdim for the rank
			const long idim=axis/this->dim_per_vector();
			const long jdim=axis%this->dim_per_vector()+1;

			// note: no slicing necessary, since we've copied this to result (incl shrinking)
			result.ref_vector(idim)=madness::transform_dir(this->ref_vector(idim),c,jdim);
            MADNESS_ASSERT(result.has_structure());

			return result;
		}

	};

	/// sophisticated version of ortho2

	/// after calling this we will have an optimally rank-reduced representation
	/// with the left and right subspaces being bi-orthogonal and normalized;
	/// outline of the algorithm:
	///  - canonical orthogonalization of the subspaces (screen for small eigenvalues)
	///  - SVD of the modified overlap (incorporates the roots of eigenvalues)
	/// operation count is O(kr^2 + r^3)
	///
	/// @param[in/out]	x normalized left subspace
	/// @param[in/out]	y normalize right subspace
	/// @param[in/out]	weights weights
	/// @param[in]		thresh	truncation threshold
	template<typename T>
	void ortho3(Tensor<T>& x, Tensor<T>& y, Tensor<double>& weights, const double& thresh) {

		typedef Tensor<T> tensorT;

		const long rank=x.dim(0);
		const double w_max=weights.absmax()*rank;		// max Frobenius norm


		// overlap of 1 and 2
		tensorT S1=inner(x,x,1,1);
		tensorT S2=inner(y,y,1,1);	// 0.5 / 2.1

//	    print("norm(S1)",S1.normf());
//	    print("norm(S2)",S2.normf());

		// diagonalize
		tensorT U1, U2;
		Tensor<double> e1, e2;
	    syev(S1,U1,e1);
	    syev(S2,U2,e2);										// 2.3 / 4.0


	    const double e1_max=e1.absmax();
	    const double e2_max=e2.absmax();

		// fast return if possible
		if ((e1_max*w_max<thresh) or (e2_max*w_max<thresh)) {
			x.clear();
			y.clear();
			weights.clear();
			return;
		}

	    // remove small negative eigenvalues
	    e1.screen(1.e-13);
	    e2.screen(1.e-13);
	    Tensor<double> sqrt_e1(rank), sqrt_e2(rank);


	    // shrink U1, U2
	    int lo1=0;
	    int lo2=0;
	    for (unsigned int r=0; r<rank; r++) {
	    	if (e1(r)*w_max<thresh) lo1=r+1;
	    	if (e2(r)*w_max<thresh) lo2=r+1;
	    	sqrt_e1(r)=sqrt(std::abs(e1(r)));
	    	sqrt_e2(r)=sqrt(std::abs(e2(r)));
	    }

	    U1=U1(Slice(_),Slice(lo1,-1));
	    U2=U2(Slice(_),Slice(lo2,-1));
	    sqrt_e1=sqrt_e1(Slice(lo1,-1));
	    sqrt_e2=sqrt_e2(Slice(lo2,-1));
	    unsigned int rank1=rank-lo1;
	    unsigned int rank2=rank-lo2;						// 0.0 / 0.0


	    MADNESS_ASSERT(sqrt_e1.size()==rank1);
	    MADNESS_ASSERT(sqrt_e2.size()==rank2);


	    // set up overlap M; include X+
	    tensorT M(rank1,rank2);
	    for (unsigned int i=0; i<rank1; i++) {
	    	for (unsigned int j=0; j<rank2; j++) {
	    		for (unsigned int r=0; r<rank; r++) {
		    		M(i,j)+=U1(r,i)*sqrt_e1(i)*weights(r)*U2(r,j) * sqrt_e2(j);
//			    		M(i,j)+=U1(r,i)*weights(r)*U2(r,j);
	    		}
	    	}
	    }


	    // include X-
    	for (unsigned int r=0; r<rank1; r++) {
    		double fac=1.0/sqrt_e1(r);
    		for (unsigned int t=0; t<rank; t++) {
	    		U1(t,r)*=fac;
//	    		if (sqrt_e1(r)<thresh) throw;
    		}
    	}

	   	for (unsigned int r=0; r<rank2; r++) {
    		double fac=1.0/sqrt_e2(r);
    		for (unsigned int t=0; t<rank; t++) {
	    		U2(t,r)*=fac;
//	    		if (sqrt_e2(r)<thresh) throw;
	    	}
	    }													// 0.2 / 0.6

	    // decompose M
		tensorT Up,VTp;
		Tensor<double> Sp;
		svd(M,Up,Sp,VTp);									// 1.5 / 3.0

		// make transformation matrices
		Up=inner(Up,U1,0,1);
		VTp=inner(VTp,U2,1,1);

		// find the maximal singular value that's supposed to contribute
		// singular values are ordered (largest first)
		double residual=0.0;
		long i;
		for (i=Sp.dim(0)-1; i>=0; i--) {
			residual+=Sp(i)*Sp(i);
			if (residual>thresh*thresh) break;
		}

//		i=std::min(i,long(0));

	    Up=Up(Slice(0,i),Slice(_));
	    VTp=VTp(Slice(0,i),Slice(_));


		// convert SVD output to our convention
		if (i>=0) {

			// transform 1 and 2
		    x=inner(Up,x,1,0);
		    y=inner(VTp,y,1,0);				// 0.5 / 2.5
		    weights=Sp(Slice(0,i));

		} else {
			x.clear();
			y.clear();
			weights.clear();
		}

		return;
	}



	/// specialized version of ortho3

	/// does the same as ortho3, but takes two bi-orthonormal configs as input
	/// and saves on the inner product. Result will be written onto the first config
	///
	/// @param[in/out]	x1	left subspace, will hold the result on exit
	/// @param[in/out]	y1	right subspace, will hold the result on exit
	/// @param[in]		x2	left subspace, will be accumulated onto x1
	/// @param[in]		y2	right subspace, will be accumulated onto y1
	template<typename T>
	void ortho4(Tensor<T>& x1, Tensor<T>& y1, Tensor<double>& w1,
				const Tensor<T>& x2, const Tensor<T>& y2, const Tensor<double>& w2,
				const double& thresh) {


		typedef Tensor<T> tensorT;

		const long rank1=x1.dim(0);
		const long rank2=x2.dim(0);
		const long rank=rank1+rank2;

		// for convenience: blocks of the matrices
		const Slice s0(0,rank1-1), s1(rank1,rank-1);


		const double w_max=std::max(w1.absmax(),w2.absmax());
		const double norm_max=w_max*rank;		// max Frobenius norm

		// the overlap between 1 and 2;
		// the overlap of 1 and 1, and 2 and 2 is assumed to be the identity matrix
		tensorT Sx12=inner(x1,x2,1,1);
		tensorT Sy12=inner(y1,y2,1,1);

		tensorT Sx(rank,rank);
		tensorT Sy(rank,rank);

		// the identity matrix (half of it)
		for (long i=0; i<rank; i++) {
			Sx(i,i)=0.5;
			Sy(i,i)=0.5;
		}
		Sx(s0,s1)=Sx12;
		Sy(s0,s1)=Sy12;
		Sx+=transpose(Sx);
		Sy+=transpose(Sy);

		// overlap of 1 and 2
//		tensorT S1=inner(x,x,1,1);
//		tensorT S2=inner(y,y,1,1);	// 0.5 / 2.1

		// diagonalize
		tensorT U1, U2;
		Tensor<double> e1, e2;
	    syev(Sx,U1,e1);
	    syev(Sy,U2,e2);										// 2.3 / 4.0

//	    print("norm(Sx)",Sx.normf());
//	    print("norm(Sy)",Sy.normf());

	    const double e1_max=e1.absmax();
	    const double e2_max=e2.absmax();

		// fast return if possible
		if ((e1_max*norm_max<thresh) or (e2_max*norm_max<thresh)) {
			x1.clear();
			y1.clear();
			w1.clear();
			return;
		}

	    // remove small negative eigenvalues
	    e1.screen(1.e-13);
	    e2.screen(1.e-13);
	    Tensor<double> sqrt_e1(rank), sqrt_e2(rank);


	    // shrink U1, U2
	    int lo1=0;
	    int lo2=0;
	    for (unsigned int r=0; r<rank; r++) {
	    	if (e1(r)*norm_max<thresh) lo1=r+1;
	    	if (e2(r)*norm_max<thresh) lo2=r+1;
	    	sqrt_e1(r)=sqrt(std::abs(e1(r)));
	    	sqrt_e2(r)=sqrt(std::abs(e2(r)));
	    }

	    U1=U1(Slice(_),Slice(lo1,-1));
	    U2=U2(Slice(_),Slice(lo2,-1));
	    sqrt_e1=sqrt_e1(Slice(lo1,-1));
	    sqrt_e2=sqrt_e2(Slice(lo2,-1));
	    unsigned int rank_x=rank-lo1;
	    unsigned int rank_y=rank-lo2;						// 0.0 / 0.0


	    MADNESS_ASSERT(sqrt_e1.size()==rank_x);
	    MADNESS_ASSERT(sqrt_e2.size()==rank_y);


	    // set up overlap M; include X+
	    tensorT M(rank_x,rank_y);
	    for (unsigned int i=0; i<rank_x; i++) {
	    	for (unsigned int j=0; j<rank_y; j++) {
	    		for (unsigned int r=0; r<rank; r++) {
		    		if (r<rank1) M(i,j)+=U1(r,i)*sqrt_e1(i)*w1(r)*U2(r,j) * sqrt_e2(j);
		    		if (r>=rank1) M(i,j)+=U1(r,i)*sqrt_e1(i)*w2(r-rank1)*U2(r,j) * sqrt_e2(j);
	    		}
	    	}
	    }


	    // include X-
    	for (unsigned int r=0; r<rank_x; r++) {
    		double fac=1.0/sqrt_e1(r);
    		for (unsigned int t=0; t<rank; t++) {
	    		U1(t,r)*=fac;
//	    		if (sqrt_e1(r)<thresh) throw;
    		}
    	}

	   	for (unsigned int r=0; r<rank_y; r++) {
    		double fac=1.0/sqrt_e2(r);
    		for (unsigned int t=0; t<rank; t++) {
	    		U2(t,r)*=fac;
//	    		if (sqrt_e2(r)<thresh) throw;
	    	}
	    }													// 0.2 / 0.6


	    // decompose M
		tensorT Up,VTp;
		Tensor<double> Sp;
		svd(M,Up,Sp,VTp);									// 1.5 / 3.0

		// make transformation matrices
		Up=inner(Up,U1,0,1);
		VTp=inner(VTp,U2,1,1);

		// find the maximal singular value that's supposed to contribute
		// singular values are ordered (largest first)
		double residual=0.0;
		long i;
		for (i=Sp.dim(0)-1; i>=0; i--) {
			residual+=Sp(i)*Sp(i);
			if (residual>thresh*thresh) break;
		}

		// convert SVD output to our convention
		if (i>=0) {

			// make it contiguous
		    tensorT Up1=copy(Up(Slice(0,i),s0));
		    tensorT Up2=copy(Up(Slice(0,i),s1));
		    tensorT VTp1=copy(VTp(Slice(0,i),s0));
		    tensorT VTp2=copy(VTp(Slice(0,i),s1));

			// transform 1 and 2
		    x1=inner(Up1,x1,1,0);
		    inner_result(Up2,x2,1,0,x1);
		    y1=inner(VTp1,y1,1,0);
		    inner_result(VTp2,y2,1,0,y1);
		    w1=Sp(Slice(0,i));

		} else {
			x1.clear();
			y1.clear();
			w1.clear();
		}

		return;
	}


	/// orthonormalize and truncate right subspace (y) using symmetric orthogonalization
	template<typename T>
	void ortho5(Tensor<T>& x, Tensor<T>& y, Tensor<double>& weights, const double& thresh) {

		MADNESS_EXCEPTION("no SRConf::ortho5 for the time being",0);
		typedef Tensor<T> tensorT;

		const long rank=x.dim(0);
		const double norm_max=((abs(weights)).sum());
		print("norm_max",norm_max);

		// fast return if possible
		if (norm_max<thresh) {
			x.clear();
			y.clear();
			weights.clear();
			return;
		}

//		// normalize x and turn its norm and the weights over to y
//		for (unsigned int i=0; i<rank; i++) {
//			double norm=x(i,Slice(_)).normf();
//			double fac=1.0/norm;
//			if (norm<1.e-14) fac=0.0;
//			x(i,Slice(_)).scale(fac);
//			y(i,Slice(_)).scale(norm*weights(i));
//			weights(i)=1.0;
//		}

		// overlap of 1 and 2
		tensorT S=inner(y,y,1,1);

		// diagonalize
		tensorT U;
		Tensor<double> e;
		syev(S,U,e);


		// remove small negative eigenvalues
		e.screen(1.e-14);
		Tensor<double> sqrt_e(rank);
		for (int i=0; i<rank; i++) sqrt_e(i)=sqrt(std::abs(e(i)));

		print("e",e);

		// shrink U, U
		double residual=0.0;
		int lo=0;
		for (int i=0; i<rank; i++) {
//			residual+=std::abs(e(i));
			residual+=e(i)*e(i);
//			print("r,t,norm_max,lo",residual,thresh,norm_max,lo);
//			if (residual*w_max>thresh*thresh) break;
			if (residual*norm_max*norm_max>thresh*thresh) break;
			lo=i+1;
		}


		if (lo==rank) {
			x.clear();
			y.clear();
			weights.clear();
			return;
		}

		U=(U(Slice(_),Slice(lo,-1)));
		sqrt_e=sqrt_e(Slice(lo,-1));
		e=e(Slice(lo,-1));
//		weights=weights(Slice(lo,-1));


		unsigned int rank1=rank-lo;

		// make Y+, Y-
		tensorT Ym(rank,rank1);
		tensorT Yp(rank,rank1);
		for (unsigned int i=0; i<rank; i++) {
			for (unsigned int j=0; j<rank1; j++) {
//				Ym(i,j)=U(i,j)/sqrt_e(j);
//				Yp(i,j)=U(i,j)*sqrt_e(j);

				Ym(i,j)=U(i,j);
				Yp(i,j)=U(i,j)*weights(i);

			}
		}

		// transform 1 and 2
		x=inner(Yp,x,0,0);
		y=inner(Ym,y,0,0);				// 0.5 / 2.5
		weights=Tensor<double>(rank1);
		weights=1.0;

		return;

	}


	/// orthonormalize and truncate right subspace (y) using symmetric orthogonalization
	template<typename T>
	void ortho6(Tensor<T>& x, Tensor<T>& y, Tensor<double>& weights, const double& thresh) {

		typedef Tensor<T> tensorT;
		const long rank=x.dim(0);

		const double w_max=weights.absmax();
		if (rank*rank*w_max<thresh) {
			x.clear();
			y.clear();
			weights.clear();
			return;
		}
		// assuming all vectors x are aligned gives the factor R*R
		// hoping the weights fall off quickly
		// || A || = sum_rr'  s_r s_r' <x | x> <y | y>
		//         = sum_rr'  s_r s_r' (1 1)
		//							   (1 1)
		//         = xw_norm  sum_rr' <y | y>
		const double xw_norm2=rank*rank*(w_max*w_max);

		tensorT S=inner(y,y,1,1);

		// diagonalize
		tensorT U,V;
		Tensor<double> e;
		syev(S,U,e);

		// fast return if possible
		const double e_sum=e.sum();
		if (e_sum*xw_norm2<thresh) {
			x.clear();
			y.clear();
			weights.clear();
			return;
		}

		// shrink U, U
		double residual=0.0;
		int lo=0;
		for (int i=0; i<rank; i++) {
//			residual+=std::max(e(i),e.max()*1.e-14);
			residual+=e(i)*e(i);
			if (residual*xw_norm2>thresh*thresh) break;
			lo=i+1;
		}

		if (lo==rank) {
			x.clear();
			y.clear();
			weights.clear();
			return;
		}

		U=(U(Slice(_),Slice(lo,-1)));
		unsigned int rank1=rank-lo;


		// include weights to x
		V=copy(U);
		for (int i=0; i<rank; i++) {
			V(i,Slice(_))*=weights(i);
		}

		// transform 1 and 2
		x=inner(V,x,0,0);
		y=inner(U,y,0,0);				// 0.5 / 2.5
		weights=Tensor<double>(rank1);
		weights=1.0;
		return;

	}

	/// add two bi-orthonormal configs

	/// follow sec. 8 of my notes: Addition of SVD decompositions
	/// DOESN'T WORK..
	template<typename T>
	void ortho7(Tensor<T>& x1, Tensor<T>& y1, Tensor<double>& w1,
			const Tensor<T>& x2, const Tensor<T>& y2, const Tensor<double>& w2, const double& thresh) {

		MADNESS_EXCEPTION("ortho7 does not work",0);
		typedef Tensor<T> tensorT;
		const long rank1=x1.dim(0);
		const long rank2=x2.dim(0);
		const long rank=rank1+rank2;

		// overlap of the two configurations
		tensorT U=inner(y1,y2,1,1);


		// for convenience: blocks of the matrices
		const Slice s0(0,rank1-1), s1(rank1,rank-1);

		// the matrices X-, X+, Y-, Y+
		tensorT Xm(rank,rank), Xp(rank,rank), Ym(rank,rank), Yp(rank,rank), E(rank,rank);
		for (long i=0; i<rank; i++) {
			Xm(i,i)=1.0;
			Xp(i,i)=1.0;
			Ym(i,i)=1.0;
			Yp(i,i)=1.0;
			E(i,i)=1.0;
		}

		Xm(s0,s1)=-1.0*U;
		Xp(s0,s1)=U;
		Ym(s1,s0)=-1.0*transpose(U);
		Yp(s1,s0)=transpose(U);
		print("identity");
		print((inner(Yp,Ym)-E).normf());
		print((inner(Ym,Yp)-E).normf());
		print((inner(Ym,Ym,0,0)-E).normf());
		print((inner(Ym,Ym,1,0)-E).normf());
		print((inner(Ym,Ym,0,1)-E).normf());
		print((inner(Ym,Ym,1,1)-E).normf());

		print("identity 2");

		tensorT tmp1=inner(Ym(s0,s0),y1) + inner(Ym(s0,s1),y2);
		tensorT tmp2=inner(Xm(s0,s0),x1) + inner(Xm(s0,s1),x2);
		print((inner(y1,tmp1,1,1)-E(s0,s0)).normf());				// 0
		print((inner(x1,tmp2,1,1)-E(s0,s0)).normf());				// not 0

		print("U UT");
		print(inner(U,U,1,1));
		print("UT U");
		print(inner(U,U,0,0));
		return;
		print((inner(tmp1,tmp1,1,1)-E).normf());
		print((inner(tmp2,tmp2,1,1)-E).normf());

		print((inner(x1,x1,1,1)-E(s0,s0)).normf());
		print((inner(y1,y1,1,1)-E(s0,s0)).normf());
		print((inner(x2,x2,1,1)-E(s1,s1)).normf());
		print((inner(y2,y2,1,1)-E(s1,s1)).normf());

		return;

		// the M matrix
		tensorT M(rank,rank);
		for (int i=0; i<rank1; i++) M(i,i)=w1(i);
		for (int i=0; i<rank2; i++) M(rank1+i,rank1+i)=w2(i);
		M=inner(inner(Xp,M),Yp);

		// SVD on the M matrix
		tensorT Um,VTm;
		Tensor<double> s;
		svd(M,Um,s,VTm);

		// the transformation matrices for x1, x2, y1, y2
		tensorT t11=inner(Um,Xm(Slice(_),s0));
		tensorT t12=inner(Um,Xm(Slice(_),s1));
		tensorT t21=inner(VTm,Ym(Slice(_),s0),0,0);
		tensorT t22=inner(VTm,Ym(Slice(_),s1),0,0);
//		tensorT t11=Xm(Slice(_),s1);
//		tensorT t12=(Xm(Slice(_),s1));
//		tensorT t21=(Ym(Slice(_),s0));
//		tensorT t22=(Ym(Slice(_),s1));

		x1=inner(t11,x1) + inner(t12,x2);
		y1=inner(t21,y1) + inner(t22,y2);

		print("identity 1");
		print((inner(x1,x1,1,1)-E).normf());
		print((inner(y1,y1,1,1)-E).normf());

		w1=s;
	}

	template<typename T>
	static inline
	std::ostream& operator<<(std::ostream& s, const SRConf<T>& sr) {

		s << "dim_          " << sr.dim_ << "\n";;
		s << "rank_         " << sr.rank_ << "\n";;
		s << "maxk_         " << sr.maxk_ << "\n";;
		s << "vector_.size()" << sr.vector_.size() << "\n";
		s << "has_data()    " << sr.has_data() << "\n";
		s << "TensorType    " << sr.type() << "\n\n";
		return s;
	}
}

#endif /* SRCONF_H_ */