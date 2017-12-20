//
// Created by blueeyedhush on 23.11.17.
//

#ifndef FRAMEWORK_ROUNDROBIN2DPARTITION_H
#define FRAMEWORK_ROUNDROBIN2DPARTITION_H

#include <string>
#include <vector>
#include <unordered_set>
#include <boost/pool/object_pool.hpp>
#include <GraphPartitionHandle.h>
#include <GraphPartition.h>
#include <utils/AdjacencyListReader.h>
#include <utils/NonCopyable.h>
#include "shared.h"

template <typename TLocalId> using RR2DGlobalId = ALHPGlobalVertexId<TLocalId>;

template <typename TLocalId, typename TNumId>
class RoundRobin2DPartition : public GraphPartition<RR2DGlobalId<TLocalId>, TLocalId, TNumId> {
	using P = GraphPartition<RR2DGlobalId<TLocalId>, TLocalId, TNumId>;
	IMPORT_ALIASES(P)

public:
	MPI_Datatype getGlobalVertexIdDatatype() {

	}
	
	LocalId toLocalId(const GlobalId, VERTEX_TYPE* vtype = nullptr) {

	}

	NodeId toMasterNodeId(const GlobalId) {

	}

	GlobalId toGlobalId(const LocalId) {

	}

	NumericId toNumeric(const GlobalId) {

	}

	NumericId toNumeric(const LocalId) {

	}

	std::string idToString(const GlobalId) {

	}

	std::string idToString(const LocalId) {

	}

	bool isSame(const GlobalId, const GlobalId) {

	}

	bool isValid(const GlobalId) {

	}


	void foreachMasterVertex(std::function<ITER_PROGRESS (const LocalId)>) {

	}

	size_t masterVerticesCount() {

	}

	size_t masterVerticesMaxCount() {

	}

	
	void foreachCoOwner(LocalId, bool returnSelf, std::function<ITER_PROGRESS (const NodeId)>) {

	}
	
	void foreachNeighbouringVertex(LocalId, std::function<ITER_PROGRESS (const GlobalId)>) {

	}
};

namespace details { namespace RR2D {
	using EdgeCount = unsigned int;
	using NodeCount = NodeId;
	using VertexHandle = unsigned int;
	using ElementCount = unsigned long long;
	const auto ElementCountDt = MPI_UNSIGNED_LONG_LONG;

	const ElementCount EDGES_MAX_COUNT = 1000;
	const ElementCount VERTEX_MAX_COUNT = 100;
	const ElementCount COOWNERS_MAX_COUNT = 20;

	struct EdgeTableOffset {
		EdgeTableOffset(NodeId nodeId, EdgeCount offset, bool master) : nodeId(nodeId), offset(offset), master(master) {}

		NodeId nodeId;
		EdgeCount offset;
		bool master;
	};

	// @todo rename to MpiWindow
	template <typename T>
	class MpiWindowDesc : NonCopyable {
	public:
		MpiWindowDesc(MPI_Datatype datatype) : dt(datatype) {};

		MpiWindowDesc(MpiWindowDesc&& o): dt(o.dt), win(o.win), data(o.data) {
			o.win = MPI_WIN_NULL;
		};

		MpiWindowDesc& operator=(MpiWindowDesc&& o) {
			win = o.win;
			data = o.data;
			dt = o.dt;
			return *this;
		};

		void put(NodeId nodeId, ElementCount offset, T* data, ElementCount dataLen) {
			MPI_Put(data + offset, dataLen, dt, nodeId, offset, dataLen, dt, win);
		}

		void flush() { MPI_Win_flush_all(win); }
		void sync() { MPI_Win_sync(win); }

		static MpiWindowDesc allocate(ElementCount size, MPI_Datatype dt) {
			auto elSize = sizeof(T);
			MpiWindowDesc wd(dt);
			MPI_Win_allocate(size*elSize, elSize, MPI_INFO_NULL, MPI_COMM_WORLD, &wd.data, &wd.win);
			MPI_Win_lock_all(0, wd.win);
			return wd;
		}

		static void destroy(MpiWindowDesc &d) {
			if (d.win != MPI_WIN_NULL) {
				MPI_Win_unlock_all(d.win);
				MPI_Win_free(&d.win);
			}
		}

	private:
		MPI_Win win;
		T* data;
		MPI_Datatype dt;
	};

	struct OffsetArraySizeSpec {
		ElementCount valueCount;
		ElementCount offsetCount;

		static MPI_Datatype mpiDatatype() {
			MPI_Datatype dt;
			MPI_Type_contiguous(2, ElementCountDt, &dt);
			return dt;
		}
	};

	class OffsetTracker {
	public:
		OffsetTracker(ElementCount count) : offset(0), count(count) {}

		ElementCount get() {return offset;}

		void tryAdvance(ElementCount increment) {
			assert(offset + increment < count);
		}

	private:
		ElementCount offset;
		ElementCount count;
	};

	/* master must keep track of counts for each node separatelly */
	struct Counts {
		OffsetArraySizeSpec masters;
		OffsetArraySizeSpec shadows;
		/* offsetCount for coOwners should be identical to that of masters, but we keep it here for to keep design
		 * consistent */
		OffsetArraySizeSpec coOwners;

		static MPI_Datatype mpiDatatype() {
			auto oascDt = OffsetArraySizeSpec::mpiDatatype();
			MPI_Datatype dt;
			MPI_Type_contiguous(3, oascDt, &dt);
			return dt;
		}
	};

	class CountsForCluster {
	public:
		CountsForCluster(NodeCount nc, MPI_Datatype countDt)
				: nc(nc),
				  counts(new Counts[nc]),
				  winDesc(MpiWindowDesc<Counts>::allocate(1, countDt))
		{}

		~CountsForCluster() {
			MpiWindowDesc<Counts>::destroy(winDesc);
			delete[] counts;
		}

		/* meant to be used for both reading and _writing_ */
		Counts& get(NodeId nid) {
			return counts[nid];
		}

		void send() {
			for(NodeId nid = 0; nid < nc; nid++) {
				winDesc.put(nid, 0, counts + nid, 1);
			}
		}

		void flush() { winDesc.flush(); }
		void sync() { winDesc.sync(); }

	private:
		NodeCount nc;
		Counts *counts;
		MpiWindowDesc<Counts> winDesc;
	};

	template <typename TLocalId>
	struct ShadowDescriptor {
		ShadowDescriptor(RR2DGlobalId<TLocalId> id, ElementCount offset) : id(id), offset(offset) {}

		RR2DGlobalId<TLocalId> id;
		ElementCount offset;
	};

	template <typename T>
	class SendBufferManager {
	public:
		SendBufferManager(NodeCount nodeCount) : nc(nodeCount) {
			buffers = new std::vector<T>[nc];
		}

		~SendBufferManager() {
			delete[] buffers;
		}

		void append(NodeId id, T value) {
			buffers[id].push_back(value);
		}

		void foreachNonEmpty(std::function<void(NodeId, T*, ElementCount)> f) {
			for(NodeId i = 0; i < nc; i++) {
				auto& b = buffers[i];
				if (!b.empty()) {
					f(i, b.data(), b.size());
				}
			}
		}

		void clearBuffers() {
			for(NodeId i = 0; i < nc; i++) {
				buffers[i].clear();
			}
		}

	private:
		NodeCount nc;
		std::vector<T> *buffers;
	};

	// @todo what to do with MPI typemap?
	struct MpiTypes {
		MPI_Datatype globalId;
		MPI_Datatype localId;
		MPI_Datatype shadowDescriptor;
		MPI_Datatype nodeId;
		MPI_Datatype count;
	};

	/**
	 * Assumes we process one vertex at a time (start, register, finish)
	 * If we want to process more, we need some kind of VertexHandle to know which one we are talking about.
	 *
	 * After distributing data we don't expect any more communication. For efficiency, we let MPI allocate
	 * memory for windows, which means that lifetime of the window is tied to the lifetime of it's memory.
	 * This means that windows can be freed only after whole graph is released.
	 */
	template <typename TLocalId>
	class CommunicationWrapper {
		using GlobalId = RR2DGlobalId<TLocalId>;
		using ShadowDesc = ShadowDescriptor<TLocalId>;
		/* using IDs for counts is rather unnatural (even if justified), so typedefing */
		using LocalVerticesCount = TLocalId;

	public:
		/* called by both master and slaves */
		CommunicationWrapper(NodeCount nc, MpiTypes dts)
			: counts(CountsForCluster(nc, dts.count)),
			  mastersVwin(MpiWindowDesc<GlobalId>::allocate(EDGES_MAX_COUNT, dts.globalId)),
			  mastersOwin(MpiWindowDesc<LocalVerticesCount>::allocate(VERTEX_MAX_COUNT, dts.localId)),
			  shadowsVwin(MpiWindowDesc<GlobalId>::allocate(EDGES_MAX_COUNT, dts.globalId)),
			  shadowsOwin(MpiWindowDesc<ShadowDesc>::allocate(VERTEX_MAX_COUNT, dts.shadowDescriptor)),
			  coOwnersVwin(MpiWindowDesc<NodeId>::allocate(COOWNERS_MAX_COUNT*VERTEX_MAX_COUNT, dts.nodeId)),
			  coOwnersOwin(MpiWindowDesc<NodeCount>::allocate(COOWNERS_MAX_COUNT, dts.nodeId)),
	          mastersV(nc), mastersO(nc), shadowsV(nc), shadowsO(nc), coOwnersV(nc), coOwnersO(nc)
		{}

		/* called by master */
		void finishAllTransfers() {
			masters.flush();
			shadows.flush();
			coOwners.flush();
			counts.flush();

			placeholderReplacementBuffers.release_memory();
		}

		/* called by slaves */
		void ensureTransfersVisibility() {
			masters.sync();
			shadows.sync();
			coOwners.sync();
			counts.sync();
		}

		/* for sequential operation */
		void startAndAssignVertexTo(GlobalId gid) {
			currentVertexGid = gid;

			/* we need to add marker entries to masterNodeId's masters & coOwners offset tables so
			 * that node know that such vertex was assigned to it
			 * this is necessary even if there won't be any neighbour/coOwning nodeId assigned - in that case
			 * next in offset tables'll share the same ID
			 */

			auto& mastersCounts = counts.get(gid.nodeId).masters;
			/* put into offset table id of first unused cell in values table, then update offset table length */
			mastersO.append(gid.nodeId, mastersCounts.valueCount);
			mastersCounts.offsetCount += 1;

			auto& coOwnersCounts = counts.get(gid.nodeId).coOwners;
			/* same story as above */
			coOwnersO.append(gid.nodeId, coOwnersCounts.valueCount);
			/* in this case we skip updaing coOwnersCounts.offsetCounts since it must match mastersCounts.offsetCount */
		}

		void registerNeighbour(GlobalId neighbour, NodeId storeOn) {
			/*
			 * We need to:
			 * - check if storeOn is master; if not it needs to be added to coOwners (provided it's not yet there)
			 * - if it's a first shadow for this vertex assigned to storeOn, we have to create entry
			 * - if master: append entry to masters, otherwise: append entry to shadows (and update counts accordingly)
			 *  in shadows::offset table
			 */

			if(storeOn != currentVertexGid.nodeId) {
				currentVertexCoOwners.insert(storeOn);
				insertOffsetDescriptorOnShadowIfNeeded(storeOn);

				shadowsV.append(storeOn, neighbour);
				counts.get(storeOn).shadows.valueCount += 1;
			} else {
				mastersV.append(storeOn, neighbour);
				counts.get(storeOn).masters.valueCount += 1;
			}
		}

		void registerPlaceholderFor(OriginalVertexId oid, NodeId storeOn) {
			/*
			 * To lessen network utilization, we don't transfer placeholders, only reserve free space for them
			 * after actual data. This means we have to defer actual processing until all 'concrete' neighbours
			 * of current vertex are processed
			 */

			bool master = storeOn == currentVertexGid.nodeId;
			placeholders.push_back(std::make_pair(oid, EdgeTableOffset(storeOn, 0, master)));
		}

		/* returns offset under which placeholders were stored */
		std::vector<std::pair<OriginalVertexId, EdgeTableOffset>> finishVertex() {
			/*
			 * Frist we need to process placeholders - give them offsets and create offset entries for shadows
			 * if necessary
			 */
			for(auto& p: placeholders) {
				auto& desc = p.second;
				if (desc.master) {
					auto& c = counts.get(desc.nodeId).masters;
					desc.offset = c.valueCount;
					c.valueCount += 1;
				} else {
					currentVertexCoOwners.insert(desc.nodeId);
					insertOffsetDescriptorOnShadowIfNeeded(desc.nodeId);

					auto& c = counts.get(desc.nodeId).shadows;
					desc.offset = c.valueCount;
					c.valueCount += 1;
				}
			}

			/* send stuff */

			/* sync & rest SendBufferManagers */

			return placeholders;
		};

		/* for placeholder replacement */
		void replacePlaceholder(EdgeTableOffset eto, GlobalId gid) {
			auto* buffer = placeholderReplacementBuffers.malloc();
			*buffer = gid;

			auto& oa = eto.master ? mastersVwin : mastersOwin;
			oa.put(eto.nodeId, eto.offset, buffer, 1);

			/*
			 * Thanks to the fact that we don't actually write placeholders, we don't need flush() between
			 * writing placeholder and writing actual value
			 */
		}

	private:
		void insertOffsetDescriptorOnShadowIfNeeded(NodeId storeOn) {
			auto& c = counts.get(storeOn).shadows;
			if(currentVertexCoOwners.count(storeOn) == 0) {
				shadowsO.append(storeOn, ShadowDescriptor(currentVertexGid, c.valueCount));
				c.offsetCount += 1;
			}
		}

	private:
		/* 'global' members, shared across all vertices and nodes */
		CountsForCluster counts;
		MpiWindowDesc<GlobalId> mastersVwin;
		MpiWindowDesc<LocalVerticesCount> mastersOwin;
		MpiWindowDesc<GlobalId> shadowsVwin;
		MpiWindowDesc<ShadowDesc> shadowsOwin;
		MpiWindowDesc<NodeId> coOwnersVwin;
		MpiWindowDesc<NodeCount> coOwnersOwin;

		SendBufferManager<GlobalId> mastersV;
		SendBufferManager<LocalVerticesCount> mastersO;
		SendBufferManager<GlobalId> shadowsV;
		SendBufferManager<ShadowDesc> shadowsO;
		SendBufferManager<NodeId> coOwnersV;
		SendBufferManager<NodeCount> coOwnersO;

		/* per-vertex members, reset when new vertex is started */
		GlobalId currentVertexGid;
		std::unordered_set<NodeId> currentVertexCoOwners;
		std::vector<std::pair<OriginalVertexId, EdgeTableOffset>> placeholders;
		boost::object_pool placeholderReplacementBuffers;
	};

	template <typename TLocalId>
	class RemappingTable {
	public:
		void registerMapping(OriginalVertexId, RR2DGlobalId<TLocalId>);
		boost::optional<RR2DGlobalId<TLocalId>> toGlobalId(OriginalVertexId);
		void releaseMapping(OriginalVertexId);
	};

	class PlaceholderCache {
	public:
		/* access pattern - after remapping vertex we want to replace all occurences */
		void rememberPlaceholder(OriginalVertexId, EdgeTableOffset);
		std::vector<EdgeTableOffset> getAllPlaceholdersFor(OriginalVertexId);
	};

	template <typename TLocalId>
	class Partitioner {
	public:
		Partitioner(NodeId nodeCount);

		RR2DGlobalId<TLocalId> nextMasterId();
		TLocalId nextNodeIdForNeighbour();
	};
} }

/**
 * This class is a handle to a graph data, but it's main purpose is loading and partitioning of the graph.
 * High-level overview of the process:
 * - only master reads file and distributes data on other nodes using RMA
 * - when master reads vertex and it's neighbours from file, it chooses (in round-robin manner) to which
 * 	node this vertex should be assign (that node becomes it's master node)
 * - then it generates for vertex GlobalId, consisting of NodeId and LocalId
 * - he also saves mapping between OriginalVertexId and GlobalId (so that it knows how to remap edges that
 * 	this vertex is connected by)
 * - We may encouter and edge whose end has not yet been remapped. In that case we save information about that in
 * 	separate datastructure, and write to that node a placeholder value.
 * - for each vertex we process, we split neighbours into chunks which are assigned to different nodes
 * - we write to master (for our vertex) information about all nodes which store it's neighbours
 * - master vertices and shadows are written to different windows due to requirement that shadows must be stored after
 * 	masters, and we don't know how many vertices are going to be assigned to given node.
 * - nodes must have GlobalId -> LocalId mapping. This is easy for masters since their GlobalId = (nodeId, localId). But
 * 	for shadows we need to know original GlobalId - it must be distributed to the node, alongside assigned neighbours.
 * 	Therefore, for shadows, value table consists only from neighbours (just like for masters), but each position in
 * 	offset table contains pair (offset, originalGlobalId)
 * - after distribution is finished, ma]ster discards GlobalId -> LocalId mapping, except from values, that were
 * 	user requested to be kept (verticesToConv constructor parameter)
 *
 * 	Datastructures needed:
 * 	- Win: edge data - masters
 * 	- Win: offset table - masters
 * 	- Win: edge data - shadows
 * 	- Win: index (offset + corresponding GlobalId) - shadows
 * 	- Win: counts - master vertices, master edges, shadow vertices, shadow edges, owners table size
 * 	- Win: GlobalId -> LocalId mapping for vertices for which it has been requested
 * 	- Win: NodeIds - where are stored neighbours (list of nodes)
 * 	- Win: offsets for the above table
 * 	- master: GlobalId -> LocalId map
 * 	- master: NodeId -> (OriginalVertexId, offset) map - placeholders that need replacing
 *
 * Sizes of the windows:
 * we can try to estimate it, but knowing for sure would require vertex count and edge count. At one point
 * some resizing scheme probably needs to be developed, but for now let's keep it configurable and throw error if
 * it is not enough.
 *
 * Components:
 * - Parser - loads data from file. How does it manage memory?
 * - CommunicationWrapper
 * 	- initializes all windows,
 * 	- keeps track of the offsets,
 * 	- gives appending semantics (saving to window, returns offset) and random access
 * 	 (for fixing placeholders)
 * 	- batches provided updates and sends them
 * 	- also wraps flushing
 * 	- and setting on master node for vertex where are neighbours stored
 * - RemappingTable - holds OriginalVertexId -> GlobalId data. Even if we remap immediatelly, we still needs this mapping
 * 	for following edges we load.
 * - PlaceholderCache - holds information about holes that must be filled after we remap
 * - Partitioner - which vertex should go to whom. Could be coupled wth GlobalId generator?

 * Algorithm:
 * - data read from file by Parser, which returns vector containing whole lines
 * - GlobalId for processed vertex is not really stored, but neighbours are - we retrieve them from
 * 	Partitioner
 * 	- even though it's not stored, it must be created - we assign node as a master on an round-robin
 * 	 manner, independently from edges (which means we might end up with pretty stupid partitioning
 * 	 where master doesn't store any edges)
 * - we read mapping for neighbours from RemappingTable (or use placeholder) and submit it to CommunicationWrapper
 * 	- It can reorder vertices so that placeholders are at the end of the list (and don't really have
 * 	  to be transfered)
 * - after we finish with current vertex, CommunicationWrapper is ready to send message
 * - we also need to replace placeholders
 * 	- wait at the end of processing and then replace all
 * 	- replace as soon as mapping becomes available - helps reduce size of the mapping
 *
 */

template <typename TLocalId, typename TNumId>
class RR2DHandle : public GraphPartitionHandle<RoundRobin2DPartition<TLocalId, TNumId>> {
	using G = RoundRobin2DPartition<TLocalId, TNumId>;
	using P = GraphPartitionHandle<G>;
	IMPORT_ALIASES(G)

public:
	RR2DHandle(std::string path, std::vector<OriginalVertexId> verticesToConv)
			: G(verticesToConv, destroyGraph), path(path)
	{

	}

protected:
	virtual std::pair<G*, std::vector<GlobalId>>
	buildGraph(std::vector<OriginalVertexId> verticesToConvert) override {
		using namespace details::RR2D;

		int nodeCount, nodeId;
		MPI_Comm_size(MPI_COMM_WORLD, &nodeCount);
		MPI_Comm_rank(MPI_COMM_WORLD, &nodeId);

		// @todo commit all required types
		CommunicationWrapper<LocalId> cm;

		if (nodeId == 0) {
			AdjacencyListReader<OriginalVertexId> reader(path);
			Partitioner<LocalId> partitioner(nodeCount);
			RemappingTable<LocalId> remappingTable;
			PlaceholderCache placeholderCache;

			while(auto optionalVertexSpec = reader.getNextVertex()) {
				auto vspec = optionalVertexSpec.get();

				/* get mapping and register it */
				GlobalId mappedId = partitioner.nextMasterId();
				remappingTable.registerMapping(vspec.vertexId, mappedId);

				/* remap neighbours we can (or use placeholders) and distribute to target nodes */
				cm.startAndAssignVertexTo(mappedId);
				for(auto neighbour: vspec.neighbours) {
					LocalId nodeIdForNeigh = partitioner.nextNodeIdForNeighbour();

					if (auto optionalGid = remappingTable.toGlobalId(neighbour)) {
						cm.registerNeighbour(optionalGid.get(), nodeIdForNeigh);
					} else {
						cm.registerPlaceholderFor(neighbour, nodeIdForNeigh);
					}
				}
				auto placeholders = cm.finishVertex();

				/* remember placeholders for further replacement */
				for(auto p: placeholders) {
					auto originalId = p.first;
					auto offset = p.second;

					placeholderCache.rememberPlaceholder(originalId, offset);
				}

				/* remap placeholders that refered to node we just remapped */
				for(auto offset: placeholderCache.getAllPlaceholdersFor(vspec.vertexId)) {
					cm.replacePlaceholder(offset, mappedId);
				}
			}

			cm.finishAllTransfers();
			/* signal other nodes that graph data distribution has been finisheds */
			MPI_Barrier(MPI_COMM_WORLD);

		} else {
			/* let master do her stuff */
			MPI_Barrier(MPI_COMM_WORLD);
			cm.ensureTransfersVisibility();
		}
	};

private:
	std::string path;

	static void destroyGraph(G* g) {
		// don't forget to free type!
	}
};

#endif //FRAMEWORK_ROUNDROBIN2DPARTITION_H
