//
// Created by blueeyedhush on 23.11.17.
//

#ifndef FRAMEWORK_ROUNDROBIN2DPARTITION_H
#define FRAMEWORK_ROUNDROBIN2DPARTITION_H

#include <string>
#include <vector>
#include <GraphPartitionHandle.h>
#include <GraphPartition.h>
#include "shared.h"

template <typename TLocalId, typename TNumId>
class RoundRobin2DPartition : public GraphPartition<ALHPGlobalVertexId<TLocalId>, TLocalId, TNumId> {
	using P = GraphPartition<ALHPGlobalVertexId<TLocalId>, TLocalId, TNumId>;
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
 * 	- master: GlobalId -> LocalId map
 * 	- master: NodeId -> (OriginalVertexId, offset) map - placeholders that need replacing
 *
 * Sizes of the windows:
 * we can try to estimate it, but knowing for sure would require vertex count and edge count. At one point
 * some resizing scheme probably needs to be developed, but for now let's keep it configurable and throw error if
 * it is not enough.
 *
 */

template <typename TLocalId, typename TNumId>
class RR2DHandle : public GraphPartitionHandle<RoundRobin2DPartition<TLocalId, TNumId>> {
	using G = RoundRobin2DPartition<TLocalId, TNumId>;
	using P = GraphPartitionHandle<G>;
	IMPORT_ALIASES(G)

public:
	RR2DHandle(std::string path, std::vector<OriginalVertexId> verticesToConv)
			: GraphPartitionHandle(verticesToConv, destroyGraph)
	{

	}

protected:
	virtual std::pair<RoundRobin2DPartition*, std::vector<GlobalId>>
	buildGraph(std::vector<OriginalVertexId> verticesToConvert) override {

	};

private:
	static void destroyGraph(G* g) {
		// don't forget to free type!
	}
};

#endif //FRAMEWORK_ROUNDROBIN2DPARTITION_H
