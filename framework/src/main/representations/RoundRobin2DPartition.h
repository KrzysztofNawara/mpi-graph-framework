//
// Created by blueeyedhush on 23.11.17.
//

#ifndef FRAMEWORK_ROUNDROBIN2DPARTITION_H
#define FRAMEWORK_ROUNDROBIN2DPARTITION_H

#include <string>
#include <vector>
#include <GraphPartitionHandle.h>
#include <GraphPartition.h>
#include <utils/AdjacencyListReader.h>
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

namespace details {
	using EdgeCount = unsigned int;
	using VertexHandle = unsigned int;

	struct EdgeTableOffset {
		EdgeTableOffset(NodeId nodeId, EdgeCount offset) : nodeId(nodeId), offset(offset) {}

		NodeId nodeId;
		EdgeCount offset;
	};

	/**
	 * Assumes we process one vertex at a time (start, register, finish)
	 * If we want to process more, we need some kind of VertexHandle to know which one we are talking about.
	 */
	template <typename TLocalId>
	class WindowManager {
	public:
		void initialize();
		void ensureAllTransfersCompleted();

		/* for sequential operation */
		void startAndAssignVertexTo(NodeId nodeId);
		void registerNeighbour(RR2DGlobalId<TLocalId> neighbour, NodeId storeOn);
		void registerPlaceholderFor(OriginalVertexId oid, NodeId storedOn);
		/* returns offset under which placeholders were stored */
		std::vector<std::pair<OriginalVertexId, EdgeTableOffset>> finishVertex();

		/* for placeholder replacement */
		void replacePlaceholder(EdgeTableOffset, RR2DGlobalId<TLocalId>);
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
}

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
 * - master vertices and shadows are written to different windows - we don't know how many vertices are going
 * 	to be assigned to given node, so we don't know what local ids we should assign to shadows. Therefore, for shadows,
 * 	we only write pairs (GlobalId, neighbours). After master finishes distributing vertices, each node assigns LocalId
 * 	to each shadow and builds map GlobalId -> LocalId
 * - after distribution is finished, master discards GlobalId -> LocalId mapping, except from values, that were
 * 	user requested to be kept (verticesToConv constructor parameter)
 *
 * 	Datastructures needed:
 * 	- Win: edge data - masters
 * 	- Win: offset table - masters
 * 	- Win: edge data - shadows
 * 	- Win: index (offset + corresponding GlobalId) - shadows
 * 	- Win: counts - vertices (masters, shadows), edges,
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
 * - WindowManager
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
 * - we read mapping for neighbours from RemappingTable (or use placeholder) and submit it to WindowManager
 * 	- It can reorder vertices so that placeholders are at the end of the list (and don't really have
 * 	  to be transfered)
 * - after we finish with current vertex, WindowManager is ready to send message
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
			: GraphPartitionHandle(verticesToConv, destroyGraph), path(path)
	{

	}

protected:
	virtual std::pair<RoundRobin2DPartition*, std::vector<GlobalId>>
	buildGraph(std::vector<OriginalVertexId> verticesToConvert) override {
		using namespace details;

		int nodeCount, nodeId;
		MPI_Comm_size(MPI_COMM_WORLD, &nodeCount);
		MPI_Comm_rank(MPI_COMM_WORLD, &nodeId);

		WindowManager<LocalId> wm;
		wm.initialize();

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
				wm.startAndAssignVertexTo(mappedId.nodeId);
				for(auto neighbour: vspec.neighbours) {
					LocalId nodeIdForNeigh = partitioner.nextNodeIdForNeighbour();

					if (auto optionalGid = remappingTable.toGlobalId(neighbour)) {
						wm.registerNeighbour(optionalGid.get(), nodeIdForNeigh);
					} else {
						wm.registerPlaceholderFor(neighbour, nodeIdForNeigh);
					}
				}
				auto placeholders = wm.finishVertex();

				/* remember placeholders for further replacement */
				for(auto p: placeholders) {
					auto originalId = p.first;
					auto offset = p.second;

					placeholderCache.rememberPlaceholder(originalId, offset);
				}

				/* remap placeholders that refered to node we just remapped */
				for(auto offset: placeholderCache.getAllPlaceholdersFor(vspec.vertexId)) {
					wm.replacePlaceholder(offset, mappedId);
				}
			}

			/* signal other nodes that graph data distribution has been finisheds */
			MPI_Barrier(MPI_COMM_WORLD);

		} else {
			/* let master do her stuff */
			MPI_Barrier(MPI_COMM_WORLD);
			wm.ensureAllTransfersCompleted();
		}
	};

private:
	std::string path;

	static void destroyGraph(G* g) {
		// don't forget to free type!
	}
};

#endif //FRAMEWORK_ROUNDROBIN2DPARTITION_H
