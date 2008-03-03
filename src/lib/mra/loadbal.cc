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

  
  $Id$
*/

/// \file loadbal.cc
/// \brief Implements class methods associated with load balancing.
  
#define WORLD_INSTANTIATE_STATIC_TEMPLATES
#include <mra/mra.h>


namespace madness {
    /// find_best_partition takes the result of find_partitions, determines
    /// which is the best partition, and broadcasts that to all processors
    /// Arguments: none
    /// Return: klist -- the best partition

  template <int D>
    std::vector<typename DClass<D>::TreeCoords> LoadBalImpl<D>::find_best_partition() {
	std::vector<typename DClass<D>::TreeCoords> klist;
	bool manager = false;

	if (skeltree->world.mpi.nproc() == 1) {
	    klist.push_back(typename DClass<D>::TreeCoords(skeltree->root, 0));
	    return klist;
	}

	ProcessID manager_id = skeltree->owner(skeltree->root);
	//madness::print("find_best_partition: the owner of the root is", skeltree->owner(skeltree->root));
	if (skeltree->world.mpi.rank() == skeltree->owner(skeltree->root)) manager = true;
	//madness::print("find_best_partition: just starting out");
//	skeltree->fix_cost();
	//madness::print("find_best_partition: fixed cost");
	skeltree->find_partitions(pi);
	//madness::print("find_best_partition: before fence right after being out of find_partitions");
	skeltree->world.gop.fence();
	//madness::print("find_best_partition: just finished find_partitions");


	if (manager) {
	  //madness::print("find_best_partition: I am the manager");
 	    unsigned int shortest_list = 0, sl_index = 0, lb_index = 0;
 	    Cost load_bal_cost = 0;
	    std::vector< std::vector<typename DClass<D>::TreeCoords> > list_of_list;
	    std::vector<Cost> costlist;
	    // is this right?
	    list_of_list = skeltree->list_of_list;
	    costlist = skeltree->cost_list;

 	    int count = list_of_list.size();
 	    //madness::print("find_best_partition: length of list_of_list =", count);
	    //	    madness::print("find_best_partition: list_of_list =", list_of_list);
	    //	    madness::print("find_best_partition: costlist =", costlist);
 	    std::vector<unsigned int> len;
 	    for (int i = 0; i < count; i++) {
 		len.push_back(list_of_list[i].size());
 		if ((len[i] < shortest_list) || (shortest_list == 0)) {
 		    shortest_list = len[i];
 		    sl_index = i;
 		} else if ((len[i] == shortest_list) && (costlist[i] < costlist[sl_index])) {
 		    // all things being equal, prefer better balance
 		    shortest_list = len[i];
 		    sl_index = i;
 		}
 		if ((costlist[i] < load_bal_cost) || (load_bal_cost == 0)) {
 		    load_bal_cost = costlist[i];
 		    lb_index = i;
 		} else if ((costlist[i] == load_bal_cost) && (len[i] < list_of_list[lb_index].size())) {
 		    // all things being equal, prefer fewer cuts
 		    load_bal_cost = costlist[i];
 		    lb_index = i;
 		}
 	    }
	    
 	    CompCost ccleast = 0;
 	    int cc_index = 0;
 	    for (int i = 0; i < count; i++) {
 		CompCost cctmp = compute_comp_cost(costlist[i], len[i]-1);
 		if ((i==0) || (cctmp < ccleast)) {
 		    ccleast = cctmp;
 		    cc_index = i;
 		}
 	    }
	    /*
 	    madness::print("The load balance with the fewest broken links has cost", 
 			   costlist[sl_index], "and", shortest_list-1, "broken links");
 	    for (unsigned int i = 0; i < shortest_list; i++) {
 		list_of_list[sl_index][i].print();
 	    }
 	    madness::print("");
 	    madness::print("The load balance with the best balance has cost", 
 			   load_bal_cost, "and", list_of_list[lb_index].size()-1, 
 			   "broken links");
 	    for (unsigned int i = 0; i < list_of_list[lb_index].size(); i++) {
 		list_of_list[lb_index][i].print();
 	    }
 	    madness::print("");
 	    madness::print("The load balance with the best overall computational cost has cost",
 			   costlist[cc_index], "and", len[cc_index]-1, "broken links");
 	    for (unsigned int i = 0; i < len[cc_index]; i++) {
 		list_of_list[cc_index][i].print();
 	    }
	    */
	    
 	    for (unsigned int i = 0; i < len[cc_index]; i++) {
 		klist.push_back(list_of_list[cc_index][i]);
 	    }
 	    unsigned int ksize;
 	    ksize = klist.size();
 	    skeltree->world.gop.template broadcast<unsigned int>(ksize, manager_id);
 	    for (unsigned int i=0; i < ksize; i++) {
 	        skeltree->world.gop.template broadcast<typename DClass<D>::TreeCoords>(klist[i], manager_id);
 	    }
	    //	    madness::print("find_best_partition: number of broken links =",
	    //	klist.size()-1);
	}
	else {
	  //madness::print("find_best_partition: receiving broadcast");
	  typename DClass<D>::TreeCoords ktmp;
	  unsigned int ksize;
	  skeltree->world.gop.template broadcast<unsigned int>(ksize, manager_id);
	  for (unsigned int i=0; i < ksize; i++) {
	    skeltree->world.gop.template broadcast<typename DClass<D>::TreeCoords>(ktmp, manager_id);
	    klist.push_back(ktmp);
	  }
	}
        return klist;	
    };


    /// find_all_partitions takes the result of find_partitions, determines
    /// the cost of each partition, and broadcasts the result of find_partitions
    /// to all processors
    /// Arguments: none
    /// Return: a vector of vectors with all the partitions

  template <int D>
  std::vector< std::vector<typename DClass<D>::TreeCoords> > LoadBalImpl<D>::find_all_partitions() {
    //	std::vector<typename DClass<D>::TreeCoords> klist;
	bool manager = false;

	if (skeltree->world.mpi.nproc() == 1) {
	  std::vector< std::vector< typename DClass<D>::TreeCoords > > listoflist;
	  listoflist.push_back(std::vector<typename DClass<D>::TreeCoords>(1,typename DClass<D>::TreeCoords(skeltree->root, 0)));
	    return listoflist;
	}

	ProcessID manager_id = skeltree->owner(skeltree->root);
	//madness::print("find_best_partition: the owner of the root is", skeltree->owner(skeltree->root));
	if (skeltree->world.mpi.rank() == skeltree->owner(skeltree->root)) manager = true;
	//madness::print("find_best_partition: just starting out");
//	skeltree->fix_cost();
	//madness::print("find_best_partition: fixed cost");
	skeltree->find_partitions(pi);
	//madness::print("find_best_partition: before fence right after being out of find_partitions");
	skeltree->world.gop.fence();
	//madness::print("find_best_partition: just finished find_partitions");

	std::vector< std::vector<typename DClass<D>::TreeCoords> > list_of_list;

	if (manager) {
	  //madness::print("find_best_partition: I am the manager");
 	    unsigned int shortest_list = 0, sl_index = 0, lb_index = 0;
 	    Cost load_bal_cost = 0;
	    std::vector<Cost> costlist;
	    // is this right?
	    list_of_list = skeltree->list_of_list;
	    costlist = skeltree->cost_list;

 	    int count = list_of_list.size();
 	    //madness::print("find_best_partition: length of list_of_list =", count);
	    //	    madness::print("find_best_partition: list_of_list =", list_of_list);
	    //	    madness::print("find_best_partition: costlist =", costlist);
 	    std::vector<unsigned int> len;
 	    for (int i = 0; i < count; i++) {
 		len.push_back(list_of_list[i].size());
 		if ((len[i] < shortest_list) || (shortest_list == 0)) {
 		    shortest_list = len[i];
 		    sl_index = i;
 		} else if ((len[i] == shortest_list) && (costlist[i] < costlist[sl_index])) {
 		    // all things being equal, prefer better balance
 		    shortest_list = len[i];
 		    sl_index = i;
 		}
 		if ((costlist[i] < load_bal_cost) || (load_bal_cost == 0)) {
 		    load_bal_cost = costlist[i];
 		    lb_index = i;
 		} else if ((costlist[i] == load_bal_cost) && (len[i] < list_of_list[lb_index].size())) {
 		    // all things being equal, prefer fewer cuts
 		    load_bal_cost = costlist[i];
 		    lb_index = i;
 		}
 	    }
	    
 	    CompCost ccleast = 0;
	    std::vector<CompCost> cclist;
 	    int cc_index = 0;
 	    for (int i = 0; i < count; i++) {
 		CompCost cctmp = compute_comp_cost(costlist[i], len[i]-1);
		cclist.push_back(cctmp);
 		if ((i==0) || (cctmp < ccleast)) {
 		    ccleast = cctmp;
 		    cc_index = i;
 		}
 	    }
	    madness::print("The computational cost of each distribution is", cclist);

	    /*
 	    madness::print("The load balance with the fewest broken links has cost", 
 			   costlist[sl_index], "and", shortest_list-1, "broken links");
 	    for (unsigned int i = 0; i < shortest_list; i++) {
 		list_of_list[sl_index][i].print();
 	    }
 	    madness::print("");
 	    madness::print("The load balance with the best balance has cost", 
 			   load_bal_cost, "and", list_of_list[lb_index].size()-1, 
 			   "broken links");
 	    for (unsigned int i = 0; i < list_of_list[lb_index].size(); i++) {
 		list_of_list[lb_index][i].print();
 	    }
 	    madness::print("");
 	    madness::print("The load balance with the best overall computational cost has cost",
 			   costlist[cc_index], "and", len[cc_index]-1, "broken links");
 	    for (unsigned int i = 0; i < len[cc_index]; i++) {
 		list_of_list[cc_index][i].print();
 	    }
	    */
	    
//          for (unsigned int i = 0; i < len[cc_index]; i++) {
//  		klist.push_back(list_of_list[cc_index][i]);
//  	    }
  	    unsigned int ksize;
 	    ksize = len.size();
 	    skeltree->world.gop.template broadcast<unsigned int>(ksize, manager_id);
 	    for (unsigned int i=0; i < ksize; i++) {
	      skeltree->world.gop.template broadcast<unsigned int>(len[i], manager_id);
	      for (unsigned int j=0; j < len[i]; j++) {
 	        skeltree->world.gop.template broadcast<typename DClass<D>::TreeCoords>(list_of_list[i][j], manager_id);
	      }
 	    }
	    //	    madness::print("find_best_partition: number of broken links =",
	    //	klist.size()-1);
	}
	else {
	  //madness::print("find_best_partition: receiving broadcast");
	  typename DClass<D>::TreeCoords ktmp;
	  typename std::vector<typename DClass<D>::TreeCoords> emptylist;
	  unsigned int ksize;
	  skeltree->world.gop.template broadcast<unsigned int>(ksize, manager_id);
	  for (unsigned int i=0; i < ksize; i++) {
	    list_of_list.push_back(emptylist);
	    unsigned int jmax;
	    skeltree->world.gop.template broadcast<unsigned int>(jmax, manager_id);
	    for (unsigned int j=0; j < jmax; j++) {
	      skeltree->world.gop.template broadcast<typename DClass<D>::TreeCoords>(ktmp, manager_id);
	      list_of_list[i].push_back(ktmp);
	    }
	  }
	}
        return list_of_list;	
    };



    /// Compute the cost of a given configuration: a weighted sum of the cost of the
    /// maximally-loaded process and the total number of broken links.
    /// In the future, the factors should be calibrated for a given machine, either 
    /// during configuration and setup or in real time at the beginning of the program
    /// or upon the creation of a LoadBalImpl.
    /// Arguments: Cost c -- maximum amount of cost assigned to a node
    ///            int n -- number of broken links
    /// Return: CompCost -- the cost associated with that partitioning
    template <int D>
    CompCost LoadBalImpl<D>::compute_comp_cost(Cost c, int n) {
        CompCost compcost;
	CompCost k_to_D = pow((CompCost) k,D);
	CompCost twok_to_Dp1 = pow((CompCost) 2.0*k, D+1);
	compcost = c*(flop_time*D*twok_to_Dp1) + n*(comm_bandw*k_to_D + comm_latency);
        return compcost;
    }



    /// find_partitions performs the "melding" algorithm for load balancing: it recursively melds 
    /// and partitions the tree until it has found all possible configurations.
    /// Arguments: PartitionInfo<D> lbi -- a convenient class that stores load balancing partition information
    /// Return: none
    /// Side effect: lbi is altered
    /// Communication: manager sends message to launch make_partition; broadcast of boolean to determine
    ///                whether to continue; fences
    template <int D> 
    void LBTree<D>::find_partitions(PartitionInfo<D>& lbi) {
        bool manager = false; 
        bool keep_going = true;
        bool first_time = true;
	//madness::print("find_partitions: at beginning");
	this->world.gop.fence();

	ProcessID manager_id = this->impl.owner(root);
	if (this->world.mpi.rank() == this->impl.owner(root)) manager = true;

	while (keep_going) {
	  //madness::print("find_partitions: the verdict is to keep_going");
	    meld_all(first_time);
	    this->world.gop.fence();
	    //madness::print("find_partitions: after meld_all");
	    //madness::print("");
	    //madness::print("");
	    if (manager) {
	      unsigned int npieces;
	      Cost used_up;
	      if (first_time) {
		
	      }
	      else {
		lbi.step_num++;
	      }
	      
	      npieces = world.nproc();
	      lbi.partition_number = npieces-1;
	      Cost tpart = compute_partition_size(lbi.skel_cost, npieces);
	      used_up = 0;
	      //this->print(root);
	      //madness::print("find_partitions: about to send make_partition");
	      send(impl.owner(root), &LBTree<D>::make_partition, root, tpart, used_up, lbi, true);
	      //madness::print("find_partitions: back from send make_partition");
	      //madness::print("");
	      this->world.gop.fence();
	      //madness::print("find_partitions: P0 outside of the fence now");
	    } else {
//	      this->world.gop.template broadcast<bool>(keep_going);
	      this->world.gop.fence();
	      //madness::print("find_partitions: outside of the fence now");
	    }
	    first_time = false;
	    this->world.gop.fence();
	    //madness::print("find_partitions: done with fence");
	    if (manager) {
	      //madness::print("find_partitions: this->partition_info =", this->partition_info);
	      //madness::print("find_partitions: this->temp_list =", this->temp_list);
	      if (this->partition_info.partition_number == 0) {
		// make sure current partition is valid.  If not, quit.
		keep_going = verify_partition(this->temp_list);
		//madness::print("find_partitions: verify_partition says,", keep_going);
		int count = this->partition_info.step_num;
		if (keep_going) {
		  //madness::print("find_partitions: adding", this->temp_list, "to list_of_list");
		  list_of_list.push_back(this->temp_list);
		  //madness::print("find_partitions: size of list_of_list[", partition_info.step_num, "] =", list_of_list[this->partition_info.step_num].size());
		  //madness::print("find_partitions: list_of_list =", list_of_list);
		  this->temp_list.clear();
		  //madness::print("find_partitions: after resetting some stuff, partition_info =", this->partition_info);
		  cost_list.push_back(this->partition_info.maxcost);
		  //madness::print("find_partitions: cost_list =", cost_list);
		  lbi.reset(this->world.nproc()-1);
		  //madness::print("find_partitions: after resetting, lbi =", lbi);
		  count++;
		}
	      } else {
		keep_going = false;
	      }
	      //madness::print("find_partitions: the verdict is that keep_going =", keep_going);	
	    }
	    this->world.gop.template broadcast<bool>(keep_going, manager_id);
	}
	this->world.gop.fence();
    }

    /// Verify the validity of a given configuration: the tree should be distributed
    /// over all processors, and extraneous information should be removed.
    /// Arguments: vector<TreeCoords<D> > part_list -- the partition in question
    /// Return: bool -- whether the partition is valid
    /// Side effect: part_list is cleaned up
    /// Communication: none -- this is a job for the manager

    template <int D>
    bool LBTree<D>::verify_partition(std::vector<TreeCoords<D> >& part_list) {
      const int min_pieces = this->world.nproc();
      int size = part_list.size();
      //madness::print("verify_partition: size of part_list =", size);
      //madness::print("verify_partition: part_list =", part_list);
      if (size < min_pieces) return false;

      // Make sure that every process has at least one piece of the partition
      int m = min_pieces;
      bool invalid_partition = false;
      for (int k = 0; k < size; k++) {
	//madness::print("verify_partition: looking at", part_list[k]);
	int difff = m-part_list[k].owner;
	if (difff == 1) {
	  m--;
	} else if (difff > 1) {
	  invalid_partition = true;
	  break;
	}
      }
      if (invalid_partition) {
	//madness::print("verify_partition: invalid partition");
	return false;
      }


      typedef std::pair<typename DClass<D>::KeyD, ProcessID> part_type;
      typedef std::map<typename DClass<D>::KeyD, ProcessID> map_type;

      map_type part_map;
      for (int i = 0; i < size; i++) {
	part_map.insert(part_type(part_list[i].key, part_list[i].owner));
      }
      typename map_type::iterator it, fit;
      for (it = part_map.begin(); it != part_map.end(); ) {
	typename DClass<D>::KeyD key = it->first;
	ProcessID owner = it->second;
	if (key == root) {
	  ++it;
	} else {
	  bool erased_it = false;
	  int level = key.level();
	  for (int j = 1; j <= level; j++) {
	    fit = part_map.find(key.parent(j));
	    if (fit != part_map.end()) {
	      if (fit->second == owner) {
		part_map.erase(it++);
		erased_it = true;
	      }
	      break;
	    }
	  }
	  if (!erased_it) {
	    ++it;
	  }
	}
      }
      int pmsize = part_map.size();
      if (pmsize < min_pieces) {
	//madness::print("verify_partition: pmsize < min_pieces: return false");
	return false;
      }
      if (pmsize != size) {
	part_list.clear();
	for (it = part_map.begin(); it != part_map.end(); ++it) {
	  part_list.push_back(TreeCoords<D>(it->first, it->second));
	}
      }
      //madness::print("verify_partition: about to return true at very end");
      return true;
    }


    /// meld_all coordinates the rollup of the tree
    /// Arguments: bool first_time -- save ourselves some effort if the cost has already been initialized
    /// Return: none
    /// Side effect: tree is melded
    /// Communication: barriers, and communication by methods it calls

    template <int D>
    Void LBTree<D>::meld_all(bool first_time = false) {
      //madness::print("meld_all: after launching on everybody, about to go to fix_cost");
	this->world.gop.barrier();
	//madness::print("meld_all: about to fix_cost");
       	if (!first_time) {
	    this->fix_cost();
       	}
	//madness::print("meld_all: done with fix_cost");
	this->world.gop.barrier();
	//madness::print("meld_all: about to reset");
	this->reset(true);
	this->world.gop.barrier();
	//madness::print("meld_all: about to rollup");
	this->rollup();
	this->world.gop.barrier();
	//madness::print("meld_all: about to reset");
	this->reset(false);
	//madness::print("meld_all: done with reset");
	this->world.gop.barrier();
	//madness::print("meld_all: done with barrier; returning None");
	return None;
    }



    /// fix_cost resets the tree after the load balancing and melding have been performed, before the next
    /// round of load balancing.
    /// Arguments: none
    /// Return: Cost of entire tree
    /// Side effect: subcost (the cost of the subtree rooted at key) is reset
    /// Communication: none except communication by methods it calls


    template <int D>
    Cost LBTree<D>::fix_cost() {
	init_fix_cost();
	//madness::print("fix_cost: done with init_fix_cost");
	this->world.gop.fence();
	//this->world.gop.barrier();
	//madness::print("fix_cost: about to fix_cost_spawn");
	fix_cost_spawn();
	//madness::print("fix_cost: done with fix_cost_spawn, now for fence");
	this->world.gop.fence();
	//this->world.gop.barrier();
	//madness::print("AFTER FIXING COST");
	//for (typename DClass<D>::treeT::iterator it = impl.begin(); it != impl.end(); ++it) {
	//    madness::print(it->first, it->second);
	//}
	//madness::print("DONE FIXING IT");
	if (this->world.mpi.rank() == impl.owner(typename DClass<D>::KeyD(0))) {
	    typename DClass<D>::treeT::iterator it = impl.find(typename DClass<D>::KeyD(0));
	    return it->second.get_data().subcost;
	} else {
	    return 0;
	}
    }


    /// init_fix_cost resets and zeroes out nrecvd in each node
    /// Arguments: none
    /// Return: none
    /// Side effects: none
    /// Communication: none

    template <int D>
    void LBTree<D>::init_fix_cost() {
	for (typename DClass<D>::treeT::iterator it = impl.begin(); it != impl.end(); ++it) {
	    typename DClass<D>::KeyDConst& key = it->first;
	    typename DClass<D>::NodeD& node = it->second;

	    int dim = node.dim;
	    NodeData d = node.get_data();
	    d.subcost = d.cost;
	    node.nrecvd = dim - node.get_num_children();
	    node.set_data(d);
	    impl.insert(key,node);
	}
    }


    /// fix_cost_spawn launches sum up tree, beginning at leaf nodes
    /// Arguments: none
    /// Return: none
    /// Side effects: fixes subcost on each node
    /// Communication: sends cost of each leaf node to owner of its parent

    template <int D>
    void LBTree<D>::fix_cost_spawn() {
	for (typename DClass<D>::treeT::iterator it = impl.begin(); it != impl.end(); ++it) {
	    typename DClass<D>::KeyDConst& key = it->first;
	    typename DClass<D>::NodeD& node = it->second;
	    if (!node.has_children()) {
		typename DClass<D>::KeyD parent = key.parent();
		Cost c = node.get_data().cost;
//		madness::print("fix_cost_spawn: key", key, "is leaf child; sending", c,
//	       		       "to parent", parent, "at processor", impl.owner(parent));
		send(impl.owner(parent), &LBTree<D>::fix_cost_sum, parent, c);
	    }
	}
    }


    /// fix_cost_sum receives node cost from child, adds to this node's subcost,
    /// and, if it's added in all the costs from below, sends its cost to its parent
    /// Arguments: const Key<D> key: the key of the node in question
    ///            Cost c: the cost of the subtree rooted by the child
    /// Return: none
    /// Side effects: none
    /// Communication: may send its subtree cost to parent

    template <int D>
    Void LBTree<D>::fix_cost_sum(typename DClass<D>::KeyDConst& key, Cost c) {
	typename DClass<D>::treeT::iterator it = impl.find(key);
	typename DClass<D>::NodeD node = it->second;
	NodeData d = node.get_data();
	d.subcost += c;
//	madness::print("fix_cost_sum:", key, "received number", node.nrecvd+1, "cost", 
//		       c, " subtotal =", d.subcost);
	node.nrecvd++;
	node.set_data(d);
	impl.insert(key, node);
	if ((node.nrecvd == node.dim) && (key.level()!=0)) {
	    typename DClass<D>::KeyD parent = key.parent();
//	    madness::print("fix_cost_sum:", key, "sending cost", d.subcost, "to parent", parent);
	    task(impl.owner(parent), &LBTree<D>::fix_cost_sum, parent, d.subcost);
	}
	return None;
	
    }


    /// rollup traverses the tree, calling meld upon Nodes that have leaf children
    /// Arguments: const Key<D> key -- node at which we begin
    /// Side effect: Nodes are changed by meld
    /// Communication: just finding the nodes that match a given key
    template <int D>
    void LBTree<D>::rollup() {
	for (typename DClass<D>::treeT::iterator it = impl.begin(); it != impl.end(); ++it) {
	    typename DClass<D>::KeyD key = it->first;
	    typename DClass<D>::NodeD node = it->second;
	    if (node.has_children()) {
                // First, check to see if it has any leaf children
	        bool has_leaf_child = false;
	        for (KeyChildIterator<D> kit(key); kit; ++kit) {
            	    typename DClass<D>::treeT::iterator itc = impl.find(kit.key());
		    if (itc != impl.end()) {
		        typename DClass<D>::NodeD c = itc->second;
		        NodeData d = c.get_data();
		        if ((!c.has_children()) && (d.is_taken)) {
			    has_leaf_child = true;
			    break;
		        }
		    }
	        }
	        if (has_leaf_child) {
                    // If there is at least one leaf child, then this node gets melded.
		    this->meld(it);
		    node = it->second;
	        }
	        NodeData d = node.get_data();
	        if (d.is_taken) {
                    // Setting to false, to signify that this node has been worked on.
		    d.is_taken = false;
		    node.set_data(d);
		    impl.insert(key,node);
	        }
	    }
	}
    }

    /// reset sets the is_taken variable within all local nodes to the value specified
    /// Arguments: bool taken -- value to set is_taken to
    /// Communication: none (local iterator)
    template <int D>
    void LBTree<D>::reset(bool taken) {
	for (typename DClass<D>::treeT::iterator it = impl.begin(); it != impl.end(); ++it) {
	    typename DClass<D>::KeyD key = it->first;
	    typename DClass<D>::NodeD node = it->second;
	    NodeData d = node.get_data();

	    d.is_taken = taken;
	    node.set_data(d);
	    impl.insert(key,node);
	}
    }

    /// meld fuses leaf child(ren) to parent and deletes the leaf child(ren) in question
    /// Arguments: const Key<D> key -- node at which we begin
    /// Side effect: parent nodes are updated, and leaf nodes are deleted
    /// Communication: find and insert 
    template <int D>
    void LBTree<D>::meld(typename DClass<D>::treeT::iterator it) {
	typename DClass<D>::KeyD key = it->first;
	typename DClass<D>::NodeD node = it->second;
	std::vector<unsigned int> mylist;
	unsigned int i = 0;
	Cost cheapest = 0;
	bool not_yet_found = true;

	for (KeyChildIterator<D> kit(key); kit; ++kit) {
	    if (node.has_child(i)) {
		typename DClass<D>::treeT::iterator itc = impl.find(kit.key());
		typename DClass<D>::NodeD c = itc->second;
		NodeData d = c.get_data();
		// if the child has no children and the is_taken flag is set to true, 
		// then this child is eligible to be melded into parent
		if ((!c.has_children()) && (d.is_taken)) {
		    Cost cost = d.cost;
		    if ((cost < cheapest) || (not_yet_found)) {
                        // if this child has the cheapest cost yet, then clear the 
			// list and add this child to the list of children to be 
			// melded to the parent
			not_yet_found = false;
			cheapest = cost;
			mylist.clear();
			mylist.push_back(i);
		    } else if (cost == cheapest) {
                        // if this child's cost is equal to the cheapest cost found 
			// so far, then add this child to the list of children to be 
			// melded into parent
			mylist.push_back(i);
		    }
		}
	    }
	    i++;
	}
	if (not_yet_found) {
	    // this node has no leaf children
	    return;
	}
        // Now we do the actual melding
	NodeData d = node.get_data();
	i = 0;
	int j = 0, mlsize = mylist.size();
	for (KeyChildIterator<D> kit(key); kit; ++kit) {
	    if (mylist[j] == i) {
		impl.erase(kit.key());
		node.set_child(mylist[j], false);
		d.cost += cheapest;
		j++;
		if (j == mlsize) break;
	    }
	    i++;
	}
	node.set_data(d);
	impl.insert(key, node);
    }


    /// make_partition creates a partition.  It's called by find_partitions to actually do all the dirty 
    /// work for each partition. 
    /// Arguments: const Key<D> key -- node at which we begin
    ///            Cost partition_size -- the target size for the partition
    ///            Cost used_up -- the cost used up so far in this partition
    ///            PartitionInfo<D> lbi -- repository for information about partitioning
    ///            bool downward -- whether we are traversing down the tree at this time
    /// Return: none
    /// Side effect: lbi altered, some nodes in tree altered
    /// Communication: spawns either another make_partition for the next node in depth-first partition,
    ///                or sends totally_reset to manager, marking the end of this round of partitioning;
    ///                may also send add_to_partition to manager if node is to be added to list


  template <int D>
  Void LBTree<D>::make_partition(typename DClass<D>::KeyDConst& key, Cost partition_size, Cost used_up, PartitionInfo<D> lbi, bool downward = false) {
    
    // The fudge factor is the fraction by which you are willing to let the
    // partitions exceed the ideal partition size
    double fudge_factor = 0.1;
    Cost maxAddl = (Cost) (fudge_factor*partition_size);

//    madness::print("starting make_partition");
    
    typename DClass<D>::treeT::iterator it = impl.find(key);
    if (it == impl.end()) {
      typename DClass<D>::KeyDConst parent = key.parent();
      send(impl.owner(parent), &LBTree::make_partition, parent, partition_size, used_up, lbi);
      //madness::print("RETURN 1");
      return None;
    }
    typename DClass<D>::NodeD node = it->second;
    
    typename DClass<D>::KeyDConst parent = key.parent();
    NodeData d = node.get_data();
    
    // if either the partition is currently empty and this is a single item, or there is still 
    // room in the partition and adding this to it won't go above the fudge factor, then add this 
    // piece to the partition.
    if ((downward) && (((used_up == 0) && (!node.has_children())) ||
		       ((used_up < partition_size) && (d.subcost+used_up <= partition_size+maxAddl)))) {
      used_up += d.subcost;
      //madness::print("make_partition: adding", key, "to partition");
      send(impl.owner(root), &LBTree::add_to_partition, TreeCoords<D>(key, lbi.partition_number));
      if (key == root) {
	//madness::print("make_partition: about to totally_reset");
	send(impl.owner(root), &LBTree::totally_reset, lbi);
      } else {
	send(impl.owner(parent), &LBTree::make_partition, parent, partition_size, used_up, lbi);
      }
      //madness::print("RETURN 2");
      return None;
    }
    
    if (node.has_children()) {
      if (downward) {
	node.rpit = KeyChildIterator<D>(key);
	impl.insert(key,node);
      } else {
	++(node.rpit);
	impl.insert(key,node);
      }
      
      if (node.rpit) {
	typename DClass<D>::KeyDConst& child = node.rpit.key();
	impl.insert(key,node);
	send(impl.owner(child), &LBTree::make_partition, child, partition_size, used_up, lbi, true);
	//madness::print("RETURN 3");
	return None;
      }
    }
    
    // All done with children, or I am a leaf
    if (((used_up == 0) && (!node.has_children())) ||
	((used_up < partition_size) && (d.cost+used_up <= partition_size+maxAddl))) {
      used_up += d.cost;
      //madness::print("make_partition: adding", key, "to partition");
      send(impl.owner(root), &LBTree::add_to_partition, TreeCoords<D>(key, lbi.partition_number));
      if (key == root) {
	//madness::print("make_partition: key == root");
      } else {
	send(impl.owner(parent), &LBTree::make_partition, parent, partition_size, used_up, lbi);
      }
      //madness::print("RETURN 4");
      return None;
    } else {
      bool continue_as_normal = reset_partition(partition_size, used_up, lbi);
      if (continue_as_normal) {
	send(impl.owner(key), &LBTree::make_partition, key, partition_size, used_up, lbi, downward);
      } else {
	//madness::print("make_partition: about to totally_reset (else of else)");
	send(impl.owner(root), &LBTree<D>::totally_reset, lbi);
      }
      //madness::print("RETURN 5");
      return None;
    }
    //madness::print("make_partition: we should not be here");
    //return None;
  }

    /// totally_reset resets the partitioning.  It marks the point at which this round of partitioning
    /// is over.
    /// Arguments: PartitionInfo<D> lbi -- repository for information about partitioning
    /// Return: none
    /// Side effect: this->partition_info reset, and this->temp_list updated
    /// Communication: none
  
    template <int D>
    Void LBTree<D>::totally_reset(PartitionInfo<D> lbi) {
      //madness::print("totally_reset: at beginning, lbi =", lbi);
//      madness::print("totally_reset: totally resetting");
        this->partition_info = lbi;
	this->temp_list.push_back(typename DClass<D>::TreeCoords(root, lbi.partition_number));
	//madness::print("totally_reset: temp_list =", this->temp_list);
	//madness::print("totally_reset: and lbi =", lbi);
	//bool keep_going = true;
//	this->world.gop.template broadcast<bool>(keep_going);
	return None;
    }

    /// add_to_partition adds a node to the partition list
    /// Arguments: TreeCoords<D> p -- the listing to be added to the list
    /// Return: none
    /// Side effect: this->temp_list updated
    /// Communication: none

    template <int D>
    Void LBTree<D>::add_to_partition(typename DClass<D>::TreeCoords p) {
        this->temp_list.push_back(p);
	//madness::print("add_to_partition: partition =", temp_list);
	return None;
    }

    /// reset_partition wraps up partition X and resets variables to get ready for partition X-1
    /// Arguments: Cost partition_size -- the ideal partition size
    ///            Cost used_up -- the amount that has been used up in this partition
    ///            PartitionInfo<D> lbi -- convenient repository for partitioning information
    /// Return: bool -- whether it is okay to keep working on this partitioning
    /// Side effect: lbi.cost_left changed; lbi.maxcost, lbi.cost_left, and partition_size may change
    /// Communication: add_to_partition might be called

    template <int D>
    bool LBTree<D>::reset_partition(Cost& partition_size, Cost& used_up, PartitionInfo<D>& lbi) {
	lbi.partition_number--;
	if (used_up > lbi.maxcost) {
	  //madness::print("reset_partition: reset maxcost to", used_up);
	  lbi.maxcost = used_up;
	}
	lbi.cost_left -= used_up;
	if (lbi.partition_number > 0) {
	    used_up = 0;
	    Cost tpart = compute_partition_size(lbi.cost_left, lbi.partition_number+1);
	    if ((tpart > partition_size) || (tpart*lbi.facter < partition_size)) {
		partition_size = tpart;
	    }
	    return true;
	} else {
	    if (lbi.cost_left > lbi.maxcost) {
	        lbi.maxcost = lbi.cost_left;
	    }
	    //madness::print("reset_partition: adding root to partition and returning false");
	    send(impl.owner(root), &LBTree<D>::add_to_partition, typename DClass<D>::TreeCoords(root, lbi.partition_number));
	    //lbi.part_list.push_back(typename DClass<D>::TreeCoords(root, lbi.partition_number));
	    // totally reset!
	    return false;
	}
    }

      


    /// Compute the partition size: a straight quotient of the cost by the number of
    /// remaining partitions
    Cost compute_partition_size(Cost cost, unsigned int parts) {
        return (Cost) ceil(((double) cost)/((double) parts));
    }



     // Explicit instantiations for D=1:6
  /*
    template class LoadBalImpl<double,1>;
    template class LoadBalImpl<double,2>;
    template class LoadBalImpl<double,3>;
    template class LoadBalImpl<double,4>;
    template class LoadBalImpl<double,5>;
    template class LoadBalImpl<double,6>;

    template class LoadBalImpl<std::complex<double>,1>;
    template class LoadBalImpl<std::complex<double>,2>;
    template class LoadBalImpl<std::complex<double>,3>;
    template class LoadBalImpl<std::complex<double>,4>;
    template class LoadBalImpl<std::complex<double>,5>;
    template class LoadBalImpl<std::complex<double>,6>;
  */
    template class LoadBalImpl<1>;
    template class LoadBalImpl<2>;
    template class LoadBalImpl<3>;
    template class LoadBalImpl<4>;
    template class LoadBalImpl<5>;
    template class LoadBalImpl<6>;

    template class LBTree<1>;
    template class LBTree<2>;
    template class LBTree<3>;
    template class LBTree<4>;
    template class LBTree<5>;
    template class LBTree<6>;
}
